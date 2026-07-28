#include "bitfield_view.hpp"
#include "main_window.hpp"
#include "project_controller.hpp"
#include "workbench_theme.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPalette>
#include <QSplitter>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QStringList>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

class GuiSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void appliesWorkbookTheme();
    void createsWorkbenchFirstProject();
    void establishesMissingBaselineForIdenticalSources();
    void blocksDivergentSourcesWithoutBaseline();
    void resolvesMissingBaselineWithExplicitChoice();
    void recoversAfterInvalidInitialRtlIsFixed();
    void detectsStructuralDifferenceWithoutBaseline();
    void opensProjectAndPopulatesEditableViews();
    void navigatesHierarchyAndOpensFieldsExplicitly();
    void hierarchyContextActionsUseRightClickedTarget();
    void copiesAndPastesHierarchyObjects();
    void movesHierarchyObjectsByDrag();
    void switchesProjectsWithoutReusingFieldWorkspaceState();
    void reloadsProjectWithoutLosingFieldWorkspaceContext();
    void navigatesFieldProblemsAndFallsBackForHiddenFields();
    void navigatesEnumProblemsToExactRows();
    void supportsTrailingRowsAndFieldMovement();
    void dragsFieldsAndResolvesOverlaps();
    void cancelsInterruptedFieldDrag();
    void editsTagsAndAccessFromDoubleClick();
    void insertsRegisterBetweenRows();
    void showsUnifiedSyncStateAndGeneratedResults();
    void searchesAndNavigatesProblems();
    void copiesAndPastesEditableCells();
    void keepsUndoRedoInsideActiveEditor();
    void savesActiveEditorWithShortcut();
    void rejectsInvalidActiveEditorBeforeSave();
    void deletesFocusedRegisterAndRestoresIt();
    void editsUndoesAndSavesProject();
    void rejectsInvalidManagedRtl();
    void synchronizesManagedRtlEdits();
    void synchronizesManagedRtlRegisterOrderWithExistingBaseline();
    void synchronizesManagedRtlRegisterReparentWithExistingBaseline();
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

void createFieldDiagnosticProject(const QString& path, bool hiddenFieldOwner)
{
    createTwoRegisterProject(path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString validRange = QStringLiteral("                  msb: 0\n"
                                              "                  lsb: 0\n");
    const QString invalidRange = QStringLiteral("                  msb: 1\n"
                                                "                  lsb: 0\n");
    QCOMPARE(text.count(validRange), 1);
    text.replace(validRange, invalidRange);
    if (hiddenFieldOwner) {
        const QString structureType = QStringLiteral("              type: field\n");
        QCOMPARE(text.count(structureType), 1);
        text.replace(structureType, QStringLiteral("              type: unsigned\n"));
    }

    const QByteArray bytes = text.toUtf8();
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}

void createBitfieldDragProject(const QString& path)
{
    createProject(path);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const QString originalRange = QStringLiteral("                  msb: 0\n"
                                                 "                  lsb: 0\n");
    const QString widenedRange = QStringLiteral("                  msb: 3\n"
                                                "                  lsb: 0\n");
    QCOMPARE(text.count(originalRange), 1);
    text.replace(originalRange, widenedRange);

    const QString marker = QStringLiteral("rtl:\n");
    QVERIFY(text.contains(marker));
    const QString obstacle = QStringLiteral(R"(                - id: field-obstacle
                  name: OBSTACLE
                  msb: 9
                  lsb: 4
                  type: bits
                  sw_access: ro
                  hw_access: wo
                  reset: 0x0
                  read_side_effect: none
                  write_side_effect: none
                  enum_values: []
)");
    text.replace(marker, obstacle + marker);

    const QByteArray bytes = text.toUtf8();
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}

QPoint bitfieldPointForBit(const BitfieldView* view, std::uint32_t bit,
                           std::uint32_t registerWidth = 32)
{
    const double left = 14.0;
    const double available = std::max(1.0, static_cast<double>(view->width()) - 28.0);
    const double center =
        left + (static_cast<double>(registerWidth) - static_cast<double>(bit) - 0.5) /
            static_cast<double>(registerWidth) * available;
    return QPoint(static_cast<int>(center + 0.5), 93);
}

void makeGeneratedFilesWritable(const QString& root)
{
    QDir directory(QDir(root).filePath(QStringLiteral("generated")));
    for (const QString& name : directory.entryList(QDir::Files)) {
        QFile::setPermissions(directory.filePath(name),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

QModelIndex hierarchyIndexByObjectId(QAbstractItemModel* model, const QString& objectId,
                                     const QModelIndex& parent = {})
{
    if (model == nullptr || objectId.isEmpty()) {
        return {};
    }
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data(Qt::UserRole + 1).toString() == objectId) {
            return index;
        }
        const QModelIndex descendant = hierarchyIndexByObjectId(model, objectId, index);
        if (descendant.isValid()) {
            return descendant;
        }
    }
    return {};
}

bool dropHierarchyObject(QTreeView* view, const QModelIndex& source,
                         const QModelIndex& target)
{
    if (view == nullptr || !source.isValid() || !target.isValid() ||
        view->model() == nullptr) {
        return false;
    }
    QMimeData* mimeData = view->model()->mimeData(QModelIndexList{source});
    if (mimeData == nullptr) {
        mimeData = new QMimeData;
    }
    mimeData->setData(
        QStringLiteral("application/x-regmap-workbench-hierarchy-drag"),
        source.data(Qt::UserRole + 1).toString().toUtf8());
    view->scrollTo(target);
    QCoreApplication::processEvents();
    const QRect rectangle = view->visualRect(target);
    if (!rectangle.isValid()) {
        delete mimeData;
        return false;
    }
    const QPoint position = rectangle.center();
    QDragEnterEvent enter(position, Qt::MoveAction, mimeData,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &enter);
    QDragMoveEvent move(position, Qt::MoveAction, mimeData,
                        Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &move);
    QDropEvent drop(QPointF(position), Qt::MoveAction, mimeData,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drop);
    delete mimeData;
    QCoreApplication::processEvents();
    return drop.isAccepted();
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
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("rtl/new_device_registers.sv"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/register-map.xlsx"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/new_device_regs.h"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/register-map.md"))));
    QVERIFY(QFileInfo::exists(baselinePath));
    const auto baseline =
        regmap::loadSyncBaseline(std::filesystem::path(baselinePath.toStdWString()));
    QVERIFY(baseline.workspace.has_value());
    QCOMPARE(regmap::serializeWorkspaceState(*baseline.workspace, false),
             regmap::serializeWorkspaceState(*controller.workspace(), false));
    QVERIFY(!controller.hasProjectErrors());
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.isDirty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::establishesMissingBaselineForIdenticalSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createProject(manifestPath);

    const auto source =
        regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(source.workspace.has_value());
    QVERIFY(source.manifest.has_value());
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    source.manifest->rtl.moduleName,
                                    *source.workspace)
                .empty());
    QVERIFY(!QFileInfo::exists(baselinePath));

    ProjectController controller;
    controller.openProject(manifestPath);

    QVERIFY(!controller.requiresInitialSyncChoice());
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.hasProjectErrors());
    QVERIFY(!controller.isDirty());
    QVERIFY(QFileInfo::exists(baselinePath));
    const auto baseline =
        regmap::loadSyncBaseline(std::filesystem::path(baselinePath.toStdWString()));
    QVERIFY(baseline.workspace.has_value());
    QCOMPARE(regmap::serializeWorkspaceState(*baseline.workspace, false),
             regmap::serializeWorkspaceState(*controller.workspace(), false));
    const auto parsedRtl =
        regmap::parseManagedRtl(std::filesystem::path(rtlPath.toStdWString()));
    QVERIFY(parsedRtl.workspace.has_value());
    QCOMPARE(regmap::serializeWorkspaceState(*parsedRtl.workspace, false),
             regmap::serializeWorkspaceState(*controller.workspace(), false));
    QVERIFY(QFileInfo::exists(
        directory.filePath(QStringLiteral("generated/register-map.xlsx"))));
    QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/gui_regs.h"))));
    QVERIFY(QFileInfo::exists(
        directory.filePath(QStringLiteral("generated/register-map.md"))));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::blocksDivergentSourcesWithoutBaseline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createProject(manifestPath);

    auto source = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(source.workspace.has_value());
    QVERIFY(source.manifest.has_value());
    regmap::findRegister(*source.workspace, "reg-status")->offset = 8;
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    source.manifest->rtl.moduleName,
                                    *source.workspace)
                .empty());
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QByteArray originalManifest = manifestFile.readAll();
    manifestFile.close();
    QFile rtlFile(rtlPath);
    QVERIFY(rtlFile.open(QIODevice::ReadOnly));
    const QByteArray originalRtl = rtlFile.readAll();
    rtlFile.close();

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.openProjectPath(manifestPath);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* conflictBar = window.findChild<QWidget*>(QStringLiteral("conflictBar"));
    auto* conflictSummary = window.findChild<QLabel*>(QStringLiteral("conflictSummaryLabel"));
    auto* keepWorkbench =
        window.findChild<QPushButton*>(QStringLiteral("keepWorkbenchButton"));
    auto* useRtl = window.findChild<QPushButton*>(QStringLiteral("useRtlButton"));
    auto* diff = window.findChild<QTableView*>(QStringLiteral("diffView"));
    auto* state = window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    QVERIFY(controller != nullptr);
    QVERIFY(conflictBar != nullptr);
    QVERIFY(conflictSummary != nullptr);
    QVERIFY(keepWorkbench != nullptr);
    QVERIFY(useRtl != nullptr);
    QVERIFY(diff != nullptr);
    QVERIFY(state != nullptr);

    QVERIFY(controller->requiresInitialSyncChoice());
    QVERIFY(controller->hasConflicts());
    QVERIFY(controller->hasProjectErrors());
    QCOMPARE(controller->conflicts().size(), std::size_t{1});
    QCOMPARE(controller->conflicts().front().property, std::string("<initial-sync>"));
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->offset,
             std::uint64_t{0});
    QVERIFY(std::ranges::any_of(
        controller->diagnostics(), [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM5301";
        }));
    QVERIFY(conflictBar->isVisible());
    QVERIFY(keepWorkbench->isVisible());
    QVERIFY(useRtl->isVisible());
    QVERIFY(conflictSummary->text().contains(QStringLiteral("No synchronization baseline")));
    QVERIFY(conflictSummary->text().contains(QStringLiteral("No file has been overwritten")));
    QVERIFY(diff->model()->rowCount() >= 2);
    QVERIFY(state->text().startsWith(QStringLiteral("Conflict")));
    QVERIFY(!QFileInfo::exists(baselinePath));
    QVERIFY(!QFileInfo::exists(
        directory.filePath(QStringLiteral("generated/register-map.xlsx"))));

    controller->save();
    controller->synchronizeNow();
    QCoreApplication::processEvents();
    QVERIFY(controller->requiresInitialSyncChoice());
    QVERIFY(!QFileInfo::exists(baselinePath));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QCOMPARE(manifestFile.readAll(), originalManifest);
    manifestFile.close();
    QVERIFY(rtlFile.open(QIODevice::ReadOnly));
    QCOMPARE(rtlFile.readAll(), originalRtl);
    rtlFile.close();
}

void GuiSmokeTests::resolvesMissingBaselineWithExplicitChoice()
{
    const auto exerciseChoice = [](bool useRtlChoice) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifestPath =
            directory.filePath(QStringLiteral("project.regmap.yaml"));
        const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
        const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
        createProject(manifestPath);

        auto source =
            regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
        QVERIFY(source.workspace.has_value());
        QVERIFY(source.manifest.has_value());
        regmap::findRegister(*source.workspace, "reg-status")->offset = 8;
        QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                        source.manifest->rtl.moduleName,
                                        *source.workspace)
                    .empty());

        ProjectController controller;
        controller.openProject(manifestPath);
        QVERIFY(controller.requiresInitialSyncChoice());
        if (useRtlChoice) {
            controller.useRtlForConflicts();
        } else {
            controller.useWorkbenchForConflicts();
        }

        const std::uint64_t expected = useRtlChoice ? 8 : 0;
        QVERIFY(!controller.requiresInitialSyncChoice());
        QVERIFY(!controller.hasConflicts());
        QVERIFY(!controller.hasProjectErrors());
        QVERIFY(!controller.isDirty());
        QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset, expected);
        QVERIFY(QFileInfo::exists(baselinePath));

        const auto reopened =
            regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
        QVERIFY(reopened.workspace.has_value());
        QCOMPARE(regmap::findRegister(*reopened.workspace, "reg-status")->offset, expected);
        const auto parsedRtl =
            regmap::parseManagedRtl(std::filesystem::path(rtlPath.toStdWString()));
        QVERIFY(parsedRtl.workspace.has_value());
        QCOMPARE(regmap::findRegister(*parsedRtl.workspace, "reg-status")->offset, expected);
        const auto baseline =
            regmap::loadSyncBaseline(std::filesystem::path(baselinePath.toStdWString()));
        QVERIFY(baseline.workspace.has_value());
        QCOMPARE(regmap::findRegister(*baseline.workspace, "reg-status")->offset, expected);
        QVERIFY(QFileInfo::exists(
            directory.filePath(QStringLiteral("generated/register-map.xlsx"))));
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("generated/gui_regs.h"))));
        QVERIFY(QFileInfo::exists(
            directory.filePath(QStringLiteral("generated/register-map.md"))));

        if (useRtlChoice) {
            QVERIFY(controller.canUndo());
            controller.undo();
            QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset,
                     std::uint64_t{0});
            QVERIFY(controller.isDirty());
        }
        makeGeneratedFilesWritable(directory.path());
    };

    exerciseChoice(false);
    exerciseChoice(true);
}

void GuiSmokeTests::recoversAfterInvalidInitialRtlIsFixed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createProject(manifestPath);

    auto source = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(source.workspace.has_value());
    QVERIFY(source.manifest.has_value());
    regmap::findRegister(*source.workspace, "reg-status")->offset = 8;
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    source.manifest->rtl.moduleName,
                                    *source.workspace)
                .empty());
    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'hx"));

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(controller.hasProjectErrors());
    QVERIFY(!controller.requiresInitialSyncChoice());
    QVERIFY(!controller.hasConflicts());
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset,
             std::uint64_t{0});
    QVERIFY(!QFileInfo::exists(baselinePath));
    QVERIFY(std::ranges::any_of(
        controller.diagnostics(), [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM5003";
        }));

    editManagedRtlValue(rtlPath, QStringLiteral("reg-status"), QStringLiteral("offset"),
                        QStringLiteral("64'h8"));

    controller.synchronizeNow();
    QVERIFY(controller.requiresInitialSyncChoice());
    QVERIFY(controller.hasConflicts());
    QVERIFY(controller.hasProjectErrors());
    QCOMPARE(regmap::findRegister(*controller.workspace(), "reg-status")->offset,
             std::uint64_t{0});
    QVERIFY(!QFileInfo::exists(baselinePath));
    QVERIFY(std::ranges::any_of(
        controller.diagnostics(), [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM5301";
        }));
}

void GuiSmokeTests::detectsStructuralDifferenceWithoutBaseline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createTwoRegisterProject(manifestPath);

    auto source = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(source.workspace.has_value());
    QVERIFY(source.manifest.has_value());
    auto& registers = source.workspace->addressSpaces.front().blocks.front().registers;
    QCOMPARE(registers.size(), std::size_t{2});
    std::reverse(registers.begin(), registers.end());
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    source.manifest->rtl.moduleName,
                                    *source.workspace)
                .empty());

    const auto yaml =
        regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    const auto rtl = regmap::parseManagedRtl(std::filesystem::path(rtlPath.toStdWString()));
    QVERIFY(yaml.workspace.has_value());
    QVERIFY(rtl.workspace.has_value());
    const auto structuralChanges = regmap::diffWorkspaces(*yaml.workspace, *rtl.workspace);
    QCOMPARE(structuralChanges.size(), std::size_t{2});
    QVERIFY(std::ranges::all_of(structuralChanges, [](const regmap::ModelChange& change) {
        return change.objectKind == regmap::ObjectKind::reg &&
            change.summary == "Moved or reordered";
    }));
    QVERIFY(regmap::serializeWorkspaceState(*yaml.workspace, false) !=
            regmap::serializeWorkspaceState(*rtl.workspace, false));

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(controller.requiresInitialSyncChoice());
    QVERIFY(controller.hasConflicts());
    QVERIFY(!QFileInfo::exists(baselinePath));
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-status"));

    controller.useRtlForConflicts();
    QVERIFY(!controller.requiresInitialSyncChoice());
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.isDirty());
    QVERIFY(controller.canUndo());
    QVERIFY(QFileInfo::exists(baselinePath));
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-control"));
    controller.undo();
    QVERIFY(controller.isDirty());
    QCOMPARE(controller.changes().size(), std::size_t{2});
    QVERIFY(std::ranges::all_of(
        controller.changes(), [](const regmap::ModelChange& change) {
            return change.objectKind == regmap::ObjectKind::reg &&
                change.summary == "Moved or reordered";
        }));
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-status"));

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
    auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
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
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(problems != nullptr);
    QVERIFY(generated != nullptr);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("inspector")) == nullptr);
    QVERIFY(tabs != nullptr);
    QCOMPARE(hierarchy->model()->rowCount(), 1);
    QCOMPARE(registers->model()->rowCount(), 2);
    QCOMPARE(fields->model()->rowCount(), 0);
    QCOMPARE(enums->model()->rowCount(), 0);
    QVERIFY(fieldPanel->isHidden());
    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QCoreApplication::processEvents();
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
    QCOMPARE(registers->model()->index(0, 5).data().toString(), QStringLiteral("Editing (1)"));
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
    auto* fieldContext = window.findChild<QLabel*>(QStringLiteral("fieldContextLabel"));
    auto* closeFields = window.findChild<QPushButton*>(QStringLiteral("closeFieldsButton"));
    auto* hierarchyTitle = window.findChild<QLabel*>(QStringLiteral("hierarchyTitle"));
    auto* hierarchyAdd =
        window.findChild<QPushButton*>(QStringLiteral("hierarchyAddButton"));
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("projectToolBar"));
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(fieldContext != nullptr);
    QVERIFY(closeFields != nullptr);
    QVERIFY(hierarchyTitle != nullptr);
    QVERIFY(hierarchyAdd != nullptr);
    QVERIFY(toolbar != nullptr);
    QVERIFY(hierarchy->isHeaderHidden());
    QCOMPARE(hierarchyTitle->text(), QStringLiteral("Workspace"));
    QVERIFY(hierarchyAdd->isVisible());
    QVERIFY(hierarchyTitle->geometry().right() < hierarchyAdd->geometry().left());

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
    QCoreApplication::processEvents();

    const auto visibleLineEdit = [](QWidget* parent) -> QLineEdit* {
        const auto editors = parent->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };

    root = hierarchy->model()->index(0, 0);
    hierarchy->setCurrentIndex(root);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchyAdd->text(), QStringLiteral("+ Page"), 2000);
    QVERIFY(hierarchyAdd->toolTip().contains(QStringLiteral("Page")));
    Q_EMIT hierarchy->customContextMenuRequested(hierarchy->visualRect(root).center());
    QCoreApplication::processEvents();
    auto* rootMenu = hierarchy->findChild<QMenu*>(QStringLiteral("hierarchyContextMenu"));
    QVERIFY(rootMenu != nullptr);
    auto* newPage = rootMenu->findChild<QAction*>(QStringLiteral("newPageContextAction"));
    QVERIFY(newPage != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("renameContextAction")) != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("expandAllContextAction")) != nullptr);
    QVERIFY(rootMenu->findChild<QAction*>(QStringLiteral("collapseAllContextAction")) != nullptr);
    rootMenu->close();
    hierarchyAdd->click();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchy->model()->index(0, 0).model()
                                  ->rowCount(hierarchy->model()->index(0, 0)),
                              2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleLineEdit(hierarchy) != nullptr, 2000);
    auto* pageNameEditor = visibleLineEdit(hierarchy);
    QCOMPARE(pageNameEditor->text(), QStringLiteral("NEW_PAGE"));
    QTest::keyClick(pageNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();

    QModelIndex newPageIndex = hierarchy->currentIndex();
    QCOMPARE(newPageIndex.data().toString(), QStringLiteral("NEW_PAGE"));
    QTRY_COMPARE_WITH_TIMEOUT(hierarchyAdd->text(), QStringLiteral("+ Block"), 2000);
    QVERIFY(hierarchyAdd->toolTip().contains(QStringLiteral("Block")));
    Q_EMIT hierarchy->customContextMenuRequested(
        hierarchy->visualRect(newPageIndex).center());
    QCoreApplication::processEvents();
    auto* pageMenu = hierarchy->findChild<QMenu*>(QStringLiteral("hierarchyContextMenu"));
    QVERIFY(pageMenu != nullptr);
    auto* newBlock = pageMenu->findChild<QAction*>(QStringLiteral("newBlockContextAction"));
    QVERIFY(newBlock != nullptr);
    QVERIFY(pageMenu->findChild<QAction*>(QStringLiteral("renameContextAction")) != nullptr);
    QVERIFY(pageMenu->findChild<QAction*>(QStringLiteral("deleteContextAction")) != nullptr);
    pageMenu->close();
    hierarchyAdd->click();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchy->currentIndex().data().toString(),
                              QStringLiteral("NEW_BLOCK"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleLineEdit(hierarchy) != nullptr, 2000);
    auto* blockNameEditor = visibleLineEdit(hierarchy);
    QCOMPARE(blockNameEditor->text(), QStringLiteral("NEW_BLOCK"));
    QTest::keyClick(blockNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();

    QModelIndex newBlockIndex = hierarchy->currentIndex();
    QTRY_COMPARE_WITH_TIMEOUT(hierarchyAdd->text(), QStringLiteral("+ Register"), 2000);
    QVERIFY(hierarchyAdd->toolTip().contains(QStringLiteral("Register")));
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
    blockMenu->close();
    hierarchyAdd->click();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleLineEdit(registers) != nullptr, 2000);
    auto* registerNameEditor = visibleLineEdit(registers);
    QCOMPARE(registerNameEditor->text(), QStringLiteral("NEW_REGISTER"));
    QTest::keyClick(registerNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->index(0, 0).data().toString(),
             QStringLiteral("NEW_REGISTER"));
    hierarchyAdd->click();
    const QString firstRapidRegister =
        registers->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!firstRapidRegister.isEmpty());
    hierarchyAdd->click();
    const QString secondRapidRegister =
        registers->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!secondRapidRegister.isEmpty());
    QVERIFY(secondRapidRegister != firstRapidRegister);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 4, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleLineEdit(registers) != nullptr, 2000);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             secondRapidRegister);
    auto* latestRegisterEditor = visibleLineEdit(registers);
    QCOMPARE(latestRegisterEditor->text(), QStringLiteral("NEW_REGISTER"));
    QTest::keyClick(latestRegisterEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();

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
    registers->scrollTo(openFields);
    QCoreApplication::processEvents();
    const QRect openFieldsRectangle = registers->visualRect(openFields);
    QVERIFY(openFieldsRectangle.isValid());
    QVERIFY(registers->viewport()->rect().intersects(openFieldsRectangle));
    QTest::mouseMove(registers->viewport(), openFieldsRectangle.center());
    QCOMPARE(registers->viewport()->cursor().shape(), Qt::PointingHandCursor);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      openFieldsRectangle.center());
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QVERIFY(fields->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(fields->hasFocus(), 2000);
    QVERIFY(fieldContext->text().contains(QStringLiteral("STATUS")));
    QVERIFY(fieldContext->text().contains(QStringLiteral("1 field(s)")));
    QVERIFY(closeFields->isVisible());
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-status"));
    QCOMPARE(registers->model()->index(0, 5).data().toString(),
             QStringLiteral("Editing (1)"));
    QCOMPARE(fields->model()->index(0, 0).data().toString(), QStringLiteral("READY"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("STATUS")));
    closeFields->click();
    QTRY_VERIFY_WITH_TIMEOUT(!fieldPanel->isVisible(), 2000);
    QCOMPARE(fields->model()->rowCount(), 0);
    QCOMPARE(registers->model()->index(0, 5).data().toString(),
             QStringLiteral("Open (1)"));

    registers->setCurrentIndex(registers->model()->index(0, 5));
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fields->hasFocus(), 2000);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-status"));
    QCOMPARE(registers->model()->index(0, 5).data().toString(),
             QStringLiteral("Editing (1)"));
    closeFields->click();
    QTRY_VERIFY_WITH_TIMEOUT(!fieldPanel->isVisible(), 2000);

    registers->setCurrentIndex(registers->model()->index(0, 5));
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fields->hasFocus(), 2000);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-status"));
    QCOMPARE(registers->model()->index(0, 5).data().toString(),
             QStringLiteral("Editing (1)"));
    const QModelIndex otherRegister = registers->model()->index(1, 2);
    const QString otherRegisterId =
        registers->model()->index(1, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(!otherRegisterId.isEmpty());
    QVERIFY(otherRegisterId != QStringLiteral("reg-status"));
    registers->scrollTo(otherRegister);
    QCoreApplication::processEvents();
    const QRect otherRegisterRectangle = registers->visualRect(otherRegister);
    QVERIFY(otherRegisterRectangle.isValid());
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      otherRegisterRectangle.center());
    QTRY_VERIFY_WITH_TIMEOUT(!fieldPanel->isVisible(), 2000);
    QCOMPARE(registers->model()->index(0, 5).data().toString(),
             QStringLiteral("Open (1)"));
    QCOMPARE(registers->currentIndex().row(), 1);
    QCOMPARE(registers->model()
                 ->index(registers->currentIndex().row(), 0)
                 .data(Qt::UserRole + 1).toString(),
             otherRegisterId);
    QCOMPARE(fields->model()->rowCount(), 0);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::hierarchyContextActionsUseRightClickedTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);

    const auto visibleEditor = [](QWidget* parent) -> QLineEdit* {
        const auto editors = parent->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    const auto openHierarchyMenu = [&](const QString& targetId) -> QMenu* {
        const QModelIndex target =
            hierarchyIndexByObjectId(hierarchy->model(), targetId);
        if (!target.isValid()) {
            return nullptr;
        }
        if (QWidget* popup = QApplication::activePopupWidget()) {
            popup->close();
            QCoreApplication::processEvents();
        }
        hierarchy->expandAll();
        hierarchy->scrollTo(target);
        QCoreApplication::processEvents();
        const QRect rectangle = hierarchy->visualRect(target);
        if (!rectangle.isValid()) {
            return nullptr;
        }
        const QPoint localPosition = rectangle.center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, localPosition,
                                hierarchy->viewport()->mapToGlobal(localPosition));
        QCoreApplication::sendEvent(hierarchy->viewport(), &event);
        QCoreApplication::processEvents();
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        return menu != nullptr &&
                       menu->objectName() == QStringLiteral("hierarchyContextMenu")
                   ? menu
                   : nullptr;
    };
    const auto clickMenuAction = [](QMenu* menu, const QString& objectName) {
        if (menu == nullptr) {
            return false;
        }
        auto* action = menu->findChild<QAction*>(objectName);
        if (action == nullptr || !action->isEnabled()) {
            return false;
        }
        const QRect rectangle = menu->actionGeometry(action);
        if (!rectangle.isValid()) {
            return false;
        }
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, rectangle.center());
        return true;
    };
    const auto registerExists = [registers](const QString& objectId) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == objectId) {
                return true;
            }
        }
        return false;
    };

    const QString workspaceId = QStringLiteral("gui-workspace");
    const QString pageAId = QStringLiteral("space-main");
    const QString blockAId = QStringLiteral("block-control");
    QModelIndex workspaceIndex =
        hierarchyIndexByObjectId(hierarchy->model(), workspaceId);
    QModelIndex pageAIndex =
        hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    QModelIndex blockAIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockAId);
    QVERIFY(workspaceIndex.isValid());
    QVERIFY(pageAIndex.isValid());
    QVERIFY(blockAIndex.isValid());
    QCOMPARE(pageAIndex.parent(), workspaceIndex);
    QCOMPARE(blockAIndex.parent(), pageAIndex);

    hierarchy->setCurrentIndex(pageAIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    const int originalPageCount = hierarchy->model()->rowCount(workspaceIndex);
    QMenu* workspaceMenu = openHierarchyMenu(workspaceId);
    QVERIFY(workspaceMenu != nullptr);
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), workspaceId);
    QVERIFY(clickMenuAction(workspaceMenu, QStringLiteral("newPageContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchy->model()->rowCount(
            hierarchyIndexByObjectId(hierarchy->model(), workspaceId)),
        originalPageCount + 1, 2000);
    const QString pageBId =
        hierarchy->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!pageBId.isEmpty());
    QVERIFY(pageBId != pageAId);
    QCOMPARE(hierarchy->currentIndex().parent().data(Qt::UserRole + 1).toString(),
             workspaceId);
    QCOMPARE(visibleEditor(hierarchy)->text(), QStringLiteral("NEW_PAGE"));
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();

    pageAIndex = hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    QModelIndex pageBIndex =
        hierarchyIndexByObjectId(hierarchy->model(), pageBId);
    QVERIFY(pageAIndex.isValid());
    QVERIFY(pageBIndex.isValid());
    hierarchy->setCurrentIndex(pageAIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    QMenu* pageRenameMenu = openHierarchyMenu(pageBId);
    QVERIFY(pageRenameMenu != nullptr);
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageBId);
    QVERIFY(clickMenuAction(pageRenameMenu, QStringLiteral("renameContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    QCOMPARE(visibleEditor(hierarchy)->text(), QStringLiteral("NEW_PAGE"));
    visibleEditor(hierarchy)->selectAll();
    QTest::keyClicks(visibleEditor(hierarchy), QStringLiteral("TARGET_PAGE"));
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), pageBId).data().toString(),
        QStringLiteral("TARGET_PAGE"), 2000);
    QCOMPARE(hierarchyIndexByObjectId(hierarchy->model(), pageAId).data().toString(),
             QStringLiteral("Main"));
    QCOMPARE(hierarchyIndexByObjectId(hierarchy->model(), pageBId)
                 .parent()
                 .data(Qt::UserRole + 1)
                 .toString(),
             workspaceId);

    pageAIndex = hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    pageBIndex = hierarchyIndexByObjectId(hierarchy->model(), pageBId);
    QVERIFY(pageAIndex.isValid());
    QVERIFY(pageBIndex.isValid());
    const int pageABlockCount = hierarchy->model()->rowCount(pageAIndex);
    const int pageBBlockCount = hierarchy->model()->rowCount(pageBIndex);
    hierarchy->setCurrentIndex(pageAIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    QMenu* pageMenu = openHierarchyMenu(pageBId);
    QVERIFY(pageMenu != nullptr);
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageBId);
    const QModelIndex pageADriftIndex =
        hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    QVERIFY(pageADriftIndex.isValid());
    hierarchy->setCurrentIndex(pageADriftIndex);
    QCoreApplication::processEvents();
    QVERIFY(pageMenu->isVisible());
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    QVERIFY(clickMenuAction(pageMenu, QStringLiteral("newBlockContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    const QString blockBId =
        hierarchy->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!blockBId.isEmpty());
    QVERIFY(blockBId != blockAId);
    QCOMPARE(hierarchy->currentIndex().parent().data(Qt::UserRole + 1).toString(),
             pageBId);
    QCOMPARE(visibleEditor(hierarchy)->text(), QStringLiteral("NEW_BLOCK"));
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchy->model()->rowCount(
            hierarchyIndexByObjectId(hierarchy->model(), pageBId)),
        pageBBlockCount + 1, 2000);
    QCOMPARE(hierarchy->model()->rowCount(
                 hierarchyIndexByObjectId(hierarchy->model(), pageAId)),
             pageABlockCount);
    QCOMPARE(hierarchyIndexByObjectId(hierarchy->model(), blockBId)
                 .parent()
                 .data(Qt::UserRole + 1)
                 .toString(),
             pageBId);

    blockAIndex = hierarchyIndexByObjectId(hierarchy->model(), blockAId);
    QModelIndex blockBIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockBId);
    QVERIFY(blockAIndex.isValid());
    QVERIFY(blockBIndex.isValid());
    hierarchy->setCurrentIndex(blockBIndex);
    QCoreApplication::processEvents();
    const int blockBRegisterRows = registers->model()->rowCount();
    QVERIFY(!registerExists(QStringLiteral("reg-status")));
    hierarchy->setCurrentIndex(blockAIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), blockAId);
    QVERIFY(registerExists(QStringLiteral("reg-status")));
    const int blockARegisterRows = registers->model()->rowCount();
    QMenu* blockMenu = openHierarchyMenu(blockBId);
    QVERIFY(blockMenu != nullptr);
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), blockBId);
    const QModelIndex blockADriftIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockAId);
    QVERIFY(blockADriftIndex.isValid());
    hierarchy->setCurrentIndex(blockADriftIndex);
    QCoreApplication::processEvents();
    QVERIFY(blockMenu->isVisible());
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), blockAId);
    QVERIFY(clickMenuAction(blockMenu, QStringLiteral("newRegisterContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(registers) != nullptr, 2000);
    const QString registerBId =
        registers->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!registerBId.isEmpty());
    QVERIFY(registerBId != QStringLiteral("reg-status"));
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), blockBId);
    QCOMPARE(visibleEditor(registers)->text(), QStringLiteral("NEW_REGISTER"));
    QTest::keyClick(visibleEditor(registers), Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(),
                              blockBRegisterRows + 1, 2000);
    QVERIFY(registerExists(registerBId));
    QVERIFY(!registerExists(QStringLiteral("reg-status")));

    blockAIndex = hierarchyIndexByObjectId(hierarchy->model(), blockAId);
    hierarchy->setCurrentIndex(blockAIndex);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(),
                              blockARegisterRows, 2000);
    QVERIFY(registerExists(QStringLiteral("reg-status")));
    QVERIFY(!registerExists(registerBId));
    blockBIndex = hierarchyIndexByObjectId(hierarchy->model(), blockBId);
    hierarchy->setCurrentIndex(blockBIndex);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(registerExists(registerBId), 2000);
    QVERIFY(!registerExists(QStringLiteral("reg-status")));

    pageAIndex = hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    hierarchy->setCurrentIndex(pageAIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    QMenu* deleteMenu = openHierarchyMenu(pageBId);
    QVERIFY(deleteMenu != nullptr);
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageBId);
    const QModelIndex deleteDriftIndex =
        hierarchyIndexByObjectId(hierarchy->model(), pageAId);
    QVERIFY(deleteDriftIndex.isValid());
    hierarchy->setCurrentIndex(deleteDriftIndex);
    QCoreApplication::processEvents();
    QVERIFY(deleteMenu->isVisible());
    QCOMPARE(hierarchy->currentIndex().data(Qt::UserRole + 1).toString(), pageAId);
    bool confirmationSeen = false;
    bool confirmationDescribesTarget = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        confirmationSeen = true;
        const QString text = dialog->text();
        confirmationDescribesTarget =
            text.contains(QStringLiteral("TARGET_PAGE")) &&
            text.contains(QStringLiteral("1 block(s)")) &&
            text.contains(QStringLiteral("1 register(s)"));
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(clickMenuAction(deleteMenu, QStringLiteral("deleteContextAction")));
    QVERIFY(confirmationSeen);
    QVERIFY(confirmationDescribesTarget);
    QTRY_VERIFY_WITH_TIMEOUT(
        !hierarchyIndexByObjectId(hierarchy->model(), pageBId).isValid(), 2000);
    QVERIFY(hierarchyIndexByObjectId(hierarchy->model(), pageAId).isValid());
    QVERIFY(!hierarchyIndexByObjectId(hierarchy->model(), blockBId).isValid());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("TARGET_PAGE")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    const auto actions = window.findChildren<QAction*>();
    const auto undo = std::ranges::find_if(actions, [](const QAction* action) {
        return action->shortcut().matches(QKeySequence::Undo) == QKeySequence::ExactMatch;
    });
    QVERIFY(undo != actions.end());
    QVERIFY((*undo)->isEnabled());
    QVERIFY((*undo)->text().contains(QStringLiteral("TARGET_PAGE")));
    (*undo)->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), pageBId).isValid(), 2000);
    pageBIndex = hierarchyIndexByObjectId(hierarchy->model(), pageBId);
    blockBIndex = hierarchyIndexByObjectId(hierarchy->model(), blockBId);
    QVERIFY(pageBIndex.isValid());
    QVERIFY(blockBIndex.isValid());
    QCOMPARE(pageBIndex.data().toString(), QStringLiteral("TARGET_PAGE"));
    QCOMPARE(pageBIndex.parent().data(Qt::UserRole + 1).toString(), workspaceId);
    QCOMPARE(blockBIndex.data().toString(), QStringLiteral("NEW_BLOCK"));
    QCOMPARE(blockBIndex.parent().data(Qt::UserRole + 1).toString(), pageBId);
    QVERIFY(hierarchyIndexByObjectId(hierarchy->model(), pageAId).isValid());
    QVERIFY(hierarchyIndexByObjectId(hierarchy->model(), blockAId).isValid());
    hierarchy->setCurrentIndex(blockBIndex);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(registerExists(registerBId), 2000);
    int registerBRow = -1;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        if (registers->model()
                ->index(row, 0)
                .data(Qt::UserRole + 1)
                .toString() == registerBId) {
            registerBRow = row;
            break;
        }
    }
    QVERIFY(registerBRow >= 0);
    QCOMPARE(registers->model()->index(registerBRow, 0).data().toString(),
             QStringLiteral("NEW_REGISTER"));
    QVERIFY(!registerExists(QStringLiteral("reg-status")));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::switchesProjectsWithoutReusingFieldWorkspaceState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("first")));
    QVERIFY(root.mkpath(QStringLiteral("second")));
    const QString firstManifest =
        root.filePath(QStringLiteral("first/project.regmap.yaml"));
    const QString secondManifest =
        root.filePath(QStringLiteral("second/project.regmap.yaml"));
    createProject(firstManifest);
    createProject(secondManifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(firstManifest);
    QTest::qWait(50);

    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QCOMPARE(fields->model()->index(0, 0).data(Qt::UserRole + 1).toString(),
             QStringLiteral("field-ready"));

    window.openProjectPath(secondManifest);
    QTRY_VERIFY_WITH_TIMEOUT(!fieldPanel->isVisible(), 2000);
    QCOMPARE(fields->model()->rowCount(), 0);
    QCOMPARE(registers->model()->index(0, 0).data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-status"));

    makeGeneratedFilesWritable(root.filePath(QStringLiteral("first")));
    makeGeneratedFilesWritable(root.filePath(QStringLiteral("second")));
}

void GuiSmokeTests::reloadsProjectWithoutLosingFieldWorkspaceContext()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
    QVERIFY(controller != nullptr);
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QCOMPARE(fields->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("field-ready"));

    QFile projectFile(manifest);
    QVERIFY(projectFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QString projectText = QString::fromUtf8(projectFile.readAll());
    projectFile.close();
    const QString originalPageName = QStringLiteral("      name: Main\n");
    QCOMPARE(projectText.count(originalPageName), 1);
    projectText.replace(originalPageName, QStringLiteral("      name: Main Reloaded\n"));
    const QByteArray updatedProject = projectText.toUtf8();
    QVERIFY(projectFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(projectFile.write(updatedProject), updatedProject.size());
    projectFile.close();

    QSignalSpy projectChanged(controller, &ProjectController::projectChanged);
    QVERIFY(projectChanged.isValid());
    QAction* reload = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Reload from Disk")) {
            reload = action;
            break;
        }
    }
    QVERIFY(reload != nullptr);
    QVERIFY(reload->isEnabled());
    reload->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(projectChanged.count() > 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    const QModelIndex workspaceIndex = hierarchy->model()->index(0, 0);
    const QModelIndex pageIndex = hierarchy->model()->index(0, 0, workspaceIndex);
    QTRY_COMPARE_WITH_TIMEOUT(pageIndex.data().toString(),
                              QStringLiteral("Main Reloaded"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-status"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("field-ready"), 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::navigatesFieldProblemsAndFallsBackForHiddenFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("visible")));
    QVERIFY(root.mkpath(QStringLiteral("hidden")));
    const QString visibleManifest =
        root.filePath(QStringLiteral("visible/project.regmap.yaml"));
    const QString hiddenManifest =
        root.filePath(QStringLiteral("hidden/project.regmap.yaml"));
    createFieldDiagnosticProject(visibleManifest, false);
    createFieldDiagnosticProject(hiddenManifest, true);

    const auto fieldProblemRow = [](const QTableView* problems) {
        for (int row = 0; row < problems->model()->rowCount(); ++row) {
            if (problems->model()->index(row, 1).data().toString() ==
                    QStringLiteral("RM3036") &&
                problems->model()->index(row, 3).data().toString() ==
                QStringLiteral("field-ready")) {
                return row;
            }
        }
        return -1;
    };

    {
        MainWindow window;
        window.resize(1100, 720);
        window.show();
        window.openProjectPath(visibleManifest);
        QTest::qWait(50);

        auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
        auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
        auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
        auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
        QVERIFY(registers != nullptr);
        QVERIFY(fields != nullptr);
        QVERIFY(fieldPanel != nullptr);
        QVERIFY(problems != nullptr);
        QVERIFY(!fieldPanel->isVisible());
        registers->setCurrentIndex(registers->model()->index(1, 0));
        QCoreApplication::processEvents();
        QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
                 QStringLiteral("reg-control"));

        const int row = fieldProblemRow(problems);
        QVERIFY(row >= 0);
        Q_EMIT problems->doubleClicked(problems->model()->index(row, 2));
        QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(
            registers->currentIndex().data(Qt::UserRole + 1).toString(),
            QStringLiteral("reg-status"), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(
            fields->currentIndex().data(Qt::UserRole + 1).toString(),
            QStringLiteral("field-ready"), 2000);
    }

    {
        MainWindow window;
        window.resize(1100, 720);
        window.show();
        window.openProjectPath(hiddenManifest);
        QTest::qWait(50);

        auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
        auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
        auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
        auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
        QVERIFY(registers != nullptr);
        QVERIFY(fields != nullptr);
        QVERIFY(fieldPanel != nullptr);
        QVERIFY(problems != nullptr);

        registers->setCurrentIndex(registers->model()->index(1, 0));
        QCoreApplication::processEvents();
        QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
                 QStringLiteral("reg-control"));
        const int row = fieldProblemRow(problems);
        QVERIFY(row >= 0);
        Q_EMIT problems->doubleClicked(problems->model()->index(row, 2));
        QTRY_COMPARE_WITH_TIMEOUT(
            registers->currentIndex().data(Qt::UserRole + 1).toString(),
            QStringLiteral("reg-status"), 2000);
        QCOMPARE(registers->currentIndex().column(), 4);
        QVERIFY(!fieldPanel->isVisible());
        QCOMPARE(fields->model()->rowCount(), 0);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("set Type to field")));
    }

    makeGeneratedFilesWritable(root.filePath(QStringLiteral("visible")));
    makeGeneratedFilesWritable(root.filePath(QStringLiteral("hidden")));
}

void GuiSmokeTests::navigatesEnumProblemsToExactRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(problems != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Seed enum navigation diagnostics"),
        [](regmap::Workspace& workspace) {
            const auto enumValue = [](std::string id, std::string name,
                                      std::uint64_t value) {
                regmap::EnumValue result;
                result.id = std::move(id);
                result.name = std::move(name);
                result.value = regmap::UnsignedValue(value);
                return result;
            };

            auto* registerEnum = regmap::findRegister(workspace, "reg-control");
            auto* fieldRegister = regmap::findRegister(workspace, "reg-status");
            if (registerEnum == nullptr || fieldRegister == nullptr ||
                fieldRegister->fields.empty()) {
                return;
            }
            registerEnum->width = 2;
            registerEnum->type = regmap::FieldType::enumeration;
            registerEnum->enumValues = {
                enumValue("enum-register-off", "OFF", 0),
                enumValue("enum-register-on", "ON", 0),
            };

            auto& fieldEnum = fieldRegister->fields.front();
            fieldEnum.type = regmap::FieldType::enumeration;
            fieldEnum.enumValues = {
                enumValue("enum-field-low", "LOW", 0),
                enumValue("enum-field-high", "HIGH", 0),
            };
        }));

    const auto problemRow = [problems](const QString& objectId) {
        for (int row = 0; row < problems->model()->rowCount(); ++row) {
            if (problems->model()->index(row, 1).data().toString() ==
                    QStringLiteral("RM3041") &&
                problems->model()->index(row, 3).data().toString() == objectId) {
                return row;
            }
        }
        return -1;
    };

    QTRY_VERIFY_WITH_TIMEOUT(
        problemRow(QStringLiteral("enum-register-on")) >= 0, 2000);
    const int registerProblem = problemRow(QStringLiteral("enum-register-on"));
    Q_EMIT problems->doubleClicked(problems->model()->index(registerProblem, 2));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("enum-register-on"), 2000);

    QTRY_VERIFY_WITH_TIMEOUT(problemRow(QStringLiteral("enum-field-high")) >= 0, 2000);
    const int fieldProblem = problemRow(QStringLiteral("enum-field-high"));
    Q_EMIT problems->doubleClicked(problems->model()->index(fieldProblem, 2));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-status"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("field-ready"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("enum-field-high"), 2000);

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
    const auto findUndoAction = [&window]() -> QAction* {
        const auto actions = window.findChildren<QAction*>();
        const auto undo = std::ranges::find_if(actions, [](const QAction* action) {
            return action->shortcut().matches(QKeySequence::Undo) == QKeySequence::ExactMatch;
        });
        return undo == actions.end() ? nullptr : *undo;
    };

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
    window.statusBar()->clearMessage();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4), QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(registers->model()->index(1, 6).data().toString().isEmpty(), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("incompatible data")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    auto* registerUndo = findUndoAction();
    QVERIFY(registerUndo != nullptr);
    QVERIFY(registerUndo->isEnabled());
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    registerUndo->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("uint16"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 6).data().toString(),
                              QStringLiteral("0 .. 255"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 1, 2000);
    const auto visibleEnumEditor = [enums]() -> QLineEdit* {
        const auto editors = enums->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    Q_EMIT enums->clicked(enums->model()->index(0, 0));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleEnumEditor() != nullptr, 2000);
    auto* enumNameEditor = visibleEnumEditor();
    QCOMPARE(enumNameEditor->text(), QStringLiteral("NEW_VALUE"));
    const QString enumId =
        enums->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!enumId.isEmpty());
    QTest::keyClick(enumNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(enums->model()->index(0, 0).data().toString(), QStringLiteral("NEW_VALUE"));
    QCOMPARE(enums->model()->index(0, 1).data().toString(), QStringLiteral("0x0"));
    QCOMPARE(enums->model()->index(0, 0).data(Qt::UserRole + 1).toString(), enumId);

    const QModelIndex enumValueIndex = enums->model()->index(0, 1);
    enums->scrollTo(enumValueIndex);
    QTest::mouseClick(enums->viewport(), Qt::LeftButton, Qt::NoModifier,
                      enums->visualRect(enumValueIndex).center());
    QVERIFY(visibleEnumEditor() == nullptr);
    QTest::mouseDClick(enums->viewport(), Qt::LeftButton, Qt::NoModifier,
                      enums->visualRect(enumValueIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(visibleEnumEditor() != nullptr, 2000);
    auto* enumValueEditor = visibleEnumEditor();
    enumValueEditor->selectAll();
    QTest::keyClicks(enumValueEditor, QStringLiteral("0x3"));
    QTest::keyClick(enumValueEditor, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->index(0, 1).data().toString(),
                              QStringLiteral("0x3"), 2000);

    const int nextEnumRow = enums->model()->rowCount() - 1;
    Q_EMIT enums->clicked(enums->model()->index(nextEnumRow, 0));
    QString guardedEnumId;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString candidate =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        if (!candidate.isEmpty() && candidate != enumId) {
            guardedEnumId = candidate;
        }
    }
    QVERIFY(!guardedEnumId.isEmpty());
    QCOMPARE(registers->currentIndex().row(), 1);
    registers->setCurrentIndex(registers->model()->index(0, 0));
    QCoreApplication::processEvents();
    QVERIFY(visibleEnumEditor() == nullptr);
    registers->setCurrentIndex(registers->model()->index(1, 0));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    bool foundGuardedEnum = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        foundGuardedEnum |=
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            guardedEnumId;
    }
    QVERIFY(foundGuardedEnum);
    QVERIFY(visibleEnumEditor() == nullptr);
    QCOMPARE(registers->model()->index(1, 4).data().toString(), QStringLiteral("enum"));

    window.statusBar()->clearMessage();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("field"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    QCOMPARE(enums->model()->rowCount(), 0);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("incompatible data")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    registerUndo = findUndoAction();
    QVERIFY(registerUndo != nullptr);
    QVERIFY(registerUndo->isEnabled());
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    registerUndo->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("enum"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    bool restoredRegisterEnums = false;
    bool restoredRegisterEnumValue = false;
    bool restoredGuardedRegisterEnum = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString id =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        restoredRegisterEnums |= id == enumId;
        restoredRegisterEnumValue |=
            id == enumId &&
            enums->model()->index(row, 1).data().toString() == QStringLiteral("0x3");
        restoredGuardedRegisterEnum |= id == guardedEnumId;
    }
    QVERIFY(restoredRegisterEnums);
    QVERIFY(restoredRegisterEnumValue);
    QVERIFY(restoredGuardedRegisterEnum);
    QVERIFY(registers->model()->setData(registers->model()->index(1, 7),
                                        QStringLiteral("0x3")));
    QVERIFY(registers->model()->setData(registers->model()->index(1, 8),
                                        QStringLiteral("0x3")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 8).data().toString(),
                              QStringLiteral("0x3"), 2000);

    window.statusBar()->clearMessage();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("reserved")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("reserved"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("NONE"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 7).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("incompatible data")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    registerUndo = findUndoAction();
    QVERIFY(registerUndo != nullptr);
    QVERIFY(registerUndo->isEnabled());
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    registerUndo->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("enum"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("RW"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 7).data().toString(),
                              QStringLiteral("0x3"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 8).data().toString(),
                              QStringLiteral("0x3"), 2000);
    bool restoredAfterReserved = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        restoredAfterReserved |=
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() == enumId;
    }
    QVERIFY(restoredAfterReserved);

    const QString enumOwnerRegisterId =
        registers->model()->index(1, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(!enumOwnerRegisterId.isEmpty());
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             enumOwnerRegisterId);

    int retainedEnumRow = -1;
    int enumDeleteRow = -1;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString id =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        if (id == enumId) {
            retainedEnumRow = row;
        } else if (id == guardedEnumId) {
            enumDeleteRow = row;
        }
    }
    QVERIFY(retainedEnumRow >= 0);
    QVERIFY(enumDeleteRow >= 0);
    const QString deletedEnumName =
        enums->model()->index(enumDeleteRow, 0).data().toString();
    const QString deletedEnumValue =
        enums->model()->index(enumDeleteRow, 1).data().toString();
    QVERIFY(!deletedEnumName.isEmpty());
    QVERIFY(!deletedEnumValue.isEmpty());
    enums->setCurrentIndex(enums->model()->index(retainedEnumRow, 0));
    QCOMPARE(enums->currentIndex().data(Qt::UserRole + 1).toString(), enumId);
    const QModelIndex enumDeleteIndex = enums->model()->index(enumDeleteRow, 0);
    enums->scrollTo(enumDeleteIndex);
    QCoreApplication::processEvents();
    bool deleteEnumTriggered = false;
    QString deleteEnumFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* action =
            window.findChild<QAction*>(QStringLiteral("deleteEnumValueAction"));
        auto* menu =
            action == nullptr ? qobject_cast<QMenu*>(QApplication::activePopupWidget())
                              : qobject_cast<QMenu*>(action->parent());
        if (menu == nullptr) {
            deleteEnumFailure = QStringLiteral("Enum context menu did not open");
            return;
        }
        if (menu->objectName() != QStringLiteral("enumContextMenu")) {
            deleteEnumFailure = QStringLiteral("Unexpected enum context menu");
            menu->close();
            return;
        }
        if (action == nullptr || !action->isEnabled()) {
            deleteEnumFailure = QStringLiteral("Delete Enum Value action is unavailable");
            menu->close();
            return;
        }
        deleteEnumTriggered = true;
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    const QPoint enumDeletePosition = enums->visualRect(enumDeleteIndex).center();
    window.statusBar()->clearMessage();
    QContextMenuEvent enumDeleteEvent(
        QContextMenuEvent::Mouse, enumDeletePosition,
        enums->viewport()->mapToGlobal(enumDeletePosition));
    QCoreApplication::sendEvent(enums->viewport(), &enumDeleteEvent);
    QVERIFY2(deleteEnumFailure.isEmpty(), qPrintable(deleteEnumFailure));
    QVERIFY(deleteEnumTriggered);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QCOMPARE(registers->model()->index(1, 4).data().toString(), QStringLiteral("enum"));
    QVERIFY(enums->isVisible());
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             enumOwnerRegisterId);
    bool deletedEnumStillPresent = false;
    bool retainedEnumStillPresent = false;
    bool retainedEnumValuePreserved = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString id =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        deletedEnumStillPresent |= id == guardedEnumId;
        retainedEnumStillPresent |= id == enumId;
        retainedEnumValuePreserved |=
            id == enumId &&
            enums->model()->index(row, 1).data().toString() == QStringLiteral("0x3");
    }
    QVERIFY(!deletedEnumStillPresent);
    QVERIFY(retainedEnumStillPresent);
    QVERIFY(retainedEnumValuePreserved);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Deleted enum value")));
    QVERIFY(window.statusBar()->currentMessage().contains(deletedEnumName));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    registerUndo = findUndoAction();
    QVERIFY(registerUndo != nullptr);
    QVERIFY(registerUndo->isEnabled());
    enums->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    registerUndo->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 3, 2000);
    bool restoredDeletedEnum = false;
    bool restoredDeletedEnumName = false;
    bool restoredDeletedEnumValue = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const bool matches =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() == guardedEnumId;
        restoredDeletedEnum |= matches;
        restoredDeletedEnumName |=
            matches && enums->model()->index(row, 0).data().toString() == deletedEnumName;
        restoredDeletedEnumValue |=
            matches && enums->model()->index(row, 1).data().toString() == deletedEnumValue;
    }
    QVERIFY(restoredDeletedEnum);
    QVERIFY(restoredDeletedEnumName);
    QVERIFY(restoredDeletedEnumValue);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             enumOwnerRegisterId);

    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("field"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!fields->isVisible(), 2000);
    QCOMPARE(fields->model()->rowCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 5).data().toString(),
                              QStringLiteral("Open (0)"), 2000);
    Q_EMIT registers->clicked(registers->model()->index(1, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 1, 2000);

    Q_EMIT fields->clicked(fields->model()->index(0, 0));
    QCOMPARE(fields->model()->rowCount(), 2);
    const auto visibleFieldEditor = [fields]() -> QLineEdit* {
        const auto editors = fields->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleFieldEditor() != nullptr, 2000);
    auto* fieldNameEditor = visibleFieldEditor();
    QCOMPARE(fieldNameEditor->text(), QStringLiteral("NEW_FIELD"));
    QTest::keyClick(fieldNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(fields->model()->index(0, 0).data().toString(), QStringLiteral("NEW_FIELD"));

    const QModelIndex widthIndex = fields->model()->index(0, 4);
    fields->scrollTo(widthIndex);
    QTest::mouseClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(widthIndex).center());
    QVERIFY(visibleFieldEditor() == nullptr);
    QTest::mouseDClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(widthIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(visibleFieldEditor() != nullptr, 2000);
    auto* widthEditor = visibleFieldEditor();
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

    QVERIFY(fields->model()->setData(fields->model()->index(0, 5),
                                     QStringLiteral("field")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 5).data().toString(),
                              QStringLiteral("field"), 2000);
    const QModelIndex parentNameIndex = fields->model()->index(0, 0);
    const QString parentFieldId = parentNameIndex.data(Qt::UserRole + 1).toString();
    QVERIFY(!parentFieldId.isEmpty());
    fields->setCurrentIndex(parentNameIndex);
    QCoreApplication::processEvents();
    const auto addMemberThroughContextMenu =
        [&](const QString& targetFieldId, QString& failure) {
            bool triggered = false;
            QCoreApplication::processEvents();
            QModelIndex fieldIndex;
            for (int row = 0; row < fields->model()->rowCount(); ++row) {
                const QModelIndex candidate = fields->model()->index(row, 0);
                if (candidate.data(Qt::UserRole + 1).toString() == targetFieldId) {
                    fieldIndex = candidate;
                    break;
                }
            }
            if (!fieldIndex.isValid()) {
                failure = QStringLiteral("Field target no longer exists");
                return false;
            }
            fields->scrollTo(fieldIndex);
            const QRect fieldRectangle = fields->visualRect(fieldIndex);
            if (!fieldRectangle.isValid()) {
                failure = QStringLiteral("Field target is not visible");
                return false;
            }
            QTimer::singleShot(0, &window, [&] {
                auto* action =
                    window.findChild<QAction*>(QStringLiteral("addMemberFieldAction"));
                auto* menu =
                    action == nullptr ? qobject_cast<QMenu*>(QApplication::activePopupWidget())
                                      : qobject_cast<QMenu*>(action->parent());
                if (menu == nullptr) {
                    failure = QStringLiteral("Field context menu did not open");
                    return;
                }
                if (action == nullptr || !action->isEnabled()) {
                    failure = QStringLiteral("Add member Field action is unavailable");
                    menu->close();
                    return;
                }
                triggered = true;
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                  menu->actionGeometry(action).center());
            });
            const QPoint localPosition = fieldRectangle.center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, localPosition,
                                    fields->viewport()->mapToGlobal(localPosition));
            QCoreApplication::sendEvent(fields->viewport(), &event);
            return triggered;
        };

    QString memberMenuFailure;
    const bool memberActionTriggered =
        addMemberThroughContextMenu(parentFieldId, memberMenuFailure);
    QVERIFY2(memberMenuFailure.isEmpty(), qPrintable(memberMenuFailure));
    QVERIFY(memberActionTriggered);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 3, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(visibleFieldEditor() != nullptr, 2000);
    auto* memberNameEditor = visibleFieldEditor();
    QCOMPARE(memberNameEditor->text(), QStringLiteral("NEW_MEMBER"));
    const QString memberFieldId =
        fields->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!memberFieldId.isEmpty());
    QVERIFY(memberFieldId != parentFieldId);
    QTest::keyClick(memberNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(fields->model()->index(1, 0).data().toString(),
             QStringLiteral("NEW_MEMBER"));
    QCOMPARE(fields->currentIndex().data(Qt::UserRole + 1).toString(), memberFieldId);

    const QModelIndex memberWidthIndex = fields->model()->index(1, 4);
    fields->scrollTo(memberWidthIndex);
    QTest::mouseClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(memberWidthIndex).center());
    QVERIFY(visibleFieldEditor() == nullptr);
    QTest::mouseDClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(memberWidthIndex).center());
    QTRY_VERIFY_WITH_TIMEOUT(visibleFieldEditor() != nullptr, 2000);
    auto* memberWidthEditor = visibleFieldEditor();
    memberWidthEditor->selectAll();
    QTest::keyClicks(memberWidthEditor, QStringLiteral("2"));
    QTest::keyClick(memberWidthEditor, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(1, 4).data().toString(),
                              QStringLiteral("2"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(1, 2).data().toString(),
                              QStringLiteral("1"), 2000);
    QCOMPARE(fields->model()->index(1, 3).data().toString(), QStringLiteral("0"));

    const QModelIndex refreshedParentNameIndex = fields->model()->index(0, 0);
    fields->setCurrentIndex(refreshedParentNameIndex);
    QString guardedMemberMenuFailure;
    const bool guardedMemberActionTriggered =
        addMemberThroughContextMenu(parentFieldId, guardedMemberMenuFailure);
    QVERIFY2(guardedMemberMenuFailure.isEmpty(), qPrintable(guardedMemberMenuFailure));
    QVERIFY(guardedMemberActionTriggered);
    const QString guardedFieldId =
        fields->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!guardedFieldId.isEmpty());
    QVERIFY(guardedFieldId != memberFieldId);
    registers->setCurrentIndex(registers->model()->index(0, 0));
    QCoreApplication::processEvents();
    QVERIFY(visibleFieldEditor() == nullptr);
    Q_EMIT registers->clicked(registers->model()->index(1, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    bool foundGuardedField = false;
    for (int row = 0; row < fields->model()->rowCount(); ++row) {
        foundGuardedField |=
            fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            guardedFieldId;
    }
    QVERIFY(foundGuardedField);

    const auto fieldRowForId = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    fields->setCurrentIndex(fields->model()->index(memberRow, 0));
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 1, 2000);

    Q_EMIT enums->clicked(enums->model()->index(0, 0));
    QString guardedFieldEnumId;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString candidate =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        if (!candidate.isEmpty()) {
            guardedFieldEnumId = candidate;
        }
    }
    QVERIFY(!guardedFieldEnumId.isEmpty());
    const int parentRow = fieldRowForId(parentFieldId);
    QVERIFY(parentRow >= 0);
    fields->setCurrentIndex(fields->model()->index(parentRow, 0));
    QCoreApplication::processEvents();
    QVERIFY(visibleEnumEditor() == nullptr);

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    fields->setCurrentIndex(fields->model()->index(memberRow, 0));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    bool foundGuardedFieldEnum = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        foundGuardedFieldEnum |=
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            guardedFieldEnumId;
    }
    QVERIFY(foundGuardedFieldEnum);
    QVERIFY(visibleEnumEditor() == nullptr);

    const int fieldEnumAddRow = enums->model()->rowCount() - 1;
    Q_EMIT enums->clicked(enums->model()->index(fieldEnumAddRow, 0));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEnumEditor() != nullptr, 2000);
    auto* fieldEnumNameEditor = visibleEnumEditor();
    QCOMPARE(fieldEnumNameEditor->text(), QStringLiteral("NEW_VALUE"));
    const QString fieldEnumId =
        enums->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!fieldEnumId.isEmpty());
    QVERIFY(fieldEnumId != guardedFieldEnumId);
    QTest::keyClick(fieldEnumNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    bool foundFieldEnum = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        foundFieldEnum |=
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() == fieldEnumId;
    }
    QVERIFY(foundFieldEnum);

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("bits")));
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    const auto actions = window.findChildren<QAction*>();
    const auto undo = std::ranges::find_if(actions, [](const QAction* action) {
        return action->shortcut().matches(QKeySequence::Undo) == QKeySequence::ExactMatch;
    });
    QVERIFY(undo != actions.end());
    QVERIFY((*undo)->isEnabled());
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    (*undo)->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(fieldRowForId(memberFieldId) >= 0, 2000);
    memberRow = fieldRowForId(memberFieldId);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 5).data().toString(),
                              QStringLiteral("enum"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    bool restoredGuardedFieldEnum = false;
    bool restoredNewFieldEnum = false;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString id =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        restoredGuardedFieldEnum |= id == guardedFieldEnumId;
        restoredNewFieldEnum |= id == fieldEnumId;
    }
    QVERIFY(restoredGuardedFieldEnum);
    QVERIFY(restoredNewFieldEnum);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("bits")));
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 1, 2000);
    QCOMPARE(enums->model()->index(0, 0).data().toString(), QStringLiteral("+"));

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("uint2")));
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 6),
                                     QStringLiteral("0")));
    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 7),
                                     QStringLiteral("3")));
    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("bits")));
    QTRY_VERIFY_WITH_TIMEOUT(fieldRowForId(memberFieldId) >= 0, 2000);
    memberRow = fieldRowForId(memberFieldId);
    QTRY_VERIFY_WITH_TIMEOUT(
        fields->model()->index(memberRow, 6).data().toString().isEmpty(), 2000);
    QVERIFY(fields->model()->index(memberRow, 7).data().toString().isEmpty());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    const int parentRowBeforeRejectedType = fieldRowForId(parentFieldId);
    QVERIFY(parentRowBeforeRejectedType >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(parentRowBeforeRejectedType, 5),
                                     QStringLiteral("bits")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()->index(fieldRowForId(parentFieldId), 5).data().toString(),
        QStringLiteral("field"), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("members")));

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 8),
                                     QStringLiteral("rw")));
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 9),
                                     QStringLiteral("wo")));
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 11),
                                     QStringLiteral("clear")));
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 12),
                                     QStringLiteral("w1c")));
    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("reserved")));
    QTRY_VERIFY_WITH_TIMEOUT(fieldRowForId(memberFieldId) >= 0, 2000);
    memberRow = fieldRowForId(memberFieldId);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 8).data().toString(),
                              QStringLiteral("none"), 2000);
    QCOMPARE(fields->model()->index(memberRow, 9).data().toString(),
             QStringLiteral("none"));
    QCOMPARE(fields->model()->index(memberRow, 11).data().toString(),
             QStringLiteral("none"));
    QCOMPARE(fields->model()->index(memberRow, 12).data().toString(),
             QStringLiteral("none"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::dragsFieldsAndResolvesOverlaps()
{
    struct DragCase {
        std::uint32_t targetBit;
        std::uint32_t previewLsb;
        std::uint32_t previewMsb;
        std::optional<QMessageBox::ButtonRole> resolutionRole;
        std::uint32_t finalMovingLsb;
        std::uint32_t finalMovingMsb;
        std::uint32_t finalObstacleLsb;
        std::uint32_t finalObstacleMsb;
    };
    const std::array cases{
        DragCase{13, 12, 15, std::nullopt, 12, 15, 4, 9},
        DragCase{9, 8, 11, QMessageBox::AcceptRole, 10, 11, 4, 9},
        DragCase{5, 4, 7, QMessageBox::DestructiveRole, 4, 7, 8, 9},
    };

    for (const auto& testCase : cases) {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifest =
            directory.filePath(QStringLiteral("bitfield-drag.regmap.yaml"));
        createBitfieldDragProject(manifest);

        MainWindow window;
        window.resize(1200, 760);
        window.show();
        window.openProjectPath(manifest);
        QTest::qWait(50);

        auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
        auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
        auto* bitfield = window.findChild<BitfieldView*>(QStringLiteral("bitfieldView"));
        QVERIFY(registers != nullptr);
        QVERIFY(fields != nullptr);
        QVERIFY(bitfield != nullptr);

        const QModelIndex openFields = registers->model()->index(0, 5);
        registers->scrollTo(openFields);
        QCoreApplication::processEvents();
        QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                          registers->visualRect(openFields).center());
        QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(bitfield->isVisible(), 2000);
        QVERIFY(bitfield->width() > 28);

        const QImage initialImage = bitfield->grab().toImage();
        QVERIFY(!initialImage.isNull());
        QCoreApplication::processEvents();

        QString activatedField;
        QString previewField;
        QString requestedField;
        std::uint32_t previewLsb = 0;
        std::uint32_t previewMsb = 0;
        std::uint32_t requestedLsb = 0;
        std::uint32_t requestedMsb = 0;
        QObject::connect(bitfield, &BitfieldView::fieldActivated, &window,
                         [&](const QString& id) { activatedField = id; });
        QObject::connect(
            bitfield, &BitfieldView::fieldDragPreview, &window,
            [&](const QString& id, std::uint32_t lsb, std::uint32_t msb) {
                previewField = id;
                previewLsb = lsb;
                previewMsb = msb;
            });
        QObject::connect(
            bitfield, &BitfieldView::fieldMoveRequested, &window,
            [&](const QString& id, std::uint32_t lsb, std::uint32_t msb) {
                requestedField = id;
                requestedLsb = lsb;
                requestedMsb = msb;
            });

        const QPoint pressPoint = bitfieldPointForBit(bitfield, 1);
        const QPoint targetPoint = bitfieldPointForBit(bitfield, testCase.targetBit);
        QTest::mousePress(bitfield, Qt::LeftButton, Qt::NoModifier, pressPoint);
        QCOMPARE(activatedField, QStringLiteral("field-ready"));
        QTest::mouseMove(bitfield, targetPoint, 20);
        QCoreApplication::processEvents();
        QCOMPARE(previewField, QStringLiteral("field-ready"));
        QCOMPARE(previewLsb, testCase.previewLsb);
        QCOMPARE(previewMsb, testCase.previewMsb);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Moving field [%1:%2]")
                .arg(testCase.previewMsb)
                .arg(testCase.previewLsb)));
        const QImage previewImage = bitfield->grab().toImage();
        QVERIFY(previewImage != initialImage);

        bool dialogHandled = !testCase.resolutionRole.has_value();
        QString dialogFailure;
        if (testCase.resolutionRole) {
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    dialogFailure =
                        QStringLiteral("Field overlap dialog did not become modal");
                    return;
                }
                if (dialog->windowTitle() != QStringLiteral("Resolve field overlap")) {
                    dialogFailure = QStringLiteral("Unexpected field overlap dialog");
                    dialog->reject();
                    return;
                }
                QAbstractButton* choice = nullptr;
                for (auto* button : dialog->buttons()) {
                    if (dialog->buttonRole(button) == *testCase.resolutionRole) {
                        choice = button;
                        break;
                    }
                }
                if (choice == nullptr) {
                    dialogFailure =
                        QStringLiteral("Requested field overlap resolution is unavailable");
                    dialog->reject();
                    return;
                }
                dialogHandled = true;
                QTest::mouseClick(choice, Qt::LeftButton);
            });
        }

        QTest::mouseRelease(bitfield, Qt::LeftButton, Qt::NoModifier, targetPoint);
        QCoreApplication::processEvents();
        QVERIFY2(dialogFailure.isEmpty(), qPrintable(dialogFailure));
        QVERIFY(dialogHandled);
        QCOMPARE(requestedField, QStringLiteral("field-ready"));
        QCOMPARE(requestedLsb, testCase.previewLsb);
        QCOMPARE(requestedMsb, testCase.previewMsb);

        const auto fieldRange = [&](const QString& id) {
            for (int row = 0; row < fields->model()->rowCount(); ++row) {
                const QModelIndex name = fields->model()->index(row, 0);
                if (name.data(Qt::UserRole + 1).toString() == id) {
                    return std::pair{
                        fields->model()->index(row, 3).data().toUInt(),
                        fields->model()->index(row, 2).data().toUInt(),
                    };
                }
            }
            return std::pair{std::uint32_t{UINT32_MAX}, std::uint32_t{UINT32_MAX}};
        };
        QTRY_COMPARE_WITH_TIMEOUT(fieldRange(QStringLiteral("field-ready")).first,
                                  testCase.finalMovingLsb, 2000);
        QCOMPARE(fieldRange(QStringLiteral("field-ready")).second,
                 testCase.finalMovingMsb);
        QCOMPARE(fieldRange(QStringLiteral("field-obstacle")).first,
                 testCase.finalObstacleLsb);
        QCOMPARE(fieldRange(QStringLiteral("field-obstacle")).second,
                 testCase.finalObstacleMsb);

        makeGeneratedFilesWritable(directory.path());
    }
}

void GuiSmokeTests::cancelsInterruptedFieldDrag()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("interrupted-drag.regmap.yaml"));
    createBitfieldDragProject(manifest);

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* bitfield = window.findChild<BitfieldView*>(QStringLiteral("bitfieldView"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(bitfield != nullptr);

    const QModelIndex openFields = registers->model()->index(0, 5);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(openFields).center());
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(bitfield->isVisible(), 2000);
    QVERIFY(!bitfield->grab().isNull());

    int previewCount = 0;
    int moveRequestCount = 0;
    QObject::connect(bitfield, &BitfieldView::fieldDragPreview, &window,
                     [&](const QString&, std::uint32_t, std::uint32_t) {
                         ++previewCount;
                     });
    QObject::connect(bitfield, &BitfieldView::fieldMoveRequested, &window,
                     [&](const QString&, std::uint32_t, std::uint32_t) {
                         ++moveRequestCount;
                     });

    const QPoint pressPoint = bitfieldPointForBit(bitfield, 1);
    const QPoint firstTarget = bitfieldPointForBit(bitfield, 13);
    const QPoint secondTarget = bitfieldPointForBit(bitfield, 17);
    QTest::mousePress(bitfield, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QTest::mouseMove(bitfield, firstTarget, 20);
    QCoreApplication::processEvents();
    QVERIFY(previewCount > 0);

    QEvent interrupted(QEvent::UngrabMouse);
    QCoreApplication::sendEvent(bitfield, &interrupted);
    const int previewsAtInterruption = previewCount;
    QTest::mouseMove(bitfield, secondTarget, 20);
    QTest::mouseRelease(bitfield, Qt::LeftButton, Qt::NoModifier, secondTarget);
    QCoreApplication::processEvents();
    QCOMPARE(previewCount, previewsAtInterruption);
    QCOMPARE(moveRequestCount, 0);

    const auto fieldRange = [&](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            const QModelIndex name = fields->model()->index(row, 0);
            if (name.data(Qt::UserRole + 1).toString() == id) {
                return std::pair{
                    fields->model()->index(row, 3).data().toUInt(),
                    fields->model()->index(row, 2).data().toUInt(),
                };
            }
        }
        return std::pair{std::uint32_t{UINT32_MAX}, std::uint32_t{UINT32_MAX}};
    };
    QCOMPARE(fieldRange(QStringLiteral("field-ready")).first, std::uint32_t{0});
    QCOMPARE(fieldRange(QStringLiteral("field-ready")).second, std::uint32_t{3});

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::editsTagsAndAccessFromDoubleClick()
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
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    QCOMPARE(registers->currentIndex(), tagIndex);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("existing"));

    QTest::mouseDClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
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
    QListWidgetItem* existing = nullptr;
    for (int itemIndex = 0; itemIndex < tags->count(); ++itemIndex) {
        QVERIFY(!(tags->item(itemIndex)->flags() & Qt::ItemIsUserCheckable));
        if (tags->item(itemIndex)->text() == QStringLiteral("control")) {
            control = tags->item(itemIndex);
        } else if (tags->item(itemIndex)->text() == QStringLiteral("existing")) {
            existing = tags->item(itemIndex);
        }
    }
    QVERIFY(control != nullptr);
    QVERIFY(existing != nullptr);
    QVERIFY(!control->isSelected());
    QVERIFY(existing->isSelected());
    QTest::mouseMove(tags->viewport(), tags->visualItemRect(control).center());
    QTest::mouseClick(tags->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tags->visualItemRect(control).center());
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control, existing"), 2000);

    search->setText(QStringLiteral("EXISTING"));
    QVERIFY(!add->isEnabled());
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    search->setText(QStringLiteral("newtag"));
    QVERIFY(add->isEnabled());
    QTest::mouseClick(add, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control, newtag"), 2000);

    search->setText(QStringLiteral("keyboardtag"));
    QVERIFY(add->isEnabled());
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control, keyboardtag, newtag"), 2000);

    search->setText(QStringLiteral("bad,tag"));
    QVERIFY(!add->isEnabled());
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(registers->model()->index(0, 10).data().toString(),
             QStringLiteral("control, keyboardtag, newtag"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("commas separate tags")));

    tagPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() == nullptr, 2000);

    QModelIndex accessIndex = registers->model()->index(0, 9);
    QCOMPARE(accessIndex.data().toString(), QStringLiteral("RO"));
    QVERIFY(!(accessIndex.flags() & Qt::ItemIsEditable));
    registers->scrollTo(accessIndex);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(accessIndex).center());
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    QCOMPARE(registers->currentIndex(), accessIndex);

    QTest::mouseDClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
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
    QSignalSpy accessResetSpy(registers->model(), &QAbstractItemModel::modelReset);
    QTest::mouseClick(access->viewport(), Qt::LeftButton, Qt::NoModifier,
                      access->visualItemRect(readWrite).center());
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("RW"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(accessResetSpy.count() > 0, 2000);

    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() == nullptr, 2000);
    accessIndex = registers->model()->index(0, 9);
    registers->scrollTo(accessIndex);
    Q_EMIT registers->doubleClicked(accessIndex);
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() != nullptr, 2000);
    accessPopup = qobject_cast<QFrame*>(QApplication::activePopupWidget());
    QVERIFY(accessPopup != nullptr);
    access = accessPopup->findChild<QListWidget*>(QStringLiteral("accessOptions"));
    QVERIFY(access != nullptr);
    QCOMPARE(access->currentItem()->text(), QStringLiteral("RW"));
    access->setCurrentRow(1);
    QCOMPARE(access->currentItem()->text(), QStringLiteral("RO"));
    access->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(access, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("RO"), 2000);

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
    QCOMPARE(registers->viewport()->cursor().shape(), Qt::PointingHandCursor);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier, insertionPoint);

    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 4, 2000);
    QCOMPARE(registers->viewport()->cursor().shape(), Qt::ArrowCursor);
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
    auto* controller = window.findChild<ProjectController*>();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("resultTabs"));
    QVERIFY(state != nullptr);
    QVERIFY(generated != nullptr);
    QVERIFY(retry != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(tabs != nullptr);

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

#ifdef Q_OS_WIN
    const auto workbook = std::ranges::find_if(
        controller->artifacts(), [](const regmap::GeneratedArtifact& artifact) {
            return artifact.kind == regmap::GenerationTargetKind::xlsx;
        });
    QVERIFY(workbook != controller->artifacts().end());
    const HANDLE lockedWorkbook =
        CreateFileW(workbook->path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(lockedWorkbook != INVALID_HANDLE_VALUE);

    QVERIFY(registers->model()->setData(
        registers->model()->index(0, 11),
        QStringLiteral("Edit that encounters a locked workbook.")));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isDirty(), 2000);
    save->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().contains(QStringLiteral("output failed")), 4000);
    bool workbookFailed = false;
    QString workbookFailure;
    for (int row = 0; row < generated->model()->rowCount(); ++row) {
        if (generated->model()->index(row, 0).data().toString() ==
            QStringLiteral("xlsx")) {
            workbookFailed =
                generated->model()->index(row, 2).data().toString() ==
                QStringLiteral("Failed");
            workbookFailure =
                generated->model()->index(row, 2).data(Qt::ToolTipRole).toString();
        }
    }
    const int failureTab = tabs->currentIndex();
    const bool retryVisible = retry->isVisible();
    CloseHandle(lockedWorkbook);

    QVERIFY(workbookFailed);
    QVERIFY(workbookFailure.contains(QStringLiteral("open in Excel")));
    QCOMPARE(failureTab, 1);
    QVERIFY(retryVisible);

    QTest::mouseClick(retry, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().startsWith(QStringLiteral("Synchronized")), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(retry->isHidden(), 2000);
    for (int row = 0; row < generated->model()->rowCount(); ++row) {
        QCOMPARE(generated->model()->index(row, 2).data().toString(),
                 QStringLiteral("Synchronized"));
    }
#endif

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
    auto* fieldPanel = window.findChild<QWidget*>(QStringLiteral("fieldPanel"));
    auto* fieldContext = window.findChild<QLabel*>(QStringLiteral("fieldContextLabel"));
    auto* problems = window.findChild<QTableView*>(QStringLiteral("problemsView"));
    auto* state = window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    QVERIFY(search != nullptr);
    QVERIFY(searchResult != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(fieldContext != nullptr);
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
    QTRY_VERIFY_WITH_TIMEOUT(fieldPanel->isVisible(), 2000);
    QVERIFY(fieldContext->text().contains(QStringLiteral("STATUS")));

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

void GuiSmokeTests::copiesAndPastesHierarchyObjects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(hierarchy != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(controller->workspace() != nullptr);

    const QString pageId = QStringLiteral("space-main");
    const QString blockId = QStringLiteral("block-control");
    QModelIndex pageIndex =
        hierarchyIndexByObjectId(hierarchy->model(), pageId);
    QVERIFY(pageIndex.isValid());
    hierarchy->setCurrentIndex(pageIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(hierarchy, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(hierarchy, Qt::Key_V, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.size(),
                              std::size_t{2}, 2000);
    const auto& originalPage = controller->workspace()->addressSpaces[0];
    const auto& copiedPage = controller->workspace()->addressSpaces[1];
    QCOMPARE(QString::fromStdString(copiedPage.name), QStringLiteral("Main Copy"));
    QVERIFY(copiedPage.id != originalPage.id);
    QCOMPARE(copiedPage.blocks.size(), originalPage.blocks.size());
    QVERIFY(!copiedPage.blocks.empty());
    QVERIFY(copiedPage.blocks.front().id != originalPage.blocks.front().id);
    QCOMPARE(copiedPage.blocks.front().registers.size(),
             originalPage.blocks.front().registers.size());
    QVERIFY(!copiedPage.blocks.front().registers.empty());
    QVERIFY(copiedPage.blocks.front().registers.front().id !=
            originalPage.blocks.front().registers.front().id);
    QVERIFY(!copiedPage.blocks.front().registers.front().fields.empty());
    QVERIFY(copiedPage.blocks.front().registers.front().fields.front().id !=
            originalPage.blocks.front().registers.front().fields.front().id);
    QVERIFY(copiedPage.source.empty());
    QVERIFY(copiedPage.blocks.front().source.empty());
    QVERIFY(copiedPage.blocks.front().registers.front().source.empty());
    QVERIFY(hierarchy->currentIndex().data(Qt::UserRole + 1).toString() ==
            QString::fromStdString(copiedPage.id));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.size(),
                              std::size_t{1}, 2000);

    QModelIndex blockIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockId);
    QVERIFY(blockIndex.isValid());
    hierarchy->setCurrentIndex(blockIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(hierarchy, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(hierarchy, Qt::Key_V, Qt::ControlModifier);

    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.front().blocks.size(),
                              std::size_t{2}, 2000);
    const auto& originalBlock =
        controller->workspace()->addressSpaces.front().blocks.front();
    const auto& copiedBlock =
        controller->workspace()->addressSpaces.front().blocks.back();
    QCOMPARE(QString::fromStdString(copiedBlock.name),
             QStringLiteral("Control Copy"));
    QVERIFY(copiedBlock.id != originalBlock.id);
    QCOMPARE(copiedBlock.registers.size(), originalBlock.registers.size());
    QVERIFY(!copiedBlock.registers.empty());
    QVERIFY(copiedBlock.registers.front().id !=
            originalBlock.registers.front().id);
    QVERIFY(!copiedBlock.registers.front().fields.empty());
    QVERIFY(copiedBlock.registers.front().fields.front().id !=
            originalBlock.registers.front().fields.front().id);
    QVERIFY(hierarchy->currentIndex().data(Qt::UserRole + 1).toString() ==
            QString::fromStdString(copiedBlock.id));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.front().blocks.size(),
                              std::size_t{1}, 2000);
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::movesHierarchyObjectsByDrag()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(hierarchy != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Seed hierarchy move test"),
        [](regmap::Workspace& workspace) {
            regmap::AddressSpace page;
            page.id = "space-secondary";
            page.name = "Secondary";
            page.baseAddress = 0x10000;
            page.addressWidth = 32;
            regmap::RegisterBlock block;
            block.id = "block-secondary";
            block.name = "Secondary Block";
            block.baseAddress = 0;
            block.size = 0x1000;
            page.blocks.push_back(std::move(block));
            workspace.addressSpaces.push_back(std::move(page));
        }));
    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.size(),
                              std::size_t{2}, 2000);

    QModelIndex mainPage =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-main"));
    QModelIndex secondaryPage =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-secondary"));
    QVERIFY(mainPage.isValid());
    QVERIFY(secondaryPage.isValid());
    QVERIFY(dropHierarchyObject(hierarchy, mainPage, secondaryPage));
    QTRY_COMPARE_WITH_TIMEOUT(
        QString::fromStdString(controller->workspace()->addressSpaces.front().id),
        QStringLiteral("space-secondary"), 2000);
    QCOMPARE(QString::fromStdString(controller->workspace()->addressSpaces.back().id),
             QStringLiteral("space-main"));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        QString::fromStdString(controller->workspace()->addressSpaces.front().id),
        QStringLiteral("space-main"), 2000);

    QModelIndex sourceBlock =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("block-control"));
    secondaryPage =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-secondary"));
    QVERIFY(sourceBlock.isValid());
    QVERIFY(secondaryPage.isValid());
    QVERIFY(dropHierarchyObject(hierarchy, sourceBlock, secondaryPage));
    QTRY_VERIFY_WITH_TIMEOUT(
        std::ranges::any_of(
            controller->workspace()->addressSpaces[1].blocks,
            [](const regmap::RegisterBlock& block) {
                return block.id == "block-control";
            }),
        2000);
    QVERIFY(std::ranges::none_of(
        controller->workspace()->addressSpaces[0].blocks,
        [](const regmap::RegisterBlock& block) {
            return block.id == "block-control";
        }));
    const auto* movedBlock =
        regmap::findRegisterBlock(*controller->workspace(), "block-control");
    QVERIFY(movedBlock != nullptr);
    QVERIFY(!movedBlock->registers.empty());
    QCOMPARE(movedBlock->registers.front().id, std::string{"reg-status"});

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        std::ranges::any_of(
            controller->workspace()->addressSpaces[0].blocks,
            [](const regmap::RegisterBlock& block) {
                return block.id == "block-control";
            }),
        2000);
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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);

    const QModelIndex statusName = registers->model()->index(0, 0);
    registers->setCurrentIndex(statusName);
    registers->selectionModel()->select(
        statusName, QItemSelectionModel::ClearAndSelect);
    registers->setFocus();
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("STATUS"));

    const QModelIndex sparseFirst = registers->model()->index(0, 0);
    const QModelIndex sparseSecond = registers->model()->index(1, 1);
    registers->setCurrentIndex(sparseSecond);
    registers->selectionModel()->select(
        sparseFirst, QItemSelectionModel::ClearAndSelect);
    registers->selectionModel()->select(
        sparseSecond, QItemSelectionModel::Select);
    QApplication::clipboard()->setText(QStringLiteral("KEEP_EXISTING_CLIPBOARD"));
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("KEEP_EXISTING_CLIPBOARD"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("contiguous rectangular range")));
    QVERIFY(registers->selectionModel()->isSelected(sparseFirst));
    QVERIFY(registers->selectionModel()->isSelected(sparseSecond));

    QApplication::clipboard()->setText(QStringLiteral("CONTROL_RENAMED\t0x8"));
    const QModelIndex matrixPasteStart = registers->model()->index(1, 0);
    const QModelIndex matrixPasteEnd = registers->model()->index(1, 1);
    registers->setCurrentIndex(matrixPasteEnd);
    registers->selectionModel()->select(
        QItemSelection(matrixPasteStart, matrixPasteEnd),
        QItemSelectionModel::ClearAndSelect);
    QCOMPARE(registers->currentIndex(), matrixPasteEnd);
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL_RENAMED"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x8"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(1, 0)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 1)),
        2000);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x4"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(1, 0)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 1)),
        2000);

    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL_RENAMED"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x8"), 2000);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 0).data().toString(),
                              QStringLiteral("CONTROL"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 1).data().toString(),
                              QStringLiteral("0x4"), 2000);

    const std::size_t mixedPasteUndoDepth = controller->undoDepth();
    QApplication::clipboard()->setText(
        QStringLiteral("STATUS_MIXED\t0x0\tignored\tinvalid-width"));
    registers->setCurrentIndex(registers->model()->index(0, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 0).data().toString(),
                              QStringLiteral("STATUS_MIXED"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 1).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 3).data().toString(),
                              QStringLiteral("32"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->currentIndex().row(), 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->currentIndex().column(), 3, 2000);
    QVERIFY(registers->selectionModel()->isSelected(
        registers->model()->index(0, 3)));
    const QString mixedPasteMessage = window.statusBar()->currentMessage();
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("1 changed")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("1 unchanged")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("1 rejected")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("1 skipped")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("STATUS")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("Width")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("invalid-width")));
    QVERIFY(mixedPasteMessage.contains(QStringLiteral("expected")));
    QCOMPARE(controller->undoDepth(), mixedPasteUndoDepth + 1);
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 0).data().toString(),
                              QStringLiteral("STATUS"), 2000);
    QCOMPARE(controller->undoDepth(), mixedPasteUndoDepth);

    const QModelIndex readOnlyAddress = registers->model()->index(0, 2);
    const QString originalAddress = readOnlyAddress.data().toString();
    const std::size_t readOnlyPasteUndoDepth = controller->undoDepth();
    registers->setCurrentIndex(readOnlyAddress);
    registers->selectionModel()->select(
        readOnlyAddress, QItemSelectionModel::ClearAndSelect);
    QApplication::clipboard()->setText(QStringLiteral("0xDEADBEEF"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(controller->undoDepth(), readOnlyPasteUndoDepth);
    QCOMPARE(readOnlyAddress.data().toString(), originalAddress);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("1 skipped")));
    QVERIFY(registers->selectionModel()->isSelected(readOnlyAddress));

    const QModelIndex sourceTags = registers->model()->index(1, 10);
    registers->scrollTo(sourceTags);
    QCoreApplication::processEvents();
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(sourceTags).center());
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    QCOMPARE(registers->currentIndex(), sourceTags);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("control"));

    const QModelIndex firstTags = registers->model()->index(0, 10);
    const QModelIndex lastTags = registers->model()->index(1, 10);
    registers->scrollTo(firstTags);
    QCoreApplication::processEvents();
    const QPoint firstPosition = registers->visualRect(firstTags).center();
    const QPoint lastPosition = registers->visualRect(lastTags).center();
    QTest::mousePress(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      firstPosition);
    QTest::mouseMove(registers->viewport(), lastPosition, 20);
    QTest::mouseRelease(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                        lastPosition);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(firstTags) &&
            registers->selectionModel()->isSelected(lastTags),
        2000);
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    window.activateWindow();
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(registers->hasFocus(), 2000);
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(0, 10)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 10)),
        2000);
    const auto* status =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(status != nullptr);
    QCOMPARE(status->tags, std::vector<std::string>{"control"});
    const QString tagsPasteMessage = window.statusBar()->currentMessage();
    QVERIFY(tagsPasteMessage.contains(QStringLiteral("1 changed")));
    QVERIFY(tagsPasteMessage.contains(QStringLiteral("1 unchanged")));
    QVERIFY(tagsPasteMessage.contains(QStringLiteral("0 rejected")));

    const QModelIndex refreshedFirstTags = registers->model()->index(0, 10);
    const QModelIndex refreshedLastTags = registers->model()->index(1, 10);
    registers->setCurrentIndex(refreshedFirstTags);
    registers->selectionModel()->select(
        QItemSelection(refreshedFirstTags, refreshedLastTags),
        QItemSelectionModel::ClearAndSelect);
    QApplication::clipboard()->setText(QStringLiteral("shared"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("shared"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("shared"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(0, 10)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 10)),
        2000);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("shared"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("shared"), 2000);
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("control"), 2000);

    const QModelIndex sourceAccess = registers->model()->index(0, 9);
    registers->scrollTo(sourceAccess);
    QCoreApplication::processEvents();
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(sourceAccess).center());
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("RO"));

    const QModelIndex firstAccess = registers->model()->index(0, 9);
    const QModelIndex lastAccess = registers->model()->index(1, 9);
    registers->setCurrentIndex(lastAccess);
    registers->selectionModel()->select(
        QItemSelection(firstAccess, lastAccess),
        QItemSelectionModel::ClearAndSelect);
    QCOMPARE(registers->currentIndex(), lastAccess);
    QVERIFY(registers->selectionModel()->isSelected(firstAccess));
    QVERIFY(registers->selectionModel()->isSelected(lastAccess));
    const std::size_t rejectedPasteUndoDepth = controller->undoDepth();
    QSignalSpy rejectedPasteResetSpy(
        registers->model(), &QAbstractItemModel::modelReset);
    QApplication::clipboard()->setText(QStringLiteral("BAD"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(controller->undoDepth(), rejectedPasteUndoDepth);
    QCOMPARE(rejectedPasteResetSpy.count(), 0);
    QCOMPARE(registers->model()->index(0, 9).data().toString(), QStringLiteral("RO"));
    QCOMPARE(registers->model()->index(1, 9).data().toString(), QStringLiteral("RW"));
    const QString rejectedPasteMessage = window.statusBar()->currentMessage();
    QVERIFY(rejectedPasteMessage.contains(QStringLiteral("0 unchanged")));
    QVERIFY(rejectedPasteMessage.contains(QStringLiteral("2 rejected")));
    QVERIFY(rejectedPasteMessage.contains(QStringLiteral("STATUS")));
    QVERIFY(rejectedPasteMessage.contains(QStringLiteral("Access")));
    QVERIFY(rejectedPasteMessage.contains(QStringLiteral("none, ro, wo, or rw")));
    QCOMPARE(registers->currentIndex(), firstAccess);
    QVERIFY(registers->selectionModel()->isSelected(firstAccess));
    QVERIFY(registers->selectionModel()->isSelected(lastAccess));

    QApplication::clipboard()->setText(QStringLiteral("WO"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("WO"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("WO"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(0, 9)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 9)),
        2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->access,
             regmap::AccessMode::writeOnly);

    const std::size_t unchangedPasteUndoDepth = controller->undoDepth();
    QApplication::clipboard()->setText(QStringLiteral("WO"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(controller->undoDepth(), unchangedPasteUndoDepth);
    const QString unchangedPasteMessage = window.statusBar()->currentMessage();
    QVERIFY(unchangedPasteMessage.contains(QStringLiteral("2 unchanged")));
    QVERIFY(unchangedPasteMessage.contains(QStringLiteral("0 rejected")));
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(0, 9)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 9)),
        2000);

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("RO"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("RW"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        registers->selectionModel()->isSelected(registers->model()->index(0, 9)) &&
            registers->selectionModel()->isSelected(registers->model()->index(1, 9)),
        2000);
    QTest::keyClick(&window, Qt::Key_Y, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("WO"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("WO"), 2000);
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 9).data().toString(),
                              QStringLiteral("RO"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 9).data().toString(),
                              QStringLiteral("RW"), 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsUndoRedoInsideActiveEditor()
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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);

    QSignalSpy committedEditResetSpy(
        registers->model(), &QAbstractItemModel::modelReset);
    const QModelIndex description = registers->model()->index(0, 11);
    QVERIFY(registers->model()->setData(
        description, QStringLiteral("Committed description")));
    QTRY_VERIFY_WITH_TIMEOUT(committedEditResetSpy.count() > 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(controller->canUndo(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        QString::fromStdString(
            regmap::findRegister(*controller->workspace(), "reg-status")->description),
        QStringLiteral("Committed description"), 2000);
    const std::size_t workspaceUndoDepth = controller->undoDepth();

    const QModelIndex name = registers->model()->index(0, 0);
    registers->setCurrentIndex(name);
    registers->scrollTo(name);
    registers->setFocus(Qt::OtherFocusReason);
    registers->edit(name);
    QCoreApplication::processEvents();
    const auto visibleEditor = [registers]() -> QLineEdit* {
        const auto editors = registers->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor() != nullptr, 2000);
    QLineEdit* editor = visibleEditor();
    const QString originalName = editor->text();
    editor->setCursorPosition(static_cast<int>(originalName.size()));
    QTest::keyClicks(editor, QStringLiteral("X"));
    QCOMPARE(editor->text(), originalName + QStringLiteral("X"));

    QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(editor->text(), originalName);
    QCOMPARE(controller->undoDepth(), workspaceUndoDepth);
    QCOMPARE(
        QString::fromStdString(
            regmap::findRegister(*controller->workspace(), "reg-status")->description),
        QStringLiteral("Committed description"));

    QTest::keyClick(editor, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(editor->text(), originalName + QStringLiteral("X"));
    QCOMPARE(controller->undoDepth(), workspaceUndoDepth);
    QCOMPARE(
        QString::fromStdString(
            regmap::findRegister(*controller->workspace(), "reg-status")->description),
        QStringLiteral("Committed description"));

    QTest::keyClick(editor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->index(0, 0).data().toString(), originalName);
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::savesActiveEditorWithShortcut()
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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(!controller->isDirty());

    const QModelIndex name = registers->model()->index(0, 0);
    registers->setCurrentIndex(name);
    registers->scrollTo(name);
    registers->setFocus(Qt::OtherFocusReason);
    registers->edit(name);
    QCoreApplication::processEvents();
    const auto visibleEditor = [registers]() -> QLineEdit* {
        const auto editors = registers->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor() != nullptr, 2000);
    QLineEdit* editor = visibleEditor();
    const QString savedName = QStringLiteral("STATUS_SAVED");
    editor->selectAll();
    QTest::keyClicks(editor, savedName);
    QCOMPARE(editor->text(), savedName);
    QCOMPARE(registers->model()->index(0, 0).data().toString(),
             QStringLiteral("STATUS"));

    QSignalSpy saveResetSpy(registers->model(), &QAbstractItemModel::modelReset);
    QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 0).data().toString(),
                              savedName, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(saveResetSpy.count() > 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 2000);

    const auto reopened =
        regmap::openProject(std::filesystem::path(manifest.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QCOMPARE(QString::fromStdString(
                 regmap::findRegister(*reopened.workspace, "reg-status")->name),
             savedName);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsInvalidActiveEditorBeforeSave()
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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(!controller->isDirty());

    QSignalSpy syncSpy(controller, &ProjectController::syncStatusChanged);
    const QModelIndex width = registers->model()->index(0, 3);
    registers->setCurrentIndex(width);
    registers->scrollTo(width);
    registers->setFocus(Qt::OtherFocusReason);
    registers->edit(width);
    QCoreApplication::processEvents();
    const auto visibleEditor = [registers]() -> QLineEdit* {
        const auto editors = registers->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor() != nullptr, 2000);
    QLineEdit* invalidEditor = visibleEditor();
    invalidEditor->selectAll();
    QTest::keyClicks(invalidEditor, QStringLiteral("invalid-width"));
    QCOMPARE(invalidEditor->text(), QStringLiteral("invalid-width"));
    QCOMPARE(registers->model()->index(0, 3).data().toString(),
             QStringLiteral("32"));

    QSignalSpy resetSpy(registers->model(), &QAbstractItemModel::modelReset);
    QTest::keyClick(invalidEditor, Qt::Key_S, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(resetSpy.count() > 0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 3).data().toString(),
                              QStringLiteral("32"), 2000);
    QCOMPARE(syncSpy.count(), 0);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Edit rejected: expected")));
    QVERIFY(!controller->isDirty());
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
    QCOMPARE(fields->model()->rowCount(), 0);
    QVERIFY(!fields->isVisible());

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

void GuiSmokeTests::synchronizesManagedRtlRegisterOrderWithExistingBaseline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createTwoRegisterProject(manifestPath);

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(QFileInfo::exists(rtlPath));
    QVERIFY(QFileInfo::exists(baselinePath));
    QVERIFY(!controller.isDirty());
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-status"));

    auto rtlWorkspace = *controller.workspace();
    auto& rtlRegisters = rtlWorkspace.addressSpaces.front().blocks.front().registers;
    std::reverse(rtlRegisters.begin(), rtlRegisters.end());
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    controller.manifest()->rtl.moduleName, rtlWorkspace)
                .empty());
    controller.synchronizeNow();

    QTRY_COMPARE_WITH_TIMEOUT(
        controller.workspace()->addressSpaces.front().blocks.front().registers.front().id,
        std::string("reg-control"), 8000);
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.hasProjectErrors());
    QVERIFY(!controller.isDirty());

    const auto reopened =
        regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    const auto parsedRtl =
        regmap::parseManagedRtl(std::filesystem::path(rtlPath.toStdWString()));
    const auto baseline =
        regmap::loadSyncBaseline(std::filesystem::path(baselinePath.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QVERIFY(parsedRtl.workspace.has_value());
    QVERIFY(baseline.workspace.has_value());
    QCOMPARE(reopened.workspace->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-control"));
    QCOMPARE(parsedRtl.workspace->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-control"));
    QCOMPARE(baseline.workspace->addressSpaces.front().blocks.front().registers.front().id,
             std::string("reg-control"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::synchronizesManagedRtlRegisterReparentWithExistingBaseline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifestPath = directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath = directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    const QString baselinePath = manifestPath + QStringLiteral(".sync.json");
    createTwoRegisterProject(manifestPath);

    auto source = regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    QVERIFY(source.workspace.has_value());
    QVERIFY(source.manifest.has_value());
    regmap::RegisterBlock secondaryBlock;
    secondaryBlock.id = "block-secondary";
    secondaryBlock.name = "Secondary";
    secondaryBlock.baseAddress = 0x1000;
    secondaryBlock.size = 0x1000;
    source.workspace->addressSpaces.front().blocks.push_back(secondaryBlock);
    QVERIFY(regmap::saveProjectFile(*source.manifest, *source.workspace).empty());

    ProjectController controller;
    controller.openProject(manifestPath);
    QVERIFY(QFileInfo::exists(rtlPath));
    QVERIFY(QFileInfo::exists(baselinePath));
    QVERIFY(!controller.isDirty());
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.size(), std::size_t{2});
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.size(),
             std::size_t{2});
    QVERIFY(controller.workspace()->addressSpaces.front().blocks.back().registers.empty());

    auto rtlWorkspace = *controller.workspace();
    auto& sourceRegisters = rtlWorkspace.addressSpaces.front().blocks.front().registers;
    auto& targetRegisters = rtlWorkspace.addressSpaces.front().blocks.back().registers;
    targetRegisters.push_back(sourceRegisters.back());
    sourceRegisters.pop_back();
    QVERIFY(regmap::writeManagedRtl(std::filesystem::path(rtlPath.toStdWString()),
                                    controller.manifest()->rtl.moduleName, rtlWorkspace)
                .empty());
    controller.synchronizeNow();

    QTRY_COMPARE_WITH_TIMEOUT(
        controller.workspace()->addressSpaces.front().blocks.back().registers.size(),
        std::size_t{1}, 8000);
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.front().registers.size(),
             std::size_t{1});
    QCOMPARE(controller.workspace()->addressSpaces.front().blocks.back().registers.front().id,
             std::string("reg-control"));
    QVERIFY(!controller.hasConflicts());
    QVERIFY(!controller.hasProjectErrors());
    QVERIFY(!controller.isDirty());

    const auto reopened =
        regmap::openProject(std::filesystem::path(manifestPath.toStdWString()));
    const auto parsedRtl =
        regmap::parseManagedRtl(std::filesystem::path(rtlPath.toStdWString()));
    const auto baseline =
        regmap::loadSyncBaseline(std::filesystem::path(baselinePath.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    QVERIFY(parsedRtl.workspace.has_value());
    QVERIFY(baseline.workspace.has_value());
    QCOMPARE(reopened.workspace->addressSpaces.front().blocks.back().registers.front().id,
             std::string("reg-control"));
    QCOMPARE(parsedRtl.workspace->addressSpaces.front().blocks.back().registers.front().id,
             std::string("reg-control"));
    QCOMPARE(baseline.workspace->addressSpaces.front().blocks.back().registers.front().id,
             std::string("reg-control"));

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

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);
    GuiSmokeTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "gui_smoke_tests.moc"
