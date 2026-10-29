#include "regmap/core/file_io.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <array>

#ifdef Q_OS_WIN
#include "windows_file.hpp"
#endif

namespace regmap {

std::string describeFileIoError(const FileIoError& error)
{
    std::string result = error.stage + " (" + error.role + ": " +
        QString::fromStdWString(error.path.wstring()).toUtf8().toStdString() + ")";
    if (error.nativeErrorCode) result += " [Windows " + std::to_string(*error.nativeErrorCode) + "]";
    result += ": " + error.errorText;
    if (!error.temporaryPath.empty()) {
        result += " Temporary: " + QString::fromStdWString(error.temporaryPath.wstring()).toUtf8().toStdString();
    }
    if (error.attempts) result += " Attempts: " + std::to_string(error.attempts) + ".";
    if (!error.recovery.empty()) result += " " + error.recovery;
    return result;
}

FileReadResult readFileSnapshot(const std::filesystem::path& path, bool retainContents)
{
    FileReadResult result;
    const auto absolutePath = std::filesystem::absolute(path).lexically_normal();
    QCryptographicHash digest(QCryptographicHash::Sha256);
    std::array<char, 65536> buffer{};
#ifdef Q_OS_WIN
    detail::WindowsFile file(CreateFileW(
        detail::windowsPath(absolutePath).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) {
        result.error = detail::windowsFileError("open_read", "target", absolutePath, GetLastError());
        result.error->targetPath = absolutePath;
        return result;
    }
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            result.error = detail::windowsFileError("read", "target", absolutePath, GetLastError());
            result.error->targetPath = absolutePath;
            return result;
        }
        if (!count) break;
        digest.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(count)));
        if (retainContents) result.bytes.append(buffer.data(), count);
    }
#else
    QFile file(QString::fromStdWString(absolutePath.wstring()));
    const auto failure = [&](const char* stage) {
        FileIoError error;
        error.stage = stage;
        error.role = "target";
        error.path = error.targetPath = absolutePath;
        error.errorText = file.errorString().toUtf8().toStdString();
        error.recovery = "Check file access and retry with the current revision.";
        result.error = std::move(error);
    };
    if (!file.open(QIODevice::ReadOnly)) { failure("open_read"); return result; }
    for (;;) {
        const qint64 count = file.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (count < 0) { failure("read"); return result; }
        if (!count) break;
        digest.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(count)));
        if (retainContents) result.bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
#endif
    result.sha256 = digest.result().toHex().toStdString();
    return result;
}

} // namespace regmap
