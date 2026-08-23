#pragma once

#include "regmap/core/generation.hpp"
#include "regmap/core/manifest.hpp"

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace regmap::cli {

using GenerationTargetSet =
    std::set<GenerationTargetKind>;

struct ArtifactWriteReport {
    QJsonArray artifacts;
    std::vector<Diagnostic> diagnostics;
    int writtenCount{0};
    int skippedCount{0};
    int failedCount{0};

    [[nodiscard]] bool successful() const noexcept
    {
        return failedCount == 0;
    }
};

[[nodiscard]] GenerationTargetSet allGenerationTargets();

[[nodiscard]] std::optional<GenerationTargetKind>
parseGenerationTarget(const QString& token);

[[nodiscard]] QString generationTargetToken(
    GenerationTargetKind kind);

[[nodiscard]] QStringList generationTargetArguments(
    const GenerationTargetSet& targets);

[[nodiscard]] ProjectManifest manifestForTargets(
    const ProjectManifest& manifest,
    const GenerationTargetSet& requested,
    GenerationTargetSet* missing = nullptr);

[[nodiscard]] bool configureInitialGeneration(
    ProjectManifest& manifest,
    const QString& outputDirectory,
    const GenerationTargetSet& targets,
    const std::map<GenerationTargetKind, QString>& fileNames,
    QString& error);

[[nodiscard]] QJsonArray artifactPreviewJson(
    const std::vector<GeneratedArtifact>& artifacts);

[[nodiscard]] QJsonObject artifactStatusJson(
    const GeneratedArtifact& artifact);

[[nodiscard]] ArtifactWriteReport writeArtifactsWithReport(
    const std::vector<GeneratedArtifact>& artifacts);

} // namespace regmap::cli
