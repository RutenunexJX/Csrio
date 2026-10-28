#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"

#include <filesystem>
#include <functional>
#include <string_view>
#include <cstdint>
#include <string>
#include <vector>

namespace regmap {

using GenerationObserver = std::function<void(std::string_view stage, double milliseconds, std::uint64_t bytes, bool ok)>;

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

enum class GeneratedArtifactState {
    current,
    missing,
    modified,
    unreadable,
};

struct GeneratedArtifactInspection {
    GeneratedArtifactState state {GeneratedArtifactState::missing};
    bool readOnly {false};

    [[nodiscard]] bool contentCurrent() const noexcept
    {
        return state == GeneratedArtifactState::current;
    }

    [[nodiscard]] bool synchronized() const noexcept
    {
        return contentCurrent() && readOnly;
    }
};

[[nodiscard]] GenerationResult generateArtifacts(
    const Workspace& workspace,
    const ProjectManifest& manifest, const GenerationObserver& observer = {});

[[nodiscard]] GeneratedArtifactInspection inspectGeneratedArtifact(
    const GeneratedArtifact& artifact);

[[nodiscard]] std::vector<Diagnostic> writeGeneratedArtifacts(
    const std::vector<GeneratedArtifact>& artifacts, const GenerationObserver& observer = {});

} // namespace regmap
