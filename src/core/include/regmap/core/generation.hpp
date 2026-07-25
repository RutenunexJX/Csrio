#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace regmap {

struct GeneratedArtifact {
    GenerationTargetKind kind {GenerationTargetKind::xlsx};
    std::filesystem::path path;
    std::string content;
    std::vector<std::uint8_t> binaryContent;

    [[nodiscard]] bool isBinary() const noexcept { return !binaryContent.empty(); }
};

struct GenerationResult {
    std::vector<GeneratedArtifact> artifacts;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool hasErrors() const noexcept;
};

[[nodiscard]] GenerationResult generateArtifacts(
    const Workspace& workspace,
    const ProjectManifest& manifest);

[[nodiscard]] std::vector<Diagnostic> writeGeneratedArtifacts(
    const std::vector<GeneratedArtifact>& artifacts);

} // namespace regmap
