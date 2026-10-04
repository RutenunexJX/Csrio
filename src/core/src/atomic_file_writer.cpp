#include "atomic_file_writer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <chrono>
#include <thread>
#include <utility>

namespace regmap::detail {
namespace {

constexpr int writeAttempts = 4;

[[nodiscard]] bool retryableReplacementError(QFileDevice::FileError error) noexcept
{
#ifdef Q_OS_WIN
    return error == QFileDevice::RenameError ||
           error == QFileDevice::RemoveError ||
           error == QFileDevice::PermissionsError;
#else
    Q_UNUSED(error);
    return false;
#endif
}

} // namespace

AtomicFileWriter::AtomicFileWriter(QString path)
    : path_(QDir::cleanPath(QFileInfo(std::move(path)).absoluteFilePath())),
      initial_(capture(path_))
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

    QFile input(result.resolvedPath);
    if (!input.open(QIODevice::ReadOnly)) {
        result.error = input.error();
        result.errorText = input.errorString();
        return result;
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!digest.addData(&input) || input.error() != QFileDevice::NoError) {
        result.error = input.error() == QFileDevice::NoError
            ? QFileDevice::ReadError : input.error();
        result.errorText = input.errorString();
        return result;
    }
    result.digest = digest.result();
    // A QFile reader on Windows denies deletion sharing. Never keep this
    // handle alive across QSaveFile::commit().
    input.close();
    return result;
}

AtomicWriteResult AtomicFileWriter::checkDestination(int attempts) const
{
    if (initial_.error != QFileDevice::NoError) {
        return {false, "read", initial_.error, initial_.errorText, attempts};
    }
    const Snapshot current = capture(path_);
    if (current.error != QFileDevice::NoError) {
        return {false, "read", current.error, current.errorText, attempts};
    }
    if (current.resolvedPath != initial_.resolvedPath ||
        current.exists != initial_.exists || current.digest != initial_.digest) {
        return {false, "changed", QFileDevice::RenameError,
                QStringLiteral("The destination changed while preparing the write; "
                               "the external file was preserved."),
                attempts};
    }
    return {false, "commit", QFileDevice::NoError, {}, attempts};
}

bool AtomicFileWriter::destinationUnchanged() const
{
    return checkDestination(0).error == QFileDevice::NoError;
}

AtomicWriteResult AtomicFileWriter::write(
    const QByteArray& bytes, QIODevice::OpenMode mode) const
{
    const QByteArray payload = bytes;
    for (int attempt = 0; attempt < writeAttempts; ++attempt) {
        AtomicWriteResult checked = checkDestination(attempt);
        if (checked.error != QFileDevice::NoError) {
            return checked;
        }

        QSaveFile file(path_);
        file.setDirectWriteFallback(false);
        if (!file.open(mode)) {
            return {false, "open", file.error(), file.errorString(), attempt + 1};
        }
        if (file.write(payload) != payload.size()) {
            return {false, "write", file.error(), file.errorString(), attempt + 1};
        }

        checked = checkDestination(attempt + 1);
        if (checked.error != QFileDevice::NoError) {
            file.cancelWriting();
            return checked;
        }
        if (file.commit()) {
            return {true, "commit", QFileDevice::NoError, {}, attempt + 1};
        }

        AtomicWriteResult failure{
            false, "commit", file.error(), file.errorString(), attempt + 1};
        if (attempt + 1 == writeAttempts || !retryableReplacementError(failure.error)) {
            return failure;
        }
        // commit() discards its temporary file on failure. Recreate it with
        // the same payload only after the destination has been checked again.
        std::this_thread::sleep_for(std::chrono::milliseconds(25 * (1 << attempt)));
    }
    return {false, "commit", QFileDevice::UnspecifiedError,
            QStringLiteral("Atomic write attempts were exhausted."), writeAttempts};
}

} // namespace regmap::detail
