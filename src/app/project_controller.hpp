#pragma once

#include "workspace_inspection.hpp"

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/external_changes.hpp"
#include "regmap/core/generation.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"
#include "regmap/core/sync_diff.hpp"
#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QDateTime>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace regmap {
class ProjectLoadSnapshot;
}

struct RecoveryDraftInfo {
    bool mergeBaseAvailable{false};
    bool projectChangedSinceDraft{false};
    std::size_t conflictCount{0};
    std::vector<regmap::MergeConflict> conflicts;
    std::vector<regmap::ui::PropertyDifference> differences;
    QByteArray revision;
};

class ProjectController final : public QObject {
    Q_OBJECT

public:
    explicit ProjectController(QObject* parent = nullptr);

    bool createProject(const QString& manifestPath);
    [[nodiscard]] const regmap::ProjectManifest* manifest() const noexcept;
    [[nodiscard]] const regmap::Workspace* workspace() const noexcept;
    [[nodiscard]] const std::vector<regmap::Diagnostic>& diagnostics() const noexcept;
    [[nodiscard]] const std::vector<regmap::GeneratedArtifact>& artifacts() const noexcept;
    [[nodiscard]] bool generatedArtifactIsCurrent(
        std::size_t index) const;
    [[nodiscard]] const std::vector<regmap::ModelChange>& changes() const noexcept;
    [[nodiscard]] const std::vector<regmap::ModelChange>&
    savedChanges() const noexcept;
    [[nodiscard]] const std::vector<regmap::ModelChange>&
    externalChanges() const noexcept;
    [[nodiscard]] const std::filesystem::path& manifestPath() const noexcept;
    [[nodiscard]] bool hasProjectErrors() const noexcept;
    [[nodiscard]] bool isDirty() const;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;
    [[nodiscard]] std::size_t undoDepth() const noexcept;
    [[nodiscard]] const std::vector<regmap::MergeConflict>& conflicts() const noexcept;
    [[nodiscard]] bool hasConflicts() const noexcept;
    [[nodiscard]] bool requiresInitialSyncChoice() const noexcept;
    [[nodiscard]] bool recoveryDraftAvailable() const;
    [[nodiscard]] std::optional<RecoveryDraftInfo>
    recoveryDraftInfo(bool includePreview = false) const;
    [[nodiscard]] std::vector<regmap::ui::PropertyDifference> changeDetails(
        const QString& origin, const QString& objectId) const;
    void deferRecoveryDraft();
    [[nodiscard]] bool recoveryDraftDeferred() const noexcept { return recoveryDraftDeferred_; }
    [[nodiscard]] QDateTime recoveryDraftModified() const;
    [[nodiscard]] bool hasExternalProjectChange() const noexcept;
    [[nodiscard]] std::uint64_t externalChangeGeneration() const noexcept;
    [[nodiscard]] QString externalChangeDigest() const;
    [[nodiscard]] QString externalChangeStatus() const;
    [[nodiscard]] std::size_t rejectedExternalChangeCount() const noexcept;
    [[nodiscard]] regmap::WorkspaceChangePlan previewExternalChanges(
        const std::vector<std::string>& changeIds,
        std::uint64_t generation) const;
    bool acceptExternalChanges(
        const std::vector<std::string>& changeIds,
        std::uint64_t generation,
        QString* failureReason = nullptr);
    bool rejectExternalChanges(
        const std::vector<std::string>& changeIds,
        std::uint64_t generation,
        QString* failureReason = nullptr);
    bool restoreRecoveryDraft(
        regmap::MergePreference conflictPreference =
            regmap::MergePreference::workbench,
        const QByteArray& expectedRevision = {});
    void discardRecoveryDraft();
    void discardRecoveryDraft(const std::filesystem::path& projectPath);
    void deferExternalProjectReload();

    bool editWorkspace(
        const QString& description,
        const regmap::WorkspaceStore::Mutation& mutation,
        std::uint64_t undoGroup = 0);
    using PreparedEdit = regmap::WorkspaceStore::PreparedEdit;
    [[nodiscard]] std::optional<PreparedEdit> prepareEdit(
        const regmap::WorkspaceStore::Mutation& mutation);
    [[nodiscard]] std::optional<PreparedEdit> prepareReplacement(
        regmap::Workspace candidate, std::uint64_t sourceRevision);
    bool commitPreparedEdit(const QString& description, PreparedEdit&& edit);
    [[nodiscard]] const std::vector<regmap::Diagnostic>& modelDiagnostics() const noexcept;
    [[nodiscard]] std::uint64_t modelRevision() const noexcept { return store_.revision(); }
    [[nodiscard]] std::uint64_t modelValidationCount() const noexcept { return store_.validationCount(); }
    [[nodiscard]] std::uint64_t beginUndoGroup(const QString& description);
    bool endUndoGroup(std::uint64_t token);
    [[nodiscard]] std::size_t historyBytes() const noexcept { return store_.historyBytes(); }
    [[nodiscard]] std::uint64_t historyTrimCount() const noexcept { return store_.historyTrimCount(); }
    bool squashUndoSince(std::size_t startingDepth, const QString& description);

public slots:
    bool openProject(const QString& manifestPath);
    void reload();
    void save();
    void saveOverExternalProjectChange();
    void undo();
    void redo();
    void synchronizeNow();
    void useWorkbenchForConflicts();
    void useRtlForConflicts();
    void generateNow();

signals:
    void projectChanged();
    void diagnosticsChanged();
    void generationChanged();
    void editStateChanged();
    void conflictsChanged();
    void comparisonChanged();
    void syncStatusChanged(const QString& message);
    void recoveryDraftStatusChanged(const QString& message);
    void externalProjectChangeChanged();
    void externalProjectSaveConflict();

private slots:
    void onWatchedFileChanged(const QString& path);
    void onWatchedDirectoryChanged(const QString& path);
    void checkPendingFiles();
    void refreshGeneratedFileState();
    void writeRecoveryDraft();

private:
    QFileSystemWatcher watcher_;
    QTimer stabilityTimer_;
    QTimer generatedFileRefreshTimer_;
    QTimer recoveryDraftTimer_;
    QDateTime recoveryBaseModified_;
    QByteArray acceptedManifestDigest_;
    QByteArray recoveryBaseDigest_;
    bool recoveryDraftDeferred_{false};
    bool acceptedManifestDigestKnown_{false};
    QSet<QString> pendingFiles_;
    QHash<QString, std::pair<qint64, QDateTime>> fileSnapshots_;
    int stabilityAttempts_ {0};

    std::filesystem::path manifestPath_;
    std::optional<regmap::ProjectManifest> manifest_;
    regmap::WorkspaceStore store_;
    std::vector<regmap::Diagnostic> loadDiagnostics_;
    std::vector<regmap::Diagnostic> syncDiagnostics_;
    std::vector<regmap::Diagnostic> generationDiagnostics_;
    std::vector<regmap::Diagnostic> diagnostics_;
    std::vector<regmap::GeneratedArtifact> artifacts_;
    struct GeneratedStateSnapshot {
        std::uint64_t revision;
        std::vector<bool> current;
    };
    std::uint64_t artifactsRevision_{0};
    std::optional<GeneratedStateSnapshot> generatedStateSnapshot_;
    std::vector<regmap::ModelChange> changes_;
    std::vector<regmap::ModelChange> savedChanges_;
    std::vector<regmap::ModelChange> externalChanges_;
    std::optional<regmap::Workspace> externalWorkspace_;
    QByteArray externalManifestDigest_;
    std::uint64_t externalChangeGeneration_{0};
    std::set<std::string, std::less<>> rejectedExternalChangeIds_;
    QString externalChangeStatus_;
    std::vector<regmap::MergeConflict> conflicts_;
    std::optional<regmap::Workspace> baseline_;
    std::optional<regmap::WorkspaceDiffSnapshot> currentDiffSnapshot_;
    std::optional<regmap::WorkspaceDiffSnapshot> savedDiffSnapshot_;
    std::optional<regmap::WorkspaceDiffSnapshot> baselineDiffSnapshot_;
    std::optional<regmap::WorkspaceDiffSnapshot> externalDiffSnapshot_;
    std::uint64_t currentDiffRevision_{0};
    std::uint64_t savedDiffRevision_{0};
    std::uint64_t diffSnapshotBuildCount_{0};
    std::optional<regmap::Workspace> recoveryBaseWorkspace_;
    bool lastAcceptedModelWasValid_ {false};
    bool initialSyncChoicePending_ {false};
    bool externalProjectChangePending_ {false};
    bool externalProjectReloadDeferred_{false};

    void reloadImpl(
        bool automatic,
        regmap::ProjectLoadSnapshot* preloaded = nullptr);
    void generateImpl(bool automatic);
    void rebuildDiagnostics();
    void refreshWatchPaths();
    void notifyModelEdited();
    const regmap::WorkspaceDiffSnapshot& currentDiffSnapshot();
    std::vector<regmap::ModelChange> diffFromBaseline();
    void rebuildSavedChanges();
    void rebuildExternalChanges();
    void clearExternalComparison();
    void refreshExternalChangesFromDisk();
    void initializeSynchronization();
    void synchronizeRtl(bool automatic, bool persistWhenClean);
    void resolveConflicts(regmap::MergePreference preference);
    [[nodiscard]] bool persistSynchronizedModel(
        bool preserveDivergentRecoveryDraft = false);
    [[nodiscard]] bool manifestChangedOnDisk() const;
    void setExternalProjectChangePending(bool pending);
    [[nodiscard]] std::filesystem::path baselinePath() const;
    [[nodiscard]] static std::filesystem::path recoveryDraftPathFor(
        const std::filesystem::path& projectPath);
    [[nodiscard]] static std::filesystem::path recoveryBasePathFor(
        const std::filesystem::path& projectPath);
    [[nodiscard]] static std::filesystem::path recoveryMetadataPathFor(
        const std::filesystem::path& projectPath);
    [[nodiscard]] std::optional<regmap::Workspace>
    loadRecoveryDraftBase() const;
};
