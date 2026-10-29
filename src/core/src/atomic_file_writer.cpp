#include "atomic_file_writer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include <chrono>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#include "windows_file.hpp"
#include <cstring>
#include <vector>
#endif

namespace regmap::detail {
namespace {

constexpr int writeAttempts = 4;

[[nodiscard]] bool retryableReplacementError(const AtomicWriteResult& result) noexcept
{
#ifdef Q_OS_WIN
    if (!result.fileError || !result.fileError->nativeErrorCode) return false;
    const auto code = *result.fileError->nativeErrorCode;
    return code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION ||
           code == ERROR_ACCESS_DENIED;
#else
    Q_UNUSED(result);
    return false;
#endif
}

} // namespace

AtomicFileWriter::AtomicFileWriter(QString path, QByteArray expectedSha256)
    : path_(QDir::cleanPath(QFileInfo(std::move(path)).absoluteFilePath())),
      initial_(capture(path_)), expectedSha256_(std::move(expectedSha256))
{
}

AtomicFileWriter::Snapshot AtomicFileWriter::capture(const QString& path)
{
    Snapshot result;
    QFileInfo information(path);
    int remainingLinks = 128;
    while (information.isSymLink() && remainingLinks > 0) {
        information.setFile(information.symLinkTarget());
        --remainingLinks;
    }
    if (information.isSymLink()) {
        result.error = QFileDevice::ReadError;
        result.errorText = QStringLiteral("Cannot resolve the atomic write destination link.");
        return result;
    }
    // Follow links like QSaveFile, including links whose target does not yet
    // exist. Also retain their resolved destination to detect a changed link.
    result.resolvedPath = QDir::cleanPath(information.absoluteFilePath());
    result.exists = information.exists();
    if (!result.exists) {
        return result;
    }
    if (!information.isFile()) {
        result.error = QFileDevice::ReadError;
        result.errorText = QStringLiteral("The atomic write destination is not a regular file.");
        return result;
    }

    const auto input = readFileSnapshot(result.resolvedPath.toStdWString(), false);
    if (input.error) {
        result.error = QFileDevice::ReadError;
        result.fileError = input.error;
        result.errorText = QString::fromStdString(describeFileIoError(*input.error));
        return result;
    }
    result.digest = QByteArray::fromStdString(input.sha256);
    return result;
}

AtomicWriteResult AtomicFileWriter::checkDestination(int attempts) const
{
    if (initial_.error != QFileDevice::NoError) {
        auto error = initial_.fileError;
        if (error) error->attempts = attempts;
        return {false, "read", initial_.error, initial_.errorText, attempts, error};
    }
    const Snapshot current = capture(path_);
    if (current.error != QFileDevice::NoError) {
        auto error = current.fileError;
        if (error) error->attempts = attempts;
        return {false, "read", current.error, current.errorText, attempts, error};
    }
    if (current.resolvedPath != initial_.resolvedPath ||
        current.exists != initial_.exists || current.digest != initial_.digest ||
        (!expectedSha256_.isEmpty() && current.digest != expectedSha256_)) {
        FileIoError error;
        error.stage = "revision_check";
        error.role = "target";
        error.path = error.targetPath = path_.toStdWString();
        error.attempts = attempts;
        error.errorText = "The destination revision changed; the external file was preserved.";
        error.recovery = "Reload the project and review the changes before applying with the new revision.";
        return {false, "changed", QFileDevice::RenameError,
                QString::fromStdString(describeFileIoError(error)), attempts, error};
    }
    return {false, "commit", QFileDevice::NoError, {}, attempts, {}};
}

bool AtomicFileWriter::destinationUnchanged() const
{
    return checkDestination(0).error == QFileDevice::NoError;
}

AtomicWriteResult AtomicFileWriter::write(
    const QByteArray& bytes, QIODevice::OpenMode mode) const
{
    QByteArray payload = bytes;
#ifdef Q_OS_WIN
    if (mode.testFlag(QIODevice::Text)) payload.replace("\n", "\r\n");
#endif
    for (int attempt = 0; attempt < writeAttempts; ++attempt) {
        AtomicWriteResult checked = checkDestination(attempt + 1);
#ifdef Q_OS_WIN
        AtomicWriteResult failure = checked.error == QFileDevice::NoError
            ? writeWindows(payload, attempt + 1) : checked;
        if (failure.committed || attempt + 1 == writeAttempts ||
            !retryableReplacementError(failure)) return failure;
        // Check the revision again on every attempt, including after a lock.
        std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << attempt)));
#else
        if (checked.error != QFileDevice::NoError) return checked;

        QSaveFile file(path_);
        file.setDirectWriteFallback(false);
        if (!file.open(mode)) {
            return {false, "open", file.error(), file.errorString(), attempt + 1, {}};
        }
        if (file.write(payload) != payload.size()) {
            return {false, "write", file.error(), file.errorString(), attempt + 1, {}};
        }

        checked = checkDestination(attempt + 1);
        if (checked.error != QFileDevice::NoError) {
            file.cancelWriting();
            return checked;
        }
        if (file.commit()) {
            return {true, "commit", QFileDevice::NoError, {}, attempt + 1, {}};
        }

        AtomicWriteResult failure{
            false, "commit", file.error(), file.errorString(), attempt + 1, {}};
        if (attempt + 1 == writeAttempts || !retryableReplacementError(failure)) {
            return failure;
        }
        // commit() discards its temporary file on failure. Recreate it with
        // the same payload only after the destination has been checked again.
        std::this_thread::sleep_for(std::chrono::milliseconds(25 * (1 << attempt)));
#endif
    }
    return {false, "commit", QFileDevice::UnspecifiedError,
            QStringLiteral("Atomic write attempts were exhausted."), writeAttempts, {}};
}

#ifdef Q_OS_WIN
AtomicWriteResult AtomicFileWriter::writeWindows(const QByteArray& payload, int attempts) const
{
    const std::filesystem::path target(initial_.resolvedPath.toStdWString());
    const std::filesystem::path temporary = target.parent_path() /
        (L".csrio-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdWString() + L".tmp");
    const auto failure = [&](const char* operation, const char* stage, const char* role,
                             DWORD code, QFileDevice::FileError qtError) {
        auto error = windowsFileError(stage, role,
            std::string_view(role) == "temporary" ? temporary : target, code);
        error.targetPath = target;
        error.temporaryPath = temporary;
        error.attempts = attempts;
        return AtomicWriteResult{false, operation, qtError,
            QString::fromStdString(describeFileIoError(error)), attempts, error};
    };
    // Keep DELETE access to the source until commit. Another reader cannot
    // reopen it without delete sharing and strand it between close and rename.
    WindowsFile file(CreateFileW(windowsPath(temporary).c_str(), GENERIC_WRITE | DELETE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) return failure("open", "create_temporary", "temporary", GetLastError(), QFileDevice::OpenError);
    struct Cleanup {
        HANDLE handle;
        bool committed{false};
        ~Cleanup() {
            if (!committed) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition));
            }
        }
    } cleanup{file.get()};
    qsizetype offset = 0;
    while (offset < payload.size()) {
        const DWORD size = static_cast<DWORD>(qMin<qsizetype>(payload.size() - offset, 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(file.get(), payload.constData() + offset, size, &written, nullptr))
            return failure("write", "write_temporary", "temporary", GetLastError(), QFileDevice::WriteError);
        if (!written) return failure("write", "write_temporary", "temporary", ERROR_WRITE_FAULT, QFileDevice::WriteError);
        offset += written;
    }
    if (!FlushFileBuffers(file.get()))
        return failure("write", "flush_temporary", "temporary", GetLastError(), QFileDevice::WriteError);

    auto checked = checkDestination(attempts);
    if (checked.error != QFileDevice::NoError) {
        if (checked.fileError) checked.fileError->temporaryPath = temporary;
        return checked;
    }
    const std::wstring name = windowsPath(target);
    const auto nameBytes = name.size() * sizeof(wchar_t);
    // The extended layout is ABI-compatible with FILE_RENAME_INFO, including
    // SDKs whose configured minimum Windows version hides its Flags union.
    struct RenameInformation {
        DWORD Flags;
        HANDLE RootDirectory;
        DWORD FileNameLength;
        WCHAR FileName[1];
    };
    static_assert(offsetof(RenameInformation, FileName) == offsetof(FILE_RENAME_INFO, FileName));
    std::vector<std::byte> storage(sizeof(RenameInformation) + nameBytes);
    auto* rename = reinterpret_cast<RenameInformation*>(storage.data());
    // REPLACE_IF_EXISTS | POSIX_SEMANTICS: readers that share DELETE may keep
    // reading the old file. Exclusive/delete-denying handles still block save.
    rename->Flags = 0x00000001 | 0x00000002;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(rename->FileName, name.c_str(), nameBytes + sizeof(wchar_t));
    bool renamed = SetFileInformationByHandle(file.get(),
        static_cast<FILE_INFO_BY_HANDLE_CLASS>(22), rename, static_cast<DWORD>(storage.size()));
    if (!renamed) {
        const DWORD code = GetLastError();
        if (code == ERROR_INVALID_PARAMETER || code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION) {
            checked = checkDestination(attempts);
            if (checked.error != QFileDevice::NoError) return checked;
            // Older filesystems may only support the original atomic rename.
            // This fallback never writes or truncates the destination in place.
            rename->Flags = TRUE;
            renamed = SetFileInformationByHandle(file.get(), FileRenameInfo,
                rename, static_cast<DWORD>(storage.size()));
        } else {
            SetLastError(code);
        }
    }
    if (!renamed) {
        const DWORD code = GetLastError();
        // Attribute a target lock only when a separate delete-access probe
        // confirms it. Preserve the original rename error, not the probe code.
        const char* role = "replacement";
        WindowsFile probe(CreateFileW(windowsPath(target).c_str(), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!probe.valid()) {
            const DWORD probeCode = GetLastError();
            if (probeCode == ERROR_SHARING_VIOLATION || probeCode == ERROR_ACCESS_DENIED ||
                probeCode == ERROR_LOCK_VIOLATION) role = "target";
        }
        return failure("commit", "replace", role, code, QFileDevice::RenameError);
    }
    cleanup.committed = true;
    return {true, "commit", QFileDevice::NoError, {}, attempts, {}};
}
#endif

} // namespace regmap::detail
