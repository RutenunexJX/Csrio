#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace regmap {

struct WorkspaceFileLoadResult {
    std::optional<Workspace> workspace;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] WorkspaceFileLoadResult loadWorkspaceFromProjectFile(
    const std::filesystem::path& path);

[[nodiscard]] std::vector<Diagnostic> saveProjectFile(
    const ProjectManifest& manifest,
    const Workspace& workspace);

} // namespace regmap
