#include "main_window.hpp"
#include "address_space_view.hpp"
#include "bitfield_view.hpp"
#include "workbench_controls.hpp"
#include "workspace_inspection.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>

void MainWindow::buildWorkflowActions()
{
    generateButton_->hide();
    synchronizeButton_->hide();
    fileStateLabel_->hide();
    moreProjectButton_ = WorkbenchControls::toolButton(pageHeader_);
    moreProjectButton_->setObjectName("moreProjectButton");
    moreProjectButton_->setText("More");
    moreProjectButton_->setAccessibleName("More project operations");
    moreProjectButton_->setPopupMode(QToolButton::InstantPopup);
    moreProjectButton_->setProperty("requiresEditorCommit", true);
    moreProjectButton_->installEventFilter(this);
    auto* more = WorkbenchControls::menu(moreProjectButton_);
    more->addAction(generateAction_);
    more->addAction(synchronizeAction_);
    moreProjectButton_->setMenu(more);
    qobject_cast<QHBoxLayout*>(pageHeader_->layout())->insertWidget(
        pageHeader_->layout()->count() - 1, moreProjectButton_);

    recoveryDraftAction_ = new QAction("Recovery Draft...", this);
    recoveryDraftAction_->setObjectName("recoveryDraftAction");
    recoveryDraftAction_->setEnabled(false);
    for (auto* action : menuBar()->actions()) {
        if (action->menu() && action->menu()->actions().contains(generateAction_)) {
            action->menu()->addSeparator();
            action->menu()->addAction(recoveryDraftAction_);
        }
    }
    connect(recoveryDraftAction_, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) return;
        if (controller_.recoveryDraftAvailable()) promptRecoveryDraft();
        else QMessageBox::information(this, "Recovery Draft",
            "No different recovery draft is available for this project.");
    });

    auto* toolbar = findChild<QToolBar*>("projectToolBar");
    navigateBackAction_ = new QAction("Back", this);
    navigateForwardAction_ = new QAction("Forward", this);
    navigateBackAction_->setObjectName("navigateBackAction");
    navigateForwardAction_->setObjectName("navigateForwardAction");
    navigateBackAction_->setShortcut(QKeySequence("Ctrl+Alt+Left"));
    navigateForwardAction_->setShortcut(QKeySequence("Ctrl+Alt+Right"));
    const auto addNavigation = [this, toolbar](QAction* action, QStyle::StandardPixmap icon,
                                              const QString& name) {
        addAction(action);
        auto* button = WorkbenchControls::toolButton(toolbar);
        button->setObjectName(name);
        button->setProperty("requiresEditorCommit", true);
        button->installEventFilter(this);
        button->setDefaultAction(action);
        button->setIcon(style()->standardIcon(icon));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAccessibleName(action->text());
        button->setToolTip(action->text() + " (" + action->shortcut().toString() + ")");
        toolbar->insertWidget(toolbar->actions().front(), button);
    };
    addNavigation(navigateForwardAction_, QStyle::SP_ArrowForward, "navigateForwardButton");
    addNavigation(navigateBackAction_, QStyle::SP_ArrowBack, "navigateBackButton");
    connect(navigateBackAction_, &QAction::triggered, this, [this] { navigateHistory(-1); });
    connect(navigateForwardAction_, &QAction::triggered, this, [this] { navigateHistory(1); });

    allSearchResultsAction_ = new QAction("All Search Results...", this);
    allSearchResultsAction_->setObjectName("allSearchResultsAction");
    allSearchResultsAction_->setShortcut(QKeySequence("Ctrl+Shift+F"));
    addAction(allSearchResultsAction_);
    connect(allSearchResultsAction_, &QAction::triggered, this, &MainWindow::showAllSearchResults);
    allSearchResultsButton_ = WorkbenchControls::toolButton(toolbar);
    allSearchResultsButton_->setObjectName("allSearchResultsButton");
    allSearchResultsButton_->setProperty("requiresEditorCommit", true);
    allSearchResultsButton_->installEventFilter(this);
    allSearchResultsButton_->setDefaultAction(allSearchResultsAction_);
    allSearchResultsButton_->setToolTip("Browse every match and filter by type or Block (Ctrl+Shift+F)");
    allSearchResultsButton_->setAccessibleName("View all search results");
    auto* allResultsToolbarAction = toolbar->insertWidget(toolbar->actions().back(), allSearchResultsButton_);
    allResultsToolbarAction->setObjectName("allResultsToolbarAction");
    allResultsToolbarAction->setVisible(false);
    allSearchResultsButton_->hide();
    connect(globalSearchEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) allSearchResultsButton_->hide();
    });
    for (auto* action : menuBar()->actions()) {
        if (action->menu() && action->menu()->actions().contains(undoAction_))
            action->menu()->addAction(allSearchResultsAction_);
    }

    addressMapToggle_ = WorkbenchControls::toolButton(addressSpaceScroll_->parentWidget());
    addressMapToggle_->setObjectName("addressMapToggle");
    addressMapToggle_->setText("Address map");
    addressMapToggle_->setCheckable(true);
    addressMapToggle_->setProperty("requiresEditorCommit", true);
    addressMapToggle_->installEventFilter(this);
    addressMapToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* registerLayout = qobject_cast<QVBoxLayout*>(addressSpaceScroll_->parentWidget()->layout());
    registerLayout->insertWidget(registerLayout->indexOf(addressSpaceScroll_), addressMapToggle_);
    connect(addressMapToggle_, &QToolButton::toggled, this, &MainWindow::updateFocusedEditorLayout);

    enumValuesToggle_ = WorkbenchControls::toolButton(enumPanel_);
    enumValuesToggle_->setObjectName("enumValuesToggle");
    enumValuesToggle_->setText("Show values");
    enumValuesToggle_->setCheckable(true);
    enumValuesToggle_->setProperty("requiresEditorCommit", true);
    enumValuesToggle_->installEventFilter(this);
    auto* enumLayout = qobject_cast<QVBoxLayout*>(enumPanel_->layout());
    if (auto* header = qobject_cast<QHBoxLayout*>(enumLayout->itemAt(0)->layout()))
        header->insertWidget(1, enumValuesToggle_);
    connect(enumValuesToggle_, &QToolButton::toggled, this, &MainWindow::updateFocusedEditorLayout);

    auto* diffPanel = diffView_->parentWidget();
    auto* diffLayout = qobject_cast<QVBoxLayout*>(diffPanel->layout());
    diffLayout->removeWidget(diffView_);
    auto* splitter = new QSplitter(Qt::Vertical, diffPanel);
    splitter->setObjectName("diffDetailsSplitter");
    splitter->addWidget(diffView_);
    diffDetailsView_ = WorkbenchControls::resultTable(splitter);
    diffDetailsView_->setObjectName("diffDetailsView");
    diffDetailsView_->setAccessibleName("Selected change: property values before and after");
    diffDetailsModel_ = new QStandardItemModel(this);
    diffDetailsView_->setModel(diffDetailsModel_);
    diffDetailsView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    diffDetailsView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    diffDetailsView_->setSelectionMode(QAbstractItemView::SingleSelection);
    diffView_->installEventFilter(this);
    splitter->addWidget(diffDetailsView_);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->handle(1)->setFocusPolicy(Qt::StrongFocus);
    splitter->handle(1)->setAccessibleName("Resize changes and property values");
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    diffLayout->addWidget(splitter, 1);
    diffDetailsView_->hide();
    auto* detailsTimer = new QTimer(this);
    detailsTimer->setSingleShot(true);
    connect(detailsTimer, &QTimer::timeout, this, &MainWindow::refreshChangeDetails);
    const auto scheduleDetails = [detailsTimer] { detailsTimer->start(0); };
    connect(diffView_->selectionModel(), &QItemSelectionModel::currentChanged, this, scheduleDetails);
    connect(diffModel_, &QAbstractItemModel::modelReset, this, scheduleDetails);
    connect(diffModel_, &QAbstractItemModel::rowsInserted, this, scheduleDetails);
    connect(tabs_, &QTabWidget::currentChanged, this, scheduleDetails);
    connect(resultsToggleButton_, &QToolButton::toggled, this, scheduleDetails);
    connect(diffDetailsView_, &QTableView::doubleClicked, this, &MainWindow::activateChangeDetail);
    auto* detailsReturn = new QShortcut(QKeySequence(Qt::Key_Return), diffDetailsView_);
    detailsReturn->setContext(Qt::WidgetShortcut);
    connect(detailsReturn, &QShortcut::activated, this,
        [this] { activateChangeDetail(diffDetailsView_->currentIndex()); });
    updateNavigationActions();
}

void MainWindow::updateFocusedEditorLayout()
{
    if (!addressMapToggle_) return;
    const bool fieldsOpen = !openFieldsRegisterId_.empty();
    const bool focused = compactLayout_ && fieldsOpen;
    editorSplitter_->widget(0)->setVisible(!focused);
    closeFieldsButton_->setText(focused ? "Back to Registers" : "Close Fields");
    closeFieldsButton_->setAccessibleName(focused ? "Return to the selected Register" : "Close Fields");
    addressMapToggle_->setVisible(compactLayout_ && controller_.workspace());
    addressSpaceScroll_->setVisible(controller_.workspace() &&
        (!compactLayout_ || addressMapToggle_->isChecked()));
    addressMapToggle_->setArrowType(addressMapToggle_->isChecked() ? Qt::DownArrow : Qt::RightArrow);
    const bool implicit = enumPanel_->property("implicitBoolean").toBool();
    enumValuesToggle_->setVisible(compactLayout_ && implicit);
    enumValuesToggle_->setText(enumValuesToggle_->isChecked() ? "Hide values" : "Show values");
    enumView_->setVisible(!compactLayout_ || !implicit || enumValuesToggle_->isChecked());
}

void MainWindow::refreshChangeDetails()
{
    if (!diffDetailsModel_ || !tabs_->isVisible() || tabs_->currentIndex() != 2) return;
    if (!diffView_->currentIndex().isValid() && diffModel_->rowCount() > 0)
        diffView_->setCurrentIndex(diffModel_->index(0, 0));
    const auto index = diffView_->currentIndex();
    diffDetailsModel_->clear();
    diffDetailsModel_->setHorizontalHeaderLabels({"Object path", "Property", "Before", "After"});
    if (!index.isValid()) { diffDetailsView_->hide(); return; }
    const auto key = diffModel_->index(index.row(), 0);
    const auto details = controller_.changeDetails(key.data(changeOriginRole).toString(),
                                                   key.data(objectIdRole).toString());
    for (const auto& detail : details) {
        QList<QStandardItem*> row;
        for (const auto& value : {detail.path, detail.property, detail.before, detail.after}) {
            auto* item = new QStandardItem(value);
            item->setToolTip(value);
            item->setData(detail.objectId, objectIdRole);
            item->setData(detail.property, propertyRole);
            row.push_back(item);
        }
        diffDetailsModel_->appendRow(row);
    }
    diffDetailsView_->setVisible(!details.empty());
    diffDetailsView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    diffDetailsView_->setColumnWidth(0, 280);
    diffDetailsView_->setColumnWidth(1, 120);
    diffDetailsView_->setColumnWidth(2, 240);
    diffDetailsView_->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::activateChangeDetail(const QModelIndex& index)
{
    if (!index.isValid() || !commitActiveEditor()) return;
    const auto id = index.data(objectIdRole).toString().toStdString();
    const QString property = index.data(propertyRole).toString();
    if (!navigateToObject(id, true)) {
        statusBar()->showMessage("This object is absent from the current model; its previous values remain in the preview.", 6000);
        return;
    }
    for (auto* table : {registerView_, fieldView_, enumView_}) {
        const QString tableProperty = table == registerView_ &&
            (property == "minimum" || property == "maximum") ? QString("range") : property;
        for (int row = 0; row < table->model()->rowCount(); ++row) {
            if (table->model()->index(row, 0).data(objectIdRole).toString().toStdString() != id) continue;
            for (int column = 0; column < table->model()->columnCount(); ++column) {
                const auto cell = table->model()->index(row, column);
                if (cell.data(propertyRole).toString() == tableProperty ||
                    (table == fieldView_ && property == "lsb" &&
                     table->model()->headerData(column, Qt::Horizontal).toString() == "LSB")) {
                    if (table->isColumnHidden(column)) {
                        if (table == fieldView_) showAdvancedFieldsAction_->setChecked(true);
                        else if (table == registerView_) showDetailedRegistersAction_->setChecked(true);
                    }
                    table->setCurrentIndex(cell);
                    table->scrollTo(cell);
                    WorkbenchControls::focusWhenVisible(table, Qt::OtherFocusReason);
                    return;
                }
            }
        }
    }
    QLineEdit* context = nullptr;
    if (id == selectedBlockId_) {
        if (property == "base") context = blockBaseEdit_;
        else if (property == "size") context = blockSizeEdit_;
        else if (property == "description") context = blockDescriptionEdit_;
    } else if (id == selectedAddressId_) {
        if (property == "base") context = pageBaseEdit_;
        else if (property == "address_width") context = pageWidthEdit_;
        else if (property == "description") context = pageDescriptionEdit_;
    }
    if (context && context->isVisible()) { context->setFocus(); context->selectAll(); }
}

MainWindow::NavigationPosition MainWindow::captureNavigationPosition() const
{
    NavigationPosition result;
    result.page = selectedAddressId_; result.block = selectedBlockId_;
    result.reg = selectedRegisterId_; result.field = selectedFieldId_;
    result.openFields = openFieldsRegisterId_; result.tag = selectedTagFilter_;
    result.active = activeNavigationObjectId_;
    const auto tableState = [](QTableView* table) {
        const auto index = table->currentIndex();
        return NavigationTableState{index.data(objectIdRole).toString(),
            index.data(propertyRole).toString(), index.column(),
            table->horizontalScrollBar()->value(), table->verticalScrollBar()->value()};
    };
    result.registers = tableState(registerView_);
    result.fields = tableState(fieldView_);
    result.enums = tableState(enumView_);
    result.focusTable = lastCommandView_ == enumView_ ? 3 :
        lastCommandView_ == fieldView_ ? 2 : lastCommandView_ == hierarchyView_ ? 0 : 1;
    result.hierarchyScroll = hierarchyView_->verticalScrollBar()->value();
    if (auto* panel = findChild<QScrollArea*>("registerPanelScroll"))
        result.registerPanelScroll = panel->verticalScrollBar()->value();
    if (auto* panel = findChild<QScrollArea*>("fieldPanelScroll"))
        result.fieldPanelScroll = panel->verticalScrollBar()->value();
    return result;
}

bool MainWindow::navigateToObject(const std::string& id, bool focusTarget)
{
    updateNavigationActions();
    const auto before = captureNavigationPosition();
    if (!navigateToObjectImpl(id, focusTarget)) return false;
    if (restoringNavigation_) return true;
    if (navigationHistoryIndex_ < 0) {
        navigationHistory_.push_back(before);
        navigationHistoryIndex_ = 0;
    } else {
        navigationHistory_[static_cast<std::size_t>(navigationHistoryIndex_)] = before;
    }
    navigationHistory_.resize(static_cast<std::size_t>(navigationHistoryIndex_ + 1));
    navigationHistory_.push_back(captureNavigationPosition());
    if (navigationHistory_.size() > 64) navigationHistory_.erase(navigationHistory_.begin());
    navigationHistoryIndex_ = static_cast<int>(navigationHistory_.size()) - 1;
    updateNavigationActions();
    return true;
}

void MainWindow::restoreNavigationPosition(const NavigationPosition& position)
{
    selectedAddressId_ = position.page; selectedBlockId_ = position.block;
    selectedRegisterId_ = position.reg; selectedFieldId_ = position.field;
    openFieldsRegisterId_ = position.openFields; selectedTagFilter_ = position.tag;
    activeNavigationObjectId_ = position.active;
    refreshProject();
    const auto restoreTable = [](QTableView* table, const NavigationTableState& state) {
        for (int row = 0; row < table->model()->rowCount(); ++row) {
            if (table->model()->index(row, 0).data(objectIdRole).toString() != state.id) continue;
            int column = std::clamp(state.column, 0, table->model()->columnCount() - 1);
            if (!state.property.isEmpty()) {
                for (int candidate = 0; candidate < table->model()->columnCount(); ++candidate) {
                    if (table->model()->index(row, candidate).data(propertyRole).toString() == state.property) {
                        column = candidate; break;
                    }
                }
            }
            table->setCurrentIndex(table->model()->index(row, column));
            break;
        }
        table->horizontalScrollBar()->setValue(state.horizontal);
        table->verticalScrollBar()->setValue(state.vertical);
    };
    restoreTable(registerView_, position.registers);
    restoreTable(fieldView_, position.fields);
    restoreTable(enumView_, position.enums);
    hierarchyView_->verticalScrollBar()->setValue(position.hierarchyScroll);
    if (auto* panel = findChild<QScrollArea*>("registerPanelScroll"))
        panel->verticalScrollBar()->setValue(position.registerPanelScroll);
    if (auto* panel = findChild<QScrollArea*>("fieldPanelScroll"))
        panel->verticalScrollBar()->setValue(position.fieldPanelScroll);
    QWidget* target = position.focusTable == 3 ? static_cast<QWidget*>(enumView_) :
        position.focusTable == 2 ? static_cast<QWidget*>(fieldView_) :
        position.focusTable == 1 ? static_cast<QWidget*>(registerView_) : hierarchyView_;
    lastCommandView_ = target;
    if (auto* table = qobject_cast<QTableView*>(target)) lastCellTable_ = table;
    fieldEditContextActive_ = target == fieldView_;
    WorkbenchControls::focusWhenVisible(target, Qt::ShortcutFocusReason);
}

void MainWindow::navigateHistory(int direction)
{
    if (!commitActiveEditor() || navigationHistoryIndex_ < 0) return;
    navigationHistory_[static_cast<std::size_t>(navigationHistoryIndex_)] = captureNavigationPosition();
    const QScopedValueRollback guard(restoringNavigation_, true);
    int target = navigationHistoryIndex_ + direction;
    while (target >= 0 && target < static_cast<int>(navigationHistory_.size())) {
        const auto position = navigationHistory_[static_cast<std::size_t>(target)];
        const auto* workspace = controller_.workspace();
        if (!workspace) break;
        const std::string id = !position.field.empty() ? position.field :
            !position.reg.empty() ? position.reg : !position.block.empty() ? position.block :
            !position.page.empty() ? position.page : workspace->id;
        if (navigateToObjectImpl(id, false)) {
            navigationHistoryIndex_ = target;
            restoreNavigationPosition(position);
            break;
        }
        target += direction;
    }
    updateNavigationActions();
}

void MainWindow::updateNavigationActions()
{
    if (navigationHistoryPath_ != controller_.manifestPath()) {
        navigationHistoryPath_ = controller_.manifestPath();
        navigationHistory_.clear(); navigationHistoryIndex_ = -1;
    }
    if (navigateBackAction_) navigateBackAction_->setEnabled(navigationHistoryIndex_ > 0);
    if (navigateForwardAction_) navigateForwardAction_->setEnabled(navigationHistoryIndex_ >= 0 &&
        navigationHistoryIndex_ + 1 < static_cast<int>(navigationHistory_.size()));
    if (recoveryDraftAction_) recoveryDraftAction_->setEnabled(controller_.workspace());
}

void MainWindow::showAllSearchResults()
{
    if (!commitActiveEditor() || !controller_.workspace()) return;
    if (allSearchResultsDialog_) { allSearchResultsDialog_->raise(); return; }
    QDialog dialog(this);
    allSearchResultsDialog_ = &dialog;
    dialog.setObjectName("allSearchResultsDialog");
    dialog.setWindowTitle("All Search Results");
    dialog.resize(900, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* query = WorkbenchControls::lineEdit(&dialog);
    query->setObjectName("allResultsQuery");
    query->setPlaceholderText("Search name, address, tag, description");
    query->setText(globalSearchEdit_->text());
    layout->addWidget(query);
    auto* filters = new QHBoxLayout;
    auto* type = WorkbenchControls::comboBox(&dialog);
    type->setObjectName("allResultsType");
    type->setAccessibleName("Filter by object type");
    type->addItem("All types", "");
    for (const auto& pair : {std::pair{"Workspace", "workspace"}, {"Page", "address-space"},
                            {"Block", "block"}, {"Register", "register"}, {"Field", "field"},
                            {"Enum value", "enum"}}) type->addItem(pair.first, pair.second);
    auto* block = WorkbenchControls::comboBox(&dialog);
    block->setObjectName("allResultsBlock");
    block->setAccessibleName("Filter by Register Block");
    filters->addWidget(type); filters->addWidget(block, 1);
    layout->addLayout(filters);
    auto* count = WorkbenchControls::label(&dialog);
    count->setObjectName("allResultsCount");
    layout->addWidget(count);
    auto* table = WorkbenchControls::resultTable(&dialog);
    table->setObjectName("allResultsTable");
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    QStandardItemModel model;
    table->setModel(&model);
    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    regmap::ui::WorkspaceInspection objects;
    const auto refreshMetadata = [&] {
        const QString selectedBlock = block->currentData().toString();
        const QSignalBlocker blocker(block);
        block->clear(); block->addItem("All Blocks", "");
        objects = controller_.workspace() ? regmap::ui::inspectWorkspace(*controller_.workspace()) :
                                           regmap::ui::WorkspaceInspection{};
        for (const auto& object : objects) if (object.kind == "block")
            block->addItem(object.path, object.id);
        block->setCurrentIndex(std::max(0, block->findData(selectedBlock)));
    };
    const auto refresh = [&] {
        const QSignalBlocker blocker(globalSearchEdit_);
        globalSearchEdit_->setText(query->text());
        rebuildSearchResults();
        model.clear(); model.setHorizontalHeaderLabels({"Type", "Object / address"});
        const QString kind = type->currentData().toString();
        const QString blockId = block->currentData().toString();
        for (std::size_t index = 0; index < searchResults_.size(); ++index) {
            const QString id = QString::fromStdString(searchResults_[index]);
            const auto object = objects.value(id);
            if ((!kind.isEmpty() && object.kind != kind) ||
                (!blockId.isEmpty() && object.blockId != blockId)) continue;
            auto* name = new QStandardItem(searchResultLabels_[index]);
            name->setToolTip(searchResultLabels_[index]);
            name->setData(id, objectIdRole);
            auto* kindItem = new QStandardItem(object.kind);
            kindItem->setData(id, objectIdRole);
            model.appendRow({kindItem, name});
        }
        count->setText(QStringLiteral("%1 shown · %2 total matches").arg(model.rowCount()).arg(searchResults_.size()));
        table->setColumnWidth(0, 110);
        table->horizontalHeader()->setStretchLastSection(true);
        if (model.rowCount() > 0) table->setCurrentIndex(model.index(0, 0));
        buttons->button(QDialogButtonBox::Open)->setEnabled(model.rowCount() > 0);
    };
    refreshMetadata(); refresh();
    connect(query, &QLineEdit::textChanged, &dialog, refresh);
    connect(type, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(block, &QComboBox::currentIndexChanged, &dialog, refresh);
    connect(&controller_, &ProjectController::projectChanged, &dialog, [&] {
        refreshMetadata(); refresh();
    });
    QString selectedId;
    const auto activate = [&] {
        const auto index = table->currentIndex();
        if (index.isValid()) {
            selectedId = index.data(objectIdRole).toString();
            dialog.accept();
        }
    };
    connect(buttons, &QDialogButtonBox::accepted, &dialog, activate);
    connect(table, &QTableView::doubleClicked, &dialog, activate);
    auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), table);
    enter->setContext(Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, &dialog, activate);
    query->setFocus();
    const int result = dialog.exec();
    allSearchResultsDialog_.clear();
    if (result == QDialog::Accepted && !selectedId.isEmpty()) {
        if (navigateToObject(selectedId.toStdString(), true)) {
            const auto found = std::ranges::find(searchResults_, selectedId.toStdString());
            searchResultIndex_ = found == searchResults_.end() ? -1 :
                static_cast<int>(std::distance(searchResults_.begin(), found));
        } else {
            statusBar()->showMessage("The selected search result is no longer present in the project.", 6000);
        }
    }
    refreshSearchCompletion();
}
