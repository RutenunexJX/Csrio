#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

struct RtlParseResult {
    std::optional<Workspace> workspace;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] RtlParseResult parseManagedRtl(const std::filesystem::path& path);
[[nodiscard]] std::string renderManagedRtlRegion(const Workspace& workspace);
[[nodiscard]] std::vector<Diagnostic> writeManagedRtl(
    const std::filesystem::path& path,
    std::string_view moduleName,
    const Workspace& workspace);

} // namespace regmap
