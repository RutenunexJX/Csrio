#pragma once

#include "project_controller.hpp"

#include "regmap/core/model.hpp"

#include <QByteArray>
#include <QMainWindow>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QString>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QAction;
class AddressSpaceView;
class BitfieldView;
class QCloseEvent;
class QComboBox;
class QCompleter;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QPoint;
class QModelIndex;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QScrollArea;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QTableView;
class QToolButton;
class QTreeView;
class QWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT
    Q_PROPERTY(qulonglong inlineDiagnosticRowVisitCount READ inlineDiagnosticRowVisitCount)
    Q_PROPERTY(qulonglong inlineDiagnosticCellUpdateCount READ inlineDiagnosticCellUpdateCount)
    Q_PROPERTY(qulonglong diagnosticsRefreshCount READ diagnosticsRefreshCount)
    Q_PROPERTY(qulonglong diffRefreshCount READ diffRefreshCount)

public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openProjectPath(const QString& path);
    bool openStartupProjectPath(
        const QString& path,
        const QString& selectedObjectId = {});
    [[nodiscard]] const regmap::Workspace* currentWorkspaceForSuite(
        const QString& projectPath) const;

private:
    qulonglong inlineDiagnosticRowVisitCount() const { return inlineDiagnosticRowVisits_; }
    qulonglong inlineDiagnosticCellUpdateCount() const { return inlineDiagnosticCellUpdates_; }
    qulonglong diagnosticsRefreshCount() const { return diagnosticsRefreshCount_; }
    qulonglong diffRefreshCount() const { return diffRefreshCount_; }

    enum DataRole {
        objectIdRole = Qt::UserRole + 1,
        addressIdRole,
        blockIdRole,
        rowIndexRole,
        propertyRole,
        addRowRole,
        openFieldsRole,
        fieldsOpenRole,
        fixedAddressRole,
        resultKeyRole,
        fieldDepthRole,
        changeOriginRole,
        changeKindRole,
        inlineDiagnosticTextRole,
        inlineOriginalToolTipRole,
        inlineOriginalForegroundRole,
        inlineOriginalIconRole,
        inlineOriginalAccessibleRole,
        externalChangeIdRole,
        externalGenerationRole,
    };

    ProjectController controller_;
    QWidget* windowChrome_{nullptr};

    QTreeView* hierarchyView_{nullptr};
    QPushButton* hierarchyAddButton_{nullptr};
    QToolButton* favoriteToggleButton_{nullptr};
    QToolButton* quickNavigationButton_{nullptr};
    QTableView* registerView_{nullptr};
    QLabel* pageContextLabel_{nullptr};
    AddressSpaceView* addressSpaceView_{nullptr};
    QScrollArea* addressSpaceScroll_{nullptr};
    QLabel* blockContextLabel_{nullptr};
    QLineEdit* pageBaseEdit_{nullptr};
    QLineEdit* pageWidthEdit_{nullptr};
    QLineEdit* pageDescriptionEdit_{nullptr};
    QLineEdit* blockBaseEdit_{nullptr};
    QLineEdit* blockSizeEdit_{nullptr};
    QLineEdit* blockDescriptionEdit_{nullptr};
    QComboBox* tagFilter_{nullptr};
    QCompleter* tagFilterCompleter_{nullptr};
    QToolButton* clearTagFilterButton_{nullptr};
    QLabel* registerCountLabel_{nullptr};
    QLabel* fixedAddressLegend_{nullptr};
    QLabel* registerTagFilterLabel_{nullptr};
    QLabel* pageDescriptionLabel_{nullptr};
    QLabel* blockDescriptionLabel_{nullptr};
    QLabel* registerSelectionLabel_{nullptr};
    QWidget* registerFeedbackBar_{nullptr};
    QLabel* registerFeedbackLabel_{nullptr};
    QToolButton* registerBatchEditButton_{nullptr};
    QWidget* registerEmptyState_{nullptr};
    QLabel* registerEmptyTitleLabel_{nullptr};
    QLabel* registerEmptyHintLabel_{nullptr};
    QPushButton* registerEmptyPrimaryButton_{nullptr};
    QPushButton* registerEmptySecondaryButton_{nullptr};
    QPushButton* registerEmptyTertiaryButton_{nullptr};
    QTableView* fieldView_{nullptr};
    BitfieldView* bitfieldView_{nullptr};
    QWidget* fieldPanel_{nullptr};
    QWidget* fieldHeaderBar_{nullptr};
    QLabel* fieldContextLabel_{nullptr};
    QLabel* selectedFieldSummaryLabel_{nullptr};
    QLabel* fieldSelectionLabel_{nullptr};
    QWidget* fieldFeedbackBar_{nullptr};
    QLabel* fieldFeedbackLabel_{nullptr};
    QToolButton* fieldBatchEditButton_{nullptr};
    QPushButton* closeFieldsButton_{nullptr};
    QWidget* enumPanel_{nullptr};
    QLabel* enumContextLabel_{nullptr};
    QToolButton* customizeBooleanValuesButton_{nullptr};
    QTableView* enumView_{nullptr};
    QTableView* lastCellTable_{nullptr};
    QWidget* lastCommandView_{nullptr};
    QTabWidget* tabs_{nullptr};
    QTableView* problemsView_{nullptr};
    QTableView* generatedView_{nullptr};
    QTableView* diffView_{nullptr};
    QLabel* problemsSummaryLabel_{nullptr};
    QLineEdit* diagnosticsFilterEdit_{nullptr};
    QComboBox* diagnosticsSeverityFilter_{nullptr};
    QSplitter* workspaceSplitter_{nullptr};
    QSplitter* editorSplitter_{nullptr};
    QSplitter* resultsSplitter_{nullptr};
    QWidget* pageHeader_{nullptr};
    QLabel* projectTitleLabel_{nullptr};
    QLabel* projectPathLabel_{nullptr};
    QLabel* fileStateLabel_{nullptr};
    QToolButton* generateButton_{nullptr};
    QToolButton* synchronizeButton_{nullptr};
    QToolButton* saveSyncButton_{nullptr};
    QToolButton* moreProjectButton_{nullptr};
    QToolButton* allSearchResultsButton_{nullptr};
    QToolButton* addressMapToggle_{nullptr};
    QToolButton* enumValuesToggle_{nullptr};
    QTableView* diffDetailsView_{nullptr};
    QStandardItemModel* diffDetailsModel_{nullptr};
    QLineEdit* globalSearchEdit_{nullptr};
    QLabel* searchResultLabel_{nullptr};
    QLabel* activeContextLabel_{nullptr};
    QLabel* syncStateLabel_{nullptr};
    QLabel* recoveryStateLabel_{nullptr};
    QToolButton* resultsToggleButton_{nullptr};
    QToolButton* openXlsxButton_{nullptr};
    QPushButton* retryOutputsButton_{nullptr};
    QWidget* conflictBar_{nullptr};
    QLabel* conflictSummaryLabel_{nullptr};
    QPushButton* keepWorkbenchButton_{nullptr};
    QPushButton* useRtlButton_{nullptr};
    QWidget* externalDecisionBar_{nullptr};
    QLabel* externalDecisionStatusLabel_{nullptr};
    QPushButton* previewExternalButton_{nullptr};
    QPushButton* acceptExternalButton_{nullptr};
    QPushButton* rejectExternalButton_{nullptr};
    QPushButton* acceptAllExternalButton_{nullptr};
    QPushButton* rejectAllExternalButton_{nullptr};
    std::uint64_t presentedExternalGeneration_{0};

    QStandardItemModel* hierarchyModel_{nullptr};
    QStandardItemModel* registerModel_{nullptr};
    QStandardItemModel* fieldModel_{nullptr};
    QStandardItemModel* enumModel_{nullptr};
    QStandardItemModel* problemsModel_{nullptr};
    QStandardItemModel* generatedModel_{nullptr};
    QStandardItemModel* diffModel_{nullptr};
    QStandardItemModel* searchCompletionModel_{nullptr};
    QCompleter* searchCompleter_{nullptr};

    QAction* reloadAction_{nullptr};
    QAction* recoveryDraftAction_{nullptr};
    QAction* navigateBackAction_{nullptr};
    QAction* navigateForwardAction_{nullptr};
    QAction* allSearchResultsAction_{nullptr};
    QAction* newProjectAction_{nullptr};
    QAction* openProjectAction_{nullptr};
    QAction* saveAction_{nullptr};
    QAction* openXlsxAction_{nullptr};
    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* generateAction_{nullptr};
    QAction* synchronizeAction_{nullptr};
    QAction* useWorkbenchAction_{nullptr};
    QAction* useRtlAction_{nullptr};
    QAction* openSourceAction_{nullptr};
    QAction* deleteAction_{nullptr};
    QAction* copyAction_{nullptr};
    QAction* pasteAction_{nullptr};
    QAction* duplicateAction_{nullptr};
    QAction* showDetailedRegistersAction_{nullptr};
    QAction* showAdvancedFieldsAction_{nullptr};
    QAction* batchEditAction_{nullptr};
    QAction* copyRangeAction_{nullptr};
    QAction* pasteRangeAction_{nullptr};
    QAction* toggleFixedAddressAction_{nullptr};
    QAction* moveRegistersUpAction_{nullptr};
    QAction* moveRegistersDownAction_{nullptr};
    QAction* toggleFavoriteAction_{nullptr};
    QAction* toggleResultsAction_{nullptr};
    QAction* lightThemeAction_{nullptr};
    QAction* darkThemeAction_{nullptr};
    QMenu* recentProjectsMenu_{nullptr};

    std::string selectedAddressId_;
    QMenu* quickNavigationMenu_{nullptr};
    std::string selectedBlockId_;
    std::string selectedRegisterId_;
    std::filesystem::path displayedManifestPath_;
    std::filesystem::path diagnosticsManifestPath_;
    std::filesystem::path generatedManifestPath_;
    std::filesystem::path diffManifestPath_;
    std::string selectedFieldId_;
    std::string openFieldsRegisterId_;
    std::string selectedTagFilter_;
    std::optional<regmap::AddressSpace> copiedPage_;
    std::optional<regmap::RegisterBlock> copiedBlock_;
    std::optional<regmap::SourceLocation> currentSource_;
    bool refreshing_{false};
    std::vector<regmap::Register> copiedRegisters_;
    std::vector<regmap::Field> copiedFields_;
    bool registerRangeClipboardPresent_{false};
    bool registerRangeClipboardValid_{false};
    std::size_t registerRangeClipboardCount_{0};
    bool fieldRangeClipboardPresent_{false};
    bool fieldRangeClipboardValid_{false};
    std::size_t fieldRangeClipboardCount_{0};
    bool hierarchyClipboardPresent_{false};
    bool hierarchyClipboardValid_{false};
    QByteArray hierarchyClipboardKind_;
    std::vector<std::string> favoriteObjectIds_;
    std::vector<std::string> recentObjectIds_;
    std::string navigationWorkspaceId_;
    std::string activeNavigationObjectId_;
    std::filesystem::path discardedRecoveryProjectPath_;
    bool modelEditInProgress_{false};
    bool refreshPending_{false};
    bool incrementalRefreshAmbiguous_{false};
    QTimer* derivedRefreshTimer_{nullptr};
    std::filesystem::path derivedRefreshPath_;
    std::string derivedRefreshWorkspaceId_;
    std::uint64_t derivedRefreshGeneration_{0};
    bool diagnosticsRefreshPending_{false};
    bool diffRefreshPending_{false};
    bool displayedDiagnosticsValid_{false};
    std::vector<regmap::Diagnostic> displayedDiagnostics_;
    std::unordered_set<std::string> pendingInlineDiagnosticIds_;
    std::unordered_multimap<std::string, QPersistentModelIndex> registerRowsById_;
    std::unordered_multimap<std::string, QPersistentModelIndex> fieldRowsById_;
    std::uint64_t inlineDiagnosticRowVisits_{0};
    std::uint64_t inlineDiagnosticCellUpdates_{0};
    std::uint64_t diagnosticsRefreshCount_{0};
    std::uint64_t diffRefreshCount_{0};
    bool searchRefreshPending_{false};
    std::string pendingIncrementalObjectId_;
    std::string pendingIncrementalProperty_;
    std::uint64_t fullTableRefreshCount_{0};
    std::uint64_t incrementalTableRefreshCount_{0};
    bool committingActiveEditor_{false};
    bool activeEditorCommitRejected_{false};
    bool resultInteractionBlocked_{false};
    bool hierarchyInteractionBlocked_{false};
    bool tableInteractionBlocked_{false};
    bool auxiliaryNavigationBlocked_{false};
    bool enumOnlyEditorLayout_{false};
    bool preserveRestoredEditorState_{false};
    bool ignoreProgrammaticEditorSplitterMoves_{false};
    bool hierarchyInitialized_{false};
    bool registerColumnsInitialized_{false};
    bool fieldColumnsInitialized_{false};
    bool fieldEditContextActive_{false};
    QByteArray expandedEditorSplitterState_;
    double expandedEditorLowerFraction_{0.58};
    QByteArray registerHeaderState_;
    QByteArray fieldHeaderState_;
    bool discardRecoveryAfterReplacement_{false};
    QString lastSyncMessage_;
    QString searchQuery_;
    struct SearchEntry { std::string id; QString label; QStringList values; };
    std::vector<SearchEntry> searchIndex_;
    bool searchIndexValid_{false};
    QWidget* resultsPanel_{nullptr};
    QString lastTypedSearchText_;
    std::vector<std::string> searchResults_;
    std::vector<QString> searchResultLabels_;
    int searchResultIndex_{-1};
    bool suppressNextSearchReturn_{false};
    int generatedOutputsNeedingRetry_{0};
    int totalDiagnostics_{0};
    std::unordered_map<std::string, std::vector<regmap::Diagnostic>>
        inlineDiagnosticIndex_;
    bool resultsPanelRequested_{false};
    bool resultsPanelAutoOpenedForProblems_{false};
    bool compactLayout_{false};
    bool restoringNavigation_{false};
    std::filesystem::path navigationHistoryPath_;
    struct NavigationTableState {
        QString id;
        QString property;
        int column{0};
        int horizontal{0};
        int vertical{0};
    };
    struct NavigationPosition {
        std::string page, block, reg, field, openFields, tag, active;
        NavigationTableState registers, fields, enums;
        int registerPanelScroll{0}, fieldPanelScroll{0};
        int hierarchyScroll{0};
        int focusTable{0};
    };
    std::vector<NavigationPosition> navigationHistory_;
    int navigationHistoryIndex_{-1};
    QPointer<QWidget> allSearchResultsDialog_;
    bool uiStateRestoreComplete_{false};
    enum class EmptyStateAction {
        none,
        recentProject,
        firstRegister,
        addBlock,
        addRegister,
        selectBlock,
        clearTagFilter,
    };
    EmptyStateAction registerEmptyPrimaryAction_{EmptyStateAction::none};
    EmptyStateAction registerEmptySecondaryAction_{EmptyStateAction::none};
    EmptyStateAction registerEmptyTertiaryAction_{EmptyStateAction::none};
    QString registerEmptyRecentProjectPath_;

    void buildUi();
    void buildActions();
    void buildWorkflowActions();
    void updateFocusedEditorLayout();
    void showAllSearchResults();
    void refreshChangeDetails();
    void activateChangeDetail(const QModelIndex& index);
    bool navigateToObjectImpl(const std::string& id, bool focusTarget);
    NavigationPosition captureNavigationPosition() const;
    void restoreNavigationPosition(const NavigationPosition& position);
    void navigateHistory(int direction);
    void updateNavigationActions();
    void connectSignals();
    void restoreUiState();
    void saveUiState() const;
    void updateResponsiveLayout();
    void ensureSafeSplitterSizes();
    void updateProjectHeader(
        const QString& transientState = {});
    void updateSelectedFieldSummary();
    void updateEditorPanelMode(
        bool hasOpenFieldEditor, bool hasEnumEditor);
    [[nodiscard]] bool commitActiveEditor();
    enum class UnsavedChoice {
        save,
        discard,
        cancel,
    };
    [[nodiscard]] UnsavedChoice promptUnsavedChanges(bool closing);
    bool confirmProjectReplacement();
    void reportProjectOpenFailure();
    void rebuildRecentProjectsMenu();
    void rememberRecentProject(const QString& path);
    [[nodiscard]] QString firstValidRecentProjectPath() const;
    void promptRecoveryDraft();
    void finalizeProjectReplacement(bool succeeded);
    [[nodiscard]] bool keepCurrentProjectIfSame(
        const QString& path);
    void removeRecentProject(const QString& path);
    void openRecentProject(const QString& path);
    void requestProjectRefresh();
    void refreshProject();
    [[nodiscard]] bool refreshIncrementalEdit();
    void updateContextBar();
    void refreshDiagnostics();
    void synchronizeDerivedRefreshContext();
    void requestDerivedRefresh(bool diagnostics, bool differences);
    void rebuildInlineDiagnosticIndex();
    void refreshInlineDiagnostics();
    void applyInlineDiagnosticsToRow(
        QStandardItemModel* model, int row, bool registerRow);
    void refreshGenerated();
    void updateOpenXlsxAction();
    void openGeneratedXlsx();
    void refreshDiff();
    void updateExternalDecisionBar();
    [[nodiscard]] std::vector<std::string>
    selectedExternalChangeIds() const;
    [[nodiscard]] std::vector<std::string>
    allExternalChangeIds() const;
    void previewExternalSelection(bool all);
    void acceptExternalSelection(bool all);
    void rejectExternalSelection(bool all);
    void setResultsPanelRequested(bool requested);
    void updateBottomPanelVisibility();
    void updateSyncPresentation(const QString& message = {});
    void updateRecoveryPresentation(
        const QString& message = {});
    void resolveExternalProjectSaveConflict();
    void resolveConflictsWithConfirmation(bool useRtl);
    void applyRegisterColumnVisibility();
    void applyFieldColumnVisibility();
    void updateRegisterEmptyState();
    void updateRowSelectionPresentation();
    void showInlineFailure(const QString& message);
    void clearInlineFailure();
    void triggerRegisterEmptyAction(EmptyStateAction action);
    void showTableHeaderContextMenu(
        QTableView* view, const QPoint& position, bool registerTable);
    void fitTableColumns(QTableView* view);
    void resetTableLayout(QTableView* view, bool registerTable);
    void populateHierarchy();
    void updateHierarchyAddAction();
    void populateRegisters();
    void populateFields(const regmap::Register* reg);
    void populateEnumValues(const regmap::Register* reg, const regmap::Field* field);
    void setCurrentSource(const regmap::SourceLocation& source);
    void selectRegister(const std::string& id);
    void updateAddressSpaceView();
    void selectField(const std::string& id);
    void beginHierarchyRename(const std::string& id);
    void beginRegisterRename(const std::string& id);
    void beginFieldRename(const std::string& id);
    void beginEnumRename(const std::string& id, const std::string& ownerId,
                         bool fieldOwner);
    void openSource(const regmap::SourceLocation& source);
    enum class PropertyEditStatus {
        changed,
        unchanged,
        rejected,
    };
    struct PropertyEditResult {
        PropertyEditStatus status{PropertyEditStatus::rejected};
        QString expectation;
    };
    PropertyEditResult applyPropertyEdit(const std::string& objectId,
                                         const std::string& property,
                                         const QString& value,
                                         bool reportFeedback = true,
                                         bool applyChanges = true,
                                         regmap::Workspace* stagedWorkspace = nullptr);
    void addAddressSpace();
    void addBlock(std::string parentId = {});
    void addRegister(
        std::string parentId = {},
        std::optional<std::uint64_t>
            preferredOffset = std::nullopt);
    void addFirstRegister();
    void insertRegisterAt(int row);
    [[nodiscard]] bool canInsertRegisterAt(int row) const;
    void addField();
    void addSubfield();
    void addEnumValue();
    void deleteEnumValue(
        const std::string& id);
    void deleteSelectedEnumValues(
        const std::vector<std::string>& enumValueIds);
    void deleteSelection();
    void deleteObject(
        const std::string& id);
    void editRegisterTags(const QModelIndex& index);
    void editRegisterAccess(const QModelIndex& index);
    void openRegisterLocationAt(
        const QModelIndex& index);
    void openFieldsAt(const QModelIndex& index);
    void openFieldsForRegister(
        const std::string& registerId,
        bool toggleIfOpen = false);
    void updateFieldsAction(const std::string& registerId);
    void closeFields();
    void showHierarchyContextMenu(const QPoint& position);
    void showRegisterContextMenu(const QPoint& position);
    void showFieldContextMenu(const QPoint& position);
    void duplicateFocusedObject();
    void duplicateSelectedRegister();
    void duplicateSelectedField();
    void convertSelectedRegisterToReserved();
    void deleteSelectedRegisterAndShift();
    void deleteSelectedRegisterRangeAndShift(
        const std::vector<std::string>& registerIds);
    void deleteSelectedFieldRange(
        const std::vector<std::string>& fieldIds);
    void moveField(const std::string& fieldId, std::uint32_t lsb, std::uint32_t msb);
    void moveSelectedFieldBy(int bitDelta);
    void updateTagFilter();
    void applyTagFilterSelection(int index);
    void commitTagFilterEditor();
    void restoreTagFilterEditor();
    [[nodiscard]] std::vector<std::string> selectedRegisterIds() const;
    [[nodiscard]] std::vector<std::string> selectedFieldIds() const;
    [[nodiscard]] std::vector<std::string> selectedEnumValueIds() const;
    void batchEditSelection();
    void batchEditSelectedRegisters();
    void batchEditSelectedFields();
    void copyObjectRange();
    void pasteObjectRange();
    void copyRegisterRange();
    void pasteRegisterRange();
    void copyFieldRange();
    void pasteFieldRange();
    void toggleSelectedRegisterFixed();
    void moveSelectedRegistersBy(int direction);
    void moveRegisters(
        const std::vector<std::string>& registerIds,
        int insertionRow,
        const std::vector<std::string>& omittedFixedIds = {});
    void addSelectedObjectToRecent();
    void toggleFavoriteObject();
    void refreshRangeClipboardState();
    void updateEditActions();
    [[nodiscard]] bool fieldEditContext() const;
    bool navigateToObject(
        const std::string& id,
        bool focusTarget = false);
    void rebuildSearchResults();
    void refreshSearchCompletion();
    void runSearch(bool reverse = false);
    void clearSearch();
    void navigateProblem(bool previous);
    bool locateProblemIndex(
        const QModelIndex& index,
        bool openSourceFallback);
    void activateProblemIndex(const QModelIndex& index);
    void activateGeneratedIndex(const QModelIndex& index);
    void activateDiffIndex(const QModelIndex& index);
    void showProblemContextMenu(const QPoint& position);
    void showGeneratedContextMenu(const QPoint& position);
    void showDiffContextMenu(const QPoint& position);
    void copySelection();
    void loadNavigationState();
    void saveNavigationState() const;
    void cleanupNavigationState();
    void rebuildQuickNavigationMenu();
    void updateFavoritePresentation();
    [[nodiscard]] QString navigationObjectLabel(const std::string& id) const;
    [[nodiscard]] bool isNavigationObject(const std::string& id) const;
    void pasteSelection();
    void copyHierarchySelection();
    void pasteHierarchySelection();
    void moveHierarchyObject(const std::string& sourceId, const std::string& targetId,
                             int placement);

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
    void resizeEvent(QResizeEvent* event) override;

    [[nodiscard]] const regmap::Register* findRegister(const std::string& id) const;
    [[nodiscard]] const regmap::AddressSpace* findAddressSpace(const std::string& id) const;
    [[nodiscard]] const regmap::RegisterBlock* findBlock(const std::string& id) const;
    [[nodiscard]] const regmap::Field* findField(const regmap::Register& reg,
                                                 const std::string& id) const;
};
