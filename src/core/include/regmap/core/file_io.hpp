#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace regmap {

struct FileIoError {
    std::string stage;
    std::string role;
    std::filesystem::path path;
    std::filesystem::path targetPath;
    std::filesystem::path temporaryPath;
    std::optional<std::uint32_t> nativeErrorCode;
    std::string errorText;
    int attempts{0};
    std::string recovery;
    bool operator==(const FileIoError&) const = default;
};

[[nodiscard]] std::string describeFileIoError(const FileIoError& error);

struct FileReadResult {
    std::string bytes;
    std::string sha256;
    std::optional<FileIoError> error;
};

// Owns and closes the read handle before returning. Windows readers allow
// replacement of an open file; callers must still verify the revision.
[[nodiscard]] FileReadResult readFileSnapshot(
    const std::filesystem::path& path, bool retainContents = true);

} // namespace regmap
