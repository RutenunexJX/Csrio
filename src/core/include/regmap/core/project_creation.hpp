#pragma once

#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace regmap {

struct NewProject {
    ProjectManifest manifest;
    Workspace workspace;
};

[[nodiscard]] NewProject makeDefaultProject(
    const std::filesystem::path& manifestPath,
    std::string workspaceName,
    std::optional<ObjectId> workspaceId =
        std::nullopt);

} // namespace regmap
