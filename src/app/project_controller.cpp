#include "project_controller.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/project_creation.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

namespace {

[[nodiscard]] bool containsErrors(const std::vector<regmap::Diagnostic>& diagnostics)
{
    return std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.severity == regmap::DiagnosticSeverity::error;
    });
}

[[nodiscard]] bool isValidationDiagnostic(const regmap::Diagnostic& diagnostic)
{
    return diagnostic.code.size() >= 3 && diagnostic.code.starts_with("RM3");
}

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] QString fromUtf8(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QByteArray fileDigest(const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }
    QFile file(fromPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    return digest.addData(&file) ? digest.result() : QByteArray{};
}

[[nodiscard]] bool writeRecoveryMetadata(
    const std::filesystem::path& metadataPath,
    const QByteArray& baseDigest,
    const QByteArray& draftDigest,
    std::string_view workspaceId)
{
    if (baseDigest.isEmpty() || draftDigest.isEmpty()) {
        return false;
    }
    QSaveFile file(
        fromPath(
            metadataPath));
    if (!file.open(
            QIODevice::WriteOnly)) {
        return false;
    }
    const QJsonObject metadata{
        {QStringLiteral(
             "format_version"),
         1},
        {QStringLiteral(
             "workspace_id"),
         fromUtf8(
             workspaceId)},
        {QStringLiteral(
             "base_sha256"),
         QString::fromLatin1(
             baseDigest.toHex())},
        {QStringLiteral(
             "draft_sha256"),
         QString::fromLatin1(
             draftDigest.toHex())},
    };
    const QByteArray content =
        QJsonDocument(
            metadata)
            .toJson(
                QJsonDocument::Compact);
    if (file.write(content) !=
        content.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void appendDiagnostics(
    std::vector<regmap::Diagnostic>& destination,
    std::vector<regmap::Diagnostic> source)
{
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}

void useProjectSource(
    regmap::SourceLocation& source,
    const std::filesystem::path& projectPath)
{
    if (!source.empty()) {
        source.workbook = projectPath;
    }
}

void useProjectSources(
    regmap::PropertySources& sources,
    const std::filesystem::path& projectPath)
{
    for (auto& [property, source] : sources) {
        static_cast<void>(property);
        useProjectSource(source, projectPath);
    }
}

void useProjectSources(
    regmap::EnumValue& value,
    const std::filesystem::path& projectPath)
{
    useProjectSource(value.source, projectPath);
    useProjectSources(
        value.propertySources, projectPath);
}

void useProjectSources(
    regmap::Field& field,
    const std::filesystem::path& projectPath)
{
    useProjectSource(field.source, projectPath);
    useProjectSources(
        field.propertySources, projectPath);
    for (auto& value : field.enumValues) {
        useProjectSources(value, projectPath);
    }
    for (auto& member : field.members) {
        useProjectSources(member, projectPath);
    }
}

void useProjectSources(
    regmap::Workspace& workspace,
    const std::filesystem::path& projectPath)
{
    workspace.manifestPath = projectPath;
    for (auto& page : workspace.addressSpaces) {
        useProjectSource(page.source, projectPath);
        useProjectSources(
            page.propertySources, projectPath);
        for (auto& block : page.blocks) {
            useProjectSource(
                block.source, projectPath);
            useProjectSources(
                block.propertySources, projectPath);
            for (auto& reg : block.registers) {
                useProjectSource(
                    reg.source, projectPath);
                useProjectSources(
                    reg.propertySources, projectPath);
                for (auto& value :
                     reg.enumValues) {
                    useProjectSources(
                        value, projectPath);
                }
                for (auto& field : reg.fields) {
                    useProjectSources(
                        field, projectPath);
                }
            }
        }
    }
}

} // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent)
{
    stabilityTimer_.setSingleShot(true);
    stabilityTimer_.setInterval(350);
    generatedFileRefreshTimer_.setSingleShot(true);
    generatedFileRefreshTimer_.setInterval(350);
    recoveryDraftTimer_.setSingleShot(true);
    recoveryDraftTimer_.setInterval(1200);

    connect(
        &watcher_,
        &QFileSystemWatcher::fileChanged,
        this,
        &ProjectController::onWatchedFileChanged);
    connect(
        &watcher_,
        &QFileSystemWatcher::directoryChanged,
        this,
        &ProjectController::onWatchedDirectoryChanged);
    connect(
        &stabilityTimer_,
        &QTimer::timeout,
        this,
        &ProjectController::checkPendingFiles);
    connect(
        &generatedFileRefreshTimer_,
        &QTimer::timeout,
        this,
        &ProjectController::refreshGeneratedFileState);
    connect(
        &recoveryDraftTimer_,
        &QTimer::timeout,
        this,
        &ProjectController::writeRecoveryDraft);
}

bool ProjectController::createProject(const QString& manifestPath)
{
    const std::filesystem::path path =
        std::filesystem::absolute(std::filesystem::path(manifestPath.toStdWString()))
            .lexically_normal();
    QString displayName = QFileInfo(fromPath(path)).completeBaseName();
    if (displayName.endsWith(QStringLiteral(".regmap"), Qt::CaseInsensitive)) {
        displayName.chop(7);
    }
    if (displayName.trimmed().isEmpty()) {
        displayName = QStringLiteral("Register Map");
    }
    regmap::NewProject project =
        regmap::makeDefaultProject(
            path,
            displayName.toUtf8()
                .toStdString());
    regmap::ProjectManifest& manifest =
        project.manifest;
    regmap::Workspace& workspace =
        project.workspace;

    const auto diagnostics = regmap::saveProjectFile(manifest, workspace);
    if (containsErrors(diagnostics)) {
        const auto firstError = std::ranges::find_if(
            diagnostics, [](const regmap::Diagnostic& diagnostic) {
                return diagnostic.severity ==
                    regmap::DiagnosticSeverity::error;
            });
        const QString detail =
            firstError == diagnostics.end()
                ? QString {}
                : QStringLiteral(": %1")
                      .arg(fromUtf8(firstError->message));
        QString retainedState = QStringLiteral("no project was opened");
        if (store_.workspace() != nullptr) {
            retainedState =
                store_.dirty()
                    ? QStringLiteral(
                          "current project and unsaved Workbench edits retained")
                    : QStringLiteral("current project retained");
        }
        emit syncStatusChanged(
            QStringLiteral("Could not create %1%2; %3")
                .arg(fromPath(path), detail, retainedState));
        return false;
    }
    openProject(fromPath(path));
    return manifestPath_ == path && manifest_.has_value() &&
        store_.workspace() != nullptr &&
        store_.workspace()->id == workspace.id;
}

const regmap::ProjectManifest* ProjectController::manifest() const noexcept
{
    return manifest_ ? &*manifest_ : nullptr;
}

const regmap::Workspace* ProjectController::workspace() const noexcept
{
    return store_.workspace();
}

const std::vector<regmap::Diagnostic>& ProjectController::diagnostics() const noexcept
{
    return diagnostics_;
}

const std::vector<regmap::GeneratedArtifact>& ProjectController::artifacts() const noexcept
{
    return artifacts_;
}

bool ProjectController::generatedArtifactIsCurrent(
    std::size_t index) const
{
    if (index >= artifacts_.size()) {
        return false;
    }
    if (generatedStateSnapshot_ &&
        generatedStateSnapshot_->revision == artifactsRevision_) {
        return generatedStateSnapshot_->current[index];
    }
    return regmap::inspectGeneratedArtifact(
               artifacts_[index])
        .synchronized();
}

const std::vector<regmap::ModelChange>& ProjectController::changes() const noexcept
{
    return changes_;
}

const std::vector<regmap::ModelChange>&
ProjectController::savedChanges() const noexcept
{
    return savedChanges_;
}

const std::vector<regmap::ModelChange>&
ProjectController::externalChanges() const noexcept
{
    return externalChanges_;
}

const std::vector<regmap::MergeConflict>& ProjectController::conflicts() const noexcept
{
    return conflicts_;
}

const std::filesystem::path& ProjectController::manifestPath() const noexcept
{
    return manifestPath_;
}

bool ProjectController::hasProjectErrors() const noexcept
{
    return containsErrors(loadDiagnostics_) || containsErrors(syncDiagnostics_)
        || containsErrors(store_.diagnostics()) || !conflicts_.empty();
}

bool ProjectController::hasConflicts() const noexcept
{
    return !conflicts_.empty();
}

bool ProjectController::requiresInitialSyncChoice() const noexcept
{
    return initialSyncChoicePending_;
}

bool ProjectController::recoveryDraftAvailable() const
{
    return recoveryDraftInfo()
        .has_value();
}

std::optional<RecoveryDraftInfo>
ProjectController::recoveryDraftInfo(bool includePreview) const
{
    const auto* current = store_.workspace();
    if (manifestPath_.empty() || current == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path path = recoveryDraftPathFor(manifestPath_);
    const QFileInfo draftInfo(fromPath(path));
    if (!draftInfo.exists() || !draftInfo.isFile()) {
        return std::nullopt;
    }
    auto loaded = regmap::loadWorkspaceFromProjectFile(path);
    if (!loaded.workspace ||
        loaded.workspace->id !=
            current->id ||
        regmap::serializeWorkspaceState(
            *loaded.workspace,
            false) ==
            regmap::serializeWorkspaceState(
                *current,
                false)) {
        return std::nullopt;
    }

    RecoveryDraftInfo info;
    if (includePreview) {
        info.differences = regmap::ui::inspectDifferences(*current, *loaded.workspace);
        const QByteArray currentState = QByteArray::fromStdString(
            regmap::serializeWorkspaceState(*current, false));
        const QByteArray draftState = QByteArray::fromStdString(
            regmap::serializeWorkspaceState(*loaded.workspace, false));
        info.revision = QCryptographicHash::hash(currentState + draftState +
            fileDigest(recoveryBasePathFor(manifestPath_)) +
            fileDigest(recoveryMetadataPathFor(manifestPath_)), QCryptographicHash::Sha256);
    }
    if (const auto base =
            loadRecoveryDraftBase();
        base &&
        base->id == current->id) {
        info.mergeBaseAvailable =
            true;
        info.projectChangedSinceDraft =
            regmap::serializeWorkspaceState(
                *base,
                false) !=
            regmap::serializeWorkspaceState(
                *current,
                false);
        const auto merge =
            regmap::mergeWorkspaces(
                *base,
                *loaded.workspace,
                *current,
                regmap::MergePreference::
                    workbench);
        info.conflictCount =
            merge.conflicts.size();
        if (includePreview) info.conflicts = merge.conflicts;
    } else {
        const QDateTime savedModified =
            recoveryBaseModified_.isValid()
            ? recoveryBaseModified_
            : QFileInfo(
                  fromPath(
                      manifestPath_))
                  .lastModified();
        info.projectChangedSinceDraft =
            savedModified.isValid() &&
            draftInfo.lastModified() <=
                savedModified;
    }
    return info;
}

std::vector<regmap::ui::PropertyDifference> ProjectController::changeDetails(
    const QString& origin, const QString& objectId) const
{
    const auto* current = store_.workspace();
    if (!current) return {};
    if (origin == "saved" && store_.savedWorkspace())
        return regmap::ui::inspectDifferences(*store_.savedWorkspace(), *current, objectId);
    if (origin == "external" && externalWorkspace_)
        return regmap::ui::inspectDifferences(*current, *externalWorkspace_, objectId);
    if (origin == "rtl" && baseline_)
        return regmap::ui::inspectDifferences(*baseline_, *current, objectId);
    return {};
}

void ProjectController::deferRecoveryDraft()
{
    recoveryDraftDeferred_ = true;
    recoveryDraftTimer_.stop();
    emit recoveryDraftStatusChanged(QStringLiteral(
        "Earlier recovery draft kept; use Project > Recovery Draft to review it"));
}

QDateTime ProjectController::recoveryDraftModified() const
{
    return manifestPath_.empty()
        ? QDateTime {}
        : QFileInfo(fromPath(recoveryDraftPathFor(manifestPath_))).lastModified();
}

bool ProjectController::hasExternalProjectChange() const noexcept
{
    return externalProjectChangePending_;
}

std::uint64_t ProjectController::externalChangeGeneration() const noexcept
{
    return externalChangeGeneration_;
}

QString ProjectController::externalChangeDigest() const
{
    return QString::fromLatin1(externalManifestDigest_.toHex());
}

QString ProjectController::externalChangeStatus() const
{
    return externalChangeStatus_;
}

std::size_t ProjectController::rejectedExternalChangeCount() const noexcept
{
    return rejectedExternalChangeIds_.size();
}

regmap::WorkspaceChangePlan ProjectController::previewExternalChanges(
    const std::vector<std::string>& changeIds,
    const std::uint64_t generation) const
{
    regmap::WorkspaceChangePlan unavailable;
    const auto fail = [&unavailable](std::string message) {
        unavailable.diagnostics.push_back(
            regmap::Diagnostic{
                "RM5402", regmap::DiagnosticSeverity::error,
                std::move(message), {}, {}});
        return unavailable;
    };
    const auto* current = store_.workspace();
    if (current == nullptr || !externalWorkspace_.has_value()) {
        return fail("No valid external project revision is available.");
    }
    if (generation != externalChangeGeneration_ ||
        externalManifestDigest_.isEmpty() ||
        fileDigest(manifestPath_) != externalManifestDigest_) {
        return fail("The external project revision changed; refresh the diff before deciding.");
    }
    if (!conflicts_.empty() || initialSyncChoicePending_) {
        return fail("Resolve managed RTL conflicts before accepting disk or CLI changes.");
    }
    return regmap::planWorkspaceChanges(
        *current, *externalWorkspace_, changeIds);
}

bool ProjectController::acceptExternalChanges(
    const std::vector<std::string>& changeIds,
    const std::uint64_t generation,
    QString* failureReason)
{
    const regmap::WorkspaceChangePlan plan =
        previewExternalChanges(changeIds, generation);
    if (!plan.valid()) {
        if (failureReason != nullptr) {
            const auto error = std::ranges::find_if(
                plan.diagnostics,
                [](const regmap::Diagnostic& diagnostic) {
                    return diagnostic.severity ==
                        regmap::DiagnosticSeverity::error;
                });
            *failureReason = error == plan.diagnostics.end()
                ? QStringLiteral("The external change preview failed")
                : fromUtf8(error->message);
        }
        return false;
    }
    const regmap::Workspace candidate = *plan.workspace;
    const std::size_t appliedCount = plan.changes.size();
    const bool changed = editWorkspace(
        QStringLiteral("Accept %1 external change(s)").arg(appliedCount),
        [candidate](regmap::Workspace& workspace) {
            workspace = candidate;
        });
    if (!changed) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral(
                "The selected external changes are already present in Workbench");
        }
        return false;
    }
    externalChangeStatus_ = QStringLiteral(
        "Accepted %1 external change(s) into one undoable Workbench edit; disk is unchanged")
        .arg(appliedCount);
    emit syncStatusChanged(externalChangeStatus_);
    emit externalProjectChangeChanged();
    return true;
}

bool ProjectController::rejectExternalChanges(
    const std::vector<std::string>& changeIds,
    const std::uint64_t generation,
    QString* failureReason)
{
    const auto* current = store_.workspace();
    if (current == nullptr || !externalWorkspace_.has_value() ||
        generation != externalChangeGeneration_ ||
        externalManifestDigest_.isEmpty() ||
        fileDigest(manifestPath_) != externalManifestDigest_) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral(
                "The external project revision changed; refresh the diff before deciding");
        }
        return false;
    }
    const regmap::WorkspaceChangePlan plan = regmap::planWorkspaceChanges(
        *current, *externalWorkspace_, changeIds);
    if (plan.changes.empty()) {
        if (failureReason != nullptr) {
            *failureReason = QStringLiteral(
                "No current external changes were selected");
        }
        return false;
    }
    for (const regmap::ModelChange& change : plan.changes) {
        rejectedExternalChangeIds_.insert(change.stableId);
    }
    rebuildExternalChanges();
    externalChangeStatus_ = QStringLiteral(
        "Rejected %1 external change(s) for disk revision %2; Workbench and disk are unchanged")
        .arg(plan.changes.size())
        .arg(QString::fromLatin1(externalManifestDigest_.toHex().left(10)));
    emit comparisonChanged();
    emit externalProjectChangeChanged();
    emit syncStatusChanged(externalChangeStatus_);
    return true;
}

void ProjectController::deferExternalProjectReload()
{
    if (externalProjectChangePending_) {
        externalProjectReloadDeferred_ =
            true;
    }
}

bool ProjectController::restoreRecoveryDraft(
    regmap::MergePreference conflictPreference, const QByteArray& expectedRevision)
{
    const auto* current = store_.workspace();
    if (manifestPath_.empty() || current == nullptr) {
        return false;
    }
    const auto info = recoveryDraftInfo(!expectedRevision.isEmpty());
    if (!expectedRevision.isEmpty() && (!info || info->revision != expectedRevision)) {
        emit recoveryDraftStatusChanged(QStringLiteral(
            "Recovery preview changed; open Recovery Draft again before choosing values"));
        return false;
    }
    if (!info) {
        emit recoveryDraftStatusChanged(
            QStringLiteral(
                "Recovery draft is invalid, unchanged, or older than the saved project"));
        return false;
    }
    auto loaded = regmap::loadWorkspaceFromProjectFile(
        recoveryDraftPathFor(manifestPath_));
    if (!loaded.workspace || loaded.workspace->id != current->id) {
        emit recoveryDraftStatusChanged(
            QStringLiteral("Recovery draft is invalid or belongs to another project"));
        return false;
    }
    regmap::Workspace recovered;
    if (const auto base =
            loadRecoveryDraftBase();
        base &&
        base->id == current->id) {
        auto merged =
            regmap::mergeWorkspaces(
                *base,
                *loaded.workspace,
                *current,
                conflictPreference);
        if (!merged.merged) {
            emit recoveryDraftStatusChanged(
                QStringLiteral(
                    "Recovery draft could not be merged with the saved project"));
            return false;
        }
        recovered =
            std::move(
                *merged.merged);
    } else {
        recovered =
            std::move(
                *loaded.workspace);
    }
    useProjectSources(recovered, manifestPath_);
    const bool changed = store_.transact(
        "Restore recovery draft",
        [recovered = std::move(recovered)](regmap::Workspace& workspace) {
            workspace = recovered;
        });
    if (!changed) {
        discardRecoveryDraft();
        emit recoveryDraftStatusChanged(
            QStringLiteral(
                "Recovery draft resolved without model changes; the selected "
                "result is already current"));
        return true;
    }
    recoveryDraftDeferred_ = false;
    notifyModelEdited();
    QString message =
        info->mergeBaseAvailable &&
                info->projectChangedSinceDraft
            ? QStringLiteral(
                  "Recovery draft merged with newer project changes")
            : QStringLiteral(
                  "Recovery draft restored");
    if (info->conflictCount > 0) {
        message +=
            conflictPreference ==
                    regmap::MergePreference::
                        workbench
            ? QStringLiteral(
                  "; draft values kept for %1 conflict(s)")
                  .arg(
                      info->conflictCount)
            : QStringLiteral(
                  "; disk values kept for %1 conflict(s)")
                  .arg(
                      info->conflictCount);
    }
    message +=
        QStringLiteral(
            " with %1 pending object change(s); source links now target "
            "the project file; Save & Sync to make it permanent")
            .arg(changes_.size());
    emit recoveryDraftStatusChanged(
        message);
    return true;
}

void ProjectController::discardRecoveryDraft()
{
    recoveryDraftDeferred_ = false;
    discardRecoveryDraft(manifestPath_);
}

void ProjectController::discardRecoveryDraft(
    const std::filesystem::path& projectPath)
{
    if (projectPath.empty()) {
        return;
    }
    static_cast<void>(
        QFile::remove(
            fromPath(
                recoveryDraftPathFor(
                    projectPath))));
    static_cast<void>(
        QFile::remove(
            fromPath(
                recoveryBasePathFor(
                    projectPath))));
    static_cast<void>(
        QFile::remove(
            fromPath(
                recoveryMetadataPathFor(
                    projectPath))));
}

void ProjectController::writeRecoveryDraft()
{
    if (recoveryDraftDeferred_) return;
    if (!manifest_ || !store_.workspace() || !store_.dirty()) {
        return;
    }
    regmap::ProjectManifest draftManifest = *manifest_;
    const std::filesystem::path draftPath =
        recoveryDraftPathFor(
            manifestPath_);
    const std::filesystem::path basePath =
        recoveryBasePathFor(
            manifestPath_);
    const std::filesystem::path metadataPath =
        recoveryMetadataPathFor(
            manifestPath_);
    draftManifest.manifestPath =
        draftPath;
    std::vector<regmap::Diagnostic> diagnostics;
    QByteArray baseDigest;
    if (recoveryBaseWorkspace_) {
        baseDigest = fileDigest(basePath);
        if (recoveryBaseDigest_.isEmpty() || baseDigest != recoveryBaseDigest_) {
            appendDiagnostics(
                diagnostics,
                regmap::saveSyncBaseline(basePath, *recoveryBaseWorkspace_));
            if (!containsErrors(diagnostics)) {
                baseDigest = fileDigest(basePath);
                recoveryBaseDigest_ = baseDigest;
            }
        }
    }
    appendDiagnostics(
        diagnostics,
        regmap::saveProjectFile(
            draftManifest,
            *store_.workspace()));
    const bool metadataSaved =
        !containsErrors(diagnostics) &&
        recoveryBaseWorkspace_.has_value() &&
        writeRecoveryMetadata(
            metadataPath,
            baseDigest,
            fileDigest(draftPath),
            store_.workspace()->id);
    emit recoveryDraftStatusChanged(
        containsErrors(diagnostics)
            ? QStringLiteral("Recovery draft could not be saved")
            : !metadataSaved
            ? QStringLiteral(
                  "Recovery draft saved, but its merge baseline could not be saved")
            : QStringLiteral("Recovery draft saved at %1; project remains unsaved")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}
bool ProjectController::isDirty() const
{
    return store_.dirty();
}

bool ProjectController::canUndo() const noexcept
{
    return store_.canUndo();
}

bool ProjectController::canRedo() const noexcept
{
    return store_.canRedo();
}

QString ProjectController::undoText() const
{
    return fromUtf8(store_.undoText());
}

QString ProjectController::redoText() const
{
    return fromUtf8(store_.redoText());
}

std::size_t ProjectController::undoDepth() const noexcept
{
    return store_.undoDepth();
}

bool ProjectController::editWorkspace(
    const QString& description,
    const regmap::WorkspaceStore::Mutation& mutation)
{
    const bool changed = store_.transact(description.toUtf8().toStdString(), mutation);
    if (changed) {
        notifyModelEdited();
    }
    return changed;
}

bool ProjectController::squashUndoSince(
    std::size_t startingDepth,
    const QString& description)
{
    const bool squashed =
        store_.squashUndoSince(startingDepth, description.toUtf8().toStdString());
    if (squashed) {
        emit editStateChanged();
    }
    return squashed;
}

bool ProjectController::openProject(const QString& manifestPath)
{
    const std::filesystem::path requestedPath =
        std::filesystem::absolute(std::filesystem::path(manifestPath.toStdWString()))
            .lexically_normal();
    const QByteArray requestedDigest =
        fileDigest(requestedPath);
    auto loaded = regmap::openProject(requestedPath);
    const std::optional<std::string> requestedWorkspaceId =
        loaded.workspace ? std::optional<std::string>(loaded.workspace->id)
                         : std::nullopt;
    if (store_.workspace() != nullptr &&
        (!loaded.manifest.has_value() || !loaded.workspace.has_value())) {
        const auto firstError = std::ranges::find_if(
            loaded.diagnostics, [](const regmap::Diagnostic& diagnostic) {
                return diagnostic.severity == regmap::DiagnosticSeverity::error;
            });
        const QString fileName = QFileInfo(fromPath(requestedPath)).fileName();
        const QString detail = firstError == loaded.diagnostics.end()
            ? QString {}
            : QStringLiteral(": %1").arg(fromUtf8(firstError->message));
        emit syncStatusChanged(
            QStringLiteral("Could not open %1%2; current project retained")
                .arg(fileName, detail));
        return false;
    }

    manifestPath_ = requestedPath;
    recoveryBaseModified_ =
        QFileInfo(fromPath(requestedPath))
            .lastModified();
    manifest_.reset();
    store_ = regmap::WorkspaceStore {};
    baseline_.reset();
    recoveryBaseWorkspace_.reset();
    recoveryBaseDigest_.clear();
    initialSyncChoicePending_ = false;
    recoveryDraftDeferred_ = false;
    artifacts_.clear();
    ++artifactsRevision_;
    changes_.clear();
    savedChanges_.clear();
    externalChanges_.clear();
    externalWorkspace_.reset();
    externalManifestDigest_.clear();
    externalChangeGeneration_ = 0;
    rejectedExternalChangeIds_.clear();
    externalChangeStatus_.clear();
    conflicts_.clear();
    loadDiagnostics_.clear();
    syncDiagnostics_.clear();
    generationDiagnostics_.clear();
    diagnostics_.clear();
    lastAcceptedModelWasValid_ = false;
    externalProjectChangePending_ = false;
    externalProjectReloadDeferred_ =
        false;
    acceptedManifestDigest_.clear();
    acceptedManifestDigestKnown_ =
        false;
    reloadImpl(
        false,
        &loaded,
        requestedDigest);
    return requestedWorkspaceId.has_value() && manifestPath_ == requestedPath &&
        store_.workspace() != nullptr &&
        store_.workspace()->id == *requestedWorkspaceId;
}

void ProjectController::reload()
{
    recoveryBaseModified_ =
        QFileInfo(fromPath(manifestPath_))
            .lastModified();
    reloadImpl(false);
}

void ProjectController::reloadImpl(
    bool automatic,
    regmap::ProjectOpenResult* preloaded,
    const QByteArray& preloadedDigest)
{
    if (manifestPath_.empty()) {
        return;
    }
    if (automatic && store_.dirty()) {
        emit syncStatusChanged(
            QStringLiteral("External project change detected; local Workbench edits are retained"));
        refreshWatchPaths();
        return;
    }

    emit syncStatusChanged(
        automatic ? QStringLiteral("Synchronizing saved project data...")
                  : QStringLiteral("Loading project..."));

    const QByteArray loadedDigest =
        preloaded == nullptr
        ? fileDigest(manifestPath_)
        : preloadedDigest;
    auto loaded =
        preloaded == nullptr
        ? regmap::openProject(manifestPath_)
        : std::move(*preloaded);
    loadDiagnostics_ = std::move(loaded.diagnostics);
    std::erase_if(loadDiagnostics_, isValidationDiagnostic);
    changes_.clear();
    conflicts_.clear();
    initialSyncChoicePending_ = false;
    syncDiagnostics_.clear();

    if (loaded.manifest.has_value() && loaded.workspace.has_value()) {
        if (const auto* current = store_.workspace()) {
            changes_ = regmap::diffWorkspaces(*current, *loaded.workspace);
        }
        manifest_ = std::move(loaded.manifest);
        store_.reset(std::move(*loaded.workspace));
        clearExternalComparison();
        rebuildSavedChanges();
        recoveryBaseWorkspace_ =
            *store_.workspace();
        recoveryBaseDigest_.clear();
        acceptedManifestDigest_ =
            loadedDigest;
        acceptedManifestDigestKnown_ =
            true;
        setExternalProjectChangePending(
            manifestChangedOnDisk());
        if (externalProjectChangePending_) {
            refreshExternalChangesFromDisk();
        }
        lastAcceptedModelWasValid_ = true;
        artifacts_.clear();
        ++artifactsRevision_;
        generationDiagnostics_.clear();
        initializeSynchronization();
        rebuildDiagnostics();
        refreshWatchPaths();
        emit projectChanged();
        emit diagnosticsChanged();
        emit generationChanged();
        emit conflictsChanged();
        emit comparisonChanged();
        emit editStateChanged();

        if (externalProjectChangePending_) {
            emit syncStatusChanged(
                QStringLiteral(
                    "Project changed again while it was loading; "
                    "Workbench kept the loaded model and paused saving"));
        } else if (
            conflicts_.empty() &&
            !containsErrors(syncDiagnostics_)) {
            emit syncStatusChanged(
                automatic
                    ? QStringLiteral("Synchronized: %1 object change(s)").arg(changes_.size())
                    : QStringLiteral("Project and managed RTL loaded"));
        }
        return;
    }

    setExternalProjectChangePending(
        manifestChangedOnDisk());
    if (!lastAcceptedModelWasValid_) {
        manifest_ = std::move(loaded.manifest);
        store_ = regmap::WorkspaceStore {};
        baseline_.reset();
        emit projectChanged();
        emit editStateChanged();
    }
    rebuildDiagnostics();
    refreshWatchPaths();
    emit diagnosticsChanged();
    const auto firstError = std::ranges::find_if(
        loadDiagnostics_, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.severity ==
                regmap::DiagnosticSeverity::error;
        });
    const QString fileName =
        QFileInfo(fromPath(manifestPath_)).fileName();
    const QString detail = firstError == loadDiagnostics_.end()
        ? QString {}
        : QStringLiteral(": %1").arg(fromUtf8(firstError->message));
    emit syncStatusChanged(
        lastAcceptedModelWasValid_
            ? QStringLiteral("Synchronization rejected; retaining the last loadable model")
            : QStringLiteral("Could not open %1%2; no project loaded")
                  .arg(fileName, detail));
}

void ProjectController::initializeSynchronization()
{
    const auto* current = store_.workspace();
    if (!manifest_ || current == nullptr) {
        return;
    }
    initialSyncChoicePending_ = false;
    const std::filesystem::path statePath = baselinePath();
    if (QFileInfo::exists(fromPath(statePath))) {
        auto loaded = regmap::loadSyncBaseline(statePath);
        syncDiagnostics_ = std::move(loaded.diagnostics);
        if (!loaded.workspace) {
            baseline_.reset();
            rebuildDiagnostics();
            emit syncStatusChanged(
                QStringLiteral("Synchronization baseline is invalid; automatic RTL merge is blocked"));
            return;
        }
        baseline_ = std::move(loaded.workspace);
        synchronizeRtl(true, true);
        return;
    }

    const std::filesystem::path rtlPath = manifest_->rtl.path.resolved;
    if (!QFileInfo::exists(fromPath(rtlPath))) {
        baseline_ = *current;
        synchronizeRtl(true, true);
        return;
    }

    auto parsed = regmap::parseManagedRtl(rtlPath);
    syncDiagnostics_ = std::move(parsed.diagnostics);
    if (!parsed.workspace || containsErrors(syncDiagnostics_)) {
        baseline_.reset();
        rebuildDiagnostics();
        emit syncStatusChanged(
            QStringLiteral("Managed RTL is invalid; initial synchronization is blocked"));
        return;
    }

    if (regmap::serializeWorkspaceState(*current, false) ==
        regmap::serializeWorkspaceState(*parsed.workspace, false)) {
        baseline_ = *current;
        synchronizeRtl(true, true);
        return;
    }

    baseline_.reset();
    initialSyncChoicePending_ = true;
    changes_ = regmap::diffWorkspaces(*current, *parsed.workspace);
    regmap::MergeConflict conflict;
    conflict.objectId = current->id;
    conflict.objectKind = regmap::ObjectKind::workspace;
    conflict.objectName = current->name;
    conflict.property = "<initial-sync>";
    conflict.workbenchValue = "current Workbench model";
    conflict.rtlValue = "existing managed RTL";
    conflicts_.push_back(std::move(conflict));
    rebuildDiagnostics();
    emit syncStatusChanged(
        QStringLiteral("Initial synchronization paused: no baseline exists and Workbench differs "
                       "from managed RTL; choose which source to keep. No file was overwritten."));
}

void ProjectController::save()
{
    if (!manifest_ || !store_.workspace()) {
        return;
    }
    if (containsErrors(loadDiagnostics_)) {
        rebuildDiagnostics();
        emit diagnosticsChanged();
        emit syncStatusChanged(
            QStringLiteral(
                "Save blocked by project migration errors; repair the reported source values first"));
        return;
    }
    if (manifestChangedOnDisk()) {
        setExternalProjectChangePending(
            true);
        emit syncStatusChanged(
            QStringLiteral(
                "Save paused: the project changed on disk after it was loaded; "
                "no file was overwritten"));
        emit externalProjectSaveConflict();
        return;
    }
    setExternalProjectChangePending(
        false);
    emit syncStatusChanged(QStringLiteral("Merging Workbench and managed RTL..."));
    synchronizeRtl(false, true);
}

void ProjectController::saveOverExternalProjectChange()
{
    if (!manifest_ || !store_.workspace()) {
        return;
    }
    acceptedManifestDigest_ =
        fileDigest(manifestPath_);
    acceptedManifestDigestKnown_ =
        true;
    setExternalProjectChangePending(
        false);
    save();
}

void ProjectController::undo()
{
    if (store_.undo()) {
        notifyModelEdited();
    }
}

void ProjectController::redo()
{
    if (store_.redo()) {
        notifyModelEdited();
    }
}

void ProjectController::synchronizeNow()
{
    synchronizeRtl(false, true);
}

void ProjectController::useWorkbenchForConflicts()
{
    resolveConflicts(regmap::MergePreference::workbench);
}

void ProjectController::useRtlForConflicts()
{
    resolveConflicts(regmap::MergePreference::rtl);
}

void ProjectController::synchronizeRtl(bool automatic, bool persistWhenClean)
{
    if (!manifest_ || !store_.workspace()) {
        return;
    }
    if (initialSyncChoicePending_) {
        emit syncStatusChanged(
            QStringLiteral("Initial synchronization is paused; choose Workbench or RTL. No file "
                           "was written."));
        return;
    }
    if (!baseline_) {
        initializeSynchronization();
        refreshWatchPaths();
        emit projectChanged();
        emit diagnosticsChanged();
        emit conflictsChanged();
        emit editStateChanged();
        return;
    }

    syncDiagnostics_.clear();
    const std::filesystem::path rtlPath = manifest_->rtl.path.resolved;
    regmap::Workspace rtlWorkspace = *store_.workspace();
    if (QFileInfo::exists(fromPath(rtlPath))) {
        auto parsed = regmap::parseManagedRtl(rtlPath);
        syncDiagnostics_ = std::move(parsed.diagnostics);
        if (!parsed.workspace || containsErrors(syncDiagnostics_)) {
            rebuildDiagnostics();
            refreshWatchPaths();
            emit diagnosticsChanged();
            emit syncStatusChanged(
                store_.dirty()
                    ? QStringLiteral(
                          "Save & Sync blocked: managed RTL contains errors; "
                          "Workbench edits remain unsaved. Fix RTL, then use "
                          "Save & Sync again.")
                    : QStringLiteral(
                          "Managed RTL contains errors; the last Workbench model "
                          "is retained. Fix RTL, then retry synchronization."));
            return;
        }
        rtlWorkspace = std::move(*parsed.workspace);
    }

    auto merge = regmap::mergeWorkspaces(
        *baseline_, *store_.workspace(), rtlWorkspace, regmap::MergePreference::workbench);
    conflicts_ = std::move(merge.conflicts);
    emit conflictsChanged();
    if (!conflicts_.empty() || !merge.merged) {
        rebuildDiagnostics();
        refreshWatchPaths();
        emit diagnosticsChanged();
        emit projectChanged();
        emit syncStatusChanged(
            QStringLiteral("Synchronization paused: %1 conflict(s) require a resolution")
                .arg(conflicts_.size()));
        return;
    }

    const regmap::Workspace mergedWorkspace = *merge.merged;
    const bool modelChanged =
        store_.transact("Merge managed RTL", [mergedWorkspace](regmap::Workspace& workspace) {
            workspace = mergedWorkspace;
        });
    if (modelChanged) {
        changes_ = regmap::diffWorkspaces(*baseline_, *store_.workspace());
        rebuildSavedChanges();
        emit projectChanged();
        emit editStateChanged();
        emit comparisonChanged();
    }

    if (persistWhenClean) {
        if (persistSynchronizedModel(automatic)) {
            emit syncStatusChanged(
                automatic ? QStringLiteral("Workbench and managed RTL synchronized")
                          : QStringLiteral("Project, managed RTL, and read-only outputs saved"));
        }
        return;
    }

    rebuildDiagnostics();
    refreshWatchPaths();
    emit diagnosticsChanged();
    emit syncStatusChanged(
        QStringLiteral("Managed RTL changes merged; save to update all synchronized files"));
}

void ProjectController::resolveConflicts(regmap::MergePreference preference)
{
    if (!manifest_ || !store_.workspace()) {
        return;
    }

    if (initialSyncChoicePending_) {
        const std::filesystem::path rtlPath = manifest_->rtl.path.resolved;
        std::optional<regmap::Workspace> currentRtl;
        if (QFileInfo::exists(fromPath(rtlPath))) {
            auto parsed = regmap::parseManagedRtl(rtlPath);
            syncDiagnostics_ = std::move(parsed.diagnostics);
            if (!parsed.workspace || containsErrors(syncDiagnostics_)) {
                rebuildDiagnostics();
                emit diagnosticsChanged();
                emit syncStatusChanged(
                    QStringLiteral("Cannot resolve the initial choice while managed RTL is invalid"));
                return;
            }
            currentRtl = std::move(parsed.workspace);
        } else if (preference == regmap::MergePreference::rtl) {
            emit syncStatusChanged(
                QStringLiteral("Managed RTL no longer exists; choose Workbench or reload"));
            return;
        }

        const regmap::Workspace resolved =
            preference == regmap::MergePreference::rtl ? *currentRtl : *store_.workspace();
        const regmap::Workspace retryBaseline =
            currentRtl.has_value() ? *currentRtl : resolved;
        if (preference == regmap::MergePreference::rtl) {
            static_cast<void>(store_.transact(
                "Use managed RTL for initial synchronization",
                [resolved](regmap::Workspace& workspace) { workspace = resolved; }));
        }
        baseline_ = retryBaseline;
        initialSyncChoicePending_ = false;
        conflicts_.clear();
        changes_ = regmap::diffWorkspaces(*baseline_, *store_.workspace());
        rebuildSavedChanges();
        emit conflictsChanged();
        emit projectChanged();
        emit editStateChanged();
        emit comparisonChanged();
        if (persistSynchronizedModel()) {
            emit syncStatusChanged(
                QStringLiteral("Initial choice resolved; Workbench, RTL, and read-only outputs "
                               "synchronized"));
        }
        return;
    }

    if (!baseline_) {
        emit syncStatusChanged(
            QStringLiteral("Synchronization baseline is unavailable; conflicts cannot be resolved"));
        return;
    }
    auto parsed = regmap::parseManagedRtl(manifest_->rtl.path.resolved);
    syncDiagnostics_ = std::move(parsed.diagnostics);
    if (!parsed.workspace || containsErrors(syncDiagnostics_)) {
        rebuildDiagnostics();
        emit diagnosticsChanged();
        emit syncStatusChanged(QStringLiteral("Cannot resolve conflicts while managed RTL is invalid"));
        return;
    }
    auto merge = regmap::mergeWorkspaces(
        *baseline_, *store_.workspace(), *parsed.workspace, preference);
    if (!merge.merged) {
        emit syncStatusChanged(QStringLiteral("Conflict resolution could not build a merged model"));
        return;
    }
    const regmap::Workspace resolved = *merge.merged;
    static_cast<void>(
        store_.transact(
            "Resolve synchronization conflicts",
            [resolved](regmap::Workspace& workspace) { workspace = resolved; }));
    conflicts_.clear();
    rebuildSavedChanges();
    emit conflictsChanged();
    emit projectChanged();
    emit editStateChanged();
    emit comparisonChanged();
    if (persistSynchronizedModel()) {
        emit syncStatusChanged(
            QStringLiteral("Conflicts resolved; Workbench, RTL, and read-only outputs synchronized"));
    }
}

bool ProjectController::persistSynchronizedModel(
    bool preserveDivergentRecoveryDraft)
{
    if (!manifest_ || !store_.workspace()) {
        return false;
    }
    if (containsErrors(loadDiagnostics_) || containsErrors(store_.diagnostics())) {
        rebuildDiagnostics();
        emit diagnosticsChanged();
        emit syncStatusChanged(
            containsErrors(loadDiagnostics_)
                ? QStringLiteral("Save blocked by project migration errors; repair the reported source values first")
                : QStringLiteral("Save blocked by model validation errors"));
        return false;
    }
    if (manifestChangedOnDisk()) {
        setExternalProjectChangePending(
            true);
        rebuildDiagnostics();
        emit diagnosticsChanged();
        emit syncStatusChanged(
            QStringLiteral(
                "Save paused: the project changed on disk during synchronization; "
                "Workbench edits and the external file were both retained"));
        return false;
    }

    const bool keepRecoveryDraft =
        preserveDivergentRecoveryDraft && recoveryDraftAvailable();

    if (!watcher_.files().isEmpty()) {
        watcher_.removePaths(watcher_.files());
    }
    if (!watcher_.directories().isEmpty()) {
        watcher_.removePaths(watcher_.directories());
    }
    syncDiagnostics_.clear();
    appendDiagnostics(
        syncDiagnostics_, regmap::saveProjectFile(*manifest_, *store_.workspace()));
    if (!containsErrors(syncDiagnostics_)) {
        acceptedManifestDigest_ =
            fileDigest(manifestPath_);
        acceptedManifestDigestKnown_ =
            true;
        setExternalProjectChangePending(
            false);
        clearExternalComparison();
    }
    if (!containsErrors(syncDiagnostics_)) {
        appendDiagnostics(
            syncDiagnostics_,
            regmap::writeManagedRtl(
                manifest_->rtl.path.resolved,
                manifest_->rtl.moduleName,
                *store_.workspace()));
    }
    if (!containsErrors(syncDiagnostics_)) {
        appendDiagnostics(
            syncDiagnostics_, regmap::saveSyncBaseline(baselinePath(), *store_.workspace()));
    }
    if (containsErrors(syncDiagnostics_)) {
        rebuildDiagnostics();
        refreshWatchPaths();
        emit diagnosticsChanged();
        emit syncStatusChanged(QStringLiteral("Synchronized save failed; baseline was not advanced"));
        return false;
    }

    baseline_ = *store_.workspace();
    store_.markSaved();
    rebuildSavedChanges();
    recoveryBaseWorkspace_ =
        *store_.workspace();
    recoveryBaseDigest_.clear();
    recoveryDraftTimer_.stop();
    if (!keepRecoveryDraft && !recoveryDraftDeferred_) {
        discardRecoveryDraft();
        recoveryBaseModified_ =
            QFileInfo(fromPath(manifestPath_))
                .lastModified();
    }
    changes_.clear();
    conflicts_.clear();
    loadDiagnostics_.clear();
    generationDiagnostics_.clear();
    generateImpl(true);
    if (containsErrors(generationDiagnostics_)) {
        emit syncStatusChanged(
            QStringLiteral("Model and RTL saved; one or more read-only outputs failed"));
    }
    rebuildDiagnostics();
    refreshWatchPaths();
    emit projectChanged();
    emit diagnosticsChanged();
    emit conflictsChanged();
    emit editStateChanged();
    emit comparisonChanged();
    return !containsErrors(generationDiagnostics_);
}

bool ProjectController::manifestChangedOnDisk() const
{
    if (manifestPath_.empty() ||
        !acceptedManifestDigestKnown_) {
        return false;
    }
    return fileDigest(manifestPath_) !=
        acceptedManifestDigest_;
}

void ProjectController::setExternalProjectChangePending(
    bool pending)
{
    if (externalProjectChangePending_ ==
        pending) {
        return;
    }
    externalProjectChangePending_ =
        pending;
    if (!pending) {
        externalProjectReloadDeferred_ =
            false;
    }
    emit externalProjectChangeChanged();
}

void ProjectController::generateNow()
{
    synchronizeRtl(false, true);
}

void ProjectController::generateImpl(bool automatic)
{
    if (!manifest_ || !store_.workspace() || hasProjectErrors()) {
        if (!automatic) {
            emit syncStatusChanged(QStringLiteral("Generation blocked by project or synchronization errors"));
        }
        return;
    }

    if (!automatic) {
        emit syncStatusChanged(QStringLiteral("Generating read-only artifacts..."));
    }
    auto generation = regmap::generateArtifacts(*store_.workspace(), *manifest_);
    artifacts_ = std::move(generation.artifacts);
    ++artifactsRevision_;
    generationDiagnostics_ = std::move(generation.diagnostics);
    if (!containsErrors(generationDiagnostics_)) {
        appendDiagnostics(
            generationDiagnostics_, regmap::writeGeneratedArtifacts(artifacts_));
    }
    rebuildDiagnostics();
    emit generationChanged();
    emit diagnosticsChanged();

    if (!automatic) {
        emit syncStatusChanged(
            containsErrors(generationDiagnostics_)
                ? QStringLiteral("Generation failed")
                : QStringLiteral("Generated %1 read-only artifact(s)").arg(artifacts_.size()));
    }
}

void ProjectController::rebuildDiagnostics()
{
    diagnostics_ = loadDiagnostics_;
    diagnostics_.insert(
        diagnostics_.end(), syncDiagnostics_.begin(), syncDiagnostics_.end());
    diagnostics_.insert(
        diagnostics_.end(), store_.diagnostics().begin(), store_.diagnostics().end());
    diagnostics_.insert(
        diagnostics_.end(), generationDiagnostics_.begin(), generationDiagnostics_.end());
    for (const auto& conflict : conflicts_) {
        regmap::Diagnostic diagnostic;
        if (conflict.property == "<initial-sync>") {
            diagnostic.code = "RM5301";
            diagnostic.message =
                "No synchronization baseline exists and Workbench differs from managed RTL. "
                "Choose which source to keep; no file has been overwritten.";
        } else {
            diagnostic.code = "RM5300";
            diagnostic.message = "Workbench and managed RTL changed property '" +
                conflict.property + "' differently.";
        }
        diagnostic.objectId = conflict.objectId;
        if (manifest_) {
            diagnostic.source.workbook = manifest_->rtl.path.resolved;
            diagnostic.source.sheet = "Managed RTL";
        }
        diagnostics_.push_back(std::move(diagnostic));
    }
}

void ProjectController::refreshWatchPaths()
{
    if (!watcher_.files().isEmpty()) {
        watcher_.removePaths(watcher_.files());
    }
    if (!watcher_.directories().isEmpty()) {
        watcher_.removePaths(watcher_.directories());
    }

    QStringList files;
    QStringList directories;
    if (!manifestPath_.empty()) {
        const QString projectFile =
            fromPath(manifestPath_);
        const QFileInfo information(
            projectFile);
        if (information.exists()) {
            files.push_back(
                projectFile);
        }
        const QString directory =
            information.absolutePath();
        if (QFileInfo(directory).isDir()) {
            directories.push_back(
                directory);
        }
    }
    if (manifest_) {
        const QString rtlFile = fromPath(manifest_->rtl.path.resolved);
        if (QFileInfo::exists(rtlFile) && !files.contains(rtlFile)) {
            files.push_back(rtlFile);
        }
    }
    for (const auto& artifact : artifacts_) {
        const QString outputFile = fromPath(artifact.path);
        const QFileInfo information(outputFile);
        if (information.exists() && !files.contains(outputFile)) {
            files.push_back(outputFile);
        }
        const QString directory = information.absolutePath();
        if (QFileInfo(directory).isDir() &&
            !directories.contains(directory)) {
            directories.push_back(directory);
        }
    }
    if (!files.empty()) {
        watcher_.addPaths(files);
    }
    if (!directories.empty()) {
        watcher_.addPaths(directories);
    }
}

void ProjectController::notifyModelEdited()
{
    changes_ = baseline_ && store_.workspace()
        ? regmap::diffWorkspaces(*baseline_, *store_.workspace())
        : std::vector<regmap::ModelChange> {};
    rebuildSavedChanges();
    rebuildExternalChanges();
    if (store_.dirty()) {
        recoveryDraftTimer_.start();
    } else {
        recoveryDraftTimer_.stop();
        if (!recoveryDraftDeferred_) discardRecoveryDraft();
    }

    rebuildDiagnostics();
    emit projectChanged();
    emit diagnosticsChanged();
    emit generationChanged();
    emit editStateChanged();
    emit comparisonChanged();
    if (!store_.dirty()) {
        if (containsErrors(generationDiagnostics_)) {
            emit syncStatusChanged(
                QStringLiteral(
                    "Saved model restored; one or more read-only outputs "
                    "remain failed"));
        } else {
            bool outputsCurrent = true;
            for (std::size_t index = 0;
                 index < artifacts_.size(); ++index) {
                if (!generatedArtifactIsCurrent(index)) {
                    outputsCurrent = false;
                    break;
                }
            }
            emit syncStatusChanged(
                outputsCurrent
                    ? QStringLiteral(
                          "Saved model restored; read-only outputs are current")
                    : QStringLiteral(
                          "Saved model restored; one or more read-only "
                          "outputs need regeneration"));
        }
    } else {
        emit syncStatusChanged(
            hasProjectErrors()
                ? QStringLiteral(
                      "Model edited; resolve validation/synchronization errors")
                : QStringLiteral(
                      "Model edited; save to merge with RTL and update outputs"));
    }
}

void ProjectController::rebuildSavedChanges()
{
    const auto* saved = store_.savedWorkspace();
    const auto* current = store_.workspace();
    savedChanges_ = saved != nullptr && current != nullptr
        ? regmap::diffWorkspaces(*saved, *current)
        : std::vector<regmap::ModelChange>{};
}

void ProjectController::rebuildExternalChanges()
{
    const auto* current = store_.workspace();
    if (current == nullptr || !externalWorkspace_.has_value() ||
        externalWorkspace_->id != current->id) {
        externalChanges_.clear();
        return;
    }
    externalChanges_ = regmap::diffWorkspaces(*current, *externalWorkspace_);
    std::erase_if(
        externalChanges_,
        [this](const regmap::ModelChange& change) {
            return rejectedExternalChangeIds_.contains(change.stableId);
        });
    const QString revision = QString::fromLatin1(
        externalManifestDigest_.toHex().left(10));
    if (externalChanges_.empty() && !rejectedExternalChangeIds_.empty()) {
        externalChangeStatus_ = QStringLiteral(
            "All observed external changes are rejected for disk revision %1; Save remains paused")
            .arg(revision);
    } else if (externalChanges_.empty()) {
        externalChangeStatus_ = QStringLiteral(
            "Workbench matches disk revision %1; Save or Reload completes the reconciliation")
            .arg(revision);
    } else {
        externalChangeStatus_ = QStringLiteral(
            "%1 external change(s) pending for disk revision %2; %3 rejected")
            .arg(externalChanges_.size())
            .arg(revision)
            .arg(rejectedExternalChangeIds_.size());
    }
}

void ProjectController::clearExternalComparison()
{
    externalChanges_.clear();
    externalWorkspace_.reset();
    externalManifestDigest_.clear();
    rejectedExternalChangeIds_.clear();
    externalChangeStatus_.clear();
}

void ProjectController::refreshExternalChangesFromDisk()
{
    const auto* current = store_.workspace();
    if (current == nullptr || manifestPath_.empty()) {
        clearExternalComparison();
        emit comparisonChanged();
        return;
    }
    const QByteArray digest = fileDigest(manifestPath_);
    if (digest != externalManifestDigest_) {
        externalManifestDigest_ = digest;
        ++externalChangeGeneration_;
        rejectedExternalChangeIds_.clear();
    }
    const auto loaded = regmap::openProject(manifestPath_);
    if (loaded.workspace.has_value() &&
        loaded.workspace->id == current->id) {
        externalWorkspace_ = *loaded.workspace;
        rebuildExternalChanges();
    } else {
        externalWorkspace_.reset();
        externalChanges_.clear();
        externalChangeStatus_ = QStringLiteral(
            "The observed external project revision is invalid; Accept and Reject are unavailable");
    }
    emit comparisonChanged();
    emit externalProjectChangeChanged();
}

std::filesystem::path ProjectController::baselinePath() const
{
    if (manifestPath_.empty()) {
        return {};
    }
    return manifestPath_.parent_path()
        / (manifestPath_.filename().generic_string() + ".sync.json");
}

std::filesystem::path ProjectController::recoveryDraftPathFor(
    const std::filesystem::path& projectPath)
{
    if (projectPath.empty()) {
        return {};
    }
    return projectPath.parent_path() /
        ".regmap-workbench" /
        (projectPath.filename().generic_string() +
         ".autosave.yaml");
}

std::filesystem::path ProjectController::recoveryBasePathFor(
    const std::filesystem::path& projectPath)
{
    if (projectPath.empty()) {
        return {};
    }
    return projectPath.parent_path() /
        ".regmap-workbench" /
        (projectPath.filename().generic_string() +
         ".autosave.base.json");
}

std::filesystem::path ProjectController::recoveryMetadataPathFor(
    const std::filesystem::path& projectPath)
{
    if (projectPath.empty()) {
        return {};
    }
    return projectPath.parent_path() /
        ".regmap-workbench" /
        (projectPath.filename().generic_string() +
         ".autosave.meta.json");
}

std::optional<regmap::Workspace>
ProjectController::loadRecoveryDraftBase() const
{
    if (manifestPath_.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path draftPath =
        recoveryDraftPathFor(
            manifestPath_);
    const std::filesystem::path basePath =
        recoveryBasePathFor(
            manifestPath_);
    QFile metadataFile(
        fromPath(
            recoveryMetadataPathFor(
                manifestPath_)));
    if (!metadataFile.open(
            QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            metadataFile.readAll(),
            &parseError);
    if (parseError.error !=
            QJsonParseError::NoError ||
        !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject metadata =
        document.object();
    const QString expectedBase =
        metadata.value(
                    QStringLiteral(
                        "base_sha256"))
            .toString();
    const QString expectedDraft =
        metadata.value(
                    QStringLiteral(
                        "draft_sha256"))
            .toString();
    if (metadata.value(
                    QStringLiteral(
                        "format_version"))
                .toInt() != 1 ||
        expectedBase.size() != 64 ||
        expectedDraft.size() != 64 ||
        expectedBase !=
            QString::fromLatin1(
                fileDigest(
                    basePath)
                    .toHex()) ||
        expectedDraft !=
            QString::fromLatin1(
                fileDigest(
                    draftPath)
                    .toHex())) {
        return std::nullopt;
    }
    auto loaded =
        regmap::loadSyncBaseline(
            basePath);
    if (!loaded.workspace ||
        containsErrors(
            loaded.diagnostics) ||
        metadata.value(
                    QStringLiteral(
                        "workspace_id"))
                .toString()
                .toUtf8()
                .toStdString() !=
            loaded.workspace->id) {
        return std::nullopt;
    }
    return std::move(
        loaded.workspace);
}

void ProjectController::onWatchedFileChanged(const QString& path)
{
    const bool generatedOutput =
        std::ranges::any_of(
            artifacts_, [&](const regmap::GeneratedArtifact& artifact) {
                return fromPath(artifact.path) == path;
            });
    if (generatedOutput) {
        generatedFileRefreshTimer_.start();
        return;
    }
    pendingFiles_.insert(path);
    fileSnapshots_.remove(path);
    stabilityAttempts_ = 0;
    emit syncStatusChanged(
        path ==
                fromPath(
                    manifestPath_)
            ? QStringLiteral(
                  "Waiting for the externally saved project to become stable...")
            : QStringLiteral(
                  "Waiting for the saved editable file to become stable..."));
    stabilityTimer_.start();
}

void ProjectController::onWatchedDirectoryChanged(
    const QString&)
{
    if (manifestChangedOnDisk()) {
        const QString projectFile =
            fromPath(
                manifestPath_);
        pendingFiles_.insert(
            projectFile);
        fileSnapshots_.remove(
            projectFile);
        stabilityAttempts_ = 0;
        stabilityTimer_.start();
    }
    if (!artifacts_.empty()) {
        generatedFileRefreshTimer_.start();
    }
}

void ProjectController::refreshGeneratedFileState()
{
    refreshWatchPaths();
    std::size_t changedCount = 0;
    GeneratedStateSnapshot snapshot{artifactsRevision_, {}};
    snapshot.current.reserve(artifacts_.size());
    for (std::size_t index = 0;
         index < artifacts_.size(); ++index) {
        const bool current = regmap::inspectGeneratedArtifact(artifacts_[index]).synchronized();
        snapshot.current.push_back(current);
        if (!current) {
            ++changedCount;
        }
    }
    // Reuse this inspection only while notifying views, never across file events.
    const QPointer<ProjectController> guard(this);
    auto previousSnapshot = std::exchange(generatedStateSnapshot_, std::move(snapshot));
    emit generationChanged();
    if (!guard) return;
    generatedStateSnapshot_ = std::move(previousSnapshot);
    if (changedCount == 0) {
        return;
    }
    emit syncStatusChanged(
        store_.dirty()
            ? QStringLiteral(
                  "Read-only output changed outside Workbench; "
                  "Save & Sync will replace it with current Workbench data")
            : QStringLiteral(
                  "Read-only output changed outside Workbench; "
                  "use Retry outputs to restore it"));
}

void ProjectController::checkPendingFiles()
{
    bool stable = true;
    for (const QString& path : std::as_const(pendingFiles_)) {
        const QFileInfo information(path);
        if (!information.exists() || !information.isFile()) {
            stable = false;
            continue;
        }

        const auto signature = std::pair {information.size(), information.lastModified()};
        const auto iterator = fileSnapshots_.constFind(path);
        if (iterator == fileSnapshots_.constEnd() || iterator.value() != signature) {
            fileSnapshots_.insert(path, signature);
            stable = false;
        }
    }

    ++stabilityAttempts_;
    if (!stable && stabilityAttempts_ < 12) {
        stabilityTimer_.start();
        return;
    }

    const QSet<QString> changedPaths = pendingFiles_;
    pendingFiles_.clear();
    fileSnapshots_.clear();
    stabilityAttempts_ = 0;

    const QString projectFile =
        fromPath(
            manifestPath_);
    if (!projectFile.isEmpty() &&
        changedPaths.contains(
            projectFile)) {
        if (!manifestChangedOnDisk()) {
            setExternalProjectChangePending(
                false);
            clearExternalComparison();
            emit comparisonChanged();
            refreshWatchPaths();
        } else {
            setExternalProjectChangePending(
                true);
            refreshExternalChangesFromDisk();
            refreshWatchPaths();
            emit syncStatusChanged(
                store_.dirty()
                    ? QStringLiteral(
                          "Project changed on disk; unsaved Workbench edits were retained and per-change decisions are available")
                    : externalProjectReloadDeferred_
                    ? QStringLiteral(
                          "Project changed on disk; the active Workbench editor was retained and per-change decisions are available")
                    : QStringLiteral(
                          "Project changed on disk; review and Accept or Reject each Disk / CLI change"));
        }
        return;
    }

    const QString rtlFile = manifest_ ? fromPath(manifest_->rtl.path.resolved) : QString {};
    if (!rtlFile.isEmpty() && changedPaths.contains(rtlFile)) {
        synchronizeRtl(true, !store_.dirty());
    }
}
