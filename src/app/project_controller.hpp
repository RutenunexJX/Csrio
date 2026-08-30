#pragma once

#include "regmap/core/diagnostic.hpp"
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
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace regmap {
struct ProjectOpenResult;
}

struct RecoveryDraftInfo {
    bool mergeBaseAvailable{false};
    bool projectChangedSinceDraft{false};
    std::size_t conflictCount{0};
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
    recoveryDraftInfo() const;
    [[nodiscard]] QDateTime recoveryDraftModified() const;
    [[nodiscard]] bool hasExternalProjectChange() const noexcept;
    bool restoreRecoveryDraft(
        regmap::MergePreference conflictPreference =
            regmap::MergePreference::workbench);
    void discardRecoveryDraft();
    void discardRecoveryDraft(const std::filesystem::path& projectPath);
    void deferExternalProjectReload();

    bool editWorkspace(
        const QString& description,
        const regmap::WorkspaceStore::Mutation& mutation);
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
    std::vector<regmap::ModelChange> changes_;
    std::vector<regmap::ModelChange> savedChanges_;
    std::vector<regmap::ModelChange> externalChanges_;
    std::vector<regmap::MergeConflict> conflicts_;
    std::optional<regmap::Workspace> baseline_;
    std::optional<regmap::Workspace> recoveryBaseWorkspace_;
    bool lastAcceptedModelWasValid_ {false};
    bool initialSyncChoicePending_ {false};
    bool externalProjectChangePending_ {false};
    bool externalProjectReloadDeferred_{false};

    void reloadImpl(
        bool automatic,
        regmap::ProjectOpenResult* preloaded = nullptr,
        const QByteArray& preloadedDigest = {});
    void generateImpl(bool automatic);
    void rebuildDiagnostics();
    void refreshWatchPaths();
    void notifyModelEdited();
    void rebuildSavedChanges();
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
