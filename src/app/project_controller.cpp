#include "project_controller.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"

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
    connect(
        &watcher_,
        &QFileSystemWatcher::fileChanged,
        this,
        &ProjectController::onWatchedFileChanged);
    connect(
        &stabilityTimer_,
        &QTimer::timeout,
        this,
        &ProjectController::checkPendingFiles);
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
        emit syncStatusChanged(QStringLiteral("Could not create the project file"));
        return false;
    }
    openProject(fromPath(path));
    return store_.workspace() != nullptr && !hasProjectErrors();
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

void ProjectController::openProject(const QString& manifestPath)
{
    manifestPath_ = std::filesystem::absolute(
                        std::filesystem::path(manifestPath.toStdWString()))
                        .lexically_normal();
    manifest_.reset();
    store_ = regmap::WorkspaceStore {};
    baseline_.reset();
    artifacts_.clear();
    changes_.clear();
    conflicts_.clear();
    loadDiagnostics_.clear();
    syncDiagnostics_.clear();
    generationDiagnostics_.clear();
    diagnostics_.clear();
    lastAcceptedModelWasValid_ = false;
    reloadImpl(false);
}

void ProjectController::reload()
{
    reloadImpl(false);
}

void ProjectController::reloadImpl(bool automatic)
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

    auto loaded = regmap::openProject(manifestPath_);
    loadDiagnostics_ = std::move(loaded.diagnostics);
    std::erase_if(loadDiagnostics_, isValidationDiagnostic);
    changes_.clear();
    conflicts_.clear();
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
    emit syncStatusChanged(
        lastAcceptedModelWasValid_
            ? QStringLiteral("Synchronization rejected; retaining the last loadable model")
            : QStringLiteral("Project contains structural errors"));
}

void ProjectController::initializeSynchronization()
{
    const auto* current = store_.workspace();
    if (!manifest_ || current == nullptr) {
        return;
    }
    const std::filesystem::path statePath = baselinePath();
    if (QFileInfo::exists(fromPath(statePath))) {
        auto loaded = regmap::loadSyncBaseline(statePath);
        syncDiagnostics_ = std::move(loaded.diagnostics);
        if (!loaded.workspace) {
            rebuildDiagnostics();
            emit syncStatusChanged(
                QStringLiteral("Synchronization baseline is invalid; automatic RTL merge is blocked"));
            return;
        }
        baseline_ = std::move(loaded.workspace);
    } else {
        baseline_ = *current;
    }

    synchronizeRtl(true, true);
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
    if (!baseline_) {
        baseline_ = *store_.workspace();
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
                QStringLiteral("Managed RTL contains errors; the last Workbench model is retained"));
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

    const auto mergeChanges = regmap::diffWorkspaces(*store_.workspace(), *merge.merged);
    if (!mergeChanges.empty()) {
        const regmap::Workspace mergedWorkspace = *merge.merged;
        static_cast<void>(
            store_.transact("Merge managed RTL", [mergedWorkspace](regmap::Workspace& workspace) {
                workspace = mergedWorkspace;
            }));
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
    if (!manifest_ || !store_.workspace() || !baseline_) {
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
    static_cast<void>(persistSynchronizedModel());
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
        diagnostic.code = "RM5300";
        diagnostic.message = "Workbench and managed RTL changed property '" + conflict.property
            + "' differently.";
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

    QStringList paths;
    if (manifest_) {
        const QString rtlFile = fromPath(manifest_->rtl.path.resolved);
        if (QFileInfo::exists(rtlFile) && !paths.contains(rtlFile)) {
            paths.push_back(rtlFile);
        }
    }
    if (!paths.empty()) {
        watcher_.addPaths(paths);
    }
}

void ProjectController::notifyModelEdited()
{
    changes_ = baseline_ && store_.workspace()
        ? regmap::diffWorkspaces(*baseline_, *store_.workspace())
        : std::vector<regmap::ModelChange> {};
    artifacts_.clear();
    generationDiagnostics_.clear();
    rebuildDiagnostics();
    emit projectChanged();
    emit diagnosticsChanged();
    emit generationChanged();
    emit editStateChanged();
    emit syncStatusChanged(
        hasProjectErrors() ? QStringLiteral("Model edited; resolve validation/synchronization errors")
                           : QStringLiteral("Model edited; save to merge with RTL and update outputs"));
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
    pendingFiles_.insert(path);
    fileSnapshots_.remove(path);
    stabilityAttempts_ = 0;
    emit syncStatusChanged(QStringLiteral("Waiting for the saved editable file to become stable..."));
    stabilityTimer_.start();
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
