#pragma once

#include "regmap/core/diagnostic.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace regmap {

struct ManifestPath {
    std::filesystem::path declared;
    std::filesystem::path resolved;
};

struct RtlSyncConfig {
    ManifestPath path;
    std::string moduleName;
};

enum class GenerationTargetKind {
    xlsx,
    cHeader,
    markdown,
};

struct GenerationTargetConfig {
    GenerationTargetKind kind {GenerationTargetKind::xlsx};
    ManifestPath path;
    std::map<std::string, std::string, std::less<>> options;
};

struct ProjectManifest {
    static constexpr int currentSchemaVersion = 2;

    int schemaVersion {currentSchemaVersion};
    std::filesystem::path manifestPath;
    ObjectId workspaceId;
    std::string workspaceName;
    RtlSyncConfig rtl;
    ManifestPath outputDirectory;
    std::vector<GenerationTargetConfig> targets;
};

struct ManifestLoadResult {
    std::optional<ProjectManifest> manifest;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] ManifestLoadResult loadProjectManifest(const std::filesystem::path& path);
[[nodiscard]] std::string_view toString(GenerationTargetKind kind) noexcept;

} // namespace regmap
