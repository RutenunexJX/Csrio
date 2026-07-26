#pragma once

#include "project_controller.hpp"

#include "regmap/core/model.hpp"

#include <QMainWindow>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class QAction;
class BitfieldView;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPoint;
class QModelIndex;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QTableView;
class QTreeView;
class QWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openProjectPath(const QString& path);

private:
    enum DataRole {
        objectIdRole = Qt::UserRole + 1,
        addressIdRole,
        blockIdRole,
        rowIndexRole,
        propertyRole,
        addRowRole,
        openFieldsRole,
        fieldsOpenRole,
    };

    ProjectController controller_;

    QTreeView* hierarchyView_{nullptr};
    QPushButton* hierarchyAddButton_{nullptr};
    QTableView* registerView_{nullptr};
    QLabel* pageContextLabel_{nullptr};
    QLabel* blockContextLabel_{nullptr};
    QLineEdit* pageBaseEdit_{nullptr};
    QLineEdit* pageWidthEdit_{nullptr};
    QLineEdit* pageDescriptionEdit_{nullptr};
    QLineEdit* blockBaseEdit_{nullptr};
    QLineEdit* blockSizeEdit_{nullptr};
    QLineEdit* blockDescriptionEdit_{nullptr};
    QComboBox* tagFilter_{nullptr};
    QTableView* fieldView_{nullptr};
    BitfieldView* bitfieldView_{nullptr};
    QWidget* fieldPanel_{nullptr};
    QWidget* fieldHeaderBar_{nullptr};
    QLabel* fieldContextLabel_{nullptr};
    QPushButton* closeFieldsButton_{nullptr};
    QLabel* enumContextLabel_{nullptr};
    QTableView* enumView_{nullptr};
    QTabWidget* tabs_{nullptr};
    QTableView* problemsView_{nullptr};
    QTableView* generatedView_{nullptr};
    QTableView* diffView_{nullptr};
    QLineEdit* globalSearchEdit_{nullptr};
    QLabel* searchResultLabel_{nullptr};
    QLabel* syncStateLabel_{nullptr};
    QPushButton* retryOutputsButton_{nullptr};
    QWidget* conflictBar_{nullptr};
    QLabel* conflictSummaryLabel_{nullptr};
    QPushButton* keepWorkbenchButton_{nullptr};
    QPushButton* useRtlButton_{nullptr};

    QStandardItemModel* hierarchyModel_{nullptr};
    QStandardItemModel* registerModel_{nullptr};
    QStandardItemModel* fieldModel_{nullptr};
    QStandardItemModel* enumModel_{nullptr};
    QStandardItemModel* problemsModel_{nullptr};
    QStandardItemModel* generatedModel_{nullptr};
    QStandardItemModel* diffModel_{nullptr};

    QAction* reloadAction_{nullptr};
    QAction* saveAction_{nullptr};
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
    QAction* showAdvancedFieldsAction_{nullptr};

    std::string selectedAddressId_;
    std::string selectedBlockId_;
    std::string selectedRegisterId_;
    std::filesystem::path displayedManifestPath_;
    std::string selectedFieldId_;
    std::string openFieldsRegisterId_;
    std::string selectedTagFilter_;
    std::optional<regmap::SourceLocation> currentSource_;
    bool refreshing_{false};
    bool modelEditInProgress_{false};
    bool refreshPending_{false};
    QString lastSyncMessage_;
    QString searchQuery_;
    std::vector<std::string> searchResults_;
    int searchResultIndex_{-1};

    void buildUi();
    void buildActions();
    void connectSignals();
    bool confirmProjectReplacement();
    void requestProjectRefresh();
    void refreshProject();
    void updateContextBar();
    void refreshDiagnostics();
    void refreshGenerated();
    void refreshDiff();
    void updateBottomPanelVisibility();
    void updateSyncPresentation(const QString& message = {});
    void applyFieldColumnVisibility();
    void populateHierarchy();
    void updateHierarchyAddAction();
    void populateRegisters();
    void populateFields(const regmap::Register* reg);
    void populateEnumValues(const regmap::Register* reg, const regmap::Field* field);
    void setCurrentSource(const regmap::SourceLocation& source);
    void selectRegister(const std::string& id);
    void selectField(const std::string& id);
    void beginHierarchyRename(const std::string& id);
    void beginRegisterRename(const std::string& id);
    void beginFieldRename(const std::string& id);
    void beginEnumRename(const std::string& id, const std::string& ownerId,
                         bool fieldOwner);
    void openSource(const regmap::SourceLocation& source);
    void applyPropertyEdit(const std::string& objectId, const std::string& property,
                           const QString& value);
    void addAddressSpace();
    void addBlock();
    void addRegister();
    void insertRegisterAt(int row);
    [[nodiscard]] bool canInsertRegisterAt(int row) const;
    void addField();
    void addSubfield();
    void addEnumValue();
    void deleteSelection();
    void editRegisterTags(const QModelIndex& index);
    void editRegisterAccess(const QModelIndex& index);
    void openFieldsAt(const QModelIndex& index);
    void openFieldsForRegister(const std::string& registerId);
    void updateFieldsAction(const std::string& registerId);
    void closeFields();
    void showHierarchyContextMenu(const QPoint& position);
    void showRegisterContextMenu(const QPoint& position);
    void showFieldContextMenu(const QPoint& position);
    void convertSelectedRegisterToReserved();
    void deleteSelectedRegisterAndShift();
    void moveField(const std::string& fieldId, std::uint32_t lsb, std::uint32_t msb);
    void updateTagFilter();
    void updateEditActions();
    bool navigateToObject(const std::string& id);
    void rebuildSearchResults();
    void runSearch(bool reverse = false);
    void copySelection();
    void pasteSelection();

    void closeEvent(QCloseEvent* event) override;

    [[nodiscard]] const regmap::Register* findRegister(const std::string& id) const;
    [[nodiscard]] const regmap::AddressSpace* findAddressSpace(const std::string& id) const;
    [[nodiscard]] const regmap::RegisterBlock* findBlock(const std::string& id) const;
    [[nodiscard]] const regmap::Field* findField(const regmap::Register& reg,
                                                 const std::string& id) const;
};
