#pragma once

#include "regmap/core/project.hpp"
#include "regmap/core/serialization.hpp"

#include <yaml-cpp/yaml.h>

namespace regmap::detail {

// A composite project load decodes both sections from this one immutable input.
struct ProjectDocument {
    std::filesystem::path path;
    std::optional<YAML::Node> root;
    std::string sha256;
    std::vector<Diagnostic> diagnostics;
    ProjectLoadMetrics metrics;
};

[[nodiscard]] ProjectDocument readProjectDocument(
    const std::filesystem::path& path, bool computeDigest = false);
[[nodiscard]] ManifestLoadResult decodeProjectManifest(
    const YAML::Node& root, const std::filesystem::path& path);
[[nodiscard]] WorkspaceFileLoadResult decodeProjectWorkspace(
    const YAML::Node& root, const std::filesystem::path& path);

} // namespace regmap::detail
