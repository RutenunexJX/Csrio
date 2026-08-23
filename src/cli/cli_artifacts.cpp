#include "cli_artifacts.hpp"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonObject>

#include <algorithm>
#include <filesystem>
#include <string>

namespace regmap::cli {
namespace {

#ifdef Q_OS_WIN
using PathIdentity = QString;
#else
using PathIdentity = std::filesystem::path;
#endif

[[nodiscard]] PathIdentity pathIdentity(
    const std::filesystem::path& path)
{
    std::filesystem::path normalized =
        path.lexically_normal();
#ifdef Q_OS_WIN
    normalized.make_preferred();
    return QString::fromStdWString(
               normalized.native())
        .toCaseFolded();
#else
    return normalized;
#endif
}

[[nodiscard]] bool samePathIdentity(
    const std::filesystem::path& left,
    const std::filesystem::path& right)
{
    return pathIdentity(left) ==
        pathIdentity(right);
}

[[nodiscard]] QString fromPath(
    const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(
        path.wstring());
#else
    const std::u8string bytes =
        path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(
            bytes.data()),
        static_cast<qsizetype>(
            bytes.size()));
#endif
}

[[nodiscard]] std::filesystem::path toPath(
    const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(
        path.toStdWString());
#else
    const QByteArray bytes = path.toUtf8();
    return std::filesystem::path(
        std::string(
            bytes.constData(),
            static_cast<std::size_t>(
                bytes.size())));
#endif
}

[[nodiscard]] QByteArray artifactBytes(
    const GeneratedArtifact& artifact)
{
    if (artifact.isBinary()) {
        return QByteArray(
            reinterpret_cast<const char*>(
                artifact.binaryContent.data()),
            static_cast<qsizetype>(
                artifact.binaryContent.size()));
    }
    return QByteArray(
        artifact.content.data(),
        static_cast<qsizetype>(
            artifact.content.size()));
}

[[nodiscard]] QString artifactHash(
    const GeneratedArtifact& artifact)
{
    return QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(
            QCryptographicHash::hash(
                artifactBytes(artifact),
                QCryptographicHash::Sha256)
                .toHex()));
}

[[nodiscard]] QString artifactStateText(
    const GeneratedArtifactInspection& inspection)
{
    switch (inspection.state) {
    case GeneratedArtifactState::current:
        return inspection.readOnly
            ? QStringLiteral("synchronized")
            : QStringLiteral("writable");
    case GeneratedArtifactState::missing:
        return QStringLiteral("missing");
    case GeneratedArtifactState::modified:
        return QStringLiteral("modified");
    case GeneratedArtifactState::unreadable:
        return QStringLiteral("unreadable");
    }
    return QStringLiteral("unreadable");
}

[[nodiscard]] QJsonObject artifactBaseJson(
    const GeneratedArtifact& artifact)
{
    const QByteArray bytes = artifactBytes(artifact);
    return {
        {QStringLiteral("kind"),
         generationTargetToken(artifact.kind)},
        {QStringLiteral("path"),
         fromPath(artifact.path)},
        {QStringLiteral("bytes"),
         QString::number(bytes.size())},
        {QStringLiteral("sha256"),
         artifactHash(artifact)},
    };
}

void addInspection(
    QJsonObject& result,
    const GeneratedArtifact& artifact,
    const GeneratedArtifactInspection& inspection)
{
    const QFileInfo information(
        fromPath(artifact.path));
    result.insert(
        QStringLiteral("state"),
        artifactStateText(inspection));
    result.insert(
        QStringLiteral("exists"),
        information.exists());
    result.insert(
        QStringLiteral("content_current"),
        inspection.contentCurrent());
    result.insert(
        QStringLiteral("read_only"),
        inspection.readOnly);
    result.insert(
        QStringLiteral("synchronized"),
        inspection.synchronized());
}

[[nodiscard]] bool safeRelativePath(
    const std::filesystem::path& declared,
    bool allowCurrentDirectory)
{
    if (declared.empty() ||
        declared.is_absolute() ||
        declared.has_root_name()) {
        return false;
    }
    const std::filesystem::path normalized =
        declared.lexically_normal();
    if (normalized.empty() ||
        (!allowCurrentDirectory &&
         normalized == ".")) {
        return false;
    }
    return normalized.begin() ==
            normalized.end() ||
        *normalized.begin() != "..";
}

[[nodiscard]] bool validManifestPathContract(
    const ProjectManifest& manifest,
    QString& error)
{
    std::set<PathIdentity> paths;
    GenerationTargetSet kinds;
    for (const auto& target : manifest.targets) {
        if (!kinds.insert(target.kind).second) {
            error = QStringLiteral(
                "Each generation target kind must be configured exactly once.");
            return false;
        }
        if (!paths.insert(
                      pathIdentity(
                          target.path.resolved))
                 .second) {
            error = QStringLiteral(
                "Generation target paths must be unique; '%1' is configured more than once.")
                        .arg(
                            fromPath(
                                target.path
                                    .resolved));
            return false;
        }
        if (samePathIdentity(
                target.path.resolved,
                manifest.manifestPath)) {
            error = QStringLiteral(
                "A generated output must not overwrite the project manifest '%1'.")
                        .arg(
                            fromPath(
                                manifest
                                    .manifestPath));
            return false;
        }
        if (!manifest.rtl.path.resolved.empty() &&
            samePathIdentity(
                target.path.resolved,
                manifest.rtl.path.resolved)) {
            error = QStringLiteral(
                "A generated output must not overwrite the managed RTL file '%1'.")
                        .arg(
                            fromPath(
                                manifest.rtl.path
                                    .resolved));
            return false;
        }
    }
    if (kinds != allGenerationTargets()) {
        error = QStringLiteral(
            "Generation targets must configure xlsx, c-header, and markdown exactly once.");
        return false;
    }
    return true;
}

} // namespace

GenerationTargetSet allGenerationTargets()
{
    return {
        GenerationTargetKind::xlsx,
        GenerationTargetKind::cHeader,
        GenerationTargetKind::markdown,
    };
}

std::optional<GenerationTargetKind>
parseGenerationTarget(const QString& token)
{
    const QString normalized =
        token.trimmed().toLower();
    if (normalized == QStringLiteral("xlsx")) {
        return GenerationTargetKind::xlsx;
    }
    if (normalized == QStringLiteral("c-header")) {
        return GenerationTargetKind::cHeader;
    }
    if (normalized == QStringLiteral("markdown")) {
        return GenerationTargetKind::markdown;
    }
    return std::nullopt;
}

QString generationTargetToken(
    GenerationTargetKind kind)
{
    return QString::fromUtf8(
        toString(kind).data(),
        static_cast<qsizetype>(
            toString(kind).size()));
}

QStringList generationTargetArguments(
    const GenerationTargetSet& targets)
{
    QStringList result;
    if (targets.empty() || targets == allGenerationTargets()) {
        return result;
    }
    for (const GenerationTargetKind kind :
         allGenerationTargets()) {
        if (!targets.contains(kind)) {
            continue;
        }
        result.push_back(
            QStringLiteral("--target"));
        result.push_back(
            generationTargetToken(kind));
    }
    return result;
}

ProjectManifest manifestForTargets(
    const ProjectManifest& manifest,
    const GenerationTargetSet& requested,
    GenerationTargetSet* missing)
{
    ProjectManifest result = manifest;
    if (missing) {
        missing->clear();
    }
    if (requested.empty()) {
        return result;
    }
    result.targets.clear();
    for (const auto& target : manifest.targets) {
        if (requested.contains(target.kind)) {
            result.targets.push_back(target);
        }
    }
    if (missing) {
        *missing = requested;
        for (const auto& target : result.targets) {
            missing->erase(target.kind);
        }
    }
    return result;
}

bool configureInitialGeneration(
    ProjectManifest& manifest,
    const QString& outputDirectory,
    const GenerationTargetSet& targets,
    const std::map<GenerationTargetKind, QString>& fileNames,
    QString& error)
{
    error.clear();
    const std::filesystem::path declaredOutput =
        outputDirectory.isEmpty()
        ? manifest.outputDirectory.declared
        : toPath(outputDirectory);
    if (!safeRelativePath(declaredOutput, true)) {
        error = QStringLiteral(
            "--output-dir must stay inside the project directory.");
        return false;
    }

    manifest.outputDirectory.declared =
        declaredOutput.lexically_normal();
    manifest.outputDirectory.resolved =
        (manifest.manifestPath.parent_path() /
         manifest.outputDirectory.declared)
            .lexically_normal();

    // The manifest schema requires one entry for every supported output.
    // targets restricts this init invocation, not the saved configuration.
    static_cast<void>(targets);
    for (auto& target : manifest.targets) {
        const auto overrideName =
            fileNames.find(target.kind);
        if (overrideName != fileNames.end()) {
            target.path.declared =
                toPath(overrideName->second)
                    .lexically_normal();
        }
        if (!safeRelativePath(
                target.path.declared,
                false)) {
            error = QStringLiteral(
                "The %1 target file must stay inside the output directory.")
                .arg(
                    generationTargetToken(
                        target.kind));
            return false;
        }
        target.path.resolved =
            (manifest.outputDirectory.resolved /
             target.path.declared)
            .lexically_normal();
    }
    return validManifestPathContract(
        manifest, error);
}

QJsonArray artifactPreviewJson(
    const std::vector<GeneratedArtifact>& artifacts)
{
    QJsonArray result;
    for (const auto& artifact : artifacts) {
        const GeneratedArtifactInspection inspection =
            inspectGeneratedArtifact(artifact);
        QJsonObject item =
            artifactBaseJson(artifact);
        addInspection(
            item,
            artifact,
            inspection);
        const bool skip = inspection.synchronized();
        item.insert(
            QStringLiteral("action"),
            skip
                ? QStringLiteral("skip")
                : QStringLiteral("write"));
        item.insert(
            QStringLiteral("written"), false);
        item.insert(
            QStringLiteral("skipped"), skip);
        item.insert(
            QStringLiteral("failed"), false);
        result.append(std::move(item));
    }
    return result;
}

QJsonObject artifactStatusJson(
    const GeneratedArtifact& artifact)
{
    QJsonObject result = artifactBaseJson(artifact);
    addInspection(
        result,
        artifact,
        inspectGeneratedArtifact(artifact));
    return result;
}

ArtifactWriteReport writeArtifactsWithReport(
    const std::vector<GeneratedArtifact>& artifacts)
{
    std::vector<bool> synchronizedBefore;
    synchronizedBefore.reserve(artifacts.size());
    for (const auto& artifact : artifacts) {
        synchronizedBefore.push_back(
            inspectGeneratedArtifact(artifact)
                .synchronized());
    }

    ArtifactWriteReport result;
    result.diagnostics =
        writeGeneratedArtifacts(artifacts);
    for (std::size_t index = 0;
         index < artifacts.size();
         ++index) {
        const auto& artifact = artifacts[index];
        const GeneratedArtifactInspection after =
            inspectGeneratedArtifact(artifact);
        QJsonObject item = artifactBaseJson(artifact);
        addInspection(item, artifact, after);
        const bool skipped =
            synchronizedBefore[index] &&
            after.synchronized();
        const bool written =
            !synchronizedBefore[index] &&
            after.synchronized();
        const bool failed = !after.synchronized();
        if (skipped) {
            ++result.skippedCount;
        } else if (written) {
            ++result.writtenCount;
        } else {
            ++result.failedCount;
        }
        item.insert(
            QStringLiteral("action"),
            skipped
                ? QStringLiteral("skipped")
                : written
                    ? QStringLiteral("written")
                    : QStringLiteral("failed"));
        item.insert(
            QStringLiteral("written"), written);
        item.insert(
            QStringLiteral("skipped"), skipped);
        item.insert(
            QStringLiteral("failed"), failed);
        result.artifacts.append(std::move(item));
    }
    return result;
}

} // namespace regmap::cli
