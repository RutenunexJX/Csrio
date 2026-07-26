#include "bitfield_view.hpp"
#include "main_window.hpp"
#include "project_controller.hpp"
#include "workbench_theme.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPalette>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringList>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

class GuiSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void appliesWorkbookTheme();
    void createsWorkbenchFirstProject();
    void opensProjectAndPopulatesEditableViews();
    void navigatesHierarchyAndOpensFieldsExplicitly();
    void supportsTrailingRowsAndFieldMovement();
    void editsTagsAndAccessFromSingleClick();
    void insertsRegisterBetweenRows();
    void showsUnifiedSyncStateAndGeneratedResults();
    void searchesAndNavigatesProblems();
    void copiesAndPastesEditableCells();
    void deletesFocusedRegisterAndRestoresIt();
    void editsUndoesAndSavesProject();
    void rejectsInvalidManagedRtl();
    void synchronizesManagedRtlEdits();
    void resolvesRtlConflictFromDiffPanel();
    void reportsAndResolvesRtlConflicts();
};

namespace {

void createProject(const QString& path, std::uint64_t offset = 0)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray text = QStringLiteral(R"(schema_version: 2
workspace:
  id: gui-workspace
  name: GUI Workspace
  address_spaces:
    - id: space-main
      name: Main
      base: 0x0
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x1000
          registers:
            - id: reg-status
              name: STATUS
              offset: %1
              width: 32
              type: field
              array:
                count: 1
                stride: 0x4
              initial: 0x0
              reset: 0x0
              access: ro
              tags:
                - existing
              fields:
                - id: field-ready
                  name: READY
                  msb: 0
                  lsb: 0
                  type: bool
                  sw_access: ro
                  hw_access: wo
                  reset: 0x0
                  read_side_effect: none
                  write_side_effect: none
                  reset_domain: csr_rst_n
                  enum_values: []
rtl:
  path: rtl/gui_registers.sv
  module: gui_registers
generation:
  output_directory: generated
  targets:
    - kind: xlsx
      path: register-map.xlsx
    - kind: c-header
      path: gui_regs.h
    - kind: markdown
      path: register-map.md
)")
                                .arg(QStringLiteral("0x%1").arg(offset, 0, 16))
                                .toUtf8();
    QCOMPARE(file.write(text), text.size());
    file.close();
}

void createTwoRegisterProject(const QString& path)
{
    createProject(path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    const QString marker = QStringLiteral("rtl:\n");
    QVERIFY(text.contains(marker));
    const QString secondRegister = QStringLiteral(R"(            - id: reg-control
              name: CONTROL
              offset: 0x4
              width: 32
              type: unsigned
              array:
                count: 1
                stride: 0x4
              initial: 0x0
              reset: 0x0
              access: rw
              tags:
                - control
              fields: []
)");
    text.replace(marker, secondRegister + marker);
    const QByteArray bytes = text.toUtf8();
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}

void makeGeneratedFilesWritable(const QString& root)
{
    QDir directory(QDir(root).filePath(QStringLiteral("generated")));
    for (const QString& name : directory.entryList(QDir::Files)) {
        QFile::setPermissions(directory.filePath(name),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void editManagedRtlValue(const QString& path, const QString& objectId, const QString& property,
                         const QString& literal)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    file.close();
    const QString idMarker = QStringLiteral("\"id\":\"") + objectId + QStringLiteral("\"");
    const QString propertyMarker =
        QStringLiteral("\"property\":\"") + property + QStringLiteral("\"");
    bool changed = false;
    for (QString& line : lines) {
        if (!line.contains(idMarker) || !line.contains(propertyMarker)) {
            continue;
        }
        const qsizetype equals = line.indexOf('=');
        const qsizetype semicolon = line.indexOf(';', equals);
        QVERIFY(equals >= 0 && semicolon > equals);
        line.replace(equals + 1, semicolon - equals - 1, QStringLiteral(" ") + literal);
        changed = true;
        break;
    }
    QVERIFY(changed);
    const QByteArray text = lines.join('\n').toUtf8();
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(text), text.size());
    file.close();
}

} // namespace

void GuiSmokeTests::appliesWorkbookTheme()
{
    WorkbenchTheme::apply(*qApp);

    QCOMPARE(qApp->palette().color(QPalette::Window), QColor(QStringLiteral("#F4F7FB")));
    QCOMPARE(qApp->palette().color(QPalette::Base), QColor(QStringLiteral("#FFF9E6")));
    QCOMPARE(qApp->palette().color(QPalette::Highlight), QColor(QStringLiteral("#DCE6F1")));
    QCOMPARE(qApp->font().pointSizeF(), 10.0);
    QCOMPARE(qApp->font().weight(), QFont::Normal);
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("#17365D")));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("#C6D2E1")));
    QVERIFY(qApp->styleSheet().contains(QStringLiteral("font-weight: 600")));
    QVERIFY(!qApp->styleSheet().contains(QStringLiteral("font-weight: 700")));
}

void GuiSmokeTests::createsWorkbenchFirstProject()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("new-device.regmap.yaml"));
    ProjectController controller;

    QVERIFY(controller.createProject(manifestPath));
    QVERIFY(controller.workspace() != nullptr);
    QCOMPARE(controller.workspace()->name, std::string("new-device"));
    QVERIFY(!controller.workspace()->id.empty());
    QVERIFY(QFileInfo::exists(manifestPath));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("rtl/new_device_registers.sv"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/register-map.xlsx"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/new_device_regs.h"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/register-map.md"))));
    QVERIFY(!controller.isDirty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::opensProjectAndPopulatesEditableViews()
{
    WorkbenchTheme::apply(*qApp);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);

    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* pageBase = window.findChild<QLineEdit*>(QStringLiteral("pageBaseEdit"));
    auto* pageWidth = window.findChild<QLineEdit*>(QStringLiteral("pageWidthEdit"));
    auto* pageDescription = window.findChild<QLineEdit*>(QStringLiteral("pageDescriptionEdit"));
    auto* blockBase = window.findChild<QLineEdit*>(QStringLiteral("blockBaseEdit"));
    auto* blockSize = window.findChild<QLineEdit*>(QStringLiteral("blockSizeEdit"));
    auto* blockDescription = window.findChild<QLineEdit*>(QStringLiteral("blockDescriptionEdit"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    auto* tagFilter = window.findChild<QComboBox*>(QStringLiteral("tagFilter"));
    auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
    auto* generated = window.findChild<QTableView*>(QStringLiteral("generatedView"));
    auto* tabs = window.findChild<QTabWidget*>();
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(pageBase != nullptr);
    QVERIFY(pageWidth != nullptr);
    QVERIFY(pageDescription != nullptr);
    QVERIFY(blockBase != nullptr);
    QVERIFY(blockSize != nullptr);
    QVERIFY(blockDescription != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(tagFilter != nullptr);
    QVERIFY(problems != nullptr);
    QVERIFY(generated != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("inspector")) == nullptr);
    QVERIFY(tabs != nullptr);
    QCOMPARE(hierarchy->model()->rowCount(), 1);
    QCOMPARE(registers->model()->rowCount(), 2);
    QCOMPARE(fields->model()->rowCount(), 2);
    QCOMPARE(enums->model()->rowCount(), 2);
    QCOMPARE(registers->model()->columnCount(), 12);
    QCOMPARE(fields->model()->columnCount(), 14);
    QCOMPARE(problems->model()->rowCount(), 0);
    QCOMPARE(generated->model()->rowCount(), 3);
    QVERIFY(registers->model()->flags(registers->model()->index(0, 0)) & Qt::ItemIsEditable);
    QVERIFY(!(registers->model()->flags(registers->model()->index(0, 2)) & Qt::ItemIsEditable));
    QVERIFY(fields->model()->flags(fields->model()->index(0, 0)) & Qt::ItemIsEditable);
    QVERIFY(!(fields->model()->flags(fields->model()->index(0, 3)) & Qt::ItemIsEditable));
    QVERIFY(fields->model()->flags(fields->model()->index(0, 4)) & Qt::ItemIsEditable);
    QCOMPARE(registers->model()->index(1, 0).data().toString(), QStringLiteral("+"));
    QVERIFY(registers->model()->index(1, 1).data().toString().isEmpty());
    QCOMPARE(fields->model()->index(1, 0).data().toString(), QStringLiteral("+"));
    QVERIFY(fields->model()->index(1, 1).data().toString().isEmpty());
    QCOMPARE(pageBase->text(), QStringLiteral("0x0"));
    QCOMPARE(blockBase->text(), QStringLiteral("0x0"));
    QCOMPARE(pageWidth->text(), QStringLiteral("32"));
    QCOMPARE(blockSize->text(), QStringLiteral("0x1000"));
    QCOMPARE(registers->model()->index(0, 4).data().toString(), QStringLiteral("field"));
    QCOMPARE(registers->model()->index(0, 5).data().toString(), QStringLiteral("Open (1)"));
    QCOMPARE(registers->model()->index(0, 7).data().toString(), QStringLiteral("0x0"));
    QCOMPARE(fields->model()->headerData(11, Qt::Horizontal).toString(),
             QStringLiteral("Read Effect"));
    for (int column = 0; column < registers->model()->columnCount(); ++column) {
        const int expected = column == 11
                                 ? static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter)
                                 : static_cast<int>(Qt::AlignCenter);
        QCOMPARE(registers->model()
                     ->index(0, column)
                     .data(Qt::TextAlignmentRole)
                     .toInt(),
                 expected);
    }
    for (int column = 0; column < fields->model()->columnCount(); ++column) {
        const int expected = column == 13
                                 ? static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter)
                                 : static_cast<int>(Qt::AlignCenter);
        QCOMPARE(fields->model()->index(0, column).data(Qt::TextAlignmentRole).toInt(),
                 expected);
    }
    QCOMPARE(enums->model()->index(0, 0).data(Qt::TextAlignmentRole).toInt(),
             static_cast<int>(Qt::AlignCenter));
    QCOMPARE(enums->model()->index(0, 2).data(Qt::TextAlignmentRole).toInt(),
             static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    QCOMPARE(registers->verticalHeader()->defaultSectionSize(), 28);
    QCOMPARE(fields->verticalHeader()->defaultSectionSize(), 28);
    QCOMPARE(registers->font().pointSizeF(), 10.0);
    QCOMPARE(fields->font().pointSizeF(), 10.0);
    QVERIFY(hierarchy->uniformRowHeights());
    QVERIFY(tabs->documentMode());
    QVERIFY(fields->isColumnHidden(1));
    QVERIFY(fields->isColumnHidden(9));
    QVERIFY(fields->isColumnHidden(11));
    QVERIFY(fields->isColumnHidden(12));
    QVERIFY(!tabs->isTabVisible(0));
    QVERIFY(tabs->isTabVisible(1));
    QVERIFY(!tabs->isTabVisible(2));
    const QList<QSplitter*> splitters = window.findChildren<QSplitter*>();
    QCOMPARE(splitters.size(), 3);
    for (const auto* splitter : splitters) {
        QVERIFY(!splitter->childrenCollapsible());
        QCOMPARE(splitter->handleWidth(), 4);
    }

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::navigatesHierarchyAndOpensFieldsExplicitly()
{
    WorkbenchTheme::apply(*qApp);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1280, 800);
    window.show();
    QTest::qWait(50);

    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("projectToolBar"));
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(toolbar != nullptr);

    QStringList toolbarCommands;
    for (QAction* action : toolbar->actions()) {
        if (!action->isSeparator() && !action->text().isEmpty()) {
            toolbarCommands.push_back(action->text());
        }
    }
    QCOMPARE(toolbarCommands.size(), 3);
    QVERIFY(std::ranges::any_of(toolbarCommands,
                               [](const QString& text) { return text.startsWith("New Project"); }));
    QVERIFY(std::ranges::any_of(toolbarCommands,
                               [](const QString& text) { return text.startsWith("Open Project"); }));
    QVERIFY(toolbarCommands.contains(QStringLiteral("Save && Sync")));
    QVERIFY(std::ranges::none_of(toolbarCommands,
                                [](const QString& text) {
                                    return text.contains("Undo") || text.contains("Redo") ||
                                           text.contains("Add Page") ||
                                           text.contains("Register Block") ||
                                           text.contains("Delete Selected") ||
                                           text.contains("Open Source") ||
                                           text.contains("Reload");
                                }));

    QModelIndex root = hierarchy->model()->index(0, 0);
    QModelIndex page = hierarchy->model()->index(0, 0, root);
    hierarchy->expandAll();
    hierarchy->scrollTo(page);
    hierarchy->setCurrentIndex(page);
    hierarchy->setFocus();
    QCoreApplication::processEvents();
    QVERIFY(hierarchy->visualRect(page).height() >= 30);
    hierarchy->edit(page);
    QTRY_VERIFY_WITH_TIMEOUT(hierarchy->findChild<QLineEdit*>() != nullptr, 2000);
    auto* treeEditor = hierarchy->findChild<QLineEdit*>();
    QVERIFY(treeEditor->height() >= treeEditor->fontMetrics().height() + 8);
    QTest::keyClick(treeEditor, Qt::Key_Escape);

    root = hierarchy->model()->index(0, 0);
    Q_EMIT hierarchy->customContextMenuRequested(hierarchy->visualRect(root).center());
    QCoreApplication::processEvents();
    auto* rootMenu = hierarchy->findChild<QMenu*>(QStringLiteral("hierarchyContextMenu"));
    QVERIFY(rootMenu != nullptr);
    auto* newPage = rootMenu->findChild<QAction*>(QStringLiteral("newPageContextAction"));
    QVERIFY(newPage != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("renameContextAction")) != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("expandAllContextAction")) != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("collapseAllContextAction")) != nullptr);
    newPage->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchy->model()->index(0, 0).model()
                                  ->rowCount(hierarchy->model()->index(0, 0)),
                              2, 2000);

    QModelIndex newPageIndex = hierarchy->currentIndex();
    QCOMPARE(newPageIndex.data().toString(), QStringLiteral("NEW_PAGE"));
    Q_EMIT hierarchy->customContextMenuRequested(
        hierarchy->visualRect(newPageIndex).center());
    QCoreApplication::processEvents();
    auto* pageMenu = hierarchy->findChild<QMenu*>(QStringLiteral("hierarchyContextMenu"));
    QVERIFY(pageMenu != nullptr);
    auto* newBlock = pageMenu->findChild<QAction*>(QStringLiteral("newBlockContextAction"));
    QVERIFY(newBlock != nullptr);
    QVERIFY(pageMenu->findChild<QAction*>(QStringLiteral("renameContextAction")) != nullptr);
    QVERIFY(pageMenu->findChild<QAction*>(QStringLiteral("deleteContextAction")) != nullptr);
    newBlock->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchy->currentIndex().data().toString(),
                              QStringLiteral("NEW_BLOCK"), 2000);

    QModelIndex newBlockIndex = hierarchy->currentIndex();
    Q_EMIT hierarchy->customContextMenuRequested(
        hierarchy->visualRect(newBlockIndex).center());
    QCoreApplication::processEvents();
    auto* blockMenu = hierarchy->findChild<QMenu*>(QStringLiteral("hierarchyContextMenu"));
    QVERIFY(blockMenu != nullptr);
    auto* newRegister =
        blockMenu->findChild<QAction*>(QStringLiteral("newRegisterContextAction"));
    QVERIFY(newRegister != nullptr);
    QVERIFY(blockMenu->findChild<QAction*>(QStringLiteral("renameContextAction")) != nullptr);
    QVERIFY(blockMenu->findChild<QAction*>(QStringLiteral("deleteContextAction")) != nullptr);
    newRegister->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 2, 2000);
    QCOMPARE(registers->model()->index(0, 0).data().toString(),
             QStringLiteral("NEW_REGISTER"));

    root = hierarchy->model()->index(0, 0);
    page = hierarchy->model()->index(0, 0, root);
    const QModelIndex originalBlock = hierarchy->model()->index(0, 0, page);
    hierarchy->setCurrentIndex(originalBlock);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 3, 2000);

    registers->setCurrentIndex(registers->model()->index(1, 0));
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!fieldPanel->isVisible(), 2000);
    const QModelIndex openFields = registers->model()->index(0, 5);
    QCOMPARE(openFields.data().toString(), QStringLiteral("Open (1)"));
    Q_EMIT registers->clicked(openFields);
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QVERIFY(fields->isVisible());
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-status"));
    QCOMPARE(fields->model()->index(0, 0).data().toString(), QStringLiteral("READY"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::supportsTrailingRowsAndFieldMovement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1280, 800);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    auto* bitfield = window.findChild<BitfieldView*>(QStringLiteral("bitfieldView"));
    auto* pageBase = window.findChild<QLineEdit*>(QStringLiteral("pageBaseEdit"));
    auto* blockBase = window.findChild<QLineEdit*>(QStringLiteral("blockBaseEdit"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(bitfield != nullptr);
    QVERIFY(pageBase != nullptr);
    QVERIFY(blockBase != nullptr);

    pageBase->setText(QStringLiteral("0x1000"));
    Q_EMIT pageBase->editingFinished();
    blockBase->setText(QStringLiteral("0x20"));
    Q_EMIT blockBase->editingFinished();
    QCOMPARE(registers->model()->index(0, 2).data().toString(), QStringLiteral("0x1020"));

    Q_EMIT registers->clicked(registers->model()->index(1, 0));
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(registers->model()->index(1, 0).data().toString(), QStringLiteral("NEW_REGISTER"));
    QCOMPARE(registers->model()->index(1, 4).data().toString(), QStringLiteral("uint32"));
    QCOMPARE(registers->model()->index(1, 7).data().toString(), QStringLiteral("0x0"));
    QCOMPARE(fields->model()->rowCount(), 0);
    QVERIFY(!fields->isVisible());
    QVERIFY(
        registers->model()->setData(registers->model()->index(1, 6), QStringLiteral("0 .. 255")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 6).data().toString(),
                              QStringLiteral("0 .. 255"), 2000);
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4), QStringLiteral("uint16")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 3).data().toString(),
                              QStringLiteral("16"), 2000);
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4), QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 1, 2000);
    Q_EMIT enums->clicked(enums->model()->index(0, 0));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QCOMPARE(enums->model()->index(0, 0).data().toString(), QStringLiteral("NEW_VALUE"));
    QCOMPARE(enums->model()->index(0, 1).data().toString(), QStringLiteral("0x0"));
    QCOMPARE(registers->model()->index(1, 4).data().toString(), QStringLiteral("enum"));

    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 1, 2000);

    Q_EMIT fields->clicked(fields->model()->index(0, 0));
    QCOMPARE(fields->model()->rowCount(), 2);
    QCOMPARE(fields->model()->index(0, 0).data().toString(), QStringLiteral("NEW_FIELD"));
    const QModelIndex widthIndex = fields->model()->index(0, 4);
    fields->scrollTo(widthIndex);
    QTest::mouseClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(widthIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(fields->findChild<QLineEdit*>() != nullptr, 2000);
    auto* widthEditor = fields->findChild<QLineEdit*>();
    widthEditor->selectAll();
    QTest::keyClicks(widthEditor, QStringLiteral("4"));
    QTest::keyClick(widthEditor, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 2).data().toString(), QStringLiteral("3"),
                              2000);
    QCOMPARE(fields->model()->index(0, 3).data().toString(), QStringLiteral("0"));
    QVERIFY(fields->model()->setData(fields->model()->index(0, 4), QStringLiteral("0")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 4).data().toString(), QStringLiteral("4"),
                              2000);

    const QString fieldId = fields->model()->index(0, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(!fieldId.isEmpty());
    Q_EMIT bitfield->fieldMoveRequested(fieldId, 4, 7);
    QCOMPARE(fields->model()->index(0, 2).data().toString(), QStringLiteral("7"));
    QCOMPARE(fields->model()->index(0, 3).data().toString(), QStringLiteral("4"));
    QVERIFY(fields->model()->setData(fields->model()->index(0, 5), QStringLiteral("uint8")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 2).data().toString(), QStringLiteral("11"),
                              2000);
    QCOMPARE(fields->model()->index(0, 4).data().toString(), QStringLiteral("8"));
    QCOMPARE(fields->model()->index(0, 5).data().toString(), QStringLiteral("uint8"));
    QVERIFY(fields->model()->setData(fields->model()->index(0, 7), QStringLiteral("255")));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::editsTagsAndAccessFromSingleClick()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(registers != nullptr);
    QCOMPARE(registers->model()->rowCount(), 3);

    QModelIndex tagIndex = registers->model()->index(0, 10);
    registers->scrollTo(tagIndex);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(tagIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() != nullptr, 2000);
    auto* tagPopup = qobject_cast<QFrame*>(QApplication::activePopupWidget());
    QVERIFY(tagPopup != nullptr);
    QCOMPARE(tagPopup->objectName(), QStringLiteral("tagPopup"));
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    auto* search = tagPopup->findChild<QLineEdit*>(QStringLiteral("tagSearch"));
    auto* add = tagPopup->findChild<QToolButton*>(QStringLiteral("addTagButton"));
    auto* tags = tagPopup->findChild<QListWidget*>(QStringLiteral("tagOptions"));
    QVERIFY(search != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(tags != nullptr);
    QVERIFY(tags->styleSheet().contains(QStringLiteral("item:hover")));

    QListWidgetItem* control = nullptr;
    for (int itemIndex = 0; itemIndex < tags->count(); ++itemIndex) {
        QVERIFY(!(tags->item(itemIndex)->flags() & Qt::ItemIsUserCheckable));
        if (tags->item(itemIndex)->text() == QStringLiteral("control")) {
            control = tags->item(itemIndex);
        }
    }
    QVERIFY(control != nullptr);
    QVERIFY(!control->isSelected());
    QTest::mouseMove(tags->viewport(), tags->visualItemRect(control).center());
    QTest::mouseClick(tags->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tags->visualItemRect(control).center());
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control, existing"), 2000);

    search->setText(QStringLiteral("EXISTING"));
    QVERIFY(!add->isEnabled());
    search->setText(QStringLiteral("newtag"));
    QVERIFY(add->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control, existing, newtag"), 2000);
    tagPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() == nullptr, 2000);

    QModelIndex accessIndex = registers->model()->index(0, 9);
    QCOMPARE(accessIndex.data().toString(), QStringLiteral("RO"));
    QVERIFY(!(accessIndex.flags() & Qt::ItemIsEditable));
    registers->scrollTo(accessIndex);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(accessIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() != nullptr, 2000);
    auto* accessPopup = qobject_cast<QFrame*>(QApplication::activePopupWidget());
    QVERIFY(accessPopup != nullptr);
    QCOMPARE(accessPopup->objectName(), QStringLiteral("accessPopup"));
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    auto* access = accessPopup->findChild<QListWidget*>(QStringLiteral("accessOptions"));
    QVERIFY(access != nullptr);
    QStringList accessValues;
    QListWidgetItem* readWrite = nullptr;
    for (int itemIndex = 0; itemIndex < access->count(); ++itemIndex) {
        accessValues.push_back(access->item(itemIndex)->text());
        QVERIFY(!(access->item(itemIndex)->flags() & Qt::ItemIsUserCheckable));
        if (access->item(itemIndex)->text() == QStringLiteral("RW")) {
            readWrite = access->item(itemIndex);
        }
    }
    QCOMPARE(accessValues, QStringList({QStringLiteral("NONE"), QStringLiteral("RO"),
                                        QStringLiteral("WO"), QStringLiteral("RW")}));
    QVERIFY(readWrite != nullptr);
    QTest::mouseClick(access->viewport(), Qt::LeftButton, Qt::NoModifier,
                      access->visualItemRect(readWrite).center());
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("RW"), 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::insertsRegisterBetweenRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(registers != nullptr);
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(registers->model()->index(0, 0).data().toString(), QStringLiteral("STATUS"));
    QCOMPARE(registers->model()->index(1, 0).data().toString(), QStringLiteral("CONTROL"));

    const QModelIndex nextRegister = registers->model()->index(1, 0);
    registers->scrollToTop();
    const QRect nextRectangle = registers->visualRect(nextRegister);
    QVERIFY(nextRectangle.isValid());
    QTest::mouseMove(registers->viewport(),
                     registers->visualRect(registers->model()->index(0, 0)).center());
    const QPoint nonInsertionPoint(120, nextRectangle.top());
    QTest::mouseMove(registers->viewport(), nonInsertionPoint);
    QTest::qWait(20);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier, nonInsertionPoint);
    QCOMPARE(registers->model()->rowCount(), 3);

    QTest::mouseMove(registers->viewport(),
                     registers->visualRect(registers->model()->index(0, 0)).center());
    const QPoint insertionPoint(18, nextRectangle.top());
    QTest::mouseMove(registers->viewport(), insertionPoint);
    QTest::qWait(20);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier, insertionPoint);

    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 4, 2000);
    QCOMPARE(registers->model()->index(1, 0).data().toString(), QStringLiteral("NEW_REGISTER"));
    QCOMPARE(registers->model()->index(1, 1).data().toString(), QStringLiteral("0x4"));
    QCOMPARE(registers->model()->index(2, 0).data().toString(), QStringLiteral("CONTROL"));
    QCOMPARE(registers->model()->index(2, 1).data().toString(), QStringLiteral("0x8"));
    QCOMPARE(registers->model()->index(3, 0).data().toString(), QStringLiteral("+"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::showsUnifiedSyncStateAndGeneratedResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1200, 760);
    window.show();
    QTest::qWait(50);

    auto* state = window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    auto* generated = window.findChild<QTableView*>(QStringLiteral("generatedView"));
    auto* retry = window.findChild<QPushButton*>(QStringLiteral("retryOutputsButton"));
    auto* save = window.findChild<QAction*>(QStringLiteral("saveSyncAction"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(state != nullptr);
    QVERIFY(generated != nullptr);
    QVERIFY(retry != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(registers != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Synchronized")), 2000);
    QCOMPARE(generated->model()->rowCount(), 3);
    QCOMPARE(generated->model()->columnCount(), 4);
    for (int row = 0; row < generated->model()->rowCount(); ++row) {
        QCOMPARE(generated->model()->index(row, 2).data().toString(),
                 QStringLiteral("Synchronized"));
        QVERIFY(!generated->model()->index(row, 3).data().toString().isEmpty());
    }
    QVERIFY(!retry->isVisible());
    QCOMPARE(save->text(), QStringLiteral("Save && Sync"));
    QVERIFY(save->isEnabled());

    QVERIFY(registers->model()->setData(registers->model()->index(0, 11),
                                        QStringLiteral("Edited status description.")));
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Unsaved")), 2000);
    save->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Synchronized")), 4000);
    QCOMPARE(generated->model()->rowCount(), 3);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::searchesAndNavigatesProblems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1200, 760);
    window.show();
    QTest::qWait(50);

    auto* search = window.findChild<QLineEdit*>(QStringLiteral("globalSearchEdit"));
    auto* searchResult = window.findChild<QLabel*>(QStringLiteral("searchResultLabel"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
    auto* state = window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    QVERIFY(search != nullptr);
    QVERIFY(searchResult != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(problems != nullptr);
    QVERIFY(state != nullptr);

    search->setText(QStringLiteral("CONTROL"));
    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);
    QVERIFY(searchResult->text().startsWith(QStringLiteral("1/")));

    search->setText(QStringLiteral("READY"));
    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-status"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->currentIndex().data(Qt::UserRole + 1).toString(),
                              QStringLiteral("field-ready"), 2000);

    QVERIFY(registers->model()->setData(registers->model()->index(1, 1),
                                        QStringLiteral("0x0")));
    QTRY_VERIFY_WITH_TIMEOUT(problems->model()->rowCount() > 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Blocked")), 2000);

    int problemRow = -1;
    QString targetId;
    for (int row = 0; row < problems->model()->rowCount(); ++row) {
        const QString candidate = problems->model()->index(row, 3).data().toString();
        if (candidate == QStringLiteral("reg-status") ||
            candidate == QStringLiteral("reg-control")) {
            problemRow = row;
            targetId = candidate;
            break;
        }
    }
    QVERIFY(problemRow >= 0);
    const QString otherId = targetId == QStringLiteral("reg-status")
        ? QStringLiteral("reg-control")
        : QStringLiteral("reg-status");
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == otherId) {
            registers->setCurrentIndex(registers->model()->index(row, 0));
            break;
        }
    }
    Q_EMIT problems->doubleClicked(problems->model()->index(problemRow, 2));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(), targetId, 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::copiesAndPastesEditableCells()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(registers != nullptr);

    const QModelIndex statusName = registers->model()->index(0, 0);
    registers->setCurrentIndex(statusName);
    registers->selectionModel()->select(
        statusName, QItemSelectionModel::ClearAndSelect);
    registers->setFocus();
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("STATUS"));

    QApplication::clipboard()->setText(QStringLiteral("CONTROL_RENAMED\t0x8"));
    registers->setCurrentIndex(registers->model()->index(1, 0));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL_RENAMED"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x8"), 2000);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x4"), 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::deletesFocusedRegisterAndRestoresIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QCOMPARE(fields->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("field-ready"));

    registers->setCurrentIndex(registers->model()->index(0, 0));
    registers->setFocus();
    QTest::keyClick(registers, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 2, 2000);
    bool foundStatus = false;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        foundStatus |= registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("reg-status");
    }
    QVERIFY(!foundStatus);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 3, 2000);
    foundStatus = false;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        foundStatus |= registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("reg-status");
    }
    QVERIFY(foundStatus);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::editsUndoesAndSavesProject()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifestPath);

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(controller.workspace() != nullptr);
    QVERIFY(!controller.isDirty());
    QVERIFY(
        controller.editWorkspace(QStringLiteral("Move STATUS"), [](regmap::Workspace& workspace) {
            regmap::findRegister(workspace, "reg-status")->offset = 4;
        }));
    QVERIFY(controller.isDirty());
    QVERIFY(controller.canUndo());
    QCOMPARE(controller.changes().size(), std::size_t{1});
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{4});

    controller.undo();
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{0});
    QVERIFY(!controller.isDirty());
    QVERIFY(controller.changes().empty());
    controller.redo();
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{4});
    QCOMPARE(controller.changes().size(), std::size_t{1});
    controller.save();
    QVERIFY(!controller.isDirty());
    QVERIFY(controller.changes().empty());
    QFile savedManifest(manifestPath);
    QVERIFY(savedManifest.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(!savedManifest.readAll().contains("reset_domain"));
    savedManifest.close();

    const auto reopened = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QCOMPARE(regmap::findRegister(*reopened.workspace, "reg-status")->offset, std::uint64_t{4});
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsInvalidManagedRtl()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifestPath);

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(QFileInfo::exists(rtlPath));
    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'hx"));

    QTRY_VERIFY_WITH_TIMEOUT(controller.hasProjectErrors(), 8000);
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{0});
    QVERIFY(std::ranges::any_of(controller.diagnostics(), [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM5003";
    }));
    QVERIFY(!controller.isDirty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::synchronizesManagedRtlEdits()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifestPath);

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(QFileInfo::exists(rtlPath));
    QVERIFY(!controller.isDirty());
    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'h8"));

    QTRY_COMPARE_WITH_TIMEOUT(regmap::findRegister(*controller.workspace(), "reg-status")->offset,
                              std::uint64_t{8}, 8000);
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.isDirty());
    const auto reopened = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QCOMPARE(regmap::findRegister(*reopened.workspace, "reg-status")->offset, std::uint64_t{8});
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::resolvesRtlConflictFromDiffPanel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifestPath);

    MainWindow window;
    window.openProjectPath(manifestPath);
    window.resize(1200, 760);
    window.show();
    QTest::qWait(50);
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* diff = window.findChild<QTableView*>(QStringLiteral("diffView"));
    auto* conflictBar = window.findChild<QWidget*>(QStringLiteral("conflictBar"));
    auto* conflictSummary = window.findChild<QLabel*>(QStringLiteral("conflictSummaryLabel"));
    auto* keepWorkbench =
        window.findChild<QPushButton*>(QStringLiteral("keepWorkbenchButton"));
    auto* useRtl = window.findChild<QPushButton*>(QStringLiteral("useRtlButton"));
    auto* state = window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    QVERIFY(registers != nullptr);
    QVERIFY(diff != nullptr);
    QVERIFY(conflictBar != nullptr);
    QVERIFY(conflictSummary != nullptr);
    QVERIFY(keepWorkbench != nullptr);
    QVERIFY(useRtl != nullptr);
    QVERIFY(state != nullptr);
    QVERIFY(!conflictBar->isVisible());

    QVERIFY(registers->model()->setData(registers->model()->index(0, 1),
                                        QStringLiteral("0x4")));
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Unsaved")), 2000);
    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'h8"));

    QTRY_VERIFY_WITH_TIMEOUT(conflictBar->isVisible(), 8000);
    QVERIFY(conflictSummary->text().contains(QStringLiteral("1 RTL conflict")));
    QVERIFY(keepWorkbench->isVisible());
    QVERIFY(useRtl->isVisible());
    QVERIFY(diff->model()->rowCount() >= 1);
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Conflict")), 2000);

    QTest::mouseClick(useRtl, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!conflictBar->isVisible(), 8000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 1).data().toString(),
                              QStringLiteral("0x8"), 8000);
    QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Synchronized")), 8000);
    QVERIFY(state->toolTip().startsWith(QStringLiteral("Conflicts resolved")));
    QVERIFY(!state->toolTip().contains(QStringLiteral("paused"), Qt::CaseInsensitive));
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::reportsAndResolvesRtlConflicts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifestPath);

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(controller.editWorkspace(QStringLiteral("Workbench offset"),
                                     [](regmap::Workspace& workspace) {
                                         regmap::findRegister(workspace, "reg-status")->offset = 4;
                                     }));
    QVERIFY(controller.isDirty());
    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'h8"));

    QTRY_VERIFY_WITH_TIMEOUT(controller.hasConflicts(), 8000);
    QCOMPARE(controller.conflicts().size(), std::size_t{1});
    QCOMPARE(controller.conflicts().front().property, std::string("offset"));
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{4});

    controller.useRtlForConflicts();
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.isDirty());
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, std::uint64_t{8});
    const auto reopened = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QCOMPARE(regmap::findRegister(*reopened.workspace, "reg-status")->offset, std::uint64_t{8});
    makeGeneratedFilesWritable(directory.path());
}

QTEST_MAIN(GuiSmokeTests)

#include "gui_smoke_tests.moc"
