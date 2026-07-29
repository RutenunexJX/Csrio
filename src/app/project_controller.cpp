#include "project_controller.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <cctype>
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

[[nodiscard]] std::string systemVerilogIdentifier(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 1);
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        result.push_back(
            std::isalnum(character) != 0 || rawCharacter == '_' ? rawCharacter : '_');
    }
    if (result.empty()) {
        result = "register_map";
    }
    const auto first = static_cast<unsigned char>(result.front());
    if (std::isalpha(first) == 0 && result.front() != '_') {
        result.insert(result.begin(), '_');
    }
    return result;
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

} // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent)
{
    stabilityTimer_.setSingleShot(true);
    stabilityTimer_.setInterval(350);
    generatedFileRefreshTimer_.setSingleShot(true);
    generatedFileRefreshTimer_.setInterval(350);
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
    const std::string baseName =
        systemVerilogIdentifier(displayName.toUtf8().toStdString());

    regmap::Workspace workspace;
    workspace.id = "workspace-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    workspace.name = displayName.toUtf8().toStdString();
    workspace.manifestPath = path;

    regmap::ProjectManifest manifest;
    manifest.manifestPath = path;
    manifest.workspaceId = workspace.id;
    manifest.workspaceName = workspace.name;
    manifest.rtl.moduleName = baseName + "_registers";
    manifest.rtl.path.declared =
        std::filesystem::path("rtl") / (manifest.rtl.moduleName + ".sv");
    manifest.rtl.path.resolved =
        (path.parent_path() / manifest.rtl.path.declared).lexically_normal();
    manifest.outputDirectory.declared = "generated";
    manifest.outputDirectory.resolved = (path.parent_path() / "generated").lexically_normal();

    const auto addTarget = [&](regmap::GenerationTargetKind kind, std::filesystem::path name) {
        regmap::GenerationTargetConfig target;
        target.kind = kind;
        target.path.declared = std::move(name);
        target.path.resolved =
            (manifest.outputDirectory.resolved / target.path.declared).lexically_normal();
        manifest.targets.push_back(std::move(target));
    };
    addTarget(regmap::GenerationTargetKind::xlsx, "register-map.xlsx");
    addTarget(regmap::GenerationTargetKind::cHeader, baseName + "_regs.h");
    addTarget(regmap::GenerationTargetKind::markdown, "register-map.md");

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
    const auto& artifact = artifacts_[index];
    QFile file(fromPath(artifact.path));
    const QIODevice::OpenMode mode =
        artifact.isBinary()
            ? QIODevice::ReadOnly
            : QIODevice::ReadOnly | QIODevice::Text;
    if (!file.open(mode)) {
        return false;
    }
    const QByteArray expected =
        artifact.isBinary()
            ? QByteArray(
                  reinterpret_cast<const char*>(
                      artifact.binaryContent.data()),
                  static_cast<qsizetype>(
                      artifact.binaryContent.size()))
            : QByteArray(
                  artifact.content.data(),
                  static_cast<qsizetype>(artifact.content.size()));
    if (artifact.isBinary() &&
        file.size() != expected.size()) {
        return false;
    }
    return file.readAll() == expected;
}

const std::vector<regmap::ModelChange>& ProjectController::changes() const noexcept
{
    return changes_;
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
    manifest_.reset();
    store_ = regmap::WorkspaceStore {};
    baseline_.reset();
    initialSyncChoicePending_ = false;
    artifacts_.clear();
    changes_.clear();
    conflicts_.clear();
    loadDiagnostics_.clear();
    syncDiagnostics_.clear();
    generationDiagnostics_.clear();
    diagnostics_.clear();
    lastAcceptedModelWasValid_ = false;
    reloadImpl(false, &loaded);
    return requestedWorkspaceId.has_value() && manifestPath_ == requestedPath &&
        store_.workspace() != nullptr &&
        store_.workspace()->id == *requestedWorkspaceId;
}

void ProjectController::reload()
{
    reloadImpl(false);
}

void ProjectController::reloadImpl(bool automatic, regmap::ProjectOpenResult* preloaded)
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

    auto loaded = preloaded == nullptr ? regmap::openProject(manifestPath_)
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
        lastAcceptedModelWasValid_ = true;
        artifacts_.clear();
        generationDiagnostics_.clear();
        initializeSynchronization();
        rebuildDiagnostics();
        refreshWatchPaths();
        emit projectChanged();
        emit diagnosticsChanged();
        emit generationChanged();
        emit conflictsChanged();
        emit editStateChanged();

        if (conflicts_.empty() && !containsErrors(syncDiagnostics_)) {
            emit syncStatusChanged(
                automatic
                    ? QStringLiteral("Synchronized: %1 object change(s)").arg(changes_.size())
                    : QStringLiteral("Project and managed RTL loaded"));
        }
        return;
    }

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
    emit syncStatusChanged(QStringLiteral("Merging Workbench and managed RTL..."));
    synchronizeRtl(false, true);
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
        emit projectChanged();
        emit editStateChanged();
    }

    if (persistWhenClean) {
        if (persistSynchronizedModel()) {
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
        emit conflictsChanged();
        emit projectChanged();
        emit editStateChanged();
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
    emit conflictsChanged();
    emit projectChanged();
    emit editStateChanged();
    if (persistSynchronizedModel()) {
        emit syncStatusChanged(
            QStringLiteral("Conflicts resolved; Workbench, RTL, and read-only outputs synchronized"));
    }
}

bool ProjectController::persistSynchronizedModel()
{
    if (!manifest_ || !store_.workspace()) {
        return false;
    }
    if (containsErrors(store_.diagnostics())) {
        rebuildDiagnostics();
        emit diagnosticsChanged();
        emit syncStatusChanged(QStringLiteral("Save blocked by model validation errors"));
        return false;
    }

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
    return !containsErrors(generationDiagnostics_);
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
    rebuildDiagnostics();
    emit projectChanged();
    emit diagnosticsChanged();
    emit generationChanged();
    emit editStateChanged();
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

std::filesystem::path ProjectController::baselinePath() const
{
    if (manifestPath_.empty()) {
        return {};
    }
    return manifestPath_.parent_path()
        / (manifestPath_.filename().generic_string() + ".sync.json");
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
    emit syncStatusChanged(QStringLiteral("Waiting for the saved editable file to become stable..."));
    stabilityTimer_.start();
}

void ProjectController::onWatchedDirectoryChanged(
    const QString&)
{
    if (!artifacts_.empty()) {
        generatedFileRefreshTimer_.start();
    }
}

void ProjectController::refreshGeneratedFileState()
{
    refreshWatchPaths();
    std::size_t changedCount = 0;
    for (std::size_t index = 0;
         index < artifacts_.size(); ++index) {
        if (!generatedArtifactIsCurrent(index)) {
            ++changedCount;
        }
    }
    emit generationChanged();
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

    const QString rtlFile = manifest_ ? fromPath(manifest_->rtl.path.resolved) : QString {};
    if (!rtlFile.isEmpty() && changedPaths.contains(rtlFile)) {
        synchronizeRtl(true, !store_.dirty());
    }
}
