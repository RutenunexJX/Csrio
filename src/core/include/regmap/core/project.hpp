#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace regmap {

struct ProjectOpenResult {
    std::optional<ProjectManifest> manifest;
    std::optional<Workspace> workspace;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] ProjectOpenResult openProject(const std::filesystem::path& manifestPath);

} // namespace regmap
