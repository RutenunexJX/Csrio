#pragma once

#include "project_controller.hpp"

#include "regmap/core/model.hpp"

#include <QMainWindow>

#include <optional>
#include <string>

class QAction;
class BitfieldView;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPoint;
class QModelIndex;
class QStandardItem;
class QStandardItemModel;
class QTabWidget;
class QTableView;
class QTreeView;

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
    };

    ProjectController controller_;

    QTreeView* hierarchyView_{nullptr};
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
    QLabel* enumContextLabel_{nullptr};
    QTableView* enumView_{nullptr};
    QTabWidget* tabs_{nullptr};
    QTableView* problemsView_{nullptr};
    QTableView* generatedView_{nullptr};
    QTableView* diffView_{nullptr};

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

    std::string selectedAddressId_;
    std::string selectedBlockId_;
    std::string selectedRegisterId_;
    std::string selectedFieldId_;
    std::string selectedTagFilter_;
    std::optional<regmap::SourceLocation> currentSource_;
    bool refreshing_{false};
    bool modelEditInProgress_{false};
    bool refreshPending_{false};

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
    void populateHierarchy();
    void populateRegisters();
    void populateFields(const regmap::Register* reg);
    void populateEnumValues(const regmap::Register* reg, const regmap::Field* field);
    void setCurrentSource(const regmap::SourceLocation& source);
    void selectRegister(const std::string& id);
    void selectField(const std::string& id);
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
    void showRegisterContextMenu(const QPoint& position);
    void showFieldContextMenu(const QPoint& position);
    void convertSelectedRegisterToReserved();
    void deleteSelectedRegisterAndShift();
    void moveField(const std::string& fieldId, std::uint32_t lsb, std::uint32_t msb);
    void updateTagFilter();
    void updateEditActions();

    void closeEvent(QCloseEvent* event) override;

    [[nodiscard]] const regmap::Register* findRegister(const std::string& id) const;
    [[nodiscard]] const regmap::AddressSpace* findAddressSpace(const std::string& id) const;
    [[nodiscard]] const regmap::RegisterBlock* findBlock(const std::string& id) const;
    [[nodiscard]] const regmap::Field* findField(const regmap::Register& reg,
                                                 const std::string& id) const;
};
