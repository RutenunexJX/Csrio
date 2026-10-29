#pragma once

#include "regmap/core/file_io.hpp"

#include <QDir>
#include <QString>
#include <qt_windows.h>

namespace regmap::detail {

class WindowsFile final {
public:
    explicit WindowsFile(HANDLE handle) : handle_(handle) {}
    ~WindowsFile() { if (valid()) CloseHandle(handle_); }
    WindowsFile(const WindowsFile&) = delete;
    WindowsFile& operator=(const WindowsFile&) = delete;
    [[nodiscard]] bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
};

inline std::wstring windowsPath(const std::filesystem::path& path)
{
    QString native = QDir::toNativeSeparators(QString::fromStdWString(
        std::filesystem::absolute(path).lexically_normal().wstring()));
    if (!native.startsWith(QStringLiteral("\\\\?\\"))) {
        native = native.startsWith(QStringLiteral("\\\\"))
            ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2)
            : QStringLiteral("\\\\?\\") + native;
    }
    return native.toStdWString();
}

inline FileIoError windowsFileError(
    const char* stage, const char* role, const std::filesystem::path& path,
    DWORD code)
{
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    FileIoError error;
    error.stage = stage;
    error.role = role;
    error.path = path;
    error.nativeErrorCode = code;
    error.errorText = length ? QString::fromWCharArray(message, static_cast<int>(length))
                                  .trimmed().toUtf8().toStdString()
                            : "Windows error " + std::to_string(code);
    if (message) LocalFree(message);
    error.recovery = "Check the reported file's sharing locks and directory permissions, "
                     "then retry with the current revision. No in-place overwrite was attempted.";
    return error;
}

inline std::optional<FileIoError> createWindowsDirectory(const std::filesystem::path& path)
{
    const auto native = windowsPath(path);
    if (CreateDirectoryW(native.c_str(), nullptr)) return std::nullopt;
    DWORD code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS) {
        const DWORD attributes = GetFileAttributesW(native.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return std::nullopt;
    } else if (code == ERROR_PATH_NOT_FOUND && path.parent_path() != path) {
        if (auto error = createWindowsDirectory(path.parent_path())) return error;
        if (CreateDirectoryW(native.c_str(), nullptr)) return std::nullopt;
        code = GetLastError();
        if (code == ERROR_ALREADY_EXISTS) {
            const DWORD attributes = GetFileAttributesW(native.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                return std::nullopt;
        }
    }
    return windowsFileError("create_directory", "directory", path, code);
}

} // namespace regmap::detail
