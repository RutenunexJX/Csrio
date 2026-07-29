#include "bitfield_view.hpp"
#include "main_window.hpp"
#include "project_controller.hpp"
#include "workbench_theme.hpp"

#include "regmap/core/project.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/validation.hpp"
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
#include <QFileDialog>
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
#include <QPointer>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QStringList>
#include <QStatusBar>
#include <QStandardPaths>
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
    void treatsInvalidInitialRtlAsRecoverableAfterCreation();
    void establishesMissingBaselineForIdenticalSources();
    void blocksDivergentSourcesWithoutBaseline();
    void resolvesMissingBaselineWithExplicitChoice();
    void recoversAfterInvalidInitialRtlIsFixed();
    void detectsStructuralDifferenceWithoutBaseline();
    void opensProjectAndPopulatesEditableViews();
    void persistsWorkbenchLayoutPreferences();
    void keepsEnumEditorCompact();
    void navigatesHierarchyAndOpensFieldsExplicitly();
    void hierarchyContextActionsUseRightClickedTarget();
    void confirmsHierarchyDeletionImpactAndRestoresIt();
    void placesNewHierarchyObjectsWithoutAddressErrors();
    void copiesAndPastesHierarchyObjects();
    void movesHierarchyObjectsByDrag();
    void switchesProjectsWithoutReusingFieldWorkspaceState();
    void retainsCurrentProjectWhenReplacementCannotLoad();
    void retainsCurrentProjectWhenCreationFails();
    void reportsExplicitOpenFailure();
    void reloadsProjectWithoutLosingFieldWorkspaceContext();
    void confirmsDiscardBeforeReloadingDirtyProject();
    void protectsUnsavedChangesWhenClosing();
    void confirmsUnsavedChangesBeforeReplacingProject();
    void preflightsActiveEditorBeforeProjectChoosers();
    void startsProjectChoosersInUsefulDirectories();
    void opensAndCleansRecentProjectsSafely();
    void navigatesFieldProblemsAndFallsBackForHiddenFields();
    void navigatesEnumValuesFromSearchAndProblems();
    void supportsTrailingRowsAndFieldMovement();
    void keepsRegisterFieldConversionImmediatelyUsable();
    void keepsEnumConversionsImmediatelyUsable();
    void confirmsCompoundTypeChangesBeforeRemovingChildren();
    void confirmsEnumTypeChangesBeforeRemovingValues();
    void keepsCompoundFieldsUsableDuringConversionAndDeletion();
    void confirmsFieldDeletionImpactAndRestoresIt();
    void dragsFieldsAndResolvesOverlaps();
    void rejectsFieldMoveThatInvalidatesValueContracts();
    void confirmsDeletionOfFullyCoveredFieldsDuringDrag();
    void cancelsInterruptedFieldDrag();
    void editsTagsAndAccessFromDoubleClick();
    void closesAnchoredPopupEditorsWithEscape();
    void keepsPopupEditingActionsLocal();
    void editsFieldAccessFromConstrainedChoices();
    void keepsAccessAndEffectsConsistentDuringEditing();
    void closesTagPopupWhenFilteredRegisterDisappears();
    void insertsRegisterBetweenRows();
    void showsUnifiedSyncStateAndGeneratedResults();
    void reportsBlockedUnsavedSyncAndRecovers();
    void searchesAndNavigatesProblems();
    void refreshesSearchResultsAfterModelChanges();
    void copiesAndPastesEditableCells();
    void rejectsOutOfRangeNumericEdits();
    void rejectsInvalidNumericRangesDuringEditing();
    void protectsNumericRangesDuringShapeChanges();
    void duplicatesCompleteRegisterSafely();
    void duplicatesCompleteFieldSafely();
    void duplicatesFocusedObjectsWithShortcut();
    void confirmsReservedConversionBeforeClearingContent();
    void rejectsAddressEditsThatIntroduceConflicts();
    void rejectsRecoveryEditsThatReplaceAddressProblems();
    void rejectsGeometryConflictsFromWidthAndTypeEdits();
    void synchronizesFieldResetEdits();
    void protectsEnumContractsDuringEditing();
    void rejectsRegisterResetsOutsideFieldEnums();
    void rejectsDuplicateObjectNamesDuringEditing();
    void rejectsEmptyWorkspaceNameDuringEditing();
    void requiresExplicitCellEditing();
    void keepsUndoRedoInsideActiveEditor();
    void savesActiveEditorWithShortcut();
    void savesActiveChoiceEditorWithShortcut();
    void rejectsInvalidActiveEditorBeforeSave();
    void deletesFocusedRegisterAndRestoresIt();
    void rejectsUnsafeDeleteAndShift();
    void editsUndoesAndSavesProject();
    void rejectsInvalidManagedRtl();
    void synchronizesManagedRtlEdits();
    void synchronizesManagedRtlRegisterOrderWithExistingBaseline();
    void synchronizesManagedRtlRegisterReparentWithExistingBaseline();
    void resolvesRtlConflictFromDiffPanel();
    void reportsAndResolvesRtlConflicts();
};

namespace {

class SettingsScope final {
public:
    explicit SettingsScope(
        const QString& application)
        : organization_(
              QCoreApplication::organizationName()),
          application_(
              QCoreApplication::applicationName())
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("RegMapWorkbenchTests"));
        QCoreApplication::setApplicationName(
            application);
        QSettings settings;
        settings.remove(QStringLiteral("ui"));
        settings.sync();
    }

    ~SettingsScope()
    {
        QSettings settings;
        settings.remove(QStringLiteral("ui"));
        settings.sync();
        QCoreApplication::setOrganizationName(
            organization_);
        QCoreApplication::setApplicationName(
            application_);
    }

private:
    QString organization_;
    QString application_;
};

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

bool invokeTableContextAction(MainWindow& window,
                              QTableView* table, int row,
                              const QString& actionObjectName,
                              QString& failure)
{
    if (table == nullptr || row < 0 ||
        row >= table->model()->rowCount()) {
        failure = QStringLiteral("Table row is unavailable");
        return false;
    }
    const QModelIndex index = table->model()->index(row, 0);
    table->setCurrentIndex(index);
    table->scrollTo(index);
    QCoreApplication::processEvents();
    bool triggered = false;
    QTimer::singleShot(0, &window, [&] {
        auto* action =
            window.findChild<QAction*>(actionObjectName);
        auto* menu =
            action == nullptr
                ? qobject_cast<QMenu*>(
                      QApplication::activePopupWidget())
                : qobject_cast<QMenu*>(action->parent());
        if (action == nullptr || menu == nullptr ||
            !action->isEnabled()) {
            failure = QStringLiteral(
                "Requested table action is unavailable");
            if (menu != nullptr) {
                menu->close();
            }
            return;
        }
        triggered = true;
        QTest::mouseClick(
            menu, Qt::LeftButton, Qt::NoModifier,
            menu->actionGeometry(action).center());
    });
    const QPoint position = table->visualRect(index).center();
    QContextMenuEvent event(
        QContextMenuEvent::Mouse, position,
        table->viewport()->mapToGlobal(position));
    QCoreApplication::sendEvent(table->viewport(), &event);
    QCoreApplication::processEvents();
    return triggered;
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

void GuiSmokeTests::treatsInvalidInitialRtlAsRecoverableAfterCreation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("rtl")));
    const QString manifestPath =
        directory.filePath(QStringLiteral("created.regmap.yaml"));
    const QString rtlPath =
        directory.filePath(QStringLiteral("rtl/created_registers.sv"));
    const QString baselinePath =
        manifestPath + QStringLiteral(".sync.json");

    QFile rtlFile(rtlPath);
    QVERIFY(rtlFile.open(
        QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray invalidRtl(
        "module created_registers;\n"
        "// RMW:BEGIN\n"
        "localparam logic [63:0] INVALID = 64'hx;\n");
    QCOMPARE(rtlFile.write(invalidRtl), invalidRtl.size());
    rtlFile.close();

    ProjectController controller;
    QSignalSpy statusChanged(
        &controller, &ProjectController::syncStatusChanged);
    QVERIFY(controller.createProject(manifestPath));
    QVERIFY(controller.workspace() != nullptr);
    QVERIFY(controller.manifest() != nullptr);
    QCOMPARE(controller.manifestPath(),
             std::filesystem::path(manifestPath.toStdWString()));
    QCOMPARE(controller.workspace()->name, std::string("created"));
    QVERIFY(controller.hasProjectErrors());
    QVERIFY(!controller.isDirty());
    QVERIFY(!QFileInfo::exists(baselinePath));
    QVERIFY(std::ranges::any_of(
        controller.diagnostics(),
        [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.severity ==
                regmap::DiagnosticSeverity::error;
        }));
    QVERIFY(!statusChanged.isEmpty());
    QVERIFY(statusChanged.last().at(0).toString().contains(
        QStringLiteral("Managed RTL is invalid")));
    QVERIFY(statusChanged.last().at(0).toString().contains(
        QStringLiteral("synchronization is blocked")));

    QVERIFY(QFile::remove(rtlPath));
    controller.synchronizeNow();
    QVERIFY(!controller.hasProjectErrors());
    QVERIFY(!controller.isDirty());
    QVERIFY(QFileInfo::exists(rtlPath));
    QVERIFY(QFileInfo::exists(baselinePath));

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

    bool choiceDialogSeen = false;
    bool choiceImpactDescribed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        choiceDialogSeen = true;
        auto* resolve = dialog->findChild<QPushButton*>(
            QStringLiteral("confirmConflictResolutionButton"));
        choiceImpactDescribed =
            dialog->windowTitle() ==
                QStringLiteral("Choose Initial Synchronization Source") &&
            dialog->text().contains(
                QStringLiteral("complete managed RTL model")) &&
            dialog->informativeText().contains(
                QStringLiteral(
                    "current Workbench model and project file will be replaced")) &&
            dialog->informativeText().contains(
                QStringLiteral("detected model difference(s)")) &&
            dialog->informativeText().contains(
                QStringLiteral("No file is changed if you cancel")) &&
            resolve != nullptr &&
            resolve->text() == QStringLiteral("Use complete RTL") &&
            dialog->defaultButton() ==
                dialog->button(QMessageBox::Cancel);
        QTest::mouseClick(
            dialog->button(QMessageBox::Cancel), Qt::LeftButton);
    });
    QTest::mouseClick(useRtl, Qt::LeftButton);
    QVERIFY(choiceDialogSeen);
    QVERIFY(choiceImpactDescribed);
    QVERIFY(controller->requiresInitialSyncChoice());
    QVERIFY(controller->hasConflicts());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("choice cancelled · no file changed")));
    QVERIFY(!QFileInfo::exists(baselinePath));

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
    QCOMPARE(registers->model()->index(0, 9).data().toString(), QStringLiteral("RO"));
    QCOMPARE(fields->model()->index(0, 8).data().toString(), QStringLiteral("RO"));
    QCOMPARE(fields->model()->index(0, 9).data().toString(), QStringLiteral("WO"));
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

void GuiSmokeTests::persistsWorkbenchLayoutPreferences()
{
    SettingsScope settingsScope(
        QStringLiteral("UiStatePersistenceTest"));

    QByteArray geometry;
    QList<int> workspaceSizes;
    QList<int> editorSizes;
    QList<int> resultsSizes;
    {
        MainWindow first;
        first.resize(700, 650);
        first.move(20, 20);
        first.show();
        first.activateWindow();
        QTest::qWait(50);

        auto* workspace =
            first.findChild<QSplitter*>(
                QStringLiteral("workspaceSplitter"));
        auto* editor =
            first.findChild<QSplitter*>(
                QStringLiteral("editorSplitter"));
        auto* results =
            first.findChild<QSplitter*>(
                QStringLiteral("resultsSplitter"));
        auto* advanced =
            first.findChild<QAction*>(
                QStringLiteral("showAdvancedFieldsAction"));
        QVERIFY(workspace != nullptr);
        QVERIFY(editor != nullptr);
        QVERIFY(results != nullptr);
        QVERIFY(advanced != nullptr);

        workspace->setSizes({315, 865});
        editor->setSizes({430, 326});
        results->setSizes({545, 215});
        advanced->setChecked(true);
        QCoreApplication::processEvents();

        geometry = first.saveGeometry();
        workspaceSizes = workspace->sizes();
        editorSizes = editor->sizes();
        resultsSizes = results->sizes();
        QVERIFY(!geometry.isEmpty());
        QVERIFY(advanced->isChecked());
        QVERIFY(first.close());
        QTRY_VERIFY_WITH_TIMEOUT(
            !first.isVisible(), 2000);
    }

    QSettings settings;
    settings.sync();
    QVERIFY(settings.contains(
        QStringLiteral("ui/v1/mainWindowGeometry")));
    QVERIFY(settings.contains(
        QStringLiteral("ui/v1/workspaceSplitter")));
    QVERIFY(settings.contains(
        QStringLiteral("ui/v1/editorSplitter")));
    QVERIFY(settings.contains(
        QStringLiteral("ui/v1/resultsSplitter")));
    QVERIFY(settings.value(
        QStringLiteral("ui/v1/showAdvancedFieldColumns"))
                .toBool());
    QCOMPARE(
        settings.value(
                    QStringLiteral("ui/v1/mainWindowGeometry"))
            .toByteArray(),
        geometry);

    settings.remove(
        QStringLiteral("ui/v1/mainWindowGeometry"));
    settings.sync();
    QByteArray defaultGeometry;
    {
        MainWindow defaultWindow;
        defaultGeometry = defaultWindow.saveGeometry();
    }
    QVERIFY(!defaultGeometry.isEmpty());
    settings.setValue(
        QStringLiteral("ui/v1/mainWindowGeometry"),
        geometry);
    settings.sync();

    MainWindow restored;
    QVERIFY(restored.saveGeometry() != defaultGeometry);
    restored.show();
    restored.activateWindow();
    QTest::qWait(50);
    auto* workspace =
        restored.findChild<QSplitter*>(
            QStringLiteral("workspaceSplitter"));
    auto* editor =
        restored.findChild<QSplitter*>(
            QStringLiteral("editorSplitter"));
    auto* results =
        restored.findChild<QSplitter*>(
            QStringLiteral("resultsSplitter"));
    auto* advanced =
        restored.findChild<QAction*>(
            QStringLiteral("showAdvancedFieldsAction"));
    QVERIFY(workspace != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(results != nullptr);
    QVERIFY(advanced != nullptr);
    QCOMPARE(workspace->sizes(), workspaceSizes);
    QCOMPARE(editor->sizes(), editorSizes);
    QCOMPARE(results->sizes(), resultsSizes);
    QVERIFY(advanced->isChecked());
    QVERIFY(restored.close());
}

void GuiSmokeTests::keepsEnumEditorCompact()
{
    SettingsScope settingsScope(
        QStringLiteral("EnumPanelLayoutTest"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1200, 800);
    window.show();
    QTest::qWait(50);

    auto* registers =
        window.findChild<QTableView*>(
            QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(
            QStringLiteral("fieldView"));
    auto* fieldPanel =
        window.findChild<QWidget*>(
            QStringLiteral("fieldPanel"));
    auto* enumPanel =
        window.findChild<QWidget*>(
            QStringLiteral("enumPanel"));
    auto* enumContext =
        enumPanel == nullptr
            ? nullptr
            : enumPanel->findChild<QLabel*>(
                  QStringLiteral("contextTitle"));
    auto* enums =
        window.findChild<QTableView*>(
            QStringLiteral("enumView"));
    auto* editor =
        window.findChild<QSplitter*>(
            QStringLiteral("editorSplitter"));
    auto* closeFields =
        window.findChild<QPushButton*>(
            QStringLiteral("closeFieldsButton"));
    auto* controller =
        window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(enumPanel != nullptr);
    QVERIFY(enumContext != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(closeFields != nullptr);
    QVERIFY(controller != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure scalar Boolean fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* reg =
                    regmap::findRegister(
                        workspace, "reg-control")) {
                reg->width = 1;
                reg->type =
                    regmap::FieldType::boolean;
                reg->minimumValue.reset();
                reg->maximumValue.reset();
                reg->enumValues.clear();
            }
        }));
    QVERIFY(regmap::validateWorkspace(
                *controller->workspace())
                .empty());

    const QModelIndex openFields =
        registers->model()->index(0, 5);
    registers->setCurrentIndex(openFields);
    registers->selectionModel()->select(
        openFields, QItemSelectionModel::ClearAndSelect);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(
        fields->isVisible(), 2000);
    editor->setSizes({430, 300});
    QCoreApplication::processEvents();
    const QList<int> expandedSizes =
        editor->sizes();
    const QByteArray expandedState =
        editor->saveState();
    QVERIFY(expandedSizes[1] > 0);
    closeFields->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        !fieldPanel->isVisible(), 2000);

    const QModelIndex scalar =
        registers->model()->index(1, 0);
    registers->setCurrentIndex(scalar);
    registers->selectionModel()->select(
        scalar, QItemSelectionModel::ClearAndSelect);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(
        fieldPanel->isVisible(), 2000);
    QVERIFY(!fields->isVisible());
    QVERIFY(enumPanel->isVisible());
    QVERIFY(enumContext->isVisible());
    QVERIFY(enums->isVisible());
    QCOMPARE(enums->model()->rowCount(), 2);
    QVERIFY(enumPanel->y() <= 2);
    const int enumGap =
        enums->mapTo(enumPanel, QPoint(0, 0)).y() -
        (enumContext->mapTo(enumPanel, QPoint(0, 0)).y() +
         enumContext->height());
    QVERIFY(enumGap >= 0);
    QVERIFY(enumGap <= 8);
    QVERIFY(fieldPanel->height() <=
            enumPanel->height() + 2);

    registers->setCurrentIndex(openFields);
    registers->selectionModel()->select(
        openFields, QItemSelectionModel::ClearAndSelect);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(
        fields->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        enumPanel->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        fieldPanel->height() >
            enumPanel->height() + 40,
        2000);
    const QList<int> restoredSizes =
        editor->sizes();
    QCOMPARE(restoredSizes.size(), 2);
    const double expandedFraction =
        static_cast<double>(expandedSizes[1]) /
        static_cast<double>(
            expandedSizes[0] + expandedSizes[1]);
    const double restoredFraction =
        static_cast<double>(restoredSizes[1]) /
        static_cast<double>(
            restoredSizes[0] + restoredSizes[1]);
    QVERIFY(qAbs(restoredFraction -
                 expandedFraction) < 0.08);
    QVERIFY(enumPanel->y() >
            fields->geometry().bottom());

    closeFields->click();
    QTRY_VERIFY_WITH_TIMEOUT(
        !fieldPanel->isVisible(), 2000);
    registers->setCurrentIndex(scalar);
    registers->selectionModel()->select(
        scalar, QItemSelectionModel::ClearAndSelect);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(
        fieldPanel->isVisible(), 2000);
    QVERIFY(fieldPanel->height() <=
            enumPanel->height() + 2);

    controller->save();
    QTRY_VERIFY_WITH_TIMEOUT(
        !controller->isDirty(), 5000);
    QVERIFY(window.close());
    QTRY_VERIFY_WITH_TIMEOUT(
        !window.isVisible(), 2000);
    QSettings settings;
    settings.sync();
    QCOMPARE(
        settings.value(
                    QStringLiteral(
                        "ui/v1/editorSplitter"))
            .toByteArray(),
        expandedState);

    makeGeneratedFilesWritable(
        directory.path());
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
    QCOMPARE(latestRegisterEditor->text(), QStringLiteral("NEW_REGISTER_3"));
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

    workspaceMenu = openHierarchyMenu(workspaceId);
    QVERIFY(workspaceMenu != nullptr);
    QVERIFY(clickMenuAction(workspaceMenu, QStringLiteral("newPageContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    const QString pageCId =
        hierarchy->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!pageCId.isEmpty());
    QVERIFY(pageCId != pageAId);
    QVERIFY(pageCId != pageBId);
    QCOMPARE(visibleEditor(hierarchy)->text(), QStringLiteral("NEW_PAGE_2"));
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchy->model()->rowCount(
            hierarchyIndexByObjectId(hierarchy->model(), workspaceId)),
        originalPageCount + 2, 2000);

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

    pageMenu = openHierarchyMenu(pageBId);
    QVERIFY(pageMenu != nullptr);
    QVERIFY(clickMenuAction(pageMenu, QStringLiteral("newBlockContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    const QString blockCId =
        hierarchy->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!blockCId.isEmpty());
    QVERIFY(blockCId != blockAId);
    QVERIFY(blockCId != blockBId);
    QCOMPARE(visibleEditor(hierarchy)->text(), QStringLiteral("NEW_BLOCK_2"));
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchy->model()->rowCount(
            hierarchyIndexByObjectId(hierarchy->model(), pageBId)),
        pageBBlockCount + 2, 2000);

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

    blockMenu = openHierarchyMenu(blockBId);
    QVERIFY(blockMenu != nullptr);
    QVERIFY(clickMenuAction(blockMenu, QStringLiteral("newRegisterContextAction")));
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(registers) != nullptr, 2000);
    const QString registerCId =
        registers->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!registerCId.isEmpty());
    QVERIFY(registerCId != registerBId);
    QCOMPARE(visibleEditor(registers)->text(), QStringLiteral("NEW_REGISTER_2"));
    QTest::keyClick(visibleEditor(registers), Qt::Key_Escape);
    QCoreApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(),
                              blockBRegisterRows + 2, 2000);
    QVERIFY(registerExists(registerCId));
    QVERIFY(registerExists(registerBId));

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
            text.contains(QStringLiteral("2 Blocks")) &&
            text.contains(QStringLiteral("2 Registers")) &&
            dialog->defaultButton() ==
                dialog->button(QMessageBox::No);
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

void GuiSmokeTests::confirmsHierarchyDeletionImpactAndRestoresIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* hierarchy =
        window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(hierarchy != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure hierarchy deletion fixture"),
        [](regmap::Workspace& workspace) {
            auto* page = regmap::findAddressSpace(workspace, "space-main");
            auto* block =
                regmap::findRegisterBlock(workspace, "block-control");
            auto* status = regmap::findRegister(workspace, "reg-status");
            auto* ready = regmap::findField(workspace, "field-ready");
            QVERIFY(page != nullptr);
            QVERIFY(block != nullptr);
            QVERIFY(status != nullptr);
            QVERIFY(ready != nullptr);
            page->description = "Page definition.";
            block->description = "Block definition.";
            status->description = "Status register definition.";
            status->initialValue = regmap::UnsignedValue(1);
            status->resetValue = regmap::UnsignedValue(1);
            ready->resetValue = regmap::UnsignedValue(1);
            ready->description = "Ready field definition.";
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    struct DeleteInvocation {
        bool dialogSeen{false};
        bool defaultedToCancel{false};
        QString text;
    };
    const auto invokeDelete =
        [&](const QString& objectId,
            QMessageBox::StandardButton response) {
            DeleteInvocation result;
            const QModelIndex target =
                hierarchyIndexByObjectId(hierarchy->model(), objectId);
            if (!target.isValid()) {
                return result;
            }
            window.activateWindow();
            hierarchy->expandAll();
            hierarchy->setCurrentIndex(target);
            hierarchy->scrollTo(target);
            hierarchy->setFocus(Qt::OtherFocusReason);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    return;
                }
                result.dialogSeen = true;
                result.text = dialog->text();
                result.defaultedToCancel =
                    dialog->windowTitle() ==
                        QStringLiteral("Delete Register-Map Objects") &&
                    dialog->defaultButton() ==
                        dialog->button(QMessageBox::No);
                if (auto* button = dialog->button(response)) {
                    QTest::mouseClick(button, Qt::LeftButton);
                } else {
                    dialog->reject();
                }
            });
            QTest::keyClick(hierarchy, Qt::Key_Delete);
            return result;
        };

    const std::size_t initialUndoDepth = controller->undoDepth();
    const DeleteInvocation cancelledBlock =
        invokeDelete(QStringLiteral("block-control"), QMessageBox::No);
    QVERIFY(cancelledBlock.dialogSeen);
    QVERIFY(cancelledBlock.defaultedToCancel);
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("block Control")));
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("1 Register")));
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("1 Field")));
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("1 tag")));
    QVERIFY(cancelledBlock.text.contains(
        QStringLiteral("3 non-zero Initial/Reset values")));
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("3 descriptions")));
    QVERIFY(cancelledBlock.text.contains(QStringLiteral("Ctrl+Z")));
    QVERIFY(hierarchyIndexByObjectId(
                hierarchy->model(), QStringLiteral("block-control"))
                .isValid());
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Delete cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("block Control kept")));

    const DeleteInvocation deletedBlock =
        invokeDelete(QStringLiteral("block-control"), QMessageBox::Yes);
    QVERIFY(deletedBlock.dialogSeen);
    QVERIFY(deletedBlock.defaultedToCancel);
    QTRY_VERIFY_WITH_TIMEOUT(
        !hierarchyIndexByObjectId(
             hierarchy->model(), QStringLiteral("block-control"))
             .isValid(),
        2000);
    QVERIFY(hierarchyIndexByObjectId(
                hierarchy->model(), QStringLiteral("space-main"))
                .isValid());
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Deleted block Control")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        hierarchyIndexByObjectId(
            hierarchy->model(), QStringLiteral("block-control"))
            .isValid(),
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(regmap::findField(*controller->workspace(), "field-ready") !=
            nullptr);

    const DeleteInvocation cancelledPage =
        invokeDelete(QStringLiteral("space-main"), QMessageBox::No);
    QVERIFY(cancelledPage.dialogSeen);
    QVERIFY(cancelledPage.defaultedToCancel);
    QVERIFY(cancelledPage.text.contains(QStringLiteral("page Main")));
    QVERIFY(cancelledPage.text.contains(QStringLiteral("1 Block")));
    QVERIFY(cancelledPage.text.contains(QStringLiteral("1 Register")));
    QVERIFY(cancelledPage.text.contains(QStringLiteral("1 Field")));
    QVERIFY(cancelledPage.text.contains(QStringLiteral("1 tag")));
    QVERIFY(cancelledPage.text.contains(
        QStringLiteral("3 non-zero Initial/Reset values")));
    QVERIFY(cancelledPage.text.contains(QStringLiteral("4 descriptions")));
    QVERIFY(hierarchyIndexByObjectId(
                hierarchy->model(), QStringLiteral("space-main"))
                .isValid());
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("page Main kept")));

    const DeleteInvocation deletedPage =
        invokeDelete(QStringLiteral("space-main"), QMessageBox::Yes);
    QVERIFY(deletedPage.dialogSeen);
    QVERIFY(deletedPage.defaultedToCancel);
    QTRY_VERIFY_WITH_TIMEOUT(
        !hierarchyIndexByObjectId(
             hierarchy->model(), QStringLiteral("space-main"))
             .isValid(),
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        hierarchyIndexByObjectId(
            hierarchy->model(), QStringLiteral("space-main"))
            .isValid(),
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    const auto* restored =
        regmap::findRegister(*controller->workspace(), "reg-status");
    const auto* restoredField =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(restored != nullptr);
    QVERIFY(restoredField != nullptr);
    QCOMPARE(restored->description,
             std::string("Status register definition."));
    QVERIFY(restored->initialValue ==
            std::optional(regmap::UnsignedValue(1)));
    QVERIFY(restored->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QCOMPARE(restoredField->description,
             std::string("Ready field definition."));
    QVERIFY(restoredField->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::placesNewHierarchyObjectsWithoutAddressErrors()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* hierarchy =
        window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* add =
        window.findChild<QPushButton*>(QStringLiteral("hierarchyAddButton"));
    QVERIFY(controller != nullptr);
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(add != nullptr);

    const auto visibleEditor = [](QWidget* parent) -> QLineEdit* {
        const auto editors = parent->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) {
                return editor->isVisible();
            });
        return visible == editors.end() ? nullptr : *visible;
    };
    const auto selectHierarchyObject =
        [hierarchy](const QString& objectId) {
            const QModelIndex index =
                hierarchyIndexByObjectId(hierarchy->model(), objectId);
            if (!index.isValid()) {
                return false;
            }
            hierarchy->setCurrentIndex(index);
            hierarchy->scrollTo(index);
            hierarchy->setFocus(Qt::OtherFocusReason);
            QCoreApplication::processEvents();
            return true;
        };

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure a Page gap"),
        [](regmap::Workspace& workspace) {
            auto* page =
                regmap::findAddressSpace(workspace, "space-main");
            auto* block =
                regmap::findRegisterBlock(workspace, "block-control");
            QVERIFY(page != nullptr);
            QVERIFY(block != nullptr);
            page->baseAddress = 0;
            page->addressWidth = 13;
            block->baseAddress = 0x1000;
            block->size = 0x1000;
        }));
    QTRY_VERIFY_WITH_TIMEOUT(
        selectHierarchyObject(QStringLiteral("space-main")), 2000);
    QCOMPARE(add->text(), QStringLiteral("+ Block"));
    add->click();
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();

    const auto* pageAfterGap =
        regmap::findAddressSpace(
            *controller->workspace(), "space-main");
    QVERIFY(pageAfterGap != nullptr);
    QCOMPARE(pageAfterGap->blocks.size(), std::size_t{2});
    const auto gapBlock = std::ranges::find_if(
        pageAfterGap->blocks, [](const regmap::RegisterBlock& block) {
            return block.id != "block-control";
        });
    QVERIFY(gapBlock != pageAfterGap->blocks.end());
    QCOMPARE(gapBlock->baseAddress, std::uint64_t{0});
    QVERIFY(gapBlock->size.has_value());
    QCOMPARE(*gapBlock->size, std::uint64_t{0x1000});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure a small empty Page"),
        [](regmap::Workspace& workspace) {
            auto* page =
                regmap::findAddressSpace(workspace, "space-main");
            QVERIFY(page != nullptr);
            page->baseAddress = 0;
            page->addressWidth = 8;
            page->blocks.clear();
        }));
    QTRY_VERIFY_WITH_TIMEOUT(
        selectHierarchyObject(QStringLiteral("space-main")), 2000);
    QCOMPARE(add->text(), QStringLiteral("+ Block"));
    add->click();
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(hierarchy) != nullptr, 2000);
    const QString smallBlockId =
        hierarchy->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!smallBlockId.isEmpty());
    QTest::keyClick(visibleEditor(hierarchy), Qt::Key_Escape);
    QCoreApplication::processEvents();

    const auto* smallBlock =
        regmap::findRegisterBlock(
            *controller->workspace(),
            smallBlockId.toUtf8().toStdString());
    QVERIFY(smallBlock != nullptr);
    QCOMPARE(smallBlock->baseAddress, std::uint64_t{0});
    QVERIFY(smallBlock->size.has_value());
    QCOMPARE(*smallBlock->size, std::uint64_t{0x100});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(selectHierarchyObject(QStringLiteral("space-main")));
    const std::size_t fullPageUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    add->click();
    QCoreApplication::processEvents();
    const auto* fullPage =
        regmap::findAddressSpace(
            *controller->workspace(), "space-main");
    QVERIFY(fullPage != nullptr);
    QCOMPARE(fullPage->blocks.size(), std::size_t{1});
    QCOMPARE(controller->undoDepth(), fullPageUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("no free address range")));

    QVERIFY(selectHierarchyObject(smallBlockId));
    QCOMPARE(add->text(), QStringLiteral("+ Register"));
    add->click();
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(registers) != nullptr, 2000);
    QTest::keyClick(visibleEditor(registers), Qt::Key_Escape);
    QCoreApplication::processEvents();
    smallBlock =
        regmap::findRegisterBlock(
            *controller->workspace(),
            smallBlockId.toUtf8().toStdString());
    QVERIFY(smallBlock != nullptr);
    QCOMPARE(smallBlock->registers.size(), std::size_t{1});
    QCOMPARE(smallBlock->registers.front().offset, std::uint64_t{0});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Fill the small Block"),
        [smallBlockId](regmap::Workspace& workspace) {
            if (auto* block = regmap::findRegisterBlock(
                    workspace,
                    smallBlockId.toUtf8().toStdString())) {
                block->size = 4;
            }
        }));
    QCoreApplication::processEvents();
    QVERIFY(selectHierarchyObject(smallBlockId));
    const std::size_t fullBlockUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    add->click();
    QCoreApplication::processEvents();
    smallBlock =
        regmap::findRegisterBlock(
            *controller->workspace(),
            smallBlockId.toUtf8().toStdString());
    QVERIFY(smallBlock != nullptr);
    QCOMPARE(smallBlock->registers.size(), std::size_t{1});
    QCOMPARE(controller->undoDepth(), fullBlockUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot add a 32-bit Register")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Block Size")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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

void GuiSmokeTests::retainsCurrentProjectWhenReplacementCannotLoad()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("current")));
    QVERIFY(root.mkpath(QStringLiteral("broken")));
    const QString currentManifest =
        root.filePath(QStringLiteral("current/project.regmap.yaml"));
    const QString brokenManifest =
        root.filePath(QStringLiteral("broken/project.regmap.yaml"));
    createProject(currentManifest);

    QFile brokenFile(brokenManifest);
    QVERIFY(brokenFile.open(
        QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray brokenText("schema_version: [\n");
    QCOMPARE(brokenFile.write(brokenText), brokenText.size());
    brokenFile.close();

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(currentManifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(controller->workspace() != nullptr);
    const std::filesystem::path originalManifestPath = controller->manifestPath();
    const QString originalRegisterId =
        registers->currentIndex().data(Qt::UserRole + 1).toString();

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved workspace rename"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Unsaved Workspace Name";
        }));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isDirty(), 2000);

    QSignalSpy projectChanged(controller, &ProjectController::projectChanged);
    QSignalSpy statusChanged(controller, &ProjectController::syncStatusChanged);
    QVERIFY(!window.openProjectPath(brokenManifest));
    QCoreApplication::processEvents();

    QVERIFY(controller->workspace() != nullptr);
    QCOMPARE(QString::fromStdString(controller->workspace()->name),
             QStringLiteral("Unsaved Workspace Name"));
    QCOMPARE(controller->manifestPath(), originalManifestPath);
    QVERIFY(controller->isDirty());
    QCOMPARE(projectChanged.count(), 0);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             originalRegisterId);
    QVERIFY(!statusChanged.isEmpty());
    QVERIFY(statusChanged.last().at(0).toString().contains(
        QStringLiteral("current project retained")));

    makeGeneratedFilesWritable(root.filePath(QStringLiteral("current")));
}

void GuiSmokeTests::retainsCurrentProjectWhenCreationFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString currentManifest =
        directory.filePath(QStringLiteral("current.regmap.yaml"));
    createProject(currentManifest);

    const QString blockedParent =
        directory.filePath(QStringLiteral("not-a-directory"));
    QFile blocker(blockedParent);
    QVERIFY(blocker.open(
        QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray sentinel("keep this file unchanged");
    QCOMPARE(blocker.write(sentinel), sentinel.size());
    blocker.close();
    const QString failedManifest =
        QDir(blockedParent).filePath(
            QStringLiteral("failed-project.regmap.yaml"));

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(currentManifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(controller != nullptr);
    QVERIFY(controller->workspace() != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved creation failure edit"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Unsaved Workspace Kept";
        }));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->changes().size(), std::size_t{1});
    const std::size_t undoDepth = controller->undoDepth();
    const std::filesystem::path originalManifest =
        controller->manifestPath();

    QSignalSpy projectChanged(
        controller, &ProjectController::projectChanged);
    QSignalSpy statusChanged(
        controller, &ProjectController::syncStatusChanged);
    QVERIFY(!controller->createProject(failedManifest));
    QCoreApplication::processEvents();

    QCOMPARE(projectChanged.count(), 0);
    QVERIFY(!statusChanged.isEmpty());
    const QString failure = statusChanged.last().at(0).toString();
    QVERIFY(failure.contains(
        QStringLiteral("failed-project.regmap.yaml")));
    QVERIFY(failure.contains(QStringLiteral("Cannot create")));
    QVERIFY(failure.contains(QStringLiteral("current project")));
    QVERIFY(failure.contains(
        QStringLiteral("unsaved Workbench edits retained")));
    QCOMPARE(window.statusBar()->currentMessage(), failure);

    QCOMPARE(controller->manifestPath(), originalManifest);
    QVERIFY(controller->workspace() != nullptr);
    QCOMPARE(controller->workspace()->name,
             std::string("Unsaved Workspace Kept"));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(controller->canUndo());
    QVERIFY(!QFileInfo::exists(failedManifest));

    QVERIFY(blocker.open(QIODevice::ReadOnly));
    QCOMPARE(blocker.readAll(), sentinel);
    blocker.close();

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::reportsExplicitOpenFailure()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("current")));
    QVERIFY(root.mkpath(QStringLiteral("broken")));
    const QString currentManifest =
        root.filePath(
            QStringLiteral("current/current.regmap.yaml"));
    const QString brokenManifest =
        root.filePath(
            QStringLiteral("broken/broken.regmap.yaml"));
    createProject(currentManifest);
    QFile brokenFile(brokenManifest);
    QVERIFY(brokenFile.open(
        QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray brokenText("schema_version: [\n");
    QCOMPARE(brokenFile.write(brokenText), brokenText.size());
    brokenFile.close();

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    QVERIFY(window.openProjectPath(currentManifest));
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* openProject =
        window.findChild<QAction*>(QStringLiteral("openProjectAction"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(openProject != nullptr);
    const std::filesystem::path originalManifest =
        controller->manifestPath();
    const QString selectedRegister =
        registers->currentIndex().data(Qt::UserRole + 1).toString();
    QSignalSpy projectChanged(
        controller, &ProjectController::projectChanged);

    bool pickerSeen = false;
    bool errorSeen = false;
    bool recoveryDescribed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* picker = qobject_cast<QFileDialog*>(
            QApplication::activeModalWidget());
        if (picker == nullptr) {
            return;
        }
        pickerSeen = true;
        picker->setDirectory(
            QFileInfo(brokenManifest).absolutePath());
        picker->selectFile(brokenManifest);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            if (dialog == nullptr) {
                return;
            }
            errorSeen = true;
            recoveryDescribed =
                dialog->windowTitle() ==
                    QStringLiteral("Open Project") &&
                dialog->icon() == QMessageBox::Critical &&
                dialog->text().contains(
                    QStringLiteral("broken.regmap.yaml")) &&
                dialog->text().contains(
                    QStringLiteral("Cannot parse manifest")) &&
                dialog->text().contains(
                    QStringLiteral("current project retained")) &&
                dialog->defaultButton() ==
                    dialog->button(QMessageBox::Ok);
            QTest::mouseClick(
                dialog->button(QMessageBox::Ok), Qt::LeftButton);
        });
        QVERIFY(QMetaObject::invokeMethod(
            picker, "accept", Qt::DirectConnection));
    });
    openProject->trigger();

    QVERIFY(pickerSeen);
    QVERIFY(errorSeen);
    QVERIFY(recoveryDescribed);
    QCOMPARE(projectChanged.count(), 0);
    QCOMPARE(controller->manifestPath(), originalManifest);
    QVERIFY(controller->workspace() != nullptr);
    QVERIFY(!controller->isDirty());
    QCOMPARE(registers->currentIndex()
                 .data(Qt::UserRole + 1)
                 .toString(),
             selectedRegister);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("broken.regmap.yaml")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("current project retained")));

    makeGeneratedFilesWritable(
        root.filePath(QStringLiteral("current")));
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

void GuiSmokeTests::confirmsDiscardBeforeReloadingDirtyProject()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* hierarchy =
        window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    QVERIFY(controller != nullptr);
    QVERIFY(hierarchy != nullptr);
    QAction* reload = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Reload from Disk")) {
            reload = action;
            break;
        }
    }
    QVERIFY(reload != nullptr);
    QVERIFY(reload->isEnabled());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved workspace rename"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Local Unsaved Workspace";
        }));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->changes().size(), std::size_t{1});
    const std::size_t dirtyUndoDepth = controller->undoDepth();
    QVERIFY(dirtyUndoDepth > 0);

    QFile projectFile(manifest);
    QVERIFY(projectFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QString projectText = QString::fromUtf8(projectFile.readAll());
    projectFile.close();
    const QString originalPageName = QStringLiteral("      name: Main\n");
    QCOMPARE(projectText.count(originalPageName), 1);
    projectText.replace(
        originalPageName,
        QStringLiteral("      name: Main Reloaded Safely\n"));
    const QByteArray updatedProject = projectText.toUtf8();
    QVERIFY(projectFile.open(
        QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(projectFile.write(updatedProject), updatedProject.size());
    projectFile.close();

    struct ReloadInvocation {
        bool dialogSeen{false};
        bool impactDescribed{false};
    };
    const auto invokeReload =
        [&](bool confirm) {
            ReloadInvocation result;
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    return;
                }
                result.dialogSeen = true;
                auto* proceed = dialog->findChild<QPushButton*>(
                    QStringLiteral("confirmReloadFromDiskButton"));
                result.impactDescribed =
                    dialog->windowTitle() ==
                        QStringLiteral("Reload Project") &&
                    dialog->text().contains(
                        QStringLiteral("1 unsaved Workbench change")) &&
                    dialog->text().contains(
                        QStringLiteral("project.regmap.yaml")) &&
                    dialog->informativeText().contains(
                        QStringLiteral("Undo history")) &&
                    dialog->informativeText().contains(
                        QStringLiteral("Use Save & Sync first")) &&
                    proceed != nullptr &&
                    proceed->text() ==
                        QStringLiteral("Discard and Reload") &&
                    dialog->defaultButton() ==
                        dialog->button(QMessageBox::Cancel);
                if (confirm && proceed != nullptr) {
                    QTest::mouseClick(proceed, Qt::LeftButton);
                } else {
                    QTest::mouseClick(
                        dialog->button(QMessageBox::Cancel),
                        Qt::LeftButton);
                }
            });
            reload->trigger();
            return result;
        };

    const ReloadInvocation cancelled = invokeReload(false);
    QVERIFY(cancelled.dialogSeen);
    QVERIFY(cancelled.impactDescribed);
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->undoDepth(), dirtyUndoDepth);
    QCOMPARE(
        controller->workspace()->name,
        std::string("Local Unsaved Workspace"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Reload cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unsaved Workbench edits kept")));

    const ReloadInvocation confirmed = invokeReload(true);
    QVERIFY(confirmed.dialogSeen);
    QVERIFY(confirmed.impactDescribed);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->workspace()->name,
        std::string("GUI Workspace"), 3000);
    QCOMPARE(controller->undoDepth(), std::size_t{0});
    QVERIFY(!controller->canUndo());
    const QModelIndex workspaceIndex =
        hierarchy->model()->index(0, 0);
    const QModelIndex pageIndex =
        hierarchy->model()->index(0, 0, workspaceIndex);
    QTRY_COMPARE_WITH_TIMEOUT(
        pageIndex.data().toString(),
        QStringLiteral("Main Reloaded Safely"), 3000);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto diskProject =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(diskProject.workspace.has_value());
    QCOMPARE(diskProject.workspace->name,
             std::string("GUI Workspace"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::protectsUnsavedChangesWhenClosing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("close-protection.regmap.yaml"));
    const QString rtlPath =
        directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* state =
        window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    QVERIFY(controller != nullptr);
    QVERIFY(state != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved close edit"),
        [](regmap::Workspace& workspace) {
            regmap::findRegister(workspace, "reg-status")->description =
                "Unsaved close description";
        }));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->changes().size(), std::size_t{1});
    const std::size_t undoDepth = controller->undoDepth();

    struct CloseInvocation {
        bool dialogSeen{false};
        bool impactDescribed{false};
        bool closed{false};
    };
    const auto invokeClose =
        [&](QMessageBox::StandardButton response) {
            CloseInvocation result;
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    return;
                }
                result.dialogSeen = true;
                auto* save = dialog->findChild<QPushButton*>(
                    QStringLiteral("saveUnsavedChangesButton"));
                auto* discard = dialog->findChild<QPushButton*>(
                    QStringLiteral("discardUnsavedChangesButton"));
                auto* cancel = dialog->findChild<QPushButton*>(
                    QStringLiteral("cancelUnsavedChangesButton"));
                result.impactDescribed =
                    dialog->windowTitle() ==
                        QStringLiteral("Unsaved Register Map") &&
                    dialog->text().contains(
                        QStringLiteral("1 unsaved Workbench change")) &&
                    dialog->text().contains(
                        QStringLiteral("close-protection.regmap.yaml")) &&
                    dialog->text().contains(QStringLiteral("before closing")) &&
                    dialog->informativeText().contains(
                        QStringLiteral("Save & Sync")) &&
                    dialog->informativeText().contains(
                        QStringLiteral("Undo history")) &&
                    dialog->informativeText().contains(
                        QStringLiteral("cannot be recovered")) &&
                    save != nullptr && discard != nullptr &&
                    cancel != nullptr &&
                    dialog->defaultButton() == save &&
                    dialog->escapeButton() == cancel;
                if (auto* button = dialog->button(response)) {
                    QTest::mouseClick(button, Qt::LeftButton);
                } else {
                    dialog->reject();
                }
            });
            result.closed = window.close();
            return result;
        };

    const CloseInvocation cancelled =
        invokeClose(QMessageBox::Cancel);
    QVERIFY(cancelled.dialogSeen);
    QVERIFY(cancelled.impactDescribed);
    QVERIFY(!cancelled.closed);
    QVERIFY(window.isVisible());
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Close cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 unsaved Workbench change kept")));

    editManagedRtlValue(
        rtlPath, QStringLiteral("reg-status"),
        QStringLiteral("offset"), QStringLiteral("64'hx"));
    const CloseInvocation blockedSave =
        invokeClose(QMessageBox::Save);
    QVERIFY(blockedSave.dialogSeen);
    QVERIFY(blockedSave.impactDescribed);
    QVERIFY(!blockedSave.closed);
    QVERIFY(window.isVisible());
    QVERIFY(controller->isDirty());
    QTRY_VERIFY_WITH_TIMEOUT(controller->hasProjectErrors(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().startsWith(QStringLiteral("Blocked")), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Workbench edits remain unsaved")));

    const auto unchanged =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(unchanged.workspace.has_value());
    QCOMPARE(regmap::findRegister(*unchanged.workspace, "reg-status")
                 ->description,
             std::string{});

    const CloseInvocation discarded =
        invokeClose(QMessageBox::Discard);
    QVERIFY(discarded.dialogSeen);
    QVERIFY(discarded.impactDescribed);
    QVERIFY(discarded.closed);
    QVERIFY(!window.isVisible());
    QVERIFY(controller->isDirty());
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")
                 ->description,
             std::string("Unsaved close description"));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsUnsavedChangesBeforeReplacingProject()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("current")));
    QVERIFY(root.mkpath(QStringLiteral("replacement")));
    const QString currentManifest =
        root.filePath(
            QStringLiteral("current/current-project.regmap.yaml"));
    const QString replacementManifest =
        root.filePath(
            QStringLiteral("replacement/replacement-project.regmap.yaml"));
    createProject(currentManifest);
    createProject(replacementManifest, 8);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(currentManifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(controller != nullptr);
    QAction* openProject = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text().startsWith(QStringLiteral("Open Project"))) {
            openProject = action;
            break;
        }
    }
    QVERIFY(openProject != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved replacement edit"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Unsaved Current Workspace";
        }));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->changes().size(), std::size_t{1});
    const std::size_t undoDepth = controller->undoDepth();

    struct ReplacementInvocation {
        bool pickerSeen{false};
        bool dialogSeen{false};
        bool impactDescribed{false};
    };
    const auto invokeOpen =
        [&](QMessageBox::StandardButton response) {
            ReplacementInvocation result;
            QTimer::singleShot(0, &window, [&] {
                auto* picker =
                    qobject_cast<QFileDialog*>(
                        QApplication::activeModalWidget());
                if (picker == nullptr) {
                    return;
                }
                result.pickerSeen = true;
                picker->setDirectory(
                    QFileInfo(replacementManifest).absolutePath());
                picker->selectFile(replacementManifest);
                QTimer::singleShot(0, &window, [&] {
                    auto* dialog =
                        qobject_cast<QMessageBox*>(
                            QApplication::activeModalWidget());
                    if (dialog == nullptr) {
                        return;
                    }
                    result.dialogSeen = true;
                    auto* save = dialog->findChild<QPushButton*>(
                        QStringLiteral("saveUnsavedChangesButton"));
                    auto* discard = dialog->findChild<QPushButton*>(
                        QStringLiteral("discardUnsavedChangesButton"));
                    auto* cancel = dialog->findChild<QPushButton*>(
                        QStringLiteral("cancelUnsavedChangesButton"));
                    result.impactDescribed =
                        dialog->windowTitle() ==
                            QStringLiteral("Unsaved Register Map") &&
                        dialog->text().contains(
                            QStringLiteral("1 unsaved Workbench change")) &&
                        dialog->text().contains(
                            QStringLiteral("current-project.regmap.yaml")) &&
                        dialog->text().contains(
                            QStringLiteral("opening another project")) &&
                        dialog->informativeText().contains(
                            QStringLiteral("Save & Sync")) &&
                        dialog->informativeText().contains(
                            QStringLiteral("Undo history")) &&
                        dialog->informativeText().contains(
                            QStringLiteral("cannot be recovered")) &&
                        save != nullptr && discard != nullptr &&
                        cancel != nullptr &&
                        dialog->defaultButton() == save &&
                        dialog->escapeButton() == cancel;
                    if (auto* button = dialog->button(response)) {
                        QTest::mouseClick(button, Qt::LeftButton);
                    } else {
                        dialog->reject();
                    }
                });
                QVERIFY(QMetaObject::invokeMethod(
                    picker, "accept", Qt::DirectConnection));
            });
            openProject->trigger();
            return result;
        };

    const ReplacementInvocation cancelled =
        invokeOpen(QMessageBox::Cancel);
    QVERIFY(cancelled.pickerSeen);
    QVERIFY(cancelled.dialogSeen);
    QVERIFY(cancelled.impactDescribed);
    QCOMPARE(controller->manifestPath(),
             std::filesystem::path(currentManifest.toStdWString()));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(controller->workspace()->name,
             std::string("Unsaved Current Workspace"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Project change cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 unsaved Workbench change kept")));

    const ReplacementInvocation discarded =
        invokeOpen(QMessageBox::Discard);
    QVERIFY(discarded.pickerSeen);
    QVERIFY(discarded.dialogSeen);
    QVERIFY(discarded.impactDescribed);
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->manifestPath(),
        std::filesystem::path(replacementManifest.toStdWString()), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 3000);
    QCOMPARE(controller->workspace()->name,
             std::string("GUI Workspace"));
    QCOMPARE(controller->undoDepth(), std::size_t{0});

    const auto currentDisk =
        regmap::openProject(
            std::filesystem::path(currentManifest.toStdWString()));
    QVERIFY(currentDisk.workspace.has_value());
    QCOMPARE(currentDisk.workspace->name, std::string("GUI Workspace"));

    makeGeneratedFilesWritable(root.filePath(QStringLiteral("current")));
    makeGeneratedFilesWritable(
        root.filePath(QStringLiteral("replacement")));
}

void GuiSmokeTests::preflightsActiveEditorBeforeProjectChoosers()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* newProject =
        window.findChild<QAction*>(QStringLiteral("newProjectAction"));
    auto* openProject =
        window.findChild<QAction*>(QStringLiteral("openProjectAction"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(newProject != nullptr);
    QVERIFY(openProject != nullptr);
    QVERIFY(!controller->isDirty());

    const auto visibleEditor = [registers]() -> QLineEdit* {
        const auto editors = registers->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) {
                return editor->isVisible();
            });
        return visible == editors.end() ? nullptr : *visible;
    };
    const auto startEdit =
        [&](const QModelIndex& index, const QString& text) {
            registers->setCurrentIndex(index);
            registers->scrollTo(index);
            registers->setFocus(Qt::OtherFocusReason);
            registers->edit(index);
            QCoreApplication::processEvents();
            QTRY_VERIFY_WITH_TIMEOUT(visibleEditor() != nullptr, 2000);
            QLineEdit* editor = visibleEditor();
            editor->selectAll();
            QTest::keyClicks(editor, text);
            QCOMPARE(editor->text(), text);
        };
    const auto invokeWithoutChooser =
        [&](QAction* action) {
            bool chooserSeen = false;
            QTimer::singleShot(0, &window, [&] {
                if (auto* picker = qobject_cast<QFileDialog*>(
                        QApplication::activeModalWidget())) {
                    chooserSeen = true;
                    picker->reject();
                }
            });
            action->trigger();
            QCoreApplication::processEvents();
            return chooserSeen;
        };

    const QModelIndex width = registers->model()->index(0, 3);
    startEdit(width, QStringLiteral("invalid-width"));
    QVERIFY(!invokeWithoutChooser(newProject));
    QCOMPARE(registers->model()->index(0, 3).data().toString(),
             QStringLiteral("32"));
    QVERIFY(!controller->isDirty());
    QCOMPARE(controller->manifestPath(),
             std::filesystem::path(manifest.toStdWString()));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Edit rejected: expected")));
    QVERIFY(QApplication::activeModalWidget() == nullptr);

    startEdit(registers->model()->index(0, 3),
              QStringLiteral("still-invalid"));
    QVERIFY(!invokeWithoutChooser(openProject));
    QCOMPARE(registers->model()->index(0, 3).data().toString(),
             QStringLiteral("32"));
    QVERIFY(!controller->isDirty());
    QCOMPARE(controller->manifestPath(),
             std::filesystem::path(manifest.toStdWString()));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Edit rejected: expected")));
    QVERIFY(QApplication::activeModalWidget() == nullptr);

    const QModelIndex description =
        registers->model()->index(0, 11);
    const QString retainedDescription =
        QStringLiteral("Valid edit retained after cancelling Open");
    startEdit(description, retainedDescription);

    bool chooserSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto* picker = qobject_cast<QFileDialog*>(
            QApplication::activeModalWidget());
        if (picker == nullptr) {
            return;
        }
        chooserSeen = true;
        picker->reject();
    });
    openProject->trigger();
    QVERIFY(chooserSeen);
    QVERIFY(controller->isDirty());
    QCOMPARE(registers->model()->index(0, 11).data().toString(),
             retainedDescription);
    QCOMPARE(QString::fromStdString(
                 regmap::findRegister(*controller->workspace(), "reg-status")
                     ->description),
             retainedDescription);
    QCOMPARE(controller->manifestPath(),
             std::filesystem::path(manifest.toStdWString()));
    QVERIFY(controller->canUndo());

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 2000);
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::startsProjectChoosersInUsefulDirectories()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    const auto chooserDirectory =
        [](MainWindow& window, QAction* action) {
            QString directory;
            QTimer::singleShot(0, &window, [&] {
                auto* picker = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget());
                if (picker == nullptr) {
                    return;
                }
                directory = picker->directory().absolutePath();
                picker->reject();
            });
            action->trigger();
            return QDir::cleanPath(directory);
        };

    QTemporaryDir projectRoot;
    QVERIFY(projectRoot.isValid());
    QDir root(projectRoot.path());
    QVERIFY(root.mkpath(QStringLiteral("maps/device")));
    const QString manifest =
        root.filePath(
            QStringLiteral("maps/device/project.regmap.yaml"));
    createProject(manifest);

    MainWindow projectWindow;
    projectWindow.resize(1100, 720);
    projectWindow.show();
    QVERIFY(projectWindow.openProjectPath(manifest));
    QTest::qWait(50);
    auto* newProject = projectWindow.findChild<QAction*>(
        QStringLiteral("newProjectAction"));
    auto* openProject = projectWindow.findChild<QAction*>(
        QStringLiteral("openProjectAction"));
    QVERIFY(newProject != nullptr);
    QVERIFY(openProject != nullptr);
    const QString expectedProjectDirectory =
        QDir::cleanPath(QFileInfo(manifest).absolutePath());
    QCOMPARE(
        chooserDirectory(projectWindow, newProject),
        expectedProjectDirectory);
    QCOMPARE(
        chooserDirectory(projectWindow, openProject),
        expectedProjectDirectory);

    MainWindow emptyWindow;
    emptyWindow.resize(900, 600);
    emptyWindow.show();
    QTest::qWait(50);
    auto* emptyNew = emptyWindow.findChild<QAction*>(
        QStringLiteral("newProjectAction"));
    auto* emptyOpen = emptyWindow.findChild<QAction*>(
        QStringLiteral("openProjectAction"));
    QVERIFY(emptyNew != nullptr);
    QVERIFY(emptyOpen != nullptr);
    const QString expectedDocuments = QDir::cleanPath(
        QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation));
    QVERIFY(!expectedDocuments.isEmpty());
    QCOMPARE(
        chooserDirectory(emptyWindow, emptyNew),
        expectedDocuments);
    QCOMPARE(
        chooserDirectory(emptyWindow, emptyOpen),
        expectedDocuments);

    makeGeneratedFilesWritable(projectRoot.path());
}

void GuiSmokeTests::opensAndCleansRecentProjectsSafely()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    constexpr auto settingsKey = "projects/recent";

    struct SettingsScope {
        QString organization =
            QCoreApplication::organizationName();
        QString application =
            QCoreApplication::applicationName();
        QString key;

        explicit SettingsScope(QString settingsKey)
            : key(settingsKey)
        {
            QCoreApplication::setOrganizationName(
                QStringLiteral("RegMapWorkbenchTests"));
            QCoreApplication::setApplicationName(
                QStringLiteral("RecentProjectsTest"));
            QSettings settings;
            settings.remove(key);
            settings.sync();
        }

        ~SettingsScope()
        {
            QSettings settings;
            settings.remove(key);
            settings.sync();
            QCoreApplication::setOrganizationName(organization);
            QCoreApplication::setApplicationName(application);
        }
    } settingsScope(QString::fromLatin1(settingsKey));

    QSettings settings;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("current")));
    QVERIFY(root.mkpath(QStringLiteral("recent")));
    const QString currentManifest =
        root.filePath(
            QStringLiteral("current/current.regmap.yaml"));
    const QString recentManifest =
        root.filePath(
            QStringLiteral("recent/recent.regmap.yaml"));
    const QString missingManifest =
        root.filePath(
            QStringLiteral("missing/missing.regmap.yaml"));
    createProject(currentManifest);
    createProject(recentManifest, 8);
    const QString normalizedRecent =
        QDir::cleanPath(
            QFileInfo(recentManifest).absoluteFilePath());
    const QString normalizedMissing =
        QDir::cleanPath(
            QFileInfo(missingManifest).absoluteFilePath());
    settings.setValue(
        QString::fromLatin1(settingsKey),
        QStringList{normalizedMissing});
    settings.sync();

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    QVERIFY(window.openProjectPath(currentManifest));
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* openProject =
        window.findChild<QAction*>(QStringLiteral("openProjectAction"));
    auto* recentMenu =
        window.findChild<QMenu*>(QStringLiteral("recentProjectsMenu"));
    QVERIFY(controller != nullptr);
    QVERIFY(openProject != nullptr);
    QVERIFY(recentMenu != nullptr);

    const auto recentAction =
        [&](const QString& path) -> QAction* {
            const auto actions = recentMenu->actions();
            const auto match = std::ranges::find_if(
                actions, [&](const QAction* action) {
                    return action->objectName() ==
                               QStringLiteral("recentProjectAction") &&
                        action->data().toString().compare(
                            path, Qt::CaseInsensitive) == 0;
                });
            return match == actions.end() ? nullptr : *match;
        };

    bool pickerSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto* picker = qobject_cast<QFileDialog*>(
            QApplication::activeModalWidget());
        if (picker == nullptr) {
            return;
        }
        pickerSeen = true;
        picker->setDirectory(
            QFileInfo(recentManifest).absolutePath());
        picker->selectFile(recentManifest);
        QVERIFY(QMetaObject::invokeMethod(
            picker, "accept", Qt::DirectConnection));
    });
    openProject->trigger();
    QVERIFY(pickerSeen);
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->manifestPath(),
        std::filesystem::path(
            recentManifest.toStdWString()), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        recentAction(normalizedRecent) != nullptr, 2000);
    QAction* recordedRecent = recentAction(normalizedRecent);
    QCOMPARE(
        recentMenu->actions().front()->data().toString(),
        normalizedRecent);

    QVERIFY(window.openProjectPath(currentManifest));
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Unsaved recent-project edit"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Unsaved Current Workspace";
        }));
    QVERIFY(controller->isDirty());
    const std::size_t undoDepth = controller->undoDepth();

    bool cancelSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        cancelSeen = true;
        QTest::mouseClick(
            dialog->button(QMessageBox::Cancel), Qt::LeftButton);
    });
    recordedRecent = recentAction(normalizedRecent);
    QVERIFY(recordedRecent != nullptr);
    recordedRecent->trigger();
    QVERIFY(cancelSeen);
    QCOMPARE(
        controller->manifestPath(),
        std::filesystem::path(
            currentManifest.toStdWString()));
    QVERIFY(controller->isDirty());
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(controller->workspace()->name,
             std::string("Unsaved Current Workspace"));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 2000);
    recordedRecent = recentAction(normalizedRecent);
    QVERIFY(recordedRecent != nullptr);
    recordedRecent->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->manifestPath(),
        std::filesystem::path(
            recentManifest.toStdWString()), 3000);
    QVERIFY(!controller->isDirty());

    QAction* missing = recentAction(normalizedMissing);
    QVERIFY(missing != nullptr);
    bool missingDialogSeen = false;
    bool missingRecoveryDescribed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        missingDialogSeen = true;
        missingRecoveryDescribed =
            dialog->windowTitle() ==
                QStringLiteral("Recent Project Missing") &&
            dialog->text().contains(
                QStringLiteral("missing.regmap.yaml")) &&
            dialog->text().contains(
                QStringLiteral("removed from Open Recent")) &&
            dialog->text().contains(
                QStringLiteral("current project is unchanged"));
        QTest::mouseClick(
            dialog->button(QMessageBox::Ok), Qt::LeftButton);
    });
    const std::filesystem::path beforeMissing =
        controller->manifestPath();
    missing->trigger();
    QVERIFY(missingDialogSeen);
    QVERIFY(missingRecoveryDescribed);
    QCOMPARE(controller->manifestPath(), beforeMissing);
    QTRY_VERIFY_WITH_TIMEOUT(
        recentAction(normalizedMissing) == nullptr, 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("entry removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("current project unchanged")));

    settings.sync();
    const QStringList stored =
        settings.value(
                    QString::fromLatin1(settingsKey))
            .toStringList();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.front(), normalizedRecent);

    auto* clearRecent = recentMenu->findChild<QAction*>(
        QStringLiteral("clearRecentProjectsAction"));
    QVERIFY(clearRecent != nullptr);
    clearRecent->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        recentAction(normalizedRecent) == nullptr, 2000);
    settings.sync();
    QVERIFY(settings
                .value(QString::fromLatin1(settingsKey))
                .toStringList()
                .isEmpty());
    QCOMPARE(recentMenu->actions().size(), 1);
    QVERIFY(!recentMenu->actions().front()->isEnabled());
    QCOMPARE(recentMenu->actions().front()->text(),
             QStringLiteral("(No recent projects)"));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Recent projects cleared")));

    makeGeneratedFilesWritable(
        root.filePath(QStringLiteral("current")));
    makeGeneratedFilesWritable(
        root.filePath(QStringLiteral("recent")));
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

void GuiSmokeTests::navigatesEnumValuesFromSearchAndProblems()
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
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("globalSearchEdit"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(problems != nullptr);
    QVERIFY(search != nullptr);

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
                enumValue("enum-register-disabled", "DISABLED", 0),
                enumValue("enum-register-enabled", "ENABLED", 0),
            };

            auto& fieldEnum = fieldRegister->fields.front();
            fieldEnum.type = regmap::FieldType::enumeration;
            fieldEnum.enumValues = {
                enumValue("enum-field-low", "LOW", 0),
                enumValue("enum-field-high", "HIGH", 0),
            };
        }));

    search->setText(QStringLiteral("ENABLED"));
    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("enum-register-enabled"), 2000);

    search->setText(QStringLiteral("HIGH"));
    Q_EMIT search->returnPressed();
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
        problemRow(QStringLiteral("enum-register-enabled")) >= 0, 2000);
    const int registerProblem = problemRow(QStringLiteral("enum-register-enabled"));
    Q_EMIT problems->doubleClicked(problems->model()->index(registerProblem, 2));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("enum-register-enabled"), 2000);

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
    const auto acceptTypeCleanup = [&window] {
        QTimer::singleShot(0, &window, [] {
            auto* dialog =
                qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog == nullptr) {
                return;
            }
            if (auto* confirm = dialog->button(QMessageBox::Yes)) {
                QTest::mouseClick(confirm, Qt::LeftButton);
            } else {
                dialog->reject();
            }
        });
    };

    pageBase->setText(QStringLiteral("0x1000"));
    Q_EMIT pageBase->editingFinished();
    blockBase->setText(QStringLiteral("0x20"));
    Q_EMIT blockBase->editingFinished();
    QCOMPARE(registers->model()->index(0, 2).data().toString(), QStringLiteral("0x1020"));

    const int initialRegisterRows = registers->model()->rowCount();
    Q_EMIT registers->clicked(
        registers->model()->index(initialRegisterRows - 1, 11));
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->rowCount(), initialRegisterRows);

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
    acceptTypeCleanup();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4), QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(registers->model()->index(1, 6).data().toString().isEmpty(), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Range bounds removed")));
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
    acceptTypeCleanup();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    const auto visibleEnumEditor = [enums]() -> QLineEdit* {
        const auto editors = enums->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QCOMPARE(enums->model()->index(0, 0).data().toString(), QStringLiteral("NEW_VALUE"));
    QCOMPARE(enums->model()->index(0, 1).data().toString(), QStringLiteral("0x0"));
    const QString enumId =
        enums->model()->index(0, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(!enumId.isEmpty());
    Q_EMIT enums->clicked(enums->model()->index(0, 2));
    QCoreApplication::processEvents();
    QCOMPARE(enums->model()->rowCount(), 2);
    QCOMPARE(visibleEnumEditor(), nullptr);
    Q_EMIT enums->clicked(enums->model()->index(0, 0));
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
    int guardedEnumRow = -1;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        const QString candidate =
            enums->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        if (!candidate.isEmpty() && candidate != enumId) {
            guardedEnumId = candidate;
            guardedEnumRow = row;
        }
    }
    QVERIFY(!guardedEnumId.isEmpty());
    QVERIFY(guardedEnumRow >= 0);
    QCOMPARE(enums->model()->index(guardedEnumRow, 0).data().toString(),
             QStringLiteral("NEW_VALUE_2"));
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
    acceptTypeCleanup();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("field"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);
    QCOMPARE(enums->model()->rowCount(), 0);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Enum values removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("NEW_FIELD created")));
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
    acceptTypeCleanup();
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

    acceptTypeCleanup();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("field"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!fields->isVisible(), 2000);
    QCOMPARE(fields->model()->rowCount(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 5).data().toString(),
                              QStringLiteral("Open (1)"), 2000);
    Q_EMIT registers->clicked(registers->model()->index(1, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 2, 2000);
    const auto visibleFieldEditor = [fields]() -> QLineEdit* {
        const auto editors = fields->findChildren<QLineEdit*>();
        const auto visible = std::ranges::find_if(
            editors, [](const QLineEdit* editor) { return editor->isVisible(); });
        return visible == editors.end() ? nullptr : *visible;
    };
    QCOMPARE(fields->model()->index(0, 0).data().toString(), QStringLiteral("NEW_FIELD"));
    QVERIFY(visibleFieldEditor() == nullptr);

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

    acceptTypeCleanup();
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

    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 3, 2000);
    const QString memberFieldId =
        fields->model()->index(1, 0).data(Qt::UserRole + 1).toString();
    QVERIFY(!memberFieldId.isEmpty());
    QVERIFY(memberFieldId != parentFieldId);
    QCOMPARE(fields->model()->index(1, 0).data().toString(),
             QStringLiteral("NEW_MEMBER"));
    QCOMPARE(fields->model()->index(1, 5).data().toString(),
             QStringLiteral("bits"));
    QVERIFY(visibleFieldEditor() == nullptr);

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
    const int guardedMemberRow = fieldRowForId(guardedFieldId);
    QVERIFY(guardedMemberRow >= 0);
    QCOMPARE(fields->model()->index(guardedMemberRow, 0).data().toString(),
             QStringLiteral("NEW_MEMBER_2"));

    int memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    fields->setCurrentIndex(fields->model()->index(memberRow, 0));
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QCOMPARE(enums->model()->index(0, 0).data().toString(),
             QStringLiteral("NEW_VALUE"));
    QCOMPARE(enums->model()->index(0, 1).data().toString(),
             QStringLiteral("0x0"));
    const QString guardedFieldEnumId =
        enums->model()->index(0, 0).data(Qt::UserRole + 1).toString();
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
    QCOMPARE(fieldEnumNameEditor->text(), QStringLiteral("NEW_VALUE_2"));
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
    acceptTypeCleanup();
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
    acceptTypeCleanup();
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("bits")));
    QTRY_VERIFY_WITH_TIMEOUT(!enums->isVisible(), 2000);

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 5),
                                     QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QCOMPARE(enums->model()->index(0, 0).data().toString(),
             QStringLiteral("NEW_VALUE"));
    QCOMPARE(enums->model()->index(0, 1).data().toString(),
             QStringLiteral("0x0"));

    memberRow = fieldRowForId(memberFieldId);
    QVERIFY(memberRow >= 0);
    acceptTypeCleanup();
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
    acceptTypeCleanup();
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
    bool compoundCancelSeen = false;
    QString compoundCancelFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            compoundCancelFailure = QStringLiteral("Compound confirmation did not open");
            return;
        }
        compoundCancelSeen = true;
        if (dialog->windowTitle() != QStringLiteral("Change Compound Field Type")) {
            compoundCancelFailure = QStringLiteral("Unexpected compound confirmation");
        }
        if (auto* cancel = dialog->button(QMessageBox::No)) {
            QTest::mouseClick(cancel, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(fields->model()->setData(fields->model()->index(parentRowBeforeRejectedType, 5),
                                     QStringLiteral("bits")));
    QCoreApplication::processEvents();
    QVERIFY2(compoundCancelFailure.isEmpty(), qPrintable(compoundCancelFailure));
    QVERIFY(compoundCancelSeen);
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()->index(fieldRowForId(parentFieldId), 5).data().toString(),
        QStringLiteral("field"), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("kept")));

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
                              QStringLiteral("NONE"), 2000);
    QCOMPARE(fields->model()->index(memberRow, 9).data().toString(),
             QStringLiteral("NONE"));
    QCOMPARE(fields->model()->index(memberRow, 11).data().toString(),
             QStringLiteral("none"));
    QCOMPARE(fields->model()->index(memberRow, 12).data().toString(),
             QStringLiteral("none"));

    const int secondFieldAddRow = fields->model()->rowCount() - 1;
    Q_EMIT fields->clicked(fields->model()->index(secondFieldAddRow, 0));
    QTRY_VERIFY_WITH_TIMEOUT(visibleFieldEditor() != nullptr, 2000);
    auto* secondFieldNameEditor = visibleFieldEditor();
    QCOMPARE(secondFieldNameEditor->text(), QStringLiteral("NEW_FIELD_2"));
    const QString secondFieldId =
        fields->currentIndex().data(Qt::UserRole + 1).toString();
    QVERIFY(!secondFieldId.isEmpty());
    QTest::keyClick(secondFieldNameEditor, Qt::Key_Escape);
    QCoreApplication::processEvents();

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsRegisterFieldConversionImmediatelyUsable()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    const std::size_t initialUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("field")));
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control") != nullptr &&
            regmap::findRegister(*controller->workspace(), "reg-control")->fields.size() == 1,
        2000);
    const auto* reg =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(reg != nullptr);
    QCOMPARE(reg->type, regmap::FieldType::structure);
    const std::string firstFieldId = reg->fields.front().id;
    QVERIFY(!firstFieldId.empty());
    QCOMPARE(QString::fromStdString(reg->fields.front().name),
             QStringLiteral("NEW_FIELD"));
    QCOMPARE(reg->fields.front().lsb, 0U);
    QCOMPARE(reg->fields.front().msb, 0U);
    QCOMPARE(reg->fields.front().softwareAccess, regmap::AccessMode::readWrite);
    QCOMPARE(reg->fields.front().hardwareAccess, regmap::AccessMode::none);
    QCOMPARE(reg->fields.front().resetValue,
             std::optional(regmap::UnsignedValue(0)));
    QCOMPARE(reg->fields.front().writeSideEffect,
             regmap::WriteSideEffect::write);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 5).data().toString(),
                              QStringLiteral("Open (1)"), 2000);
    QVERIFY(!fields->isVisible());
    QCOMPARE(fields->model()->rowCount(), 0);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("NEW_FIELD created")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    const auto actions = window.findChildren<QAction*>();
    const auto undo = std::ranges::find_if(actions, [](const QAction* action) {
        return action->shortcut().matches(QKeySequence::Undo) ==
               QKeySequence::ExactMatch;
    });
    const auto redo = std::ranges::find_if(actions, [](const QAction* action) {
        return action->shortcut().matches(QKeySequence::Redo) ==
               QKeySequence::ExactMatch;
    });
    QVERIFY(undo != actions.end());
    QVERIFY(redo != actions.end());
    (*undo)->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control") != nullptr &&
            regmap::findRegister(*controller->workspace(), "reg-control")->type ==
                regmap::FieldType::unsignedInteger &&
            regmap::findRegister(*controller->workspace(), "reg-control")->fields.empty(),
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    (*redo)->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), firstFieldId) != nullptr, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    Q_EMIT registers->clicked(registers->model()->index(1, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 2, 2000);
    QCOMPARE(fields->model()->index(0, 0).data().toString(),
             QStringLiteral("NEW_FIELD"));
    QCOMPARE(fields->model()->index(0, 0).data(Qt::UserRole + 1).toString(),
             QString::fromStdString(firstFieldId));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsEnumConversionsImmediatelyUsable()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Seed distinct enum references"),
        [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-control");
            QVERIFY(reg != nullptr);
            reg->initialValue = regmap::UnsignedValue(1);
            reg->resetValue = regmap::UnsignedValue(2);
        }));

    const auto registerRowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const auto actions = window.findChildren<QAction*>();
    const auto addEnum = std::ranges::find_if(
        actions, [](const QAction* action) {
            return action->text() == QStringLiteral("Add Enum Value");
        });
    QVERIFY(addEnum != actions.end());
    QTRY_VERIFY_WITH_TIMEOUT(
        registerRowForId(QStringLiteral("reg-status")) >= 0, 2000);
    const int statusRow = registerRowForId(QStringLiteral("reg-status"));
    registers->setCurrentIndex(registers->model()->index(statusRow, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const std::size_t structuredUndoDepth = controller->undoDepth();
    (*addEnum)->trigger();
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-status")->type,
        regmap::FieldType::structure);
    QVERIFY(regmap::findField(
                *controller->workspace(), "field-ready") != nullptr);
    QCOMPARE(controller->undoDepth(), structuredUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Change Type to enum first")));

    QTRY_VERIFY_WITH_TIMEOUT(
        registerRowForId(QStringLiteral("reg-control")) >= 0, 2000);
    const int controlRow = registerRowForId(QStringLiteral("reg-control"));
    registers->setCurrentIndex(registers->model()->index(controlRow, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    const std::size_t initialUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4), QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")->type ==
            regmap::FieldType::enumeration,
        2000);
    const auto* enumRegister =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(enumRegister != nullptr);
    QCOMPARE(enumRegister->enumValues.size(), std::size_t{2});
    QVERIFY(std::ranges::any_of(
        enumRegister->enumValues, [](const regmap::EnumValue& value) {
            return value.value == regmap::UnsignedValue(1);
        }));
    QVERIFY(std::ranges::any_of(
        enumRegister->enumValues, [](const regmap::EnumValue& value) {
            return value.value == regmap::UnsignedValue(2);
        }));
    QVERIFY(enumRegister->enumValues[0].id != enumRegister->enumValues[1].id);
    const std::string firstRegisterEnumId = enumRegister->enumValues[0].id;
    const std::string secondRegisterEnumId = enumRegister->enumValues[1].id;
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 3, 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2 Enum values created")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")->type ==
            regmap::FieldType::unsignedInteger,
        2000);
    const auto* restoredNumeric =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(restoredNumeric != nullptr);
    QVERIFY(restoredNumeric->enumValues.empty());
    QCOMPARE(*restoredNumeric->initialValue, regmap::UnsignedValue(1));
    QCOMPARE(*restoredNumeric->resetValue, regmap::UnsignedValue(2));
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    controller->redo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")
                ->enumValues.size() == 2,
        2000);
    const auto* redoneEnum =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QCOMPARE(redoneEnum->enumValues[0].id, firstRegisterEnumId);
    QCOMPARE(redoneEnum->enumValues[1].id, secondRegisterEnumId);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")->type ==
            regmap::FieldType::unsignedInteger,
        2000);
    registers->setCurrentIndex(
        registers->model()->index(registerRowForId(QStringLiteral("reg-control")), 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    (*addEnum)->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")
                ->enumValues.size() == 2,
        2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->type,
             regmap::FieldType::enumeration);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 3, 2000);
    const auto enumEditors = enums->findChildren<QLineEdit*>();
    const auto visibleEnumEditor =
        std::ranges::find_if(enumEditors, [](const QLineEdit* editor) {
            return editor->isVisible();
        });
    if (visibleEnumEditor != enumEditors.end()) {
        QTest::keyClick(*visibleEnumEditor, Qt::Key_Escape);
    }
    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")->type ==
            regmap::FieldType::unsignedInteger,
        2000);

    Q_EMIT registers->clicked(registers->model()->index(statusRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRowForId = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        fieldRowForId(QStringLiteral("field-ready")) >= 0, 2000);
    const int readyRow = fieldRowForId(QStringLiteral("field-ready"));
    const std::size_t fieldUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5), QStringLiteral("enum")));
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->type ==
            regmap::FieldType::enumeration,
        2000);
    const auto* enumField =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(enumField != nullptr);
    QCOMPARE(enumField->enumValues.size(), std::size_t{1});
    QCOMPARE(enumField->enumValues.front().value, regmap::UnsignedValue(0));
    const std::string fieldEnumId = enumField->enumValues.front().id;
    QCOMPARE(controller->undoDepth(), fieldUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), 2, 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("NEW_VALUE created")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->type ==
            regmap::FieldType::boolean,
        2000);
    QVERIFY(regmap::findField(
                *controller->workspace(), "field-ready")
                ->enumValues.empty());
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);
    controller->redo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findEnumValue(*controller->workspace(), fieldEnumId) != nullptr,
        2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")
                 ->enumValues.front()
                 .id,
             fieldEnumId);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsCompoundTypeChangesBeforeRemovingChildren()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    const std::size_t initialUndoDepth = controller->undoDepth();
    const QModelIndex registerType = registers->model()->index(0, 4);
    bool registerCancelSeen = false;
    QString registerCancelFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            registerCancelFailure = QStringLiteral("Register confirmation did not open");
            return;
        }
        registerCancelSeen = true;
        if (dialog->windowTitle() != QStringLiteral("Change Field Register Type") ||
            !dialog->text().contains(QStringLiteral("STATUS")) ||
            !dialog->text().contains(QStringLiteral("1 Field")) ||
            !dialog->text().contains(QStringLiteral("Ctrl+Z"))) {
            registerCancelFailure = QStringLiteral("Register confirmation lacks impact details");
        }
        if (auto* cancel = dialog->button(QMessageBox::No)) {
            QTest::mouseClick(cancel, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(registers->model()->setData(registerType, QStringLiteral("uint32")));
    QCoreApplication::processEvents();
    QVERIFY2(registerCancelFailure.isEmpty(), qPrintable(registerCancelFailure));
    QVERIFY(registerCancelSeen);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 4).data().toString(),
                              QStringLiteral("field"), 2000);
    QVERIFY(regmap::findField(*controller->workspace(), "field-ready") != nullptr);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("kept")));

    auto* clipboardData = new QMimeData;
    clipboardData->setText(QStringLiteral("uint32"));
    QApplication::clipboard()->setMimeData(clipboardData);
    registers->setCurrentIndex(registers->model()->index(0, 4));
    registers->selectionModel()->select(
        registers->model()->index(0, 4),
        QItemSelectionModel::ClearAndSelect);
    window.activateWindow();
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const auto actions = window.findChildren<QAction*>();
    const auto paste = std::ranges::find_if(actions, [](const QAction* action) {
        return action->shortcut().matches(QKeySequence::Paste) ==
               QKeySequence::ExactMatch;
    });
    QVERIFY(paste != actions.end());
    window.statusBar()->clearMessage();
    (*paste)->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->type,
             regmap::FieldType::structure);
    QVERIFY(regmap::findField(*controller->workspace(), "field-ready") != nullptr);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("individually confirmed")));

    bool registerAcceptSeen = false;
    QString registerAcceptFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            registerAcceptFailure = QStringLiteral("Register confirmation did not reopen");
            return;
        }
        registerAcceptSeen = true;
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            registerAcceptFailure = QStringLiteral("Register confirmation has no Yes button");
            dialog->reject();
        }
    });
    QVERIFY(registers->model()->setData(registers->model()->index(0, 4),
                                        QStringLiteral("uint32")));
    QCoreApplication::processEvents();
    QVERIFY2(registerAcceptFailure.isEmpty(), qPrintable(registerAcceptFailure));
    QVERIFY(registerAcceptSeen);
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-status")->type ==
            regmap::FieldType::unsignedInteger,
        2000);
    QVERIFY(regmap::findRegister(*controller->workspace(), "reg-status")->fields.empty());
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 Field removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready") != nullptr, 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->type,
             regmap::FieldType::structure);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 2, 2000);
    QVERIFY(fields->model()->setData(fields->model()->index(0, 5),
                                     QStringLiteral("field")));
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->members.size() == 1,
        2000);
    const std::string memberId =
        regmap::findField(*controller->workspace(), "field-ready")->members.front().id;
    const std::size_t compoundUndoDepth = controller->undoDepth();

    bool fieldAcceptSeen = false;
    QString fieldAcceptFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            fieldAcceptFailure = QStringLiteral("Field confirmation did not open");
            return;
        }
        fieldAcceptSeen = true;
        if (dialog->windowTitle() != QStringLiteral("Change Compound Field Type") ||
            !dialog->text().contains(QStringLiteral("READY")) ||
            !dialog->text().contains(QStringLiteral("1 Member")) ||
            !dialog->text().contains(QStringLiteral("Ctrl+Z"))) {
            fieldAcceptFailure = QStringLiteral("Field confirmation lacks impact details");
        }
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            fieldAcceptFailure = QStringLiteral("Field confirmation has no Yes button");
            dialog->reject();
        }
    });
    QVERIFY(fields->model()->setData(fields->model()->index(0, 5),
                                     QStringLiteral("bits")));
    QCoreApplication::processEvents();
    QVERIFY2(fieldAcceptFailure.isEmpty(), qPrintable(fieldAcceptFailure));
    QVERIFY(fieldAcceptSeen);
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->type ==
            regmap::FieldType::bits,
        2000);
    const auto* scalar =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(scalar != nullptr);
    QVERIFY(scalar->members.empty());
    QCOMPARE(scalar->softwareAccess, regmap::AccessMode::readOnly);
    QCOMPARE(scalar->hardwareAccess, regmap::AccessMode::none);
    QCOMPARE(scalar->readSideEffect, regmap::ReadSideEffect::none);
    QCOMPARE(scalar->writeSideEffect, regmap::WriteSideEffect::none);
    QCOMPARE(controller->undoDepth(), compoundUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 Member Field removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), memberId) != nullptr, 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->type,
             regmap::FieldType::structure);
    QCOMPARE(controller->undoDepth(), compoundUndoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsEnumTypeChangesBeforeRemovingValues()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    const auto enumValues = [](std::string_view prefix) {
        regmap::EnumValue zero;
        zero.id = std::string(prefix) + "-zero";
        zero.name = "ZERO";
        zero.value = regmap::UnsignedValue(0);
        regmap::EnumValue one;
        one.id = std::string(prefix) + "-one";
        one.name = "ONE";
        one.value = regmap::UnsignedValue(1);
        return std::vector<regmap::EnumValue>{
            std::move(zero), std::move(one)};
    };
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure enum removal fixture"),
        [enumValues](regmap::Workspace& workspace) {
            auto* control =
                regmap::findRegister(workspace, "reg-control");
            auto* ready = regmap::findField(workspace, "field-ready");
            QVERIFY(control != nullptr);
            QVERIFY(ready != nullptr);
            control->type = regmap::FieldType::enumeration;
            control->enumValues = enumValues("register-enum");
            ready->type = regmap::FieldType::enumeration;
            ready->enumValues = enumValues("field-enum");
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        registerRowForId(QStringLiteral("reg-control")) >= 0, 2000);
    const int controlRow = registerRowForId(QStringLiteral("reg-control"));
    registers->setCurrentIndex(registers->model()->index(controlRow, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    const std::size_t registerUndoDepth = controller->undoDepth();
    bool registerCancelSeen = false;
    QString registerCancelFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            registerCancelFailure =
                QStringLiteral("Register Enum confirmation did not open");
            return;
        }
        registerCancelSeen = true;
        if (dialog->windowTitle() !=
                QStringLiteral("Change Enum Register Type") ||
            !dialog->text().contains(QStringLiteral("CONTROL")) ||
            !dialog->text().contains(QStringLiteral("2 Enum values")) ||
            !dialog->text().contains(QStringLiteral("Ctrl+Z")) ||
            dialog->defaultButton() != dialog->button(QMessageBox::No)) {
            registerCancelFailure =
                QStringLiteral("Register Enum confirmation lacks impact details");
        }
        if (auto* cancel = dialog->button(QMessageBox::No)) {
            QTest::mouseClick(cancel, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("uint32")));
    QCoreApplication::processEvents();
    QVERIFY2(registerCancelFailure.isEmpty(),
             qPrintable(registerCancelFailure));
    QVERIFY(registerCancelSeen);
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")->type,
        regmap::FieldType::enumeration);
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")
            ->enumValues.size(),
        std::size_t{2});
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("kept")));

    auto* clipboardData = new QMimeData;
    clipboardData->setText(QStringLiteral("uint32"));
    QApplication::clipboard()->setMimeData(clipboardData);
    const QModelIndex registerType =
        registers->model()->index(controlRow, 4);
    registers->setCurrentIndex(registerType);
    registers->selectionModel()->select(
        registerType, QItemSelectionModel::ClearAndSelect);
    window.activateWindow();
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const auto actions = window.findChildren<QAction*>();
    const auto paste = std::ranges::find_if(
        actions, [](const QAction* action) {
            return action->shortcut().matches(QKeySequence::Paste) ==
                   QKeySequence::ExactMatch;
        });
    QVERIFY(paste != actions.end());
    window.statusBar()->clearMessage();
    (*paste)->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")->type,
        regmap::FieldType::enumeration);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY2(
        window.statusBar()->currentMessage().contains(
            QStringLiteral("individually confirmed")),
        qPrintable(window.statusBar()->currentMessage()));

    bool registerAcceptSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        registerAcceptSeen = true;
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("uint32")));
    QCoreApplication::processEvents();
    QVERIFY(registerAcceptSeen);
    const auto* numeric =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(numeric != nullptr);
    QCOMPARE(numeric->type, regmap::FieldType::unsignedInteger);
    QVERIFY(numeric->enumValues.empty());
    QCOMPARE(controller->undoDepth(), registerUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2 Enum values removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findEnumValue(
            *controller->workspace(), "register-enum-zero") != nullptr,
        2000);
    QVERIFY(regmap::findEnumValue(
                *controller->workspace(), "register-enum-one") != nullptr);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);

    const int statusRow =
        registerRowForId(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    Q_EMIT registers->clicked(
        registers->model()->index(statusRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    int readyRow = -1;
    for (int row = 0; row < fields->model()->rowCount(); ++row) {
        if (fields->model()
                ->index(row, 0)
                .data(Qt::UserRole + 1)
                .toString() == QStringLiteral("field-ready")) {
            readyRow = row;
            break;
        }
    }
    QVERIFY(readyRow >= 0);
    const std::size_t fieldUndoDepth = controller->undoDepth();
    auto* fieldClipboardData = new QMimeData;
    fieldClipboardData->setText(QStringLiteral("bits"));
    QApplication::clipboard()->setMimeData(fieldClipboardData);
    const QModelIndex fieldType = fields->model()->index(readyRow, 5);
    fields->setCurrentIndex(fieldType);
    fields->selectionModel()->select(
        fieldType, QItemSelectionModel::ClearAndSelect);
    window.activateWindow();
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    window.statusBar()->clearMessage();
    (*paste)->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findField(*controller->workspace(), "field-ready")->type,
        regmap::FieldType::enumeration);
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);
    QVERIFY2(
        window.statusBar()->currentMessage().contains(
            QStringLiteral("individually confirmed")),
        qPrintable(window.statusBar()->currentMessage()));

    bool fieldAcceptSeen = false;
    QString fieldAcceptFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            fieldAcceptFailure =
                QStringLiteral("Field Enum confirmation did not open");
            return;
        }
        fieldAcceptSeen = true;
        if (dialog->windowTitle() !=
                QStringLiteral("Change Enum Field Type") ||
            !dialog->text().contains(QStringLiteral("READY")) ||
            !dialog->text().contains(QStringLiteral("2 Enum values"))) {
            fieldAcceptFailure =
                QStringLiteral("Field Enum confirmation lacks impact details");
        }
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5), QStringLiteral("bits")));
    QCoreApplication::processEvents();
    QVERIFY2(fieldAcceptFailure.isEmpty(), qPrintable(fieldAcceptFailure));
    QVERIFY(fieldAcceptSeen);
    const auto* bitsField =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(bitsField != nullptr);
    QCOMPARE(bitsField->type, regmap::FieldType::bits);
    QVERIFY(bitsField->enumValues.empty());
    QCOMPARE(controller->undoDepth(), fieldUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2 Enum values removed")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findEnumValue(
            *controller->workspace(), "field-enum-zero") != nullptr,
        2000);
    QVERIFY(regmap::findEnumValue(
                *controller->workspace(), "field-enum-one") != nullptr);
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsCompoundFieldsUsableDuringConversionAndDeletion()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Widen scalar Field fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* field = regmap::findField(workspace, "field-ready")) {
                field->msb = 3;
                field->type = regmap::FieldType::bits;
            }
        }));

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 2, 2000);
    const std::size_t initialUndoDepth = controller->undoDepth();

    QVERIFY(fields->model()->setData(fields->model()->index(0, 5),
                                     QStringLiteral("field")));
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready") != nullptr &&
            regmap::findField(*controller->workspace(), "field-ready")->members.size() == 1,
        2000);
    const auto* compound =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(compound != nullptr);
    QCOMPARE(compound->type, regmap::FieldType::structure);
    QCOMPARE(compound->softwareAccess, regmap::AccessMode::none);
    QCOMPARE(compound->hardwareAccess, regmap::AccessMode::none);
    const std::string firstMemberId = compound->members.front().id;
    QVERIFY(!firstMemberId.empty());
    QCOMPARE(QString::fromStdString(compound->members.front().name),
             QStringLiteral("NEW_MEMBER"));
    QCOMPARE(compound->members.front().lsb, 0U);
    QCOMPARE(compound->members.front().msb, 0U);
    QCOMPARE(compound->members.front().softwareAccess,
             regmap::AccessMode::readOnly);
    QCOMPARE(compound->members.front().hardwareAccess,
             regmap::AccessMode::writeOnly);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 3, 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("NEW_MEMBER created")));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));

    const auto findAction = [&window](QKeySequence::StandardKey key) -> QAction* {
        const auto actions = window.findChildren<QAction*>();
        const auto found = std::ranges::find_if(actions, [key](const QAction* action) {
            return action->shortcut().matches(QKeySequence(key)) ==
                   QKeySequence::ExactMatch;
        });
        return found == actions.end() ? nullptr : *found;
    };
    auto* undo = findAction(QKeySequence::Undo);
    auto* redo = findAction(QKeySequence::Redo);
    QVERIFY(undo != nullptr);
    QVERIFY(redo != nullptr);
    undo->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready") != nullptr &&
            regmap::findField(*controller->workspace(), "field-ready")->type ==
                regmap::FieldType::bits &&
            regmap::findField(*controller->workspace(), "field-ready")->members.empty(),
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    redo->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), firstMemberId) != nullptr, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    const auto rowForId = [fields](std::string_view id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString()
                    .toStdString() == id) {
                return row;
            }
        }
        return -1;
    };
    int firstMemberRow = rowForId(firstMemberId);
    QVERIFY(firstMemberRow >= 0);
    fields->setCurrentIndex(fields->model()->index(firstMemberRow, 0));
    fields->setFocus(Qt::OtherFocusReason);
    window.statusBar()->clearMessage();
    QTest::keyClick(fields, Qt::Key_Delete);
    QCoreApplication::processEvents();
    QVERIFY(regmap::findField(*controller->workspace(), firstMemberId) != nullptr);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("final Member")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("add another Member")));

    const int parentRow = rowForId("field-ready");
    QVERIFY(parentRow >= 0);
    fields->setCurrentIndex(fields->model()->index(parentRow, 0));
    bool addMemberTriggered = false;
    QString addMemberFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* action =
            window.findChild<QAction*>(QStringLiteral("addMemberFieldAction"));
        auto* menu =
            action == nullptr ? qobject_cast<QMenu*>(QApplication::activePopupWidget())
                              : qobject_cast<QMenu*>(action->parent());
        if (menu == nullptr || action == nullptr || !action->isEnabled()) {
            addMemberFailure = QStringLiteral("Add Member action is unavailable");
            if (menu != nullptr) {
                menu->close();
            }
            return;
        }
        addMemberTriggered = true;
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    const QModelIndex parentIndex = fields->model()->index(parentRow, 0);
    const QPoint position = fields->visualRect(parentIndex).center();
    QContextMenuEvent contextEvent(
        QContextMenuEvent::Mouse, position,
        fields->viewport()->mapToGlobal(position));
    QCoreApplication::sendEvent(fields->viewport(), &contextEvent);
    QVERIFY2(addMemberFailure.isEmpty(), qPrintable(addMemberFailure));
    QVERIFY(addMemberTriggered);
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->members.size() == 2,
        2000);
    const std::string secondMemberId =
        regmap::findField(*controller->workspace(), "field-ready")->members.back().id;
    QTest::keyClick(fields, Qt::Key_Escape);
    QCoreApplication::processEvents();

    firstMemberRow = rowForId(firstMemberId);
    QVERIFY(firstMemberRow >= 0);
    fields->setCurrentIndex(fields->model()->index(firstMemberRow, 0));
    QCOMPARE(fields->currentIndex().data(Qt::UserRole + 1).toString(),
             QString::fromStdString(firstMemberId));
    window.activateWindow();
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const auto actions = window.findChildren<QAction*>();
    const auto deleteAction =
        std::ranges::find_if(actions, [](const QAction* action) {
            return action->shortcut().matches(QKeySequence::Delete) ==
                   QKeySequence::ExactMatch;
        });
    QVERIFY(deleteAction != actions.end());
    window.statusBar()->clearMessage();
    (*deleteAction)->trigger();
    QTest::qWait(50);
    QVERIFY2(regmap::findField(*controller->workspace(), firstMemberId) == nullptr,
             qPrintable(window.statusBar()->currentMessage()));
    QVERIFY(regmap::findField(*controller->workspace(), secondMemberId) != nullptr);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->members.size(),
             1U);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsFieldDeletionImpactAndRestoresIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Field deletion fixture"),
        [](regmap::Workspace& workspace) {
            auto* status = regmap::findRegister(workspace, "reg-status");
            auto* ready = regmap::findField(workspace, "field-ready");
            QVERIFY(status != nullptr);
            QVERIFY(ready != nullptr);
            status->resetValue = regmap::UnsignedValue(1);
            ready->type = regmap::FieldType::enumeration;
            ready->resetValue = regmap::UnsignedValue(1);
            ready->description = "Ready field definition.";
            regmap::EnumValue clear;
            clear.id = "enum-ready-clear";
            clear.name = "CLEAR";
            clear.value = regmap::UnsignedValue(0);
            regmap::EnumValue set;
            set.id = "enum-ready-set";
            set.name = "SET";
            set.value = regmap::UnsignedValue(1);
            ready->enumValues = {clear, set};

            regmap::Field auxiliary;
            auxiliary.id = "field-auxiliary";
            auxiliary.name = "AUXILIARY";
            auxiliary.msb = 1;
            auxiliary.lsb = 1;
            auxiliary.type = regmap::FieldType::bits;
            auxiliary.softwareAccess = regmap::AccessMode::readOnly;
            auxiliary.hardwareAccess = regmap::AccessMode::writeOnly;
            auxiliary.resetValue = regmap::UnsignedValue(0);
            auxiliary.readSideEffect = regmap::ReadSideEffect::none;
            auxiliary.writeSideEffect = regmap::WriteSideEffect::none;
            status->fields.push_back(std::move(auxiliary));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->rowCount(), 3, 2000);
    const auto rowForId = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };

    struct DeleteInvocation {
        bool dialogSeen{false};
        bool impactDescribed{false};
    };
    const auto invokeDelete =
        [&](const QString& fieldId,
            QMessageBox::StandardButton response) {
            DeleteInvocation result;
            const int row = rowForId(fieldId);
            if (row < 0) {
                return result;
            }
            window.activateWindow();
            fields->setCurrentIndex(fields->model()->index(row, 0));
            fields->scrollTo(fields->currentIndex());
            fields->setFocus(Qt::OtherFocusReason);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    return;
                }
                result.dialogSeen = true;
                result.impactDescribed =
                    dialog->windowTitle() ==
                        QStringLiteral("Delete Register-Map Objects") &&
                    dialog->text().contains(QStringLiteral("field READY")) &&
                    dialog->text().contains(QStringLiteral("2 Enum values")) &&
                    dialog->text().contains(
                        QStringLiteral("1 non-zero Initial/Reset value")) &&
                    dialog->text().contains(QStringLiteral("1 description")) &&
                    dialog->text().contains(QStringLiteral("Ctrl+Z")) &&
                    dialog->defaultButton() ==
                        dialog->button(QMessageBox::No);
                if (auto* button = dialog->button(response)) {
                    QTest::mouseClick(button, Qt::LeftButton);
                } else {
                    dialog->reject();
                }
            });
            QTest::keyClick(fields, Qt::Key_Delete);
            return result;
        };

    const std::size_t initialUndoDepth = controller->undoDepth();
    const DeleteInvocation cancelled =
        invokeDelete(QStringLiteral("field-ready"), QMessageBox::No);
    QVERIFY(cancelled.dialogSeen);
    QVERIFY(cancelled.impactDescribed);
    const auto* retained =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(retained != nullptr);
    QCOMPARE(retained->enumValues.size(), std::size_t{2});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Delete cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("field READY kept")));

    const DeleteInvocation confirmed =
        invokeDelete(QStringLiteral("field-ready"), QMessageBox::Yes);
    QVERIFY(confirmed.dialogSeen);
    QVERIFY(confirmed.impactDescribed);
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready") == nullptr,
        2000);
    QVERIFY(regmap::findField(
                *controller->workspace(), "field-auxiliary") != nullptr);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Deleted field READY")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready") != nullptr,
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    const auto* restored =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(restored != nullptr);
    QCOMPARE(restored->type, regmap::FieldType::enumeration);
    QCOMPARE(restored->enumValues.size(), std::size_t{2});
    QVERIFY(restored->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QCOMPARE(restored->description,
             std::string("Ready field definition."));

    const int simpleRow = rowForId(QStringLiteral("field-auxiliary"));
    QVERIFY(simpleRow >= 0);
    window.activateWindow();
    fields->setCurrentIndex(fields->model()->index(simpleRow, 0));
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    bool unexpectedConfirmation = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog =
                qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
            unexpectedConfirmation = true;
            dialog->reject();
        }
    });
    QTest::keyClick(fields, Qt::Key_Delete);
    QCoreApplication::processEvents();
    QVERIFY(!unexpectedConfirmation);
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(
            *controller->workspace(), "field-auxiliary") == nullptr,
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Deleted field AUXILIARY")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(
            *controller->workspace(), "field-auxiliary") != nullptr,
        2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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

void GuiSmokeTests::rejectsFieldMoveThatInvalidatesValueContracts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("field-contract-drag.regmap.yaml"));
    createBitfieldDragProject(manifest);

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* bitfield =
        window.findChild<BitfieldView*>(QStringLiteral("bitfieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(bitfield != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure constrained Enum Field"),
        [](regmap::Workspace& workspace) {
            auto* ready =
                regmap::findField(workspace, "field-ready");
            QVERIFY(ready != nullptr);
            ready->type = regmap::FieldType::enumeration;
            ready->minimumValue.reset();
            ready->maximumValue.reset();
            ready->members.clear();
            ready->enumValues.clear();

            regmap::EnumValue zero;
            zero.id = "enum-ready-zero";
            zero.name = "ZERO";
            zero.value = regmap::UnsignedValue(0);
            ready->enumValues.push_back(std::move(zero));

            regmap::EnumValue fifteen;
            fifteen.id = "enum-ready-fifteen";
            fifteen.name = "FIFTEEN";
            fifteen.value = regmap::UnsignedValue(15);
            ready->enumValues.push_back(std::move(fifteen));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const QModelIndex openFields =
        registers->model()->index(0, 5);
    registers->scrollTo(openFields);
    QCoreApplication::processEvents();
    QTest::mouseClick(
        registers->viewport(), Qt::LeftButton, Qt::NoModifier,
        registers->visualRect(openFields).center());
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(bitfield->isVisible(), 2000);

    bool dialogHandled = false;
    QString dialogFailure;
    const auto handleDialog = [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            dialogFailure =
                QStringLiteral("Field overlap dialog did not become modal");
            return;
        }
        QAbstractButton* trimMoving = nullptr;
        for (auto* button : dialog->buttons()) {
            if (dialog->buttonRole(button) ==
                QMessageBox::AcceptRole) {
                trimMoving = button;
                break;
            }
        }
        if (trimMoving == nullptr) {
            dialogFailure =
                QStringLiteral("Trim-moving resolution is unavailable");
            dialog->reject();
            return;
        }
        dialogHandled = true;
        QTest::mouseClick(trimMoving, Qt::LeftButton);
    };

    const std::size_t undoDepth = controller->undoDepth();
    QTimer::singleShot(0, &window, handleDialog);
    Q_EMIT bitfield->fieldMoveRequested(
        QStringLiteral("field-ready"), 8, 11);
    QCoreApplication::processEvents();

    QVERIFY2(dialogFailure.isEmpty(), qPrintable(dialogFailure));
    QVERIFY(dialogHandled);
    const auto* ready =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(ready != nullptr);
    QCOMPARE(ready->lsb, std::uint32_t{0});
    QCOMPARE(ready->msb, std::uint32_t{3});
    QCOMPARE(ready->enumValues.size(), std::size_t{2});
    QCOMPARE(
        ready->enumValues.back().value,
        regmap::UnsignedValue(15));
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot move Field READY")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Enum value")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsDeletionOfFullyCoveredFieldsDuringDrag()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("field-delete-drag.regmap.yaml"));
    createBitfieldDragProject(manifest);

    MainWindow window;
    window.resize(1200, 760);
    window.show();
    window.openProjectPath(manifest);
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* bitfield =
        window.findChild<BitfieldView*>(QStringLiteral("bitfieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(bitfield != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure full-overlap Fields"),
        [](regmap::Workspace& workspace) {
            auto* ready =
                regmap::findField(workspace, "field-ready");
            auto* obstacle =
                regmap::findField(workspace, "field-obstacle");
            QVERIFY(ready != nullptr);
            QVERIFY(obstacle != nullptr);
            ready->type = regmap::FieldType::bits;
            ready->enumValues.clear();
            obstacle->lsb = 4;
            obstacle->msb = 7;
            obstacle->type = regmap::FieldType::bits;
            obstacle->enumValues.clear();
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const QModelIndex openFields =
        registers->model()->index(0, 5);
    registers->scrollTo(openFields);
    QCoreApplication::processEvents();
    QTest::mouseClick(
        registers->viewport(), Qt::LeftButton, Qt::NoModifier,
        registers->visualRect(openFields).center());
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(bitfield->isVisible(), 2000);

    bool dialogHandled = false;
    bool deletionExplained = false;
    bool cancelIsDefault = false;
    QString dialogFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
        if (dialog == nullptr) {
            dialogFailure =
                QStringLiteral("Field overlap dialog did not become modal");
            return;
        }
        deletionExplained =
            dialog->informativeText().contains(
                QStringLiteral("delete 1 fully covered Field")) &&
            dialog->informativeText().contains(
                QStringLiteral("OBSTACLE"));
        cancelIsDefault =
            dialog->defaultButton() != nullptr &&
            dialog->buttonRole(dialog->defaultButton()) ==
                QMessageBox::RejectRole;

        QAbstractButton* destructive = nullptr;
        for (auto* button : dialog->buttons()) {
            if (dialog->buttonRole(button) ==
                QMessageBox::DestructiveRole) {
                destructive = button;
                break;
            }
        }
        if (destructive == nullptr ||
            !destructive->text().contains(
                QStringLiteral("delete"), Qt::CaseInsensitive)) {
            dialogFailure =
                QStringLiteral("Explicit delete choice is unavailable");
            dialog->reject();
            return;
        }
        dialogHandled = true;
        QTest::mouseClick(destructive, Qt::LeftButton);
    });

    const std::size_t undoDepth = controller->undoDepth();
    Q_EMIT bitfield->fieldMoveRequested(
        QStringLiteral("field-ready"), 4, 7);
    QCoreApplication::processEvents();

    QVERIFY2(dialogFailure.isEmpty(), qPrintable(dialogFailure));
    QVERIFY(dialogHandled);
    QVERIFY(deletionExplained);
    QVERIFY(cancelIsDefault);
    const auto* ready =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(ready != nullptr);
    QCOMPARE(ready->lsb, std::uint32_t{4});
    QCOMPARE(ready->msb, std::uint32_t{7});
    QVERIFY(regmap::findField(
                *controller->workspace(),
                "field-obstacle") == nullptr);
    QCOMPARE(controller->undoDepth(), undoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    controller->undo();
    ready = regmap::findField(
        *controller->workspace(), "field-ready");
    const auto* restoredObstacle =
        regmap::findField(
            *controller->workspace(), "field-obstacle");
    QVERIFY(ready != nullptr);
    QVERIFY(restoredObstacle != nullptr);
    QCOMPARE(ready->lsb, std::uint32_t{0});
    QCOMPARE(ready->msb, std::uint32_t{3});
    QCOMPARE(restoredObstacle->lsb, std::uint32_t{4});
    QCOMPARE(restoredObstacle->msb, std::uint32_t{7});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
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

void GuiSmokeTests::closesAnchoredPopupEditorsWithEscape()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(
            QStringLiteral("popup-escape.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    QVERIFY(window.openProjectPath(manifest));
    window.resize(1100, 720);
    window.show();
    window.activateWindow();
    QTest::qWait(50);

    auto* controller =
        window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(
            QStringLiteral("registerView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    const std::size_t undoDepth =
        controller->undoDepth();

    const QModelIndex tagIndex =
        registers->model()->index(0, 10);
    const QString originalTags =
        tagIndex.data().toString();
    registers->setCurrentIndex(tagIndex);
    Q_EMIT registers->doubleClicked(tagIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* tagPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(tagPopup != nullptr);
    QCOMPARE(tagPopup->objectName(),
             QStringLiteral("tagPopup"));
    auto* tagSearch =
        tagPopup->findChild<QLineEdit*>(
            QStringLiteral("tagSearch"));
    auto* tagCloseShortcut =
        tagPopup->findChild<QShortcut*>(
            QStringLiteral("closeAnchoredPopupShortcut"));
    QVERIFY(tagSearch != nullptr);
    QVERIFY(tagCloseShortcut != nullptr);
    QCOMPARE(tagCloseShortcut->context(),
             Qt::WidgetWithChildrenShortcut);
    tagSearch->setText(
        QStringLiteral("unconfirmed-tag"));
    tagSearch->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(tagSearch, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(registers->hasFocus(), 2000);
    QCOMPARE(registers->currentIndex(), tagIndex);
    QCOMPARE(registers->model()->index(0, 10)
                 .data().toString(),
             originalTags);
    QCOMPARE(controller->undoDepth(), undoDepth);

    const QModelIndex accessIndex =
        registers->model()->index(0, 9);
    const QString originalAccess =
        accessIndex.data().toString();
    registers->setCurrentIndex(accessIndex);
    Q_EMIT registers->doubleClicked(accessIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* accessPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(accessPopup != nullptr);
    QCOMPARE(accessPopup->objectName(),
             QStringLiteral("accessPopup"));
    auto* accessOptions =
        accessPopup->findChild<QListWidget*>(
            QStringLiteral("accessOptions"));
    auto* accessCloseShortcut =
        accessPopup->findChild<QShortcut*>(
            QStringLiteral("closeAnchoredPopupShortcut"));
    QVERIFY(accessOptions != nullptr);
    QVERIFY(accessCloseShortcut != nullptr);
    const int writeOnlyRow =
        [&] {
            for (int row = 0;
                 row < accessOptions->count(); ++row) {
                if (accessOptions->item(row)->text() ==
                    QStringLiteral("WO")) {
                    return row;
                }
            }
            return -1;
        }();
    QVERIFY(writeOnlyRow >= 0);
    accessOptions->setCurrentRow(writeOnlyRow);
    QCOMPARE(accessOptions->currentItem()->text(),
             QStringLiteral("WO"));
    accessOptions->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(accessOptions, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(registers->hasFocus(), 2000);
    QCOMPARE(registers->currentIndex(), accessIndex);
    QCOMPARE(registers->model()->index(0, 9)
                 .data().toString(),
             originalAccess);
    QCOMPARE(controller->undoDepth(), undoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsPopupEditingActionsLocal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(
            QStringLiteral("popup-clipboard.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    QVERIFY(window.openProjectPath(manifest));
    window.resize(1100, 720);
    window.show();
    window.activateWindow();
    QTest::qWait(50);

    auto* controller =
        window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(
            QStringLiteral("registerView"));
    auto* copy =
        window.findChild<QAction*>(
            QStringLiteral("copySelectionAction"));
    auto* paste =
        window.findChild<QAction*>(
            QStringLiteral("pasteSelectionAction"));
    auto* remove =
        window.findChild<QAction*>(
            QStringLiteral("deleteSelectionAction"));
    auto* undo =
        window.findChild<QAction*>(
            QStringLiteral("undoAction"));
    auto* redo =
        window.findChild<QAction*>(
            QStringLiteral("redoAction"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(copy != nullptr);
    QVERIFY(paste != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(undo != nullptr);
    QVERIFY(redo != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure first popup history sentinel"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Popup history first";
        }));
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure second popup history sentinel"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Popup history second";
        }));
    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        undo->isEnabled() && redo->isEnabled(), 2000);

    const std::size_t undoDepth =
        controller->undoDepth();
    const std::string historyName =
        controller->workspace()->name;
    const QModelIndex tagIndex =
        registers->model()->index(0, 10);
    const QString originalTags =
        tagIndex.data().toString();
    registers->setCurrentIndex(tagIndex);
    Q_EMIT registers->doubleClicked(tagIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* tagPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(tagPopup != nullptr);
    QCOMPARE(tagPopup->objectName(),
             QStringLiteral("tagPopup"));
    auto* tagSearch =
        tagPopup->findChild<QLineEdit*>(
            QStringLiteral("tagSearch"));
    auto* tagOptions =
        tagPopup->findChild<QListWidget*>(
            QStringLiteral("tagOptions"));
    QVERIFY(tagSearch != nullptr);
    QVERIFY(tagOptions != nullptr);

    tagSearch->setText(QStringLiteral("needle"));
    tagSearch->setFocus(Qt::OtherFocusReason);
    QTest::keyClicks(tagSearch, QStringLiteral("X"));
    QTest::keyClick(tagSearch, Qt::Key_Z,
                    Qt::ControlModifier);
    QCOMPARE(tagSearch->text(),
             QStringLiteral("needle"));
    QTest::keyClick(tagSearch, Qt::Key_Y,
                    Qt::ControlModifier);
    QCOMPARE(tagSearch->text(),
             QStringLiteral("needleX"));
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);

    tagSearch->setText(QStringLiteral("needle"));
    tagSearch->selectAll();
    tagSearch->setFocus(Qt::OtherFocusReason);
    QApplication::clipboard()->setText(
        QStringLiteral("COPY_SENTINEL"));
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("needle"));

    tagSearch->setText(QStringLiteral("tag"));
    tagSearch->setCursorPosition(static_cast<int>(tagSearch->text().size()));
    QApplication::clipboard()->setText(
        QStringLiteral("-filter"));
    paste->trigger();
    QCOMPARE(tagSearch->text(),
             QStringLiteral("tag-filter"));
    tagSearch->setSelection(3, 7);
    QTest::keyClick(tagSearch, Qt::Key_Delete);
    QCOMPARE(tagSearch->text(),
             QStringLiteral("tag"));
    QCOMPARE(registers->model()->index(0, 10)
                 .data().toString(),
             originalTags);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(),
             tagPopup);

    tagOptions->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QApplication::clipboard()->setText(
        QStringLiteral("LIST_COPY_SENTINEL"));
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("LIST_COPY_SENTINEL"));
    QApplication::clipboard()->setText(
        QStringLiteral("accidental-tag"));
    paste->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->index(0, 10)
                 .data().toString(),
             originalTags);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(),
             tagPopup);
    QVERIFY(tagOptions->hasFocus());
    bool tagDeleteDialogSeen = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog =
                qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
            tagDeleteDialogSeen = true;
            dialog->reject();
        }
    });
    remove->trigger();
    QTest::qWait(50);
    QVERIFY(!tagDeleteDialogSeen);
    QVERIFY(regmap::findRegister(
                *controller->workspace(), "reg-status") !=
            nullptr);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), tagPopup);
    undo->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), tagPopup);
    redo->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), tagPopup);
    tagPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);

    const QModelIndex accessIndex =
        registers->model()->index(0, 9);
    const QString originalAccess =
        accessIndex.data().toString();
    registers->setCurrentIndex(accessIndex);
    Q_EMIT registers->doubleClicked(accessIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* accessPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(accessPopup != nullptr);
    QCOMPARE(accessPopup->objectName(),
             QStringLiteral("accessPopup"));
    auto* accessOptions =
        accessPopup->findChild<QListWidget*>(
            QStringLiteral("accessOptions"));
    QVERIFY(accessOptions != nullptr);
    accessOptions->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    QApplication::clipboard()->setText(
        QStringLiteral("ACCESS_COPY_SENTINEL"));
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("ACCESS_COPY_SENTINEL"));
    QApplication::clipboard()->setText(
        QStringLiteral("WO"));
    paste->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->index(0, 9)
                 .data().toString(),
             originalAccess);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(),
             accessPopup);
    QVERIFY(accessOptions->hasFocus());
    bool accessDeleteDialogSeen = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog =
                qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
            accessDeleteDialogSeen = true;
            dialog->reject();
        }
    });
    remove->trigger();
    QTest::qWait(50);
    QVERIFY(!accessDeleteDialogSeen);
    QVERIFY(regmap::findRegister(
                *controller->workspace(), "reg-status") !=
            nullptr);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), accessPopup);
    undo->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), accessPopup);
    redo->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QCOMPARE(QApplication::activePopupWidget(), accessPopup);
    accessPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::editsFieldAccessFromConstrainedChoices()
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

    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* controller = window.findChild<ProjectController*>();
    auto* remove =
        window.findChild<QAction*>(
            QStringLiteral("deleteSelectionAction"));
    auto* undo =
        window.findChild<QAction*>(
            QStringLiteral("undoAction"));
    auto* redo =
        window.findChild<QAction*>(
            QStringLiteral("redoAction"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(undo != nullptr);
    QVERIFY(redo != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure first Access editor history sentinel"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Access history first";
        }));
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure second Access editor history sentinel"),
        [](regmap::Workspace& workspace) {
            workspace.name = "Access history second";
        }));
    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        undo->isEnabled() && redo->isEnabled(), 2000);

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const QModelIndex softwareAccess = fields->model()->index(0, 8);
    QCOMPARE(softwareAccess.data().toString(), QStringLiteral("RO"));

    fields->scrollTo(softwareAccess);
    QTest::mouseClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                      fields->visualRect(softwareAccess).center());
    QCOMPARE(fields->currentIndex(), softwareAccess);
    fields->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(fields, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("RO"));

    QTest::mouseDClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier,
                       fields->visualRect(softwareAccess).center());
    const auto visibleAccessEditor = [fields]() -> QComboBox* {
        for (auto* editor :
             fields->findChildren<QComboBox*>(QStringLiteral("fieldAccessEditor"))) {
            if (editor->isVisible()) {
                return editor;
            }
        }
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT(visibleAccessEditor() != nullptr, 2000);
    auto* editor = visibleAccessEditor();
    QStringList choices;
    for (int index = 0; index < editor->count(); ++index) {
        choices.push_back(editor->itemText(index));
    }
    QCOMPARE(choices, QStringList({QStringLiteral("NONE"), QStringLiteral("RO"),
                                   QStringLiteral("WO"), QStringLiteral("RW")}));
    QVERIFY(!editor->isEditable());

    const std::size_t undoDepth = controller->undoDepth();
    const std::string historyName =
        controller->workspace()->name;
    editor->setFocus(Qt::OtherFocusReason);
    bool deleteDialogSeen = false;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog =
                qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
            deleteDialogSeen = true;
            dialog->reject();
        }
    });
    remove->trigger();
    QTest::qWait(50);
    QVERIFY(!deleteDialogSeen);
    QVERIFY(regmap::findField(
                *controller->workspace(), "field-ready") !=
            nullptr);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(editor->hasFocus());

    QPointer<QComboBox> editorGuard(editor);
    undo->trigger();
    QCoreApplication::processEvents();
    QVERIFY(editorGuard != nullptr);
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(editorGuard->hasFocus());
    redo->trigger();
    QCoreApplication::processEvents();
    QVERIFY(editorGuard != nullptr);
    QCOMPARE(controller->workspace()->name,
             historyName);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(editorGuard->hasFocus());
    editor = editorGuard;

    editor->setCurrentText(QStringLiteral("NONE"));
    Q_EMIT editor->activated(editor->currentIndex());
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 8).data().toString(),
                              QStringLiteral("NONE"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->softwareAccess,
             regmap::AccessMode::none);
    QCOMPARE(controller->undoDepth(), undoDepth + 1);

    fields->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(fields, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 8).data().toString(),
                              QStringLiteral("RO"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::keepsAccessAndEffectsConsistentDuringEditing()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRow = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(readyRow >= 0);
    const std::size_t initialUndoDepth = controller->undoDepth();

    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 8), QStringLiteral("RW")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 8)
            .data()
            .toString(),
        QStringLiteral("RO"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->softwareAccess,
             regmap::AccessMode::readOnly);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("containing Register")));

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 11), QStringLiteral("clear")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 11)
            .data()
            .toString(),
        QStringLiteral("clear"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 8), QStringLiteral("NONE")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 8)
            .data()
            .toString(),
        QStringLiteral("NONE"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->readSideEffect,
             regmap::ReadSideEffect::none);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 2);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Read Effect cleared")));

    fields->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(fields, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 8)
            .data()
            .toString(),
        QStringLiteral("RO"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->readSideEffect,
             regmap::ReadSideEffect::clear);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 12), QStringLiteral("w1c")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 12)
            .data()
            .toString(),
        QStringLiteral("none"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("software-writable")));

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Register access conflict fixture"),
        [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            auto* field = regmap::findField(workspace, "field-ready");
            if (reg == nullptr || field == nullptr) {
                return;
            }
            reg->access = regmap::AccessMode::readWrite;
            field->softwareAccess = regmap::AccessMode::readWrite;
            field->readSideEffect = regmap::ReadSideEffect::none;
            field->writeSideEffect = regmap::WriteSideEffect::none;
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t registerUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(
        registers->model()->index(0, 9), QStringLiteral("RO")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()->index(0, 9).data().toString(),
        QStringLiteral("RW"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->access,
             regmap::AccessMode::readWrite);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->softwareAccess,
             regmap::AccessMode::readWrite);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Field software access")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::closesTagPopupWhenFilteredRegisterDisappears()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* tagFilter = window.findChild<QComboBox*>(QStringLiteral("tagFilter"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(tagFilter != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Share active filter tag"),
        [](regmap::Workspace& workspace) {
            if (auto* control = regmap::findRegister(workspace, "reg-control")) {
                control->tags.push_back("existing");
            }
        }));

    const auto rowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int existingFilter = -1;
    for (int index = 0; index < tagFilter->count(); ++index) {
        if (tagFilter->itemText(index) == QStringLiteral("existing")) {
            existingFilter = index;
            break;
        }
    }
    QVERIFY(existingFilter > 0);
    tagFilter->setCurrentIndex(existingFilter);
    QTRY_VERIFY_WITH_TIMEOUT(rowForId(QStringLiteral("reg-status")) >= 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(rowForId(QStringLiteral("reg-control")) >= 0, 2000);

    const int statusRow = rowForId(QStringLiteral("reg-status"));
    const QModelIndex statusTags = registers->model()->index(statusRow, 10);
    registers->scrollTo(statusTags);
    QCoreApplication::processEvents();
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      registers->visualRect(statusTags).center());
    QCOMPARE(registers->currentIndex(), statusTags);
    QVERIFY(QApplication::activePopupWidget() == nullptr);
    QTest::mouseDClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                       registers->visualRect(statusTags).center());
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() != nullptr, 2000);
    auto* popup = qobject_cast<QFrame*>(QApplication::activePopupWidget());
    QVERIFY(popup != nullptr);
    QCOMPARE(popup->objectName(), QStringLiteral("tagPopup"));
    auto* tags = popup->findChild<QListWidget*>(QStringLiteral("tagOptions"));
    QVERIFY(tags != nullptr);

    QListWidgetItem* existing = nullptr;
    for (int index = 0; index < tags->count(); ++index) {
        if (tags->item(index)->text() == QStringLiteral("existing")) {
            existing = tags->item(index);
            break;
        }
    }
    QVERIFY(existing != nullptr);
    QVERIFY(existing->isSelected());
    QTest::mouseClick(tags->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tags->visualItemRect(existing).center());

    QTRY_VERIFY_WITH_TIMEOUT(rowForId(QStringLiteral("reg-status")) < 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(rowForId(QStringLiteral("reg-control")) >= 0, 2000);
    QCOMPARE(tagFilter->currentText(), QStringLiteral("existing"));
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activePopupWidget() == nullptr, 2000);
    QCOMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-control"));

    const auto* status =
        regmap::findRegister(*controller->workspace(), "reg-status");
    const auto* control =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(status != nullptr);
    QVERIFY(control != nullptr);
    QVERIFY(std::ranges::find(status->tags, std::string{"existing"}) ==
            status->tags.end());
    QVERIFY(std::ranges::find(control->tags, std::string{"existing"}) !=
            control->tags.end());

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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Fill Block before insertion"),
        [](regmap::Workspace& workspace) {
            if (auto* block =
                    regmap::findRegisterBlock(workspace, "block-control")) {
                block->size = 8;
            }
        }));
    QCoreApplication::processEvents();
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
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
    const std::size_t fullBlockUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier, insertionPoint);
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(registers->model()->index(0, 1).data().toString(), QStringLiteral("0x0"));
    QCOMPARE(registers->model()->index(1, 1).data().toString(), QStringLiteral("0x4"));
    QCOMPARE(controller->undoDepth(), fullBlockUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot insert a 32-bit Register")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Block Size")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Make room for insertion"),
        [](regmap::Workspace& workspace) {
            if (auto* block =
                    regmap::findRegisterBlock(workspace, "block-control")) {
                block->size = 12;
            }
        }));
    QCoreApplication::processEvents();
    const QModelIndex retryNextRegister =
        registers->model()->index(1, 0);
    registers->scrollTo(retryNextRegister);
    const QRect retryNextRectangle =
        registers->visualRect(retryNextRegister);
    QVERIFY(retryNextRectangle.isValid());
    QTest::mouseMove(
        registers->viewport(),
        registers->visualRect(registers->model()->index(0, 0)).center());
    const QPoint retryInsertionPoint(18, retryNextRectangle.top());
    QTest::mouseMove(registers->viewport(), retryInsertionPoint);
    QTest::qWait(20);
    QTest::mouseClick(
        registers->viewport(), Qt::LeftButton, Qt::NoModifier,
        retryInsertionPoint);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 4, 2000);
    QCOMPARE(registers->viewport()->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(registers->model()->index(1, 0).data().toString(), QStringLiteral("NEW_REGISTER"));
    QCOMPARE(registers->model()->index(1, 1).data().toString(), QStringLiteral("0x4"));
    QCOMPARE(registers->model()->index(2, 0).data().toString(), QStringLiteral("CONTROL"));
    QCOMPARE(registers->model()->index(2, 1).data().toString(), QStringLiteral("0x8"));
    QCOMPARE(registers->model()->index(3, 0).data().toString(), QStringLiteral("+"));

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Make room for another insertion"),
        [](regmap::Workspace& workspace) {
            if (auto* block =
                    regmap::findRegisterBlock(workspace, "block-control")) {
                block->size = 16;
            }
        }));
    QCoreApplication::processEvents();
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const QModelIndex shiftedControl = registers->model()->index(2, 0);
    const QRect shiftedControlRectangle = registers->visualRect(shiftedControl);
    QVERIFY(shiftedControlRectangle.isValid());
    QTest::mouseMove(
        registers->viewport(),
        registers->visualRect(registers->model()->index(1, 0)).center());
    const QPoint secondInsertionPoint(18, shiftedControlRectangle.top());
    QTest::mouseMove(registers->viewport(), secondInsertionPoint);
    QTest::qWait(20);
    QTest::mouseClick(registers->viewport(), Qt::LeftButton, Qt::NoModifier,
                      secondInsertionPoint);

    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 5, 2000);
    QCOMPARE(registers->model()->index(2, 0).data().toString(),
             QStringLiteral("NEW_REGISTER_2"));
    QCOMPARE(registers->model()->index(2, 1).data().toString(), QStringLiteral("0x8"));
    QCOMPARE(registers->model()->index(3, 0).data().toString(), QStringLiteral("CONTROL"));
    QCOMPARE(registers->model()->index(3, 1).data().toString(), QStringLiteral("0xC"));
    QCOMPARE(registers->model()->index(4, 0).data().toString(), QStringLiteral("+"));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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

void GuiSmokeTests::reportsBlockedUnsavedSyncAndRecovers()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QString rtlPath =
        directory.filePath(QStringLiteral("rtl/gui_registers.sv"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1200, 760);
    window.show();
    QTest::qWait(50);

    auto* state =
        window.findChild<QLabel*>(QStringLiteral("syncStateBadge"));
    auto* save =
        window.findChild<QAction*>(QStringLiteral("saveSyncAction"));
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(state != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().startsWith(QStringLiteral("Synchronized")), 2000);

    const QString editedDescription =
        QStringLiteral("Workbench edit waiting for valid RTL.");
    QVERIFY(registers->model()->setData(
        registers->model()->index(0, 11), editedDescription));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isDirty(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().startsWith(QStringLiteral("Unsaved")), 2000);

    editManagedRtlValue(
        rtlPath, QStringLiteral("reg-status"),
        QStringLiteral("offset"), QStringLiteral("64'hx"));
    save->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(controller->hasProjectErrors(), 2000);
    QVERIFY(controller->isDirty());
    QTRY_COMPARE_WITH_TIMEOUT(
        state->text(), QStringLiteral("Blocked · 1 unsaved change(s)"),
        2000);
    QVERIFY(state->toolTip().contains(
        QStringLiteral("Workbench edits remain unsaved")));
    QVERIFY(state->toolTip().contains(QStringLiteral("Fix RTL")));
    QVERIFY(state->toolTip().contains(
        QStringLiteral("Save & Sync again")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Workbench edits remain unsaved")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Save & Sync again")));

    const auto unchanged =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(unchanged.workspace.has_value());
    QCOMPARE(
        regmap::findRegister(*unchanged.workspace, "reg-status")
            ->description,
        std::string{});

    const auto validDiskModel =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(validDiskModel.workspace.has_value());
    QVERIFY(regmap::writeManagedRtl(
                std::filesystem::path(rtlPath.toStdWString()),
                "gui_registers", *validDiskModel.workspace)
                .empty());

    save->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->hasProjectErrors(), 4000);
    QTRY_VERIFY_WITH_TIMEOUT(
        state->text().startsWith(QStringLiteral("Synchronized")), 4000);
    QVERIFY(state->toolTip().contains(
        QStringLiteral("Project, managed RTL, and read-only outputs saved")));

    const auto saved =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(saved.workspace.has_value());
    QCOMPARE(
        QString::fromStdString(
            regmap::findRegister(*saved.workspace, "reg-status")
                ->description),
        editedDescription);

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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(search != nullptr);
    QVERIFY(searchResult != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(fieldPanel != nullptr);
    QVERIFY(fieldContext != nullptr);
    QVERIFY(problems != nullptr);
    QVERIFY(state != nullptr);
    QVERIFY(controller != nullptr);

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

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Inject external address conflict"),
        [](regmap::Workspace& workspace) {
            if (auto* reg = regmap::findRegister(workspace, "reg-control")) {
                reg->offset = 0;
            }
        }));
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
    QCOMPARE(copiedBlock.baseAddress, std::uint64_t{0x1000});
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
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Base 0x1000")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("adjusted from 0x0")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.front().blocks.size(),
                              std::size_t{1}, 2000);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Constrain Page capacity"),
        [](regmap::Workspace& workspace) {
            workspace.addressSpaces.front().addressWidth = 12;
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    blockIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockId);
    QVERIFY(blockIndex.isValid());
    hierarchy->setCurrentIndex(blockIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(hierarchy, Qt::Key_C, Qt::ControlModifier);
    const std::size_t constrainedUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QTest::keyClick(hierarchy, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),
             std::size_t{1});
    QCOMPARE(controller->undoDepth(), constrainedUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot paste Register Block")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("no non-overlapping Block Base fits")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Main")));
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->workspace()->addressSpaces.front().addressWidth,
        std::uint32_t{32}, 2000);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Seed lower-address destination Page"),
        [](regmap::Workspace& workspace) {
            workspace.addressSpaces.front().blocks.front().baseAddress =
                0x2000;
            regmap::AddressSpace destination;
            destination.id = "space-small";
            destination.name = "Small";
            destination.addressWidth = 12;
            workspace.addressSpaces.push_back(std::move(destination));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    blockIndex =
        hierarchyIndexByObjectId(hierarchy->model(), blockId);
    QModelIndex smallPage =
        hierarchyIndexByObjectId(hierarchy->model(),
                                 QStringLiteral("space-small"));
    QVERIFY(blockIndex.isValid());
    QVERIFY(smallPage.isValid());
    hierarchy->setCurrentIndex(blockIndex);
    hierarchy->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(hierarchy, Qt::Key_C, Qt::ControlModifier);
    hierarchy->setCurrentIndex(smallPage);
    window.statusBar()->clearMessage();
    QTest::keyClick(hierarchy, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        controller->workspace()->addressSpaces[1].blocks.size(),
        std::size_t{1}, 2000);
    const auto& crossPageCopy =
        controller->workspace()->addressSpaces[1].blocks.front();
    QCOMPARE(crossPageCopy.baseAddress, std::uint64_t{0});
    QCOMPARE(crossPageCopy.name, std::string("Control Copy"));
    QVERIFY(crossPageCopy.id !=
            controller->workspace()
                ->addressSpaces.front()
                .blocks.front()
                .id);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Base 0x0")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("adjusted from 0x2000")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller->workspace()->addressSpaces[1].blocks.empty(), 2000);
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(controller->workspace()->addressSpaces.size(),
                              std::size_t{1}, 2000);
    QCOMPARE(
        controller->workspace()
            ->addressSpaces.front()
            .blocks.front()
            .baseAddress,
        std::uint64_t{0});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
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
            block.baseAddress = 0x1000;
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
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        std::ranges::any_of(
            controller->workspace()->addressSpaces[0].blocks,
            [](const regmap::RegisterBlock& block) {
                return block.id == "block-control";
            }),
        2000);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Create destination Base conflict"),
        [](regmap::Workspace& workspace) {
            auto* block =
                regmap::findRegisterBlock(workspace, "block-secondary");
            QVERIFY(block != nullptr);
            block->baseAddress = 0;
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    sourceBlock =
        hierarchyIndexByObjectId(hierarchy->model(),
                                 QStringLiteral("block-control"));
    secondaryPage =
        hierarchyIndexByObjectId(hierarchy->model(),
                                 QStringLiteral("space-secondary"));
    QVERIFY(sourceBlock.isValid());
    QVERIFY(secondaryPage.isValid());
    const std::size_t baseConflictUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QVERIFY(dropHierarchyObject(hierarchy, sourceBlock, secondaryPage));
    QVERIFY(std::ranges::any_of(
        controller->workspace()->addressSpaces[0].blocks,
        [](const regmap::RegisterBlock& block) {
            return block.id == "block-control";
        }));
    QVERIFY(std::ranges::none_of(
        controller->workspace()->addressSpaces[1].blocks,
        [](const regmap::RegisterBlock& block) {
            return block.id == "block-control";
        }));
    QCOMPARE(controller->undoDepth(), baseConflictUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot move Register Block Control")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Base 0x0 overlaps")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Edit Block Base")));
    controller->undo();

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Create destination Block name conflict"),
        [](regmap::Workspace& workspace) {
            auto* block =
                regmap::findRegisterBlock(workspace, "block-secondary");
            QVERIFY(block != nullptr);
            block->name = "Control";
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    sourceBlock =
        hierarchyIndexByObjectId(hierarchy->model(),
                                 QStringLiteral("block-control"));
    secondaryPage =
        hierarchyIndexByObjectId(hierarchy->model(),
                                 QStringLiteral("space-secondary"));
    QVERIFY(sourceBlock.isValid());
    QVERIFY(secondaryPage.isValid());
    const std::size_t nameConflictUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    QVERIFY(dropHierarchyObject(hierarchy, sourceBlock, secondaryPage));
    QVERIFY(std::ranges::any_of(
        controller->workspace()->addressSpaces[0].blocks,
        [](const regmap::RegisterBlock& block) {
            return block.id == "block-control";
        }));
    QCOMPARE(controller->undoDepth(), nameConflictUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("already contains a Block with that name")));
    controller->undo();
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::refreshesSearchResultsAfterModelChanges()
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

    auto* search = window.findChild<QLineEdit*>(QStringLiteral("globalSearchEdit"));
    auto* searchResult = window.findChild<QLabel*>(QStringLiteral("searchResultLabel"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(search != nullptr);
    QVERIFY(searchResult != nullptr);
    QVERIFY(registers != nullptr);

    const auto rowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };

    const QString token = QStringLiteral("SEARCH_CACHE_TARGET_7D3A");
    int controlRow = rowForId(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 11), token));
    QTRY_VERIFY_WITH_TIMEOUT(
        rowForId(QStringLiteral("reg-control")) >= 0, 2000);

    search->setText(token);
    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(searchResult->text(), QStringLiteral("1/1"), 2000);

    controlRow = rowForId(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 11),
        QStringLiteral("Description no longer matches the active search.")));
    QTRY_VERIFY_WITH_TIMEOUT(searchResult->text().isEmpty(), 2000);

    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(searchResult->text(), QStringLiteral("0/0"), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("No matching register-map object")));

    controlRow = rowForId(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 11), token));
    QTRY_VERIFY_WITH_TIMEOUT(searchResult->text().isEmpty(), 2000);
    Q_EMIT search->returnPressed();
    QTRY_COMPARE_WITH_TIMEOUT(searchResult->text(), QStringLiteral("1/1"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->currentIndex().data(Qt::UserRole + 1).toString(),
        QStringLiteral("reg-control"), 2000);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsOutOfRangeNumericEdits()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    auto* pageWidth = window.findChild<QLineEdit*>(QStringLiteral("pageWidthEdit"));
    auto* blockSize = window.findChild<QLineEdit*>(QStringLiteral("blockSizeEdit"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);
    QVERIFY(pageWidth != nullptr);
    QVERIFY(blockSize != nullptr);

    const std::size_t initialUndoDepth = controller->undoDepth();
    pageWidth->setText(QStringLiteral("0"));
    Q_EMIT pageWidth->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(pageWidth->text(), QStringLiteral("32"), 2000);
    QCOMPARE(controller->workspace()->addressSpaces.front().addressWidth,
             std::uint32_t{32});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 to 64")));

    blockSize->setText(QStringLiteral("0"));
    Q_EMIT blockSize->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockSize->text(), QStringLiteral("0x1000"), 2000);
    QCOMPARE(controller->workspace()->addressSpaces.front().blocks.front().size,
             std::optional<std::uint64_t>{0x1000});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("positive block size")));

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const QModelIndex fieldReset = fields->model()->index(0, 10);
    QCOMPARE(fieldReset.data().toString(), QStringLiteral("0x0"));
    QVERIFY(fields->model()->setData(fieldReset, QStringLiteral("0x2")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(0, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->resetValue,
             std::optional(regmap::UnsignedValue(0)));
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1-bit field")));

    const QModelIndex controlInitial = registers->model()->index(1, 7);
    QVERIFY(registers->model()->setData(controlInitial, QStringLiteral("0x100")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 7).data().toString(),
                              QStringLiteral("0x100"), 2000);
    const std::size_t registerWidthUndoDepth = controller->undoDepth();
    QVERIFY(registers->model()->setData(registers->model()->index(1, 3),
                                        QStringLiteral("8")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 3).data().toString(),
                              QStringLiteral("32"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->width,
             std::uint32_t{32});
    QCOMPARE(controller->undoDepth(), registerWidthUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial, Reset, and Enum")));

    QVERIFY(registers->model()->setData(registers->model()->index(1, 4),
                                        QStringLiteral("uint8")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 4).data().toString(),
                              QStringLiteral("uint32"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->width,
             std::uint32_t{32});
    QCOMPARE(controller->undoDepth(), registerWidthUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial and Reset")));

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure field width fixture"),
        [](regmap::Workspace& workspace) {
            auto* field = regmap::findField(workspace, "field-ready");
            if (field == nullptr) {
                return;
            }
            field->msb = 1;
            field->type = regmap::FieldType::enumeration;
            regmap::EnumValue zero;
            zero.id = "enum-field-zero";
            zero.name = "ZERO";
            zero.value = regmap::UnsignedValue(0);
            regmap::EnumValue three;
            three.id = "enum-field-three";
            three.name = "THREE";
            three.value = regmap::UnsignedValue(3);
            field->enumValues = {zero, three};
        }));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    int fieldRow = -1;
    for (int row = 0; row < fields->model()->rowCount(); ++row) {
        if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("field-ready")) {
            fieldRow = row;
            break;
        }
    }
    QVERIFY(fieldRow >= 0);
    const std::size_t fieldWidthUndoDepth = controller->undoDepth();
    QVERIFY(fields->model()->setData(fields->model()->index(fieldRow, 4),
                                     QStringLiteral("1")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(fieldRow, 4).data().toString(),
                              QStringLiteral("2"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->width(),
             std::uint64_t{2});
    QCOMPARE(regmap::findEnumValue(*controller->workspace(), "enum-field-three")->value,
             regmap::UnsignedValue(3));
    QCOMPARE(controller->undoDepth(), fieldWidthUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Reset, Enum, and member Field")));

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure enum bounds fixture"),
        [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (reg == nullptr) {
                return;
            }
            reg->width = 2;
            reg->type = regmap::FieldType::enumeration;
            reg->fields.clear();
            reg->array.stride = 1;
            regmap::EnumValue zero;
            zero.id = "enum-zero";
            zero.name = "ZERO";
            zero.value = regmap::UnsignedValue(0);
            regmap::EnumValue one;
            one.id = "enum-one";
            one.name = "ONE";
            one.value = regmap::UnsignedValue(3);
            reg->enumValues = {zero, one};
        }));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    int zeroRow = -1;
    for (int row = 0; row < enums->model()->rowCount(); ++row) {
        if (enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("enum-zero")) {
            zeroRow = row;
            break;
        }
    }
    QVERIFY(zeroRow >= 0);
    const std::size_t enumUndoDepth = controller->undoDepth();
    const QModelIndex enumValue = enums->model()->index(zeroRow, 1);
    QCOMPARE(enumValue.data().toString(), QStringLiteral("0x0"));
    QVERIFY(enums->model()->setData(enumValue, QStringLiteral("0x4")));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->index(zeroRow, 1).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(regmap::findEnumValue(*controller->workspace(), "enum-zero")->value,
             regmap::UnsignedValue(0));
    QCOMPARE(controller->undoDepth(), enumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2-bit value")));

    const std::size_t enumWidthUndoDepth = controller->undoDepth();
    QVERIFY(registers->model()->setData(registers->model()->index(0, 3),
                                        QStringLiteral("1")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 3).data().toString(),
                              QStringLiteral("2"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->width,
             std::uint32_t{2});
    QCOMPARE(controller->undoDepth(), enumWidthUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial, Reset, and Enum")));

    QVERIFY(registers->model()->setData(registers->model()->index(0, 4),
                                        QStringLiteral("bool")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 4).data().toString(),
                              QStringLiteral("enum"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->width,
             std::uint32_t{2});
    QCOMPARE(controller->undoDepth(), enumWidthUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial, Reset, and Enum")));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsInvalidNumericRangesDuringEditing()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure numeric range fixture"),
        [](regmap::Workspace& workspace) {
            auto* field = regmap::findField(workspace, "field-ready");
            if (field == nullptr) {
                return;
            }
            field->msb = 7;
            field->lsb = 0;
            field->type = regmap::FieldType::unsignedInteger;
            field->minimumValue = "0";
            field->maximumValue = "100";
            field->enumValues.clear();
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRow = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    const std::size_t initialUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 6), QStringLiteral("10 .. 1")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 6)
            .data()
            .toString(),
        QString{}, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(!regmap::findRegister(*controller->workspace(), "reg-control")
                 ->minimumValue.has_value());
    QVERIFY(!regmap::findRegister(*controller->workspace(), "reg-control")
                 ->maximumValue.has_value());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("numeric range")));

    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 6), QStringLiteral("-1 .. 10")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 6)
            .data()
            .toString(),
        QString{}, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 6),
        QStringLiteral("0 .. 0x100000000")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 6)
            .data()
            .toString(),
        QString{}, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 6), QStringLiteral("0 .. 255")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 6)
            .data()
            .toString(),
        QStringLiteral("0 .. 255"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->minimumValue,
             std::optional<std::string>{"0"});
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->maximumValue,
             std::optional<std::string>{"255"});

    const int statusRow = registerRow(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    Q_EMIT registers->clicked(registers->model()->index(statusRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRow = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(readyRow >= 0);
    const std::size_t registerRangeUndoDepth = controller->undoDepth();

    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 6), QStringLiteral("101")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 6)
            .data()
            .toString(),
        QStringLiteral("0"), 2000);
    QCOMPARE(controller->undoDepth(), registerRangeUndoDepth);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->minimumValue,
             std::optional<std::string>{"0"});
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("numeric range")));

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 7), QStringLiteral("256")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 7)
            .data()
            .toString(),
        QStringLiteral("100"), 2000);
    QCOMPARE(controller->undoDepth(), registerRangeUndoDepth);

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 7), QStringLiteral("200")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 7)
            .data()
            .toString(),
        QStringLiteral("200"), 2000);
    QCOMPARE(controller->undoDepth(), registerRangeUndoDepth + 1);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->maximumValue,
             std::optional<std::string>{"200"});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure invalid range recovery fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* field = regmap::findField(workspace, "field-ready")) {
                field->minimumValue = "invalid-minimum";
                field->maximumValue = "invalid-maximum";
            }
        }));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(readyRow >= 0);
    QCOMPARE(fields->model()->index(readyRow, 6).data().toString(),
             QStringLiteral("invalid-minimum"));
    QCOMPARE(fields->model()->index(readyRow, 7).data().toString(),
             QStringLiteral("invalid-maximum"));
    const std::size_t recoveryUndoDepth = controller->undoDepth();

    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 6), QString{}));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 6)
            .data()
            .toString(),
        QString{}, 2000);
    QCOMPARE(controller->undoDepth(), recoveryUndoDepth + 1);
    QVERIFY(!regmap::findField(*controller->workspace(), "field-ready")
                 ->minimumValue.has_value());
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->maximumValue,
             std::optional<std::string>{"invalid-maximum"});

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 7), QString{}));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 7)
            .data()
            .toString(),
        QString{}, 2000);
    QCOMPARE(controller->undoDepth(), recoveryUndoDepth + 2);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::protectsNumericRangesDuringShapeChanges()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Range lifecycle fixture"),
        [](regmap::Workspace& workspace) {
            auto* control =
                regmap::findRegister(workspace, "reg-control");
            auto* ready =
                regmap::findField(workspace, "field-ready");
            QVERIFY(control != nullptr);
            QVERIFY(ready != nullptr);
            control->type = regmap::FieldType::unsignedInteger;
            control->width = 32;
            control->minimumValue = "0";
            control->maximumValue = "300";
            control->enumValues.clear();
            ready->msb = 7;
            ready->lsb = 0;
            ready->type = regmap::FieldType::unsignedInteger;
            ready->minimumValue = "0";
            ready->maximumValue = "200";
            ready->enumValues.clear();
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        registerRowForId(QStringLiteral("reg-control")) >= 0, 2000);
    int controlRow = registerRowForId(QStringLiteral("reg-control"));
    registers->setCurrentIndex(registers->model()->index(controlRow, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const std::size_t registerUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("uint8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRowForId(QStringLiteral("reg-control")), 4)
            .data()
            .toString(),
        QStringLiteral("uint32"), 2000);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("existing Range")));

    controlRow = registerRowForId(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 3),
        QStringLiteral("8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRowForId(QStringLiteral("reg-control")), 3)
            .data()
            .toString(),
        QStringLiteral("32"), 2000);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("numeric Range")));

    controlRow = registerRowForId(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("int8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRowForId(QStringLiteral("reg-control")), 4)
            .data()
            .toString(),
        QStringLiteral("uint32"), 2000);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);

    const auto actions = window.findChildren<QAction*>();
    const auto addEnum = std::ranges::find_if(
        actions, [](const QAction* action) {
            return action->text() == QStringLiteral("Add Enum Value");
        });
    QVERIFY(addEnum != actions.end());
    window.statusBar()->clearMessage();
    (*addEnum)->trigger();
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")->type,
        regmap::FieldType::unsignedInteger);
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")
            ->maximumValue,
        std::optional<std::string>{"300"});
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Change Type to enum first")));

    bool registerCancelSeen = false;
    QString registerCancelFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            registerCancelFailure =
                QStringLiteral("Register Range confirmation did not open");
            return;
        }
        registerCancelSeen = true;
        if (dialog->windowTitle() !=
                QStringLiteral("Change Numeric Register Type") ||
            !dialog->text().contains(QStringLiteral("CONTROL")) ||
            !dialog->text().contains(QStringLiteral("2 Range bounds")) ||
            !dialog->text().contains(QStringLiteral("Ctrl+Z")) ||
            dialog->defaultButton() != dialog->button(QMessageBox::No)) {
            registerCancelFailure =
                QStringLiteral("Register Range confirmation lacks impact details");
        }
        if (auto* cancel = dialog->button(QMessageBox::No)) {
            QTest::mouseClick(cancel, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    controlRow = registerRowForId(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("enum")));
    QCoreApplication::processEvents();
    QVERIFY2(registerCancelFailure.isEmpty(),
             qPrintable(registerCancelFailure));
    QVERIFY(registerCancelSeen);
    const auto* retainedRegister =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(retainedRegister != nullptr);
    QCOMPARE(retainedRegister->type,
             regmap::FieldType::unsignedInteger);
    QCOMPARE(retainedRegister->minimumValue,
             std::optional<std::string>{"0"});
    QCOMPARE(retainedRegister->maximumValue,
             std::optional<std::string>{"300"});
    QVERIFY(retainedRegister->enumValues.empty());
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("cancelled")));

    auto* clipboardData = new QMimeData;
    clipboardData->setText(QStringLiteral("enum"));
    QApplication::clipboard()->setMimeData(clipboardData);
    controlRow = registerRowForId(QStringLiteral("reg-control"));
    const QModelIndex registerType =
        registers->model()->index(controlRow, 4);
    registers->setCurrentIndex(registerType);
    registers->selectionModel()->select(
        registerType, QItemSelectionModel::ClearAndSelect);
    window.activateWindow();
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const auto paste = std::ranges::find_if(
        actions, [](const QAction* action) {
            return action->shortcut().matches(QKeySequence::Paste) ==
                   QKeySequence::ExactMatch;
        });
    QVERIFY(paste != actions.end());
    window.statusBar()->clearMessage();
    (*paste)->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")->type,
        regmap::FieldType::unsignedInteger);
    QCOMPARE(controller->undoDepth(), registerUndoDepth);
    QVERIFY2(
        window.statusBar()->currentMessage().contains(
            QStringLiteral("individually confirmed")),
        qPrintable(window.statusBar()->currentMessage()));

    bool registerAcceptSeen = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        registerAcceptSeen = true;
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    controlRow = registerRowForId(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4),
        QStringLiteral("enum")));
    QCoreApplication::processEvents();
    QVERIFY(registerAcceptSeen);
    const auto* enumRegister =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(enumRegister != nullptr);
    QCOMPARE(enumRegister->type, regmap::FieldType::enumeration);
    QVERIFY(!enumRegister->minimumValue.has_value());
    QVERIFY(!enumRegister->maximumValue.has_value());
    QCOMPARE(enumRegister->enumValues.size(), std::size_t{1});
    QCOMPARE(controller->undoDepth(), registerUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2 Range bounds removed")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("NEW_VALUE created")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-control")->type ==
            regmap::FieldType::unsignedInteger,
        2000);
    QCOMPARE(
        regmap::findRegister(*controller->workspace(), "reg-control")
            ->maximumValue,
        std::optional<std::string>{"300"});
    QCOMPARE(controller->undoDepth(), registerUndoDepth);

    const int statusRow =
        registerRowForId(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    Q_EMIT registers->clicked(
        registers->model()->index(statusRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRowForId = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        fieldRowForId(QStringLiteral("field-ready")) >= 0, 2000);
    int readyRow = fieldRowForId(QStringLiteral("field-ready"));
    const std::size_t fieldUndoDepth = controller->undoDepth();

    for (const auto& edit :
         std::array<std::pair<int, QString>, 3>{
             std::pair{4, QStringLiteral("7")},
             std::pair{2, QStringLiteral("6")},
             std::pair{5, QStringLiteral("uint7")}}) {
        readyRow = fieldRowForId(QStringLiteral("field-ready"));
        QVERIFY(readyRow >= 0);
        QVERIFY(fields->model()->setData(
            fields->model()->index(readyRow, edit.first), edit.second));
        QTRY_COMPARE_WITH_TIMEOUT(
            fields->model()
                ->index(fieldRowForId(QStringLiteral("field-ready")), 4)
                .data()
                .toString(),
            QStringLiteral("8"), 2000);
        QCOMPARE(controller->undoDepth(), fieldUndoDepth);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Range")));
    }

    readyRow = fieldRowForId(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5),
        QStringLiteral("int8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRowForId(QStringLiteral("field-ready")), 5)
            .data()
            .toString(),
        QStringLiteral("uint8"), 2000);
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);

    auto* fieldClipboardData = new QMimeData;
    fieldClipboardData->setText(QStringLiteral("bits"));
    QApplication::clipboard()->setMimeData(fieldClipboardData);
    readyRow = fieldRowForId(QStringLiteral("field-ready"));
    const QModelIndex fieldType =
        fields->model()->index(readyRow, 5);
    fields->setCurrentIndex(fieldType);
    fields->selectionModel()->select(
        fieldType, QItemSelectionModel::ClearAndSelect);
    window.activateWindow();
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    window.statusBar()->clearMessage();
    (*paste)->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findField(*controller->workspace(), "field-ready")->type,
        regmap::FieldType::unsignedInteger);
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);
    QVERIFY2(
        window.statusBar()->currentMessage().contains(
            QStringLiteral("individually confirmed")),
        qPrintable(window.statusBar()->currentMessage()));

    bool fieldAcceptSeen = false;
    QString fieldAcceptFailure;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            fieldAcceptFailure =
                QStringLiteral("Field Range confirmation did not open");
            return;
        }
        fieldAcceptSeen = true;
        if (dialog->windowTitle() !=
                QStringLiteral("Change Numeric Field Type") ||
            !dialog->text().contains(QStringLiteral("READY")) ||
            !dialog->text().contains(QStringLiteral("2 Range bounds"))) {
            fieldAcceptFailure =
                QStringLiteral("Field Range confirmation lacks impact details");
        }
        if (auto* confirm = dialog->button(QMessageBox::Yes)) {
            QTest::mouseClick(confirm, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    readyRow = fieldRowForId(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5),
        QStringLiteral("bits")));
    QCoreApplication::processEvents();
    QVERIFY2(fieldAcceptFailure.isEmpty(),
             qPrintable(fieldAcceptFailure));
    QVERIFY(fieldAcceptSeen);
    const auto* bitsField =
        regmap::findField(*controller->workspace(), "field-ready");
    QVERIFY(bitsField != nullptr);
    QCOMPARE(bitsField->type, regmap::FieldType::bits);
    QVERIFY(!bitsField->minimumValue.has_value());
    QVERIFY(!bitsField->maximumValue.has_value());
    QCOMPARE(controller->undoDepth(), fieldUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("2 Range bounds removed")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findField(*controller->workspace(), "field-ready")->type ==
            regmap::FieldType::unsignedInteger,
        2000);
    QCOMPARE(
        regmap::findField(*controller->workspace(), "field-ready")
            ->maximumValue,
        std::optional<std::string>{"200"});
    QCOMPARE(controller->undoDepth(), fieldUndoDepth);

    readyRow = fieldRowForId(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 7), QStringLiteral("100")));
    readyRow = fieldRowForId(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5), QStringLiteral("uint7")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRowForId(QStringLiteral("field-ready")), 5)
            .data()
            .toString(),
        QStringLiteral("uint7"), 2000);
    QCOMPARE(
        regmap::findField(*controller->workspace(), "field-ready")
            ->maximumValue,
        std::optional<std::string>{"100"});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::duplicatesCompleteRegisterSafely()
{
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifest =
            directory.filePath(
                QStringLiteral("duplicate.regmap.yaml"));
        createProject(manifest);

        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1100, 720);
        window.show();
        QTest::qWait(50);

        auto* controller =
            window.findChild<ProjectController*>();
        auto* registers =
            window.findChild<QTableView*>(
                QStringLiteral("registerView"));
        QVERIFY(controller != nullptr);
        QVERIFY(registers != nullptr);
        QVERIFY(controller->editWorkspace(
            QStringLiteral("Configure duplicate fixture"),
            [](regmap::Workspace& workspace) {
                auto* reg =
                    regmap::findRegister(workspace, "reg-status");
                auto* ready =
                    regmap::findField(workspace, "field-ready");
                QVERIFY(reg != nullptr);
                QVERIFY(ready != nullptr);
                reg->description =
                    "Complete register description.";
                reg->tags = {"status", "copied"};
                ready->description = "Ready field description.";
                ready->type = regmap::FieldType::enumeration;

                regmap::EnumValue disabled;
                disabled.id = "enum-disabled";
                disabled.name = "DISABLED";
                disabled.value = regmap::UnsignedValue(0);
                disabled.description = "Disabled state.";
                regmap::EnumValue enabled;
                enabled.id = "enum-enabled";
                enabled.name = "ENABLED";
                enabled.value = regmap::UnsignedValue(1);
                enabled.description = "Enabled state.";
                ready->enumValues = {disabled, enabled};

                regmap::Field count;
                count.id = "field-count";
                count.name = "COUNT";
                count.msb = 7;
                count.lsb = 1;
                count.type =
                    regmap::FieldType::unsignedInteger;
                count.softwareAccess =
                    regmap::AccessMode::readOnly;
                count.hardwareAccess =
                    regmap::AccessMode::writeOnly;
                count.resetValue = regmap::UnsignedValue(0);
                count.readSideEffect =
                    regmap::ReadSideEffect::none;
                count.writeSideEffect =
                    regmap::WriteSideEffect::none;
                count.minimumValue = "0";
                count.maximumValue = "100";
                count.description =
                    "Numeric range description.";
                reg->fields.push_back(std::move(count));
            }));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());
        const std::size_t initialUndoDepth =
            controller->undoDepth();

        QString failure;
        QVERIFY(invokeTableContextAction(
            window, registers, 0,
            QStringLiteral("duplicateRegisterAction"),
            failure));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));

        const auto* block = regmap::findRegisterBlock(
            *controller->workspace(), "block-control");
        QVERIFY(block != nullptr);
        QCOMPARE(block->registers.size(), std::size_t{2});
        const auto& source = block->registers[0];
        const auto& copy = block->registers[1];
        const std::string copyId = copy.id;
        QCOMPARE(copy.name, std::string("STATUS Copy"));
        QCOMPARE(copy.offset, std::uint64_t{4});
        QCOMPARE(copy.type, source.type);
        QCOMPARE(copy.width, source.width);
        QCOMPARE(copy.tags, source.tags);
        QCOMPARE(copy.description, source.description);
        QCOMPARE(copy.fields.size(), source.fields.size());
        QCOMPARE(copy.fields.size(), std::size_t{2});
        QCOMPARE(copy.id == source.id, false);
        QCOMPARE(copy.fields[0].id == source.fields[0].id,
                 false);
        QCOMPARE(copy.fields[1].id == source.fields[1].id,
                 false);
        QCOMPARE(copy.fields[0].enumValues.size(),
                 std::size_t{2});
        QCOMPARE(copy.fields[0].enumValues[0].name,
                 std::string("DISABLED"));
        QCOMPARE(
            copy.fields[0].enumValues[0].id ==
                source.fields[0].enumValues[0].id,
            false);
        QCOMPARE(copy.fields[1].minimumValue,
                 std::optional<std::string>("0"));
        QCOMPARE(copy.fields[1].maximumValue,
                 std::optional<std::string>("100"));
        QVERIFY(copy.source.empty());
        QVERIFY(copy.propertySources.empty());
        QVERIFY(copy.fields[0].source.empty());
        QVERIFY(copy.fields[0].propertySources.empty());
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());
        QCOMPARE(controller->undoDepth(),
                 initialUndoDepth + 1);
        QCOMPARE(registers->model()->rowCount(), 3);
        QCOMPARE(
            registers->currentIndex()
                .data(Qt::UserRole + 1)
                .toString()
                .toStdString(),
            copyId);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Duplicated Register STATUS")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Offset 0x4")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Ctrl+Z")));

        controller->undo();
        QTRY_COMPARE_WITH_TIMEOUT(
            regmap::findRegisterBlock(
                *controller->workspace(), "block-control")
                ->registers.size(),
            std::size_t{1}, 2000);
        QCOMPARE(controller->undoDepth(), initialUndoDepth);
        QVERIFY(regmap::findRegister(
                    *controller->workspace(), copyId) == nullptr);
        const auto* restored = regmap::findRegister(
            *controller->workspace(), "reg-status");
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->fields.size(), std::size_t{2});
        QCOMPARE(restored->fields[0].enumValues.size(),
                 std::size_t{2});

        makeGeneratedFilesWritable(directory.path());
    }

    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifest =
            directory.filePath(
                QStringLiteral("full-block.regmap.yaml"));
        createProject(manifest);

        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1100, 720);
        window.show();
        QTest::qWait(50);

        auto* controller =
            window.findChild<ProjectController*>();
        auto* registers =
            window.findChild<QTableView*>(
                QStringLiteral("registerView"));
        QVERIFY(controller != nullptr);
        QVERIFY(registers != nullptr);
        QVERIFY(controller->editWorkspace(
            QStringLiteral("Fill duplicate target Block"),
            [](regmap::Workspace& workspace) {
                auto* block = regmap::findRegisterBlock(
                    workspace, "block-control");
                QVERIFY(block != nullptr);
                block->size = 4;
            }));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());
        const std::size_t initialUndoDepth =
            controller->undoDepth();

        QString failure;
        QVERIFY(invokeTableContextAction(
            window, registers, 0,
            QStringLiteral("duplicateRegisterAction"),
            failure));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        const auto* block = regmap::findRegisterBlock(
            *controller->workspace(), "block-control");
        QVERIFY(block != nullptr);
        QCOMPARE(block->registers.size(), std::size_t{1});
        QCOMPARE(controller->undoDepth(), initialUndoDepth);
        QCOMPARE(registers->model()->rowCount(), 2);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Cannot duplicate Register STATUS")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("does not fit Block Control")));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());

        makeGeneratedFilesWritable(directory.path());
    }
}

void GuiSmokeTests::duplicatesCompleteFieldSafely()
{
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifest =
            directory.filePath(
                QStringLiteral("duplicate-field.regmap.yaml"));
        createProject(manifest);

        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1100, 720);
        window.show();
        QTest::qWait(50);

        auto* controller =
            window.findChild<ProjectController*>();
        auto* registers =
            window.findChild<QTableView*>(
                QStringLiteral("registerView"));
        auto* fields =
            window.findChild<QTableView*>(
                QStringLiteral("fieldView"));
        QVERIFY(controller != nullptr);
        QVERIFY(registers != nullptr);
        QVERIFY(fields != nullptr);

        QVERIFY(controller->editWorkspace(
            QStringLiteral("Configure Field duplicate fixture"),
            [](regmap::Workspace& workspace) {
                auto* reg =
                    regmap::findRegister(workspace, "reg-status");
                QVERIFY(reg != nullptr);
                reg->resetValue = regmap::UnsignedValue(0xA0);

                regmap::EnumValue zero;
                zero.id = "enum-mode-zero";
                zero.name = "ZERO";
                zero.value = regmap::UnsignedValue(0);
                zero.description = "Zero mode.";
                regmap::EnumValue one;
                one.id = "enum-mode-one";
                one.name = "ONE";
                one.value = regmap::UnsignedValue(1);
                one.description = "Mode one.";

                regmap::Field state;
                state.id = "field-mode-state";
                state.name = "STATE";
                state.msb = 1;
                state.lsb = 1;
                state.type =
                    regmap::FieldType::enumeration;
                state.softwareAccess =
                    regmap::AccessMode::readOnly;
                state.hardwareAccess =
                    regmap::AccessMode::writeOnly;
                state.resetValue = regmap::UnsignedValue(1);
                state.readSideEffect =
                    regmap::ReadSideEffect::none;
                state.writeSideEffect =
                    regmap::WriteSideEffect::none;
                state.description =
                    "Nested enum description.";
                state.enumValues = {zero, one};
                state.source.sheet = "Legacy Fields";
                state.propertySources["name"].cell = "C7";

                regmap::Field limit;
                limit.id = "field-mode-limit";
                limit.name = "LIMIT";
                limit.msb = 0;
                limit.lsb = 0;
                limit.type =
                    regmap::FieldType::unsignedInteger;
                limit.softwareAccess =
                    regmap::AccessMode::readOnly;
                limit.hardwareAccess =
                    regmap::AccessMode::writeOnly;
                limit.resetValue = regmap::UnsignedValue(0);
                limit.readSideEffect =
                    regmap::ReadSideEffect::none;
                limit.writeSideEffect =
                    regmap::WriteSideEffect::none;
                limit.minimumValue = "0";
                limit.maximumValue = "1";
                limit.description =
                    "Nested numeric description.";

                regmap::Field mode;
                mode.id = "field-mode";
                mode.name = "MODE";
                mode.msb = 7;
                mode.lsb = 4;
                mode.type = regmap::FieldType::structure;
                mode.softwareAccess =
                    regmap::AccessMode::readOnly;
                mode.hardwareAccess =
                    regmap::AccessMode::writeOnly;
                mode.resetValue = regmap::UnsignedValue(0xA);
                mode.readSideEffect =
                    regmap::ReadSideEffect::none;
                mode.writeSideEffect =
                    regmap::WriteSideEffect::none;
                mode.description =
                    "Compound Field description.";
                mode.members = {state, limit};
                mode.source.sheet = "Legacy Fields";
                mode.propertySources["name"].cell = "C6";
                reg->fields.push_back(std::move(mode));
            }));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());

        Q_EMIT registers->clicked(
            registers->model()->index(0, 5));
        QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
        const auto fieldRowForId =
            [fields](const QString& id) {
                for (int row = 0;
                     row < fields->model()->rowCount(); ++row) {
                    if (fields->model()
                            ->index(row, 0)
                            .data(Qt::UserRole + 1)
                            .toString() == id) {
                        return row;
                    }
                }
                return -1;
            };
        QTRY_VERIFY_WITH_TIMEOUT(
            fieldRowForId(QStringLiteral("field-mode")) >= 0,
            2000);
        const std::size_t initialUndoDepth =
            controller->undoDepth();

        QString failure;
        QVERIFY(invokeTableContextAction(
            window, fields,
            fieldRowForId(QStringLiteral("field-mode")),
            QStringLiteral("duplicateFieldAction"),
            failure));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));

        const auto* reg = regmap::findRegister(
            *controller->workspace(), "reg-status");
        QVERIFY(reg != nullptr);
        QCOMPARE(reg->fields.size(), std::size_t{3});
        const auto& source = reg->fields[1];
        const auto& copy = reg->fields[2];
        const std::string copyId = copy.id;
        QCOMPARE(copy.name, std::string("MODE Copy"));
        QCOMPARE(copy.msb, std::uint32_t{11});
        QCOMPARE(copy.lsb, std::uint32_t{8});
        QCOMPARE(copy.type, source.type);
        QCOMPARE(copy.softwareAccess, source.softwareAccess);
        QCOMPARE(copy.hardwareAccess, source.hardwareAccess);
        QCOMPARE(copy.readSideEffect, source.readSideEffect);
        QCOMPARE(copy.writeSideEffect, source.writeSideEffect);
        QCOMPARE(copy.description, source.description);
        QCOMPARE(copy.members.size(), std::size_t{2});
        QCOMPARE(copy.id == source.id, false);
        QCOMPARE(copy.members[0].id ==
                     source.members[0].id,
                 false);
        QCOMPARE(copy.members[1].id ==
                     source.members[1].id,
                 false);
        QCOMPARE(copy.members[0].enumValues.size(),
                 std::size_t{2});
        QCOMPARE(copy.members[0].enumValues[1].name,
                 std::string("ONE"));
        QCOMPARE(
            copy.members[0].enumValues[1].id ==
                source.members[0].enumValues[1].id,
            false);
        QCOMPARE(copy.members[1].minimumValue,
                 std::optional<std::string>("0"));
        QCOMPARE(copy.members[1].maximumValue,
                 std::optional<std::string>("1"));
        QCOMPARE(copy.resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0)));
        QCOMPARE(copy.members[0].resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0)));
        QCOMPARE(source.resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0xA)));
        QCOMPARE(reg->resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0xA0)));
        QVERIFY(copy.source.empty());
        QVERIFY(copy.propertySources.empty());
        QVERIFY(copy.members[0].source.empty());
        QVERIFY(copy.members[0].propertySources.empty());
        QVERIFY(copy.members[0].enumValues[0].source.empty());
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());
        QCOMPARE(controller->undoDepth(),
                 initialUndoDepth + 1);
        QCOMPARE(fields->model()->rowCount(), 8);
        QCOMPARE(
            fields->currentIndex()
                .data(Qt::UserRole + 1)
                .toString()
                .toStdString(),
            copyId);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Duplicated Field MODE")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("bits 11:8")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral(
                "Reset adapted to destination bits")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Ctrl+Z")));

        controller->undo();
        QTRY_COMPARE_WITH_TIMEOUT(
            regmap::findRegister(
                *controller->workspace(), "reg-status")
                ->fields.size(),
            std::size_t{2}, 2000);
        QCOMPARE(controller->undoDepth(), initialUndoDepth);
        QVERIFY(regmap::findField(
                    *controller->workspace(), copyId) == nullptr);
        const auto* restored = regmap::findField(
            *controller->workspace(), "field-mode");
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->members.size(), std::size_t{2});
        QCOMPARE(restored->resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0xA)));

        QTRY_VERIFY_WITH_TIMEOUT(
            fieldRowForId(
                QStringLiteral("field-mode-state")) >= 0,
            2000);
        QString nestedFailure;
        QVERIFY(invokeTableContextAction(
            window, fields,
            fieldRowForId(
                QStringLiteral("field-mode-state")),
            QStringLiteral("duplicateFieldAction"),
            nestedFailure));
        QVERIFY2(nestedFailure.isEmpty(),
                 qPrintable(nestedFailure));

        const auto* modeAfterNestedCopy = regmap::findField(
            *controller->workspace(), "field-mode");
        QVERIFY(modeAfterNestedCopy != nullptr);
        QCOMPARE(modeAfterNestedCopy->members.size(),
                 std::size_t{3});
        const auto& nestedSource =
            modeAfterNestedCopy->members[0];
        const auto& nestedCopy =
            modeAfterNestedCopy->members[2];
        const std::string nestedCopyId = nestedCopy.id;
        QCOMPARE(nestedCopy.name,
                 std::string("STATE Copy"));
        QCOMPARE(nestedCopy.msb, std::uint32_t{2});
        QCOMPARE(nestedCopy.lsb, std::uint32_t{2});
        QCOMPARE(nestedCopy.enumValues.size(),
                 std::size_t{2});
        QCOMPARE(nestedCopy.enumValues[1].name,
                 std::string("ONE"));
        QCOMPARE(nestedCopy.id == nestedSource.id, false);
        QCOMPARE(nestedCopy.enumValues[1].id ==
                     nestedSource.enumValues[1].id,
                 false);
        QCOMPARE(nestedCopy.resetValue,
                 std::optional<regmap::UnsignedValue>(
                     regmap::UnsignedValue(0)));
        QCOMPARE(controller->undoDepth(),
                 initialUndoDepth + 1);
        QCOMPARE(fields->model()->rowCount(), 6);
        QCOMPARE(
            fields->currentIndex()
                .data(Qt::UserRole + 1)
                .toString()
                .toStdString(),
            nestedCopyId);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("bits 2:2")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral(
                "Reset adapted to destination bits")));

        controller->undo();
        QTRY_COMPARE_WITH_TIMEOUT(
            regmap::findField(
                *controller->workspace(), "field-mode")
                ->members.size(),
            std::size_t{2}, 2000);
        QCOMPARE(controller->undoDepth(), initialUndoDepth);

        makeGeneratedFilesWritable(directory.path());
    }

    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString manifest =
            directory.filePath(
                QStringLiteral("full-field.regmap.yaml"));
        createProject(manifest);

        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1100, 720);
        window.show();
        QTest::qWait(50);

        auto* controller =
            window.findChild<ProjectController*>();
        auto* registers =
            window.findChild<QTableView*>(
                QStringLiteral("registerView"));
        auto* fields =
            window.findChild<QTableView*>(
                QStringLiteral("fieldView"));
        QVERIFY(controller != nullptr);
        QVERIFY(registers != nullptr);
        QVERIFY(fields != nullptr);
        QVERIFY(controller->editWorkspace(
            QStringLiteral("Fill Field target"),
            [](regmap::Workspace& workspace) {
                auto* reg = regmap::findRegister(
                    workspace, "reg-status");
                QVERIFY(reg != nullptr);
                reg->width = 1;
            }));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());

        Q_EMIT registers->clicked(
            registers->model()->index(0, 5));
        QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
        const std::size_t initialUndoDepth =
            controller->undoDepth();

        QString failure;
        QVERIFY(invokeTableContextAction(
            window, fields, 0,
            QStringLiteral("duplicateFieldAction"),
            failure));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        const auto* reg = regmap::findRegister(
            *controller->workspace(), "reg-status");
        QVERIFY(reg != nullptr);
        QCOMPARE(reg->fields.size(), std::size_t{1});
        QCOMPARE(controller->undoDepth(), initialUndoDepth);
        QCOMPARE(fields->model()->rowCount(), 2);
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Cannot duplicate Field READY")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral(
                "no contiguous 1-bit free range remains in Register STATUS")));
        QVERIFY(regmap::validateWorkspace(
                    *controller->workspace())
                    .empty());

        makeGeneratedFilesWritable(directory.path());
    }
}

void GuiSmokeTests::duplicatesFocusedObjectsWithShortcut()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(
            QStringLiteral("duplicate-shortcut.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    QVERIFY(window.openProjectPath(manifest));
    window.resize(1100, 720);
    window.show();
    window.activateWindow();
    QTest::qWait(50);

    auto* controller =
        window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(
            QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(
            QStringLiteral("fieldView"));
    auto* search =
        window.findChild<QLineEdit*>(
            QStringLiteral("globalSearchEdit"));
    auto* duplicate =
        window.findChild<QAction*>(
            QStringLiteral("duplicateSelectionAction"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(search != nullptr);
    QVERIFY(duplicate != nullptr);
    QCOMPARE(duplicate->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+D")));
    QCOMPARE(duplicate->shortcutContext(),
             Qt::WidgetWithChildrenShortcut);
    QTRY_VERIFY_WITH_TIMEOUT(duplicate->isEnabled(), 2000);

    const std::size_t initialUndoDepth =
        controller->undoDepth();
    search->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(search, Qt::Key_D,
                    Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    const int addRegisterRow =
        registers->model()->rowCount() - 1;
    registers->setCurrentIndex(
        registers->model()->index(addRegisterRow, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(registers, Qt::Key_D,
                    Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral(
            "select an existing Register row")));

    const QModelIndex tagIndex =
        registers->model()->index(0, 10);
    registers->setCurrentIndex(tagIndex);
    Q_EMIT registers->doubleClicked(tagIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* tagPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(tagPopup != nullptr);
    QCOMPARE(tagPopup->objectName(),
             QStringLiteral("tagPopup"));
    auto* tagSearch =
        tagPopup->findChild<QLineEdit*>(
            QStringLiteral("tagSearch"));
    QVERIFY(tagSearch != nullptr);
    tagSearch->setText(QStringLiteral("status"));
    tagSearch->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(tagSearch, Qt::Key_D,
                    Qt::ControlModifier);
    duplicate->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QCOMPARE(QApplication::activePopupWidget(),
             tagPopup);
    QVERIFY(tagSearch->hasFocus());
    QCOMPARE(tagSearch->text(),
             QStringLiteral("status"));
    tagPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);

    const QModelIndex accessIndex =
        registers->model()->index(0, 9);
    registers->setCurrentIndex(accessIndex);
    Q_EMIT registers->doubleClicked(accessIndex);
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() != nullptr, 2000);
    auto* accessPopup =
        qobject_cast<QFrame*>(
            QApplication::activePopupWidget());
    QVERIFY(accessPopup != nullptr);
    QCOMPARE(accessPopup->objectName(),
             QStringLiteral("accessPopup"));
    auto* accessOptions =
        accessPopup->findChild<QListWidget*>(
            QStringLiteral("accessOptions"));
    QVERIFY(accessOptions != nullptr);
    QVERIFY(accessOptions->currentItem() != nullptr);
    const QString accessBefore =
        accessOptions->currentItem()->text();
    accessOptions->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(accessOptions, Qt::Key_D,
                    Qt::ControlModifier);
    duplicate->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QCOMPARE(QApplication::activePopupWidget(),
             accessPopup);
    QVERIFY(accessOptions->hasFocus());
    QCOMPARE(accessOptions->currentItem()->text(),
             accessBefore);
    accessPopup->close();
    QTRY_VERIFY_WITH_TIMEOUT(
        QApplication::activePopupWidget() == nullptr, 2000);

    registers->setCurrentIndex(
        registers->model()->index(0, 0));
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(registers, Qt::Key_D,
                    Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{2}, 2000);
    const auto* block = regmap::findRegisterBlock(
        *controller->workspace(), "block-control");
    QVERIFY(block != nullptr);
    QCOMPARE(block->registers[1].name,
             std::string("STATUS Copy"));
    QCOMPARE(block->registers[1].offset,
             std::uint64_t{4});
    QCOMPARE(controller->undoDepth(),
             initialUndoDepth + 1);
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1}, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    Q_EMIT registers->clicked(
        registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    fields->setCurrentIndex(
        fields->model()->index(0, 0));
    fields->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(fields, Qt::Key_D,
                    Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegister(
            *controller->workspace(), "reg-status")
            ->fields.size(),
        std::size_t{2}, 2000);
    const auto* readyCopy = &regmap::findRegister(
        *controller->workspace(), "reg-status")
                                  ->fields[1];
    QCOMPARE(readyCopy->name,
             std::string("READY Copy"));
    QCOMPARE(readyCopy->msb, std::uint32_t{1});
    QCOMPARE(readyCopy->lsb, std::uint32_t{1});
    QCOMPARE(controller->undoDepth(),
             initialUndoDepth + 1);
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegister(
            *controller->workspace(), "reg-status")
            ->fields.size(),
        std::size_t{1}, 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    const auto visibleEditor =
        [](QTableView* view) -> QLineEdit* {
            for (auto* editor :
                 view->findChildren<QLineEdit*>()) {
                if (editor->isVisible()) {
                    return editor;
                }
            }
            return nullptr;
        };

    const QModelIndex name =
        registers->model()->index(0, 0);
    registers->setCurrentIndex(name);
    registers->scrollTo(name);
    registers->setFocus(Qt::OtherFocusReason);
    registers->edit(name);
    QTRY_VERIFY_WITH_TIMEOUT(
        visibleEditor(registers) != nullptr, 2000);
    auto* nameEditor = visibleEditor(registers);
    nameEditor->selectAll();
    QTest::keyClicks(
        nameEditor, QStringLiteral("STATUS_FAST"));
    QTest::keyClick(nameEditor, Qt::Key_D,
                    Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        QString::fromStdString(
            regmap::findRegister(
                *controller->workspace(), "reg-status")
                ->name),
        QStringLiteral("STATUS_FAST"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{2}, 2000);
    block = regmap::findRegisterBlock(
        *controller->workspace(), "block-control");
    QCOMPARE(block->registers[1].name,
             std::string("STATUS_FAST Copy"));
    QCOMPARE(controller->undoDepth(),
             initialUndoDepth + 2);

    controller->undo();
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1}, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        QString::fromStdString(
            regmap::findRegister(
                *controller->workspace(), "reg-status")
                ->name),
        QStringLiteral("STATUS"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    const QModelIndex width =
        registers->model()->index(0, 3);
    registers->setCurrentIndex(width);
    registers->scrollTo(width);
    registers->setFocus(Qt::OtherFocusReason);
    registers->edit(width);
    QTRY_VERIFY_WITH_TIMEOUT(
        visibleEditor(registers) != nullptr, 2000);
    auto* invalidEditor = visibleEditor(registers);
    invalidEditor->selectAll();
    QTest::keyClicks(
        invalidEditor, QStringLiteral("invalid-width"));
    QTest::keyClick(invalidEditor, Qt::Key_D,
                    Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()->index(0, 3)
            .data().toString(),
        QStringLiteral("32"), 2000);
    QCOMPARE(
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control")
            ->registers.size(),
        std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Edit rejected: expected")));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::confirmsReservedConversionBeforeClearingContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Reserved conversion fixture"),
        [](regmap::Workspace& workspace) {
            auto* status = regmap::findRegister(workspace, "reg-status");
            auto* ready = regmap::findField(workspace, "field-ready");
            QVERIFY(status != nullptr);
            QVERIFY(ready != nullptr);
            status->description = "Status register definition.";
            status->initialValue = regmap::UnsignedValue(1);
            status->resetValue = regmap::UnsignedValue(1);
            ready->resetValue = regmap::UnsignedValue(1);
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };

    struct ReserveInvocation {
        bool actionTriggered{false};
        bool dialogSeen{false};
        QString failure;
    };
    const auto invokeReserve =
        [&](const QString& registerId,
            QMessageBox::StandardButton response,
            bool expectDialog) {
            ReserveInvocation result;
            const int row = registerRowForId(registerId);
            if (row < 0) {
                result.failure = QStringLiteral("Register row was not found");
                return result;
            }
            const QModelIndex index = registers->model()->index(row, 0);
            registers->setCurrentIndex(index);
            registers->scrollTo(index);
            QCoreApplication::processEvents();
            QTimer::singleShot(0, &window, [&] {
                auto* action = window.findChild<QAction*>(
                    QStringLiteral("reserveRegisterAction"));
                auto* menu =
                    action == nullptr
                        ? qobject_cast<QMenu*>(
                              QApplication::activePopupWidget())
                        : qobject_cast<QMenu*>(action->parent());
                if (menu == nullptr || action == nullptr ||
                    !action->isEnabled()) {
                    result.failure =
                        QStringLiteral("Reserved action is unavailable");
                    if (menu != nullptr) {
                        menu->close();
                    }
                    return;
                }
                result.actionTriggered = true;
                QTimer::singleShot(0, &window, [&] {
                    auto* dialog = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                    if (dialog == nullptr) {
                        if (expectDialog) {
                            result.failure =
                                QStringLiteral(
                                    "Reserved impact confirmation did not open");
                        }
                        return;
                    }
                    result.dialogSeen = true;
                    if (!expectDialog) {
                        result.failure =
                            QStringLiteral(
                                "Empty Register unexpectedly required confirmation");
                        dialog->reject();
                        return;
                    }
                    if (dialog->windowTitle() !=
                            QStringLiteral("Set Register to Reserved") ||
                        !dialog->text().contains(QStringLiteral("STATUS")) ||
                        !dialog->text().contains(QStringLiteral("offset 0x0")) ||
                        !dialog->text().contains(
                            QStringLiteral("RESERVED_0")) ||
                        !dialog->text().contains(QStringLiteral("1 Field")) ||
                        !dialog->text().contains(
                            QStringLiteral(
                                "2 non-zero Initial/Reset values")) ||
                        !dialog->text().contains(
                            QStringLiteral("Register description")) ||
                        !dialog->text().contains(
                            QStringLiteral("offset and tags will be kept")) ||
                        !dialog->text().contains(QStringLiteral("Ctrl+Z")) ||
                        dialog->defaultButton() !=
                            dialog->button(QMessageBox::No)) {
                        result.failure =
                            QStringLiteral(
                                "Reserved confirmation lacks impact or recovery details");
                    }
                    if (auto* button = dialog->button(response)) {
                        QTest::mouseClick(button, Qt::LeftButton);
                    } else {
                        result.failure =
                            QStringLiteral(
                                "Requested Reserved confirmation button is unavailable");
                        dialog->reject();
                    }
                });
                QTest::mouseClick(
                    menu, Qt::LeftButton, Qt::NoModifier,
                    menu->actionGeometry(action).center());
            });
            const QPoint position =
                registers->visualRect(index).center();
            QContextMenuEvent event(
                QContextMenuEvent::Mouse, position,
                registers->viewport()->mapToGlobal(position));
            QCoreApplication::sendEvent(registers->viewport(), &event);
            QCoreApplication::processEvents();
            return result;
        };

    const std::size_t initialUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    const ReserveInvocation cancelled =
        invokeReserve(QStringLiteral("reg-status"), QMessageBox::No, true);
    QVERIFY2(cancelled.failure.isEmpty(), qPrintable(cancelled.failure));
    QVERIFY(cancelled.actionTriggered);
    QVERIFY(cancelled.dialogSeen);
    const auto* retained =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(retained != nullptr);
    QCOMPARE(retained->name, std::string("STATUS"));
    QCOMPARE(retained->type, regmap::FieldType::structure);
    QCOMPARE(retained->fields.size(), std::size_t{1});
    QCOMPARE(retained->description,
             std::string("Status register definition."));
    QVERIFY(retained->initialValue ==
            std::optional(regmap::UnsignedValue(1)));
    QVERIFY(retained->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("conversion cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("STATUS kept")));

    window.statusBar()->clearMessage();
    const ReserveInvocation confirmed =
        invokeReserve(QStringLiteral("reg-status"), QMessageBox::Yes, true);
    QVERIFY2(confirmed.failure.isEmpty(), qPrintable(confirmed.failure));
    QVERIFY(confirmed.actionTriggered);
    QVERIFY(confirmed.dialogSeen);
    const auto* reserved =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(reserved != nullptr);
    QCOMPARE(reserved->name, std::string("RESERVED_0"));
    QVERIFY(reserved->reserved);
    QCOMPARE(reserved->type, regmap::FieldType::reserved);
    QCOMPARE(reserved->access, regmap::AccessMode::none);
    QVERIFY(reserved->fields.empty());
    QVERIFY(reserved->initialValue ==
            std::optional(regmap::UnsignedValue(0)));
    QVERIFY(reserved->resetValue ==
            std::optional(regmap::UnsignedValue(0)));
    QCOMPARE(reserved->description,
             std::string("Reserved address slot."));
    QCOMPARE(reserved->tags, std::vector<std::string>{"existing"});
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Register set to Reserved")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("1 Field")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Ctrl+Z")));

    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        regmap::findRegister(*controller->workspace(), "reg-status")->type ==
            regmap::FieldType::structure,
        2000);
    const auto* restored =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(restored != nullptr);
    QCOMPARE(restored->name, std::string("STATUS"));
    QCOMPARE(restored->fields.size(), std::size_t{1});
    QCOMPARE(restored->description,
             std::string("Status register definition."));
    QVERIFY(restored->initialValue ==
            std::optional(regmap::UnsignedValue(1)));
    QVERIFY(restored->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const std::size_t emptyUndoDepth = controller->undoDepth();
    const ReserveInvocation empty =
        invokeReserve(QStringLiteral("reg-control"), QMessageBox::No, false);
    QVERIFY2(empty.failure.isEmpty(), qPrintable(empty.failure));
    QVERIFY(empty.actionTriggered);
    QVERIFY(!empty.dialogSeen);
    const auto* emptyReserved =
        regmap::findRegister(*controller->workspace(), "reg-control");
    QVERIFY(emptyReserved != nullptr);
    QVERIFY(emptyReserved->reserved);
    QCOMPARE(emptyReserved->name, std::string("RESERVED_4"));
    QCOMPARE(emptyReserved->tags,
             std::vector<std::string>{"control"});
    QCOMPARE(controller->undoDepth(), emptyUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    controller->undo();
    QTRY_VERIFY_WITH_TIMEOUT(
        !regmap::findRegister(*controller->workspace(), "reg-control")
             ->reserved,
        2000);
    QCOMPARE(controller->undoDepth(), emptyUndoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsAddressEditsThatIntroduceConflicts()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* pageBase = window.findChild<QLineEdit*>(QStringLiteral("pageBaseEdit"));
    auto* pageWidth = window.findChild<QLineEdit*>(QStringLiteral("pageWidthEdit"));
    auto* blockBase = window.findChild<QLineEdit*>(QStringLiteral("blockBaseEdit"));
    auto* blockSize = window.findChild<QLineEdit*>(QStringLiteral("blockSizeEdit"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(pageBase != nullptr);
    QVERIFY(pageWidth != nullptr);
    QVERIFY(blockBase != nullptr);
    QVERIFY(blockSize != nullptr);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Add adjacent Block allocation"),
        [](regmap::Workspace& workspace) {
            regmap::RegisterBlock adjacent;
            adjacent.id = "block-adjacent";
            adjacent.name = "Adjacent";
            adjacent.baseAddress = 0x1000;
            adjacent.size = 0x1000;
            workspace.addressSpaces.front().blocks.push_back(
                std::move(adjacent));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRow = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    const std::size_t initialUndoDepth = controller->undoDepth();

    blockSize->setText(QStringLiteral("0x1800"));
    Q_EMIT blockSize->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockSize->text(), QStringLiteral("0x1000"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Block address layout")));

    blockBase->setText(QStringLiteral("0x800"));
    Q_EMIT blockBase->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockBase->text(), QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Block address layout")));

    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 1), QStringLiteral("0x0")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x4"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("address layout")));

    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 1), QStringLiteral("0x1000")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x4"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 1), QStringLiteral("0x8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x8"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x4"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    const int statusRow = registerRow(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(statusRow, 3), QStringLiteral("64")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-status")), 3)
            .data()
            .toString(),
        QStringLiteral("32"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Register extent")));

    blockSize->setText(QStringLiteral("0x4"));
    Q_EMIT blockSize->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockSize->text(), QStringLiteral("0x1000"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    blockBase->setText(QStringLiteral("0xffffffff"));
    Q_EMIT blockBase->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockBase->text(), QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    pageBase->setText(QStringLiteral("0x100000000"));
    Q_EMIT pageBase->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(pageBase->text(), QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    pageWidth->setText(QStringLiteral("2"));
    Q_EMIT pageWidth->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(pageWidth->text(), QStringLiteral("32"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    blockSize->setText(QStringLiteral("0x8"));
    Q_EMIT blockSize->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(blockSize->text(), QStringLiteral("0x8"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QCOMPARE(controller->workspace()
                 ->addressSpaces.front()
                 .blocks.front()
                 .size,
             std::optional<std::uint64_t>{8});

    pageBase->setText(QStringLiteral("0x1000"));
    Q_EMIT pageBase->editingFinished();
    QTRY_COMPARE_WITH_TIMEOUT(pageBase->text(), QStringLiteral("0x1000"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 2);
    QCOMPARE(controller->workspace()->addressSpaces.front().baseAddress,
             std::uint64_t{0x1000});
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure address recovery fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* reg = regmap::findRegister(workspace, "reg-control")) {
                reg->offset = 0;
            }
        }));
    QVERIFY(!regmap::validateWorkspace(*controller->workspace()).empty());
    controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    QCOMPARE(registers->model()->index(controlRow, 1).data().toString(),
             QStringLiteral("0x0"));
    const std::size_t recoveryUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 1), QStringLiteral("0x4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x4"), 2000);
    QCOMPARE(controller->undoDepth(), recoveryUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsRecoveryEditsThatReplaceAddressProblems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure multiple address Problems"),
        [](regmap::Workspace& workspace) {
            auto* block =
                regmap::findRegisterBlock(workspace, "block-control");
            auto* status =
                regmap::findRegister(workspace, "reg-status");
            auto* control =
                regmap::findRegister(workspace, "reg-control");
            QVERIFY(block != nullptr);
            QVERIFY(status != nullptr);
            QVERIFY(control != nullptr);
            status->offset = 0;
            control->offset = 0;

            regmap::Register third;
            third.id = "reg-third";
            third.name = "THIRD";
            third.offset = 0;
            third.width = 32;
            third.array.count = 1;
            third.array.stride = 4;
            third.type = regmap::FieldType::unsignedInteger;
            third.initialValue = regmap::UnsignedValue(0);
            third.resetValue = regmap::UnsignedValue(0);
            third.access = regmap::AccessMode::readWrite;
            block->registers.push_back(std::move(third));
        }));

    const auto overlapCount = [controller] {
        return static_cast<std::size_t>(
            std::ranges::count_if(
                regmap::validateWorkspace(*controller->workspace()),
                [](const regmap::Diagnostic& diagnostic) {
                    return diagnostic.code == "RM3024";
                }));
    };
    QCOMPARE(overlapCount(), std::size_t{2});

    const auto rowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const std::size_t initialUndoDepth = controller->undoDepth();
    int controlRow = rowForId(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    window.statusBar()->clearMessage();
    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 1),
        QStringLiteral("0x4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(rowForId(QStringLiteral("reg-control")), 1)
            .data()
            .toString(),
        QStringLiteral("0x0"), 2000);
    QCOMPARE(overlapCount(), std::size_t{2});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("address layout")));

    int thirdRow = rowForId(QStringLiteral("reg-third"));
    QVERIFY(thirdRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(thirdRow, 1),
        QStringLiteral("0x8")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(rowForId(QStringLiteral("reg-third")), 1)
            .data()
            .toString(),
        QStringLiteral("0x8"), 2000);
    QCOMPARE(overlapCount(), std::size_t{1});
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);

    const int statusRow =
        rowForId(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    QVERIFY(registers->model()->setData(
        registers->model()->index(statusRow, 1),
        QStringLiteral("0x4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(rowForId(QStringLiteral("reg-status")), 1)
            .data()
            .toString(),
        QStringLiteral("0x4"), 2000);
    QCOMPARE(overlapCount(), std::size_t{0});
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 2);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsGeometryConflictsFromWidthAndTypeEdits()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure geometry conflict fixture"),
        [](regmap::Workspace& workspace) {
            auto* block = regmap::findRegisterBlock(workspace, "block-control");
            auto* reg = regmap::findRegister(workspace, "reg-status");
            auto* ready = regmap::findField(workspace, "field-ready");
            if (block == nullptr || reg == nullptr || ready == nullptr) {
                return;
            }
            block->size = 8;
            ready->type = regmap::FieldType::bits;
            ready->msb = 0;
            ready->lsb = 0;
            ready->enumValues.clear();

            regmap::Field second;
            second.id = "field-second";
            second.name = "SECOND";
            second.msb = 4;
            second.lsb = 4;
            second.type = regmap::FieldType::bits;
            second.softwareAccess = regmap::AccessMode::readOnly;
            second.hardwareAccess = regmap::AccessMode::writeOnly;
            second.resetValue = regmap::UnsignedValue(0);
            second.readSideEffect = regmap::ReadSideEffect::none;
            second.writeSideEffect = regmap::WriteSideEffect::none;
            reg->fields.push_back(std::move(second));

            regmap::Field member;
            member.id = "field-member";
            member.name = "MEMBER";
            member.msb = 0;
            member.lsb = 0;
            member.type = regmap::FieldType::bits;
            member.softwareAccess = regmap::AccessMode::readOnly;
            member.hardwareAccess = regmap::AccessMode::writeOnly;
            member.resetValue = regmap::UnsignedValue(0);
            member.readSideEffect = regmap::ReadSideEffect::none;
            member.writeSideEffect = regmap::WriteSideEffect::none;

            regmap::Field parent;
            parent.id = "field-parent";
            parent.name = "PARENT";
            parent.msb = 15;
            parent.lsb = 8;
            parent.type = regmap::FieldType::structure;
            parent.softwareAccess = regmap::AccessMode::none;
            parent.hardwareAccess = regmap::AccessMode::none;
            parent.resetValue = regmap::UnsignedValue(0);
            parent.readSideEffect = regmap::ReadSideEffect::none;
            parent.writeSideEffect = regmap::WriteSideEffect::none;
            parent.members.push_back(std::move(member));
            reg->fields.push_back(std::move(parent));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    const auto registerRow = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int controlRow = registerRow(QStringLiteral("reg-control"));
    QVERIFY(controlRow >= 0);
    const std::size_t initialUndoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(
        registers->model()->index(controlRow, 4), QStringLiteral("uint64")));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-control")), 4)
            .data()
            .toString(),
        QStringLiteral("uint32"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-control")->width,
             std::uint32_t{32});
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("address layout")));

    const int statusRow = registerRow(QStringLiteral("reg-status"));
    QVERIFY(statusRow >= 0);
    Q_EMIT registers->clicked(registers->model()->index(statusRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRow = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(readyRow >= 0);

    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 2), QStringLiteral("4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 2)
            .data()
            .toString(),
        QStringLiteral("0"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("non-overlapping Field layout")));

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 4), QStringLiteral("5")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 4)
            .data()
            .toString(),
        QStringLiteral("1"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 5), QStringLiteral("uint5")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 5)
            .data()
            .toString(),
        QStringLiteral("bits"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);

    int memberRow = fieldRow(QStringLiteral("field-member"));
    QVERIFY(memberRow >= 0);
    QVERIFY(fields->model()->setData(
        fields->model()->index(memberRow, 4), QStringLiteral("9")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-member")), 4)
            .data()
            .toString(),
        QStringLiteral("1"), 2000);
    QCOMPARE(controller->undoDepth(), initialUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("non-overlapping Field layout")));

    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 4), QStringLiteral("4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 4)
            .data()
            .toString(),
        QStringLiteral("4"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->msb,
             std::uint32_t{3});
    QCOMPARE(controller->undoDepth(), initialUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Field overlap recovery fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* field = regmap::findField(workspace, "field-ready")) {
                field->msb = 4;
            }
        }));
    QVERIFY(!regmap::validateWorkspace(*controller->workspace()).empty());
    readyRow = fieldRow(QStringLiteral("field-ready"));
    QVERIFY(readyRow >= 0);
    const std::size_t recoveryUndoDepth = controller->undoDepth();

    QVERIFY(fields->model()->setData(
        fields->model()->index(readyRow, 4), QStringLiteral("4")));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-ready")), 4)
            .data()
            .toString(),
        QStringLiteral("4"), 2000);
    QCOMPARE(regmap::findField(*controller->workspace(), "field-ready")->msb,
             std::uint32_t{3});
    QCOMPARE(controller->undoDepth(), recoveryUndoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::synchronizesFieldResetEdits()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure nested reset fixture"),
        [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (reg == nullptr) {
                return;
            }
            regmap::Field member;
            member.id = "field-member";
            member.name = "MEMBER";
            member.msb = 2;
            member.lsb = 2;
            member.type = regmap::FieldType::boolean;
            member.softwareAccess = regmap::AccessMode::readOnly;
            member.hardwareAccess = regmap::AccessMode::writeOnly;
            member.resetValue = regmap::UnsignedValue(0);
            member.writeSideEffect = regmap::WriteSideEffect::none;

            regmap::Field parent;
            parent.id = "field-parent";
            parent.name = "PARENT";
            parent.msb = 15;
            parent.lsb = 8;
            parent.type = regmap::FieldType::structure;
            parent.softwareAccess = regmap::AccessMode::none;
            parent.hardwareAccess = regmap::AccessMode::none;
            parent.resetValue = regmap::UnsignedValue(0);
            parent.writeSideEffect = regmap::WriteSideEffect::none;
            parent.members.push_back(std::move(member));
            reg->fields = {std::move(parent)};
            reg->resetValue = regmap::UnsignedValue(0);
        }));

    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    int parentRow = -1;
    int memberRow = -1;
    for (int row = 0; row < fields->model()->rowCount(); ++row) {
        const QString id =
            fields->model()->index(row, 0).data(Qt::UserRole + 1).toString();
        if (id == QStringLiteral("field-parent")) {
            parentRow = row;
        } else if (id == QStringLiteral("field-member")) {
            memberRow = row;
        }
    }
    QVERIFY(parentRow >= 0);
    QVERIFY(memberRow >= 0);
    QCOMPARE(registers->model()->index(0, 8).data().toString(),
             QStringLiteral("0x0"));
    QCOMPARE(fields->model()->index(parentRow, 10).data().toString(),
             QStringLiteral("0x0"));
    QCOMPARE(fields->model()->index(memberRow, 10).data().toString(),
             QStringLiteral("0x0"));

    const std::size_t undoDepth = controller->undoDepth();
    QVERIFY(fields->model()->setData(fields->model()->index(memberRow, 10),
                                     QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x400"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x4"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x1"), 2000);
    QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-status")->resetValue,
             std::optional(regmap::UnsignedValue(0x400)));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    QCOMPARE(controller->undoDepth(), undoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Register Reset")));

    fields->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(fields, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::protectsEnumContractsDuringEditing()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);

    const auto configureEnums = [](std::vector<regmap::EnumValue>& values,
                                   std::string_view prefix) {
        regmap::EnumValue zero;
        zero.id = std::string(prefix) + "-zero";
        zero.name = "ZERO";
        zero.value = regmap::UnsignedValue(0);
        regmap::EnumValue three;
        three.id = std::string(prefix) + "-three";
        three.name = "THREE";
        three.value = regmap::UnsignedValue(3);
        values = {std::move(zero), std::move(three)};
    };
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure register Enum fixture"),
        [configureEnums](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (reg == nullptr) {
                return;
            }
            reg->width = 2;
            reg->type = regmap::FieldType::enumeration;
            reg->fields.clear();
            reg->array.stride = 1;
            configureEnums(reg->enumValues, "enum-register");
        }));
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    const auto enumRow = [enums](const QString& id) {
        for (int row = 0; row < enums->model()->rowCount(); ++row) {
            if (enums->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const auto invokeEnumDelete = [&window, enums](int row) {
        QString failure;
        bool triggered = false;
        const QModelIndex index = enums->model()->index(row, 0);
        enums->scrollTo(index);
        QCoreApplication::processEvents();

        QTimer::singleShot(0, &window, [&] {
            auto* action =
                window.findChild<QAction*>(QStringLiteral("deleteEnumValueAction"));
            auto* menu =
                action == nullptr ? qobject_cast<QMenu*>(QApplication::activePopupWidget())
                                  : qobject_cast<QMenu*>(action->parent());
            if (menu == nullptr) {
                failure = QStringLiteral("Enum context menu did not open");
                return;
            }
            if (menu->objectName() != QStringLiteral("enumContextMenu")) {
                failure = QStringLiteral("Unexpected enum context menu");
                menu->close();
                return;
            }
            if (action == nullptr || !action->isEnabled()) {
                failure = QStringLiteral("Delete Enum Value action is unavailable");
                menu->close();
                return;
            }
            triggered = true;
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(action).center());
        });
        const QPoint position = enums->visualRect(index).center();
        QContextMenuEvent event(
            QContextMenuEvent::Mouse, position,
            enums->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(enums->viewport(), &event);
        if (!triggered && failure.isEmpty()) {
            failure = QStringLiteral("Delete Enum Value action was not triggered");
        }
        return failure;
    };
    int zeroRow = enumRow(QStringLiteral("enum-register-zero"));
    int threeRow = enumRow(QStringLiteral("enum-register-three"));
    QVERIFY(zeroRow >= 0);
    QVERIFY(threeRow >= 0);
    const std::size_t registerEnumUndoDepth = controller->undoDepth();

    QVERIFY(enums->model()->setData(enums->model()->index(threeRow, 0),
                                    QStringLiteral("ZERO")));
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-register-three")), 0)
            .data()
            .toString(),
        QStringLiteral("THREE"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Enum name")));

    threeRow = enumRow(QStringLiteral("enum-register-three"));
    QVERIFY(enums->model()->setData(enums->model()->index(threeRow, 1),
                                    QStringLiteral("0x0")));
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-register-three")), 1)
            .data()
            .toString(),
        QStringLiteral("0x3"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Enum value")));

    const int registerEnumRows = enums->model()->rowCount();
    window.statusBar()->clearMessage();
    const QString registerDeleteFailure = invokeEnumDelete(zeroRow);
    QVERIFY2(registerDeleteFailure.isEmpty(), qPrintable(registerDeleteFailure));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), registerEnumRows, 2000);
    QVERIFY(enumRow(QStringLiteral("enum-register-zero")) >= 0);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot delete")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial/Reset")));

    zeroRow = enumRow(QStringLiteral("enum-register-zero"));
    QVERIFY(enums->model()->setData(enums->model()->index(zeroRow, 1),
                                    QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-register-zero")), 1)
            .data()
            .toString(),
        QStringLiteral("0x1"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 7).data().toString(),
                              QStringLiteral("0x1"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x1"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Initial/Reset updated")));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-register-zero")), 1)
            .data()
            .toString(),
        QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 7).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);

    QVERIFY(registers->model()->setData(registers->model()->index(0, 7),
                                        QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 7).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("existing Enum value")));

    QVERIFY(registers->model()->setData(registers->model()->index(0, 8),
                                        QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("existing Enum value")));

    threeRow = enumRow(QStringLiteral("enum-register-three"));
    QVERIFY(threeRow >= 0);
    window.statusBar()->clearMessage();
    const QString unreferencedDeleteFailure = invokeEnumDelete(threeRow);
    QVERIFY2(unreferencedDeleteFailure.isEmpty(),
             qPrintable(unreferencedDeleteFailure));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), registerEnumRows - 1, 2000);
    QVERIFY(enumRow(QStringLiteral("enum-register-three")) < 0);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Deleted enum value")));

    zeroRow = enumRow(QStringLiteral("enum-register-zero"));
    QVERIFY(zeroRow >= 0);
    window.statusBar()->clearMessage();
    const QString lastDeleteFailure = invokeEnumDelete(zeroRow);
    QVERIFY2(lastDeleteFailure.isEmpty(), qPrintable(lastDeleteFailure));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), registerEnumRows - 1, 2000);
    QVERIFY(enumRow(QStringLiteral("enum-register-zero")) >= 0);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("retain at least one value")));
    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), registerEnumRows, 2000);
    QCOMPARE(controller->undoDepth(), registerEnumUndoDepth);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Field Enum fixture"),
        [configureEnums](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (reg == nullptr) {
                return;
            }
            reg->width = 32;
            reg->type = regmap::FieldType::structure;
            reg->array.stride = 4;
            reg->enumValues.clear();
            regmap::Field field;
            field.id = "field-enum";
            field.name = "MODE";
            field.msb = 1;
            field.lsb = 0;
            field.type = regmap::FieldType::enumeration;
            field.softwareAccess = regmap::AccessMode::readOnly;
            field.hardwareAccess = regmap::AccessMode::writeOnly;
            field.resetValue = regmap::UnsignedValue(0);
            field.writeSideEffect = regmap::WriteSideEffect::none;
            configureEnums(field.enumValues, "enum-field");
            reg->fields = {std::move(field)};
        }));
    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(enums->isVisible(), 2000);
    int fieldRow = -1;
    for (int row = 0; row < fields->model()->rowCount(); ++row) {
        if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("field-enum")) {
            fieldRow = row;
            break;
        }
    }
    QVERIFY(fieldRow >= 0);
    const std::size_t fieldEnumUndoDepth = controller->undoDepth();
    zeroRow = enumRow(QStringLiteral("enum-field-zero"));
    QVERIFY(zeroRow >= 0);
    const int fieldEnumRows = enums->model()->rowCount();
    window.statusBar()->clearMessage();
    const QString fieldDeleteFailure = invokeEnumDelete(zeroRow);
    QVERIFY2(fieldDeleteFailure.isEmpty(), qPrintable(fieldDeleteFailure));
    QTRY_COMPARE_WITH_TIMEOUT(enums->model()->rowCount(), fieldEnumRows, 2000);
    QVERIFY(enumRow(QStringLiteral("enum-field-zero")) >= 0);
    QCOMPARE(controller->undoDepth(), fieldEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot delete")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Field Reset")));

    QVERIFY(fields->model()->setData(fields->model()->index(fieldRow, 10),
                                     QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(fieldRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), fieldEnumUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("existing Enum value")));

    QVERIFY(enums->model()->setData(enums->model()->index(zeroRow, 1),
                                    QStringLiteral("0x1")));
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-field-zero")), 1)
            .data()
            .toString(),
        QStringLiteral("0x1"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(fieldRow, 10).data().toString(),
                              QStringLiteral("0x1"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x1"), 2000);
    QCOMPARE(controller->undoDepth(), fieldEnumUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Field/Register Reset updated")));

    controller->undo();
    QTRY_COMPARE_WITH_TIMEOUT(
        enums->model()->index(enumRow(QStringLiteral("enum-field-zero")), 1)
            .data()
            .toString(),
        QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(fieldRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), fieldEnumUndoDepth);

    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsRegisterResetsOutsideFieldEnums()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure nested Enum reset fixture"),
        [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (reg == nullptr) {
                return;
            }
            reg->width = 32;
            reg->type = regmap::FieldType::structure;
            reg->array.stride = 4;
            reg->enumValues.clear();
            reg->resetValue = regmap::UnsignedValue(0);

            regmap::EnumValue zero;
            zero.id = "enum-member-zero";
            zero.name = "ZERO";
            zero.value = regmap::UnsignedValue(0);
            regmap::EnumValue three;
            three.id = "enum-member-three";
            three.name = "THREE";
            three.value = regmap::UnsignedValue(3);

            regmap::Field member;
            member.id = "field-enum-member";
            member.name = "MODE";
            member.msb = 2;
            member.lsb = 1;
            member.type = regmap::FieldType::enumeration;
            member.softwareAccess = regmap::AccessMode::readOnly;
            member.hardwareAccess = regmap::AccessMode::writeOnly;
            member.resetValue = regmap::UnsignedValue(0);
            member.writeSideEffect = regmap::WriteSideEffect::none;
            member.enumValues = {std::move(zero), std::move(three)};

            regmap::Field parent;
            parent.id = "field-structure-parent";
            parent.name = "CONTROL";
            parent.msb = 15;
            parent.lsb = 8;
            parent.type = regmap::FieldType::structure;
            parent.softwareAccess = regmap::AccessMode::none;
            parent.hardwareAccess = regmap::AccessMode::none;
            parent.resetValue = regmap::UnsignedValue(0);
            parent.writeSideEffect = regmap::WriteSideEffect::none;
            parent.members = {std::move(member)};
            reg->fields = {std::move(parent)};
        }));
    Q_EMIT registers->clicked(registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);

    const auto fieldRow = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const int parentRow = fieldRow(QStringLiteral("field-structure-parent"));
    const int memberRow = fieldRow(QStringLiteral("field-enum-member"));
    QVERIFY(parentRow >= 0);
    QVERIFY(memberRow >= 0);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t undoDepth = controller->undoDepth();

    QVERIFY(registers->model()->setData(registers->model()->index(0, 8),
                                        QStringLiteral("0x200")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Enum/Bool Field slices")));

    QVERIFY(registers->model()->setData(registers->model()->index(0, 8),
                                        QStringLiteral("0x600")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x600"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x6"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x3"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);

    QVERIFY(fields->model()->setData(fields->model()->index(parentRow, 10),
                                     QStringLiteral("0x2")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Enum/Bool Field slices")));

    QVERIFY(fields->model()->setData(fields->model()->index(parentRow, 10),
                                     QStringLiteral("0x6")));
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x600"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x6"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x3"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 8).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(parentRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(fields->model()->index(memberRow, 10).data().toString(),
                              QStringLiteral("0x0"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsDuplicateObjectNamesDuringEditing()
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

    auto* controller = window.findChild<ProjectController*>();
    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    QVERIFY(controller != nullptr);
    QVERIFY(hierarchy != nullptr);
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure duplicate-name fixture"),
        [](regmap::Workspace& workspace) {
            auto* page = regmap::findAddressSpace(workspace, "space-main");
            auto* block = regmap::findRegisterBlock(workspace, "block-control");
            auto* reg = regmap::findRegister(workspace, "reg-status");
            if (page == nullptr || block == nullptr || reg == nullptr) {
                return;
            }

            regmap::Field secondField;
            secondField.id = "field-secondary";
            secondField.name = "SECOND_FIELD";
            secondField.msb = 31;
            secondField.lsb = 31;
            secondField.type = regmap::FieldType::bits;
            secondField.softwareAccess = reg->access;
            secondField.hardwareAccess = regmap::AccessMode::none;
            secondField.resetValue =
                reg->resetValue
                    ? std::optional{reg->resetValue->slice(31, 1)}
                    : std::nullopt;
            secondField.writeSideEffect = regmap::WriteSideEffect::none;
            reg->fields.push_back(std::move(secondField));

            regmap::Register secondRegister;
            secondRegister.id = "reg-secondary";
            secondRegister.name = "SECOND_REGISTER";
            secondRegister.offset = 0x100;
            secondRegister.width = 32;
            secondRegister.array.count = 1;
            secondRegister.array.stride = 4;
            secondRegister.type = regmap::FieldType::unsignedInteger;
            secondRegister.initialValue = regmap::UnsignedValue(0);
            secondRegister.resetValue = regmap::UnsignedValue(0);
            secondRegister.access = regmap::AccessMode::readWrite;
            block->registers.push_back(std::move(secondRegister));

            regmap::RegisterBlock secondBlock;
            secondBlock.id = "block-secondary";
            secondBlock.name = "SECOND_BLOCK";
            secondBlock.baseAddress = 0x2000;
            secondBlock.size = 0x1000;
            page->blocks.push_back(std::move(secondBlock));

            regmap::AddressSpace secondPage;
            secondPage.id = "space-secondary";
            secondPage.name = "SECOND_PAGE";
            secondPage.baseAddress = 0x100000;
            secondPage.addressWidth = 32;
            workspace.addressSpaces.push_back(std::move(secondPage));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t undoDepth = controller->undoDepth();

    const QString primaryPageName =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-main"))
            .data()
            .toString();
    const QString primaryBlockName =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("block-control"))
            .data()
            .toString();
    QVERIFY(!primaryPageName.isEmpty());
    QVERIFY(!primaryBlockName.isEmpty());

    QModelIndex secondaryPage =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-secondary"));
    QVERIFY(secondaryPage.isValid());
    QVERIFY(hierarchy->model()->setData(secondaryPage, primaryPageName));
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("space-secondary"))
            .data()
            .toString(),
        QStringLiteral("SECOND_PAGE"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Page name")));

    QModelIndex secondaryBlock =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("block-secondary"));
    QVERIFY(secondaryBlock.isValid());
    QVERIFY(hierarchy->model()->setData(secondaryBlock, primaryBlockName));
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("block-secondary"))
            .data()
            .toString(),
        QStringLiteral("SECOND_BLOCK"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Block name")));

    hierarchy->setCurrentIndex(
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("block-control")));
    QCoreApplication::processEvents();
    const auto registerRow = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    int primaryRegisterRow = registerRow(QStringLiteral("reg-status"));
    int secondaryRegisterRow = registerRow(QStringLiteral("reg-secondary"));
    QVERIFY(primaryRegisterRow >= 0);
    QVERIFY(secondaryRegisterRow >= 0);
    const QString primaryRegisterName =
        registers->model()->index(primaryRegisterRow, 0).data().toString();
    QVERIFY(registers->model()->setData(
        registers->model()->index(secondaryRegisterRow, 0),
        primaryRegisterName));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-secondary")), 0)
            .data()
            .toString(),
        QStringLiteral("SECOND_REGISTER"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Register name")));

    secondaryRegisterRow = registerRow(QStringLiteral("reg-secondary"));
    QVERIFY(registers->model()->setData(
        registers->model()->index(secondaryRegisterRow, 0), QString{}));
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()
            ->index(registerRow(QStringLiteral("reg-secondary")), 0)
            .data()
            .toString(),
        QStringLiteral("SECOND_REGISTER"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("non-empty unique Register name")));

    primaryRegisterRow = registerRow(QStringLiteral("reg-status"));
    Q_EMIT registers->clicked(registers->model()->index(primaryRegisterRow, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const auto fieldRow = [fields](const QString& id) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
            if (fields->model()->index(row, 0).data(Qt::UserRole + 1).toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const int primaryFieldRow = fieldRow(QStringLiteral("field-ready"));
    const int secondaryFieldRow = fieldRow(QStringLiteral("field-secondary"));
    QVERIFY(primaryFieldRow >= 0);
    QVERIFY(secondaryFieldRow >= 0);
    const QString primaryFieldName =
        fields->model()->index(primaryFieldRow, 0).data().toString();
    QVERIFY(fields->model()->setData(
        fields->model()->index(secondaryFieldRow, 0), primaryFieldName));
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()
            ->index(fieldRow(QStringLiteral("field-secondary")), 0)
            .data()
            .toString(),
        QStringLiteral("SECOND_FIELD"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("unique Field name")));

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsEmptyWorkspaceNameDuringEditing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest = directory.filePath(QStringLiteral("project.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(900, 620);
    window.show();
    QTest::qWait(50);

    auto* controller = window.findChild<ProjectController*>();
    auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
    QVERIFY(controller != nullptr);
    QVERIFY(hierarchy != nullptr);

    const std::size_t undoDepth = controller->undoDepth();
    QModelIndex workspaceIndex =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("gui-workspace"));
    QVERIFY(workspaceIndex.isValid());
    QCOMPARE(workspaceIndex.data().toString(), QStringLiteral("GUI Workspace"));

    QVERIFY(hierarchy->model()->setData(workspaceIndex, QString{}));
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("gui-workspace"))
            .data()
            .toString(),
        QStringLiteral("GUI Workspace"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("non-empty Workspace name")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    workspaceIndex =
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("gui-workspace"));
    QVERIFY(hierarchy->model()->setData(
        workspaceIndex, QStringLiteral("Renamed Workspace")));
    QTRY_COMPARE_WITH_TIMEOUT(
        hierarchyIndexByObjectId(hierarchy->model(), QStringLiteral("gui-workspace"))
            .data()
            .toString(),
        QStringLiteral("Renamed Workspace"), 2000);
    QCOMPARE(controller->undoDepth(), undoDepth + 1);
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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
    registers->selectionModel()->select(
        statusName, QItemSelectionModel::ClearAndSelect);
    registers->selectionModel()->setCurrentIndex(
        QModelIndex{}, QItemSelectionModel::NoUpdate);
    QVERIFY(registers->selectionModel()->isSelected(statusName));
    QVERIFY(!registers->currentIndex().isValid());
    QApplication::clipboard()->setText(QStringLiteral("UNCHANGED_SENTINEL"));
    registers->setFocus();
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("STATUS"));

    const QModelIndex statusDescription = registers->model()->index(0, 11);
    registers->selectionModel()->select(
        statusDescription, QItemSelectionModel::ClearAndSelect);
    registers->selectionModel()->setCurrentIndex(
        QModelIndex{}, QItemSelectionModel::NoUpdate);
    QVERIFY(registers->selectionModel()->isSelected(statusDescription));
    QVERIFY(!registers->currentIndex().isValid());
    QApplication::clipboard()->setText(QStringLiteral("selected without current"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()->index(0, 11).data().toString(),
        QStringLiteral("selected without current"), 2000);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("1 changed")));
    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        registers->model()->index(0, 11).data().toString(), QString{}, 2000);

    const QModelIndex refreshedStatusName = registers->model()->index(0, 0);
    registers->setCurrentIndex(refreshedStatusName);
    registers->selectionModel()->select(
        refreshedStatusName, QItemSelectionModel::ClearAndSelect);
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

    registers->selectionModel()->setCurrentIndex(
        sparseFirst, QItemSelectionModel::NoUpdate);
    const std::size_t sparsePasteUndoDepth = controller->undoDepth();
    QApplication::clipboard()->setText(QStringLiteral("STATUS_BAD\t0x8"));
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->index(0, 0).data().toString(),
             QStringLiteral("STATUS"));
    QCOMPARE(registers->model()->index(0, 1).data().toString(),
             QStringLiteral("0x0"));
    QCOMPARE(controller->undoDepth(), sparsePasteUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("contiguous rectangular target")));
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
    const QModelIndex mixedPasteStart = registers->model()->index(0, 0);
    registers->setCurrentIndex(mixedPasteStart);
    registers->selectionModel()->select(
        mixedPasteStart, QItemSelectionModel::ClearAndSelect);
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

    const QModelIndex emptyDescription = registers->model()->index(0, 11);
    QCOMPARE(emptyDescription.data().toString(), QString{});
    registers->setCurrentIndex(emptyDescription);
    registers->selectionModel()->select(
        emptyDescription, QItemSelectionModel::ClearAndSelect);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QString{});

    const QModelIndex clearFirstTags = registers->model()->index(0, 10);
    const QModelIndex clearLastTags = registers->model()->index(1, 10);
    registers->setCurrentIndex(clearLastTags);
    registers->selectionModel()->select(
        QItemSelection(clearFirstTags, clearLastTags),
        QItemSelectionModel::ClearAndSelect);
    const std::size_t clearTagsUndoDepth = controller->undoDepth();
    QTest::keyClick(registers, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QString{}, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QString{}, 2000);
    QCOMPARE(controller->undoDepth(), clearTagsUndoDepth + 1);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 changed")));

    QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(0, 10).data().toString(),
                              QStringLiteral("control"), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->index(1, 10).data().toString(),
                              QStringLiteral("control"), 2000);

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure Access paste fixture"),
        [](regmap::Workspace& workspace) {
            if (auto* field = regmap::findField(workspace, "field-ready")) {
                field->softwareAccess = regmap::AccessMode::none;
                field->readSideEffect = regmap::ReadSideEffect::none;
                field->writeSideEffect = regmap::WriteSideEffect::none;
            }
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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

void GuiSmokeTests::requiresExplicitCellEditing()
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

    auto* registers = window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* fields = window.findChild<QTableView*>(QStringLiteral("fieldView"));
    auto* enums = window.findChild<QTableView*>(QStringLiteral("enumView"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(enums != nullptr);

    const auto visibleEditor = [](QTableView* view) -> QLineEdit* {
        for (auto* editor : view->findChildren<QLineEdit*>()) {
            if (editor->isVisible()) {
                return editor;
            }
        }
        return nullptr;
    };

    const QModelIndex registerName = registers->model()->index(0, 0);
    registers->setCurrentIndex(registerName);
    registers->selectionModel()->select(
        registerName, QItemSelectionModel::ClearAndSelect);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_X);
    QCoreApplication::processEvents();
    QCOMPARE(visibleEditor(registers), nullptr);
    QCOMPARE(registerName.data().toString(), QStringLiteral("STATUS"));

    QTest::keyClick(registers, Qt::Key_F2);
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(registers) != nullptr, 2000);
    QTest::keyClick(visibleEditor(registers), Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(visibleEditor(registers), nullptr, 2000);

    const QModelIndex fieldsAction = registers->model()->index(0, 5);
    registers->setCurrentIndex(fieldsAction);
    registers->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(registers, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);

    const QModelIndex fieldName = fields->model()->index(0, 0);
    fields->setCurrentIndex(fieldName);
    fields->selectionModel()->select(
        fieldName, QItemSelectionModel::ClearAndSelect);
    fields->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(fields, Qt::Key_X);
    QCoreApplication::processEvents();
    QCOMPARE(visibleEditor(fields), nullptr);
    QCOMPARE(fieldName.data().toString(), QStringLiteral("READY"));

    QTest::keyClick(fields, Qt::Key_F2);
    QTRY_VERIFY_WITH_TIMEOUT(visibleEditor(fields) != nullptr, 2000);
    QTest::keyClick(visibleEditor(fields), Qt::Key_Escape);

    for (QTableView* view : {registers, fields, enums}) {
        QVERIFY(view->editTriggers().testFlag(QAbstractItemView::DoubleClicked));
        QVERIFY(view->editTriggers().testFlag(QAbstractItemView::EditKeyPressed));
        QVERIFY(!view->editTriggers().testFlag(QAbstractItemView::AnyKeyPressed));
    }

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

    editor->setSelection(static_cast<int>(originalName.size()), 1);
    QTest::keyClick(editor, Qt::Key_Delete);
    QVERIFY(regmap::findRegister(*controller->workspace(), "reg-status") != nullptr);
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(editor->text(), originalName);
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

void GuiSmokeTests::savesActiveChoiceEditorWithShortcut()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(
            QStringLiteral("choice-editor-save.regmap.yaml"));
    createProject(manifest);

    MainWindow window;
    QVERIFY(window.openProjectPath(manifest));
    window.resize(1100, 720);
    window.show();
    window.activateWindow();
    QTest::qWait(50);

    auto* registers =
        window.findChild<QTableView*>(
            QStringLiteral("registerView"));
    auto* fields =
        window.findChild<QTableView*>(
            QStringLiteral("fieldView"));
    auto* controller =
        window.findChild<ProjectController*>();
    auto* copy =
        window.findChild<QAction*>(
            QStringLiteral("copySelectionAction"));
    auto* paste =
        window.findChild<QAction*>(
            QStringLiteral("pasteSelectionAction"));
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(controller != nullptr);
    QVERIFY(copy != nullptr);
    QVERIFY(paste != nullptr);
    QVERIFY(!controller->isDirty());

    Q_EMIT registers->clicked(
        registers->model()->index(0, 5));
    QTRY_VERIFY_WITH_TIMEOUT(fields->isVisible(), 2000);
    const QModelIndex access =
        fields->model()->index(0, 8);
    QCOMPARE(access.data().toString(),
             QStringLiteral("RO"));
    fields->setCurrentIndex(access);
    fields->scrollTo(access);
    fields->setFocus(Qt::OtherFocusReason);
    fields->edit(access);

    const auto visibleAccessEditor =
        [fields]() -> QComboBox* {
        for (auto* editor :
             fields->findChildren<QComboBox*>(
                 QStringLiteral("fieldAccessEditor"))) {
            if (editor->isVisible()) {
                return editor;
            }
        }
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        visibleAccessEditor() != nullptr, 2000);
    auto* editor = visibleAccessEditor();
    editor->setFocus(Qt::OtherFocusReason);

    QApplication::clipboard()->setText(
        QStringLiteral("CHOICE_COPY_SENTINEL"));
    copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),
             QStringLiteral("CHOICE_COPY_SENTINEL"));
    QApplication::clipboard()->setText(
        QStringLiteral("WO"));
    paste->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(editor->currentText(),
             QStringLiteral("RO"));
    QCOMPARE(fields->model()->index(0, 8)
                 .data().toString(),
             QStringLiteral("RO"));
    QVERIFY(!controller->isDirty());

    editor->setCurrentText(QStringLiteral("NONE"));
    QCOMPARE(editor->currentText(),
             QStringLiteral("NONE"));
    QCOMPARE(fields->model()->index(0, 8)
                 .data().toString(),
             QStringLiteral("RO"));
    QTest::keyClick(editor, Qt::Key_S,
                    Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(
        fields->model()->index(0, 8).data().toString(),
        QStringLiteral("NONE"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isDirty(), 2000);

    const auto reopened =
        regmap::openProject(
            std::filesystem::path(manifest.toStdWString()));
    QVERIFY(reopened.workspace.has_value());
    const auto* ready =
        regmap::findField(*reopened.workspace, "field-ready");
    QVERIFY(ready != nullptr);
    QCOMPARE(ready->softwareAccess,
             regmap::AccessMode::none);

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
    auto* generated = window.findChild<QTableView*>(QStringLiteral("generatedView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(fields != nullptr);
    QVERIFY(generated != nullptr);
    QVERIFY(controller != nullptr);
    QCOMPARE(fields->model()->rowCount(), 0);
    QVERIFY(!fields->isVisible());
    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure register deletion fixture"),
        [](regmap::Workspace& workspace) {
            auto* status = regmap::findRegister(workspace, "reg-status");
            auto* ready = regmap::findField(workspace, "field-ready");
            QVERIFY(status != nullptr);
            QVERIFY(ready != nullptr);
            status->description = "Status register definition.";
            status->initialValue = regmap::UnsignedValue(1);
            status->resetValue = regmap::UnsignedValue(1);
            ready->type = regmap::FieldType::enumeration;
            ready->resetValue = regmap::UnsignedValue(1);
            ready->description = "Ready state definition.";
            regmap::EnumValue clear;
            clear.id = "enum-ready-clear";
            clear.name = "CLEAR";
            clear.value = regmap::UnsignedValue(0);
            regmap::EnumValue set;
            set.id = "enum-ready-set";
            set.name = "SET";
            set.value = regmap::UnsignedValue(1);
            ready->enumValues = {clear, set};

            regmap::Field threshold;
            threshold.id = "field-threshold";
            threshold.name = "THRESHOLD";
            threshold.msb = 3;
            threshold.lsb = 1;
            threshold.type = regmap::FieldType::unsignedInteger;
            threshold.softwareAccess = regmap::AccessMode::readOnly;
            threshold.hardwareAccess = regmap::AccessMode::writeOnly;
            threshold.resetValue = regmap::UnsignedValue(0);
            threshold.readSideEffect = regmap::ReadSideEffect::none;
            threshold.writeSideEffect = regmap::WriteSideEffect::none;
            threshold.minimumValue = "0";
            threshold.maximumValue = "7";
            threshold.description = "Numeric threshold definition.";
            status->fields.push_back(std::move(threshold));
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t undoDepth = controller->undoDepth();

    generated->setCurrentIndex(generated->model()->index(0, 0));
    generated->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(generated, Qt::Key_Delete);
    QCoreApplication::processEvents();
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(controller->undoDepth(), undoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("focused hierarchy or editor")));

    registers->setCurrentIndex(registers->model()->index(0, 0));
    registers->setFocus();
    struct DeleteInvocation {
        bool dialogSeen{false};
        bool impactDescribed{false};
    };
    const auto invokeDelete =
        [&](QMessageBox::StandardButton response) {
            DeleteInvocation result;
            window.activateWindow();
            registers->setCurrentIndex(registers->model()->index(0, 0));
            registers->scrollTo(registers->currentIndex());
            registers->setFocus(Qt::OtherFocusReason);
            QCoreApplication::processEvents();
            QTest::qWait(10);
            QTimer::singleShot(0, &window, [&] {
                auto* dialog =
                    qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (dialog == nullptr) {
                    return;
                }
                result.dialogSeen = true;
                result.impactDescribed =
                    dialog->windowTitle() ==
                        QStringLiteral("Delete and Shift Registers") &&
                    dialog->text().contains(QStringLiteral("STATUS")) &&
                    dialog->text().contains(QStringLiteral("2 Fields/Members")) &&
                    dialog->text().contains(QStringLiteral("2 Enum values")) &&
                    dialog->text().contains(QStringLiteral("2 Range bounds")) &&
                    dialog->text().contains(QStringLiteral("1 tag")) &&
                    dialog->text().contains(
                        QStringLiteral("3 non-zero Initial/Reset values")) &&
                    dialog->text().contains(QStringLiteral("3 descriptions")) &&
                    dialog->text().contains(
                        QStringLiteral("1 following register(s)")) &&
                    dialog->text().contains(QStringLiteral("0x4")) &&
                    dialog->text().contains(QStringLiteral("Ctrl+Z")) &&
                    dialog->defaultButton() ==
                        dialog->button(QMessageBox::No);
                if (auto* button = dialog->button(response)) {
                    QTest::mouseClick(button, Qt::LeftButton);
                } else {
                    dialog->reject();
                }
            });
            QTest::keyClick(registers, Qt::Key_Delete);
            return result;
        };

    const DeleteInvocation cancelled = invokeDelete(QMessageBox::No);
    QVERIFY(cancelled.dialogSeen);
    QVERIFY(cancelled.impactDescribed);
    QCOMPARE(registers->model()->rowCount(), 3);
    QCOMPARE(controller->undoDepth(), undoDepth);
    const auto* retained =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(retained != nullptr);
    QCOMPARE(retained->fields.size(), std::size_t{2});
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Delete cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("STATUS kept")));

    const DeleteInvocation confirmed = invokeDelete(QMessageBox::Yes);
    QVERIFY(confirmed.dialogSeen);
    QVERIFY(confirmed.impactDescribed);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 2, 2000);
    bool foundStatus = false;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        foundStatus |= registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("reg-status");
    }
    QVERIFY(!foundStatus);
    QCOMPARE(registers->model()->index(0, 0).data(Qt::UserRole + 1).toString(),
             QStringLiteral("reg-control"));
    QCOMPARE(registers->model()->index(0, 1).data().toString(),
             QStringLiteral("0x0"));
    QCOMPARE(controller->undoDepth(), undoDepth + 1);

    window.activateWindow();
    registers->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    QTest::keyClick(registers, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(registers->model()->rowCount(), 3, 2000);
    QCOMPARE(controller->undoDepth(), undoDepth);
    foundStatus = false;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        foundStatus |= registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("reg-status");
    }
    QVERIFY(foundStatus);
    int controlRow = -1;
    for (int row = 0; row < registers->model()->rowCount(); ++row) {
        if (registers->model()->index(row, 0).data(Qt::UserRole + 1).toString() ==
            QStringLiteral("reg-control")) {
            controlRow = row;
        }
    }
    QVERIFY(controlRow >= 0);
    QCOMPARE(registers->model()->index(controlRow, 1).data().toString(), QStringLiteral("0x4"));
    const auto* restored =
        regmap::findRegister(*controller->workspace(), "reg-status");
    QVERIFY(restored != nullptr);
    QCOMPARE(restored->description,
             std::string("Status register definition."));
    QCOMPARE(restored->tags, std::vector<std::string>{"existing"});
    QVERIFY(restored->initialValue ==
            std::optional(regmap::UnsignedValue(1)));
    QVERIFY(restored->resetValue ==
            std::optional(regmap::UnsignedValue(1)));
    QCOMPARE(restored->fields.size(), std::size_t{2});
    const auto* ready =
        regmap::findField(*controller->workspace(), "field-ready");
    const auto* threshold =
        regmap::findField(*controller->workspace(), "field-threshold");
    QVERIFY(ready != nullptr);
    QVERIFY(threshold != nullptr);
    QCOMPARE(ready->enumValues.size(), std::size_t{2});
    QCOMPARE(ready->description,
             std::string("Ready state definition."));
    QCOMPARE(threshold->minimumValue,
             std::optional<std::string>{"0"});
    QCOMPARE(threshold->maximumValue,
             std::optional<std::string>{"7"});
    QCOMPARE(threshold->description,
             std::string("Numeric threshold definition."));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

    makeGeneratedFilesWritable(directory.path());
}

void GuiSmokeTests::rejectsUnsafeDeleteAndShift()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString manifest =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    createTwoRegisterProject(manifest);

    MainWindow window;
    window.openProjectPath(manifest);
    window.resize(1100, 720);
    window.show();
    QTest::qWait(50);

    auto* registers =
        window.findChild<QTableView*>(QStringLiteral("registerView"));
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(controller != nullptr);

    const auto rowForId = [registers](const QString& id) {
        for (int row = 0; row < registers->model()->rowCount(); ++row) {
            if (registers->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 1)
                    .toString() == id) {
                return row;
            }
        }
        return -1;
    };
    const auto triggerRejectedDelete =
        [&](const QString& id, bool& unexpectedDialog) {
            const int row = rowForId(id);
            if (row < 0) {
                return false;
            }
            registers->setCurrentIndex(
                registers->model()->index(row, 0));
            registers->setFocus(Qt::OtherFocusReason);
            window.activateWindow();
            QCoreApplication::processEvents();
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget())) {
                    unexpectedDialog = true;
                    dialog->reject();
                }
            });
            QTest::keyClick(registers, Qt::Key_Delete);
            QCoreApplication::processEvents();
            return true;
        };

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure delete-shift underflow fixture"),
        [](regmap::Workspace& workspace) {
            auto* block =
                regmap::findRegisterBlock(workspace, "block-control");
            auto* status =
                regmap::findRegister(workspace, "reg-status");
            auto* control =
                regmap::findRegister(workspace, "reg-control");
            QVERIFY(block != nullptr);
            QVERIFY(status != nullptr);
            QVERIFY(control != nullptr);
            regmap::Register statusCopy = *status;
            regmap::Register controlCopy = *control;
            statusCopy.offset = 0;
            controlCopy.offset = 8;
            block->registers = {
                std::move(controlCopy), std::move(statusCopy)};
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t underflowUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    bool underflowDialog = false;
    QVERIFY(triggerRejectedDelete(
        QStringLiteral("reg-control"), underflowDialog));
    QVERIFY(!underflowDialog);
    const auto* underflowBlock =
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control");
    QVERIFY(underflowBlock != nullptr);
    QCOMPARE(underflowBlock->registers.size(), std::size_t{2});
    QCOMPARE(underflowBlock->registers[0].id,
             std::string("reg-control"));
    QCOMPARE(underflowBlock->registers[0].offset,
             std::uint64_t{8});
    QCOMPARE(underflowBlock->registers[1].id,
             std::string("reg-status"));
    QCOMPARE(underflowBlock->registers[1].offset,
             std::uint64_t{0});
    QCOMPARE(controller->undoDepth(), underflowUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot delete and shift CONTROL")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("STATUS at Offset 0x0 would underflow")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("shifted by 0x4")));

    QVERIFY(controller->editWorkspace(
        QStringLiteral("Configure delete-shift collision fixture"),
        [](regmap::Workspace& workspace) {
            auto* block =
                regmap::findRegisterBlock(workspace, "block-control");
            auto* status =
                regmap::findRegister(workspace, "reg-status");
            auto* control =
                regmap::findRegister(workspace, "reg-control");
            QVERIFY(block != nullptr);
            QVERIFY(status != nullptr);
            QVERIFY(control != nullptr);
            regmap::Register statusCopy = *status;
            regmap::Register controlCopy = *control;
            statusCopy.offset = 0;
            controlCopy.offset = 8;

            regmap::Register third;
            third.id = "reg-third";
            third.name = "THIRD";
            third.offset = 4;
            third.width = 32;
            third.array.count = 1;
            third.array.stride = 4;
            third.type = regmap::FieldType::unsignedInteger;
            third.initialValue = regmap::UnsignedValue(0);
            third.resetValue = regmap::UnsignedValue(0);
            third.access = regmap::AccessMode::readWrite;
            block->registers = {
                std::move(statusCopy), std::move(controlCopy),
                std::move(third)};
        }));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());
    const std::size_t collisionUndoDepth = controller->undoDepth();
    window.statusBar()->clearMessage();
    bool collisionDialog = false;
    QVERIFY(triggerRejectedDelete(
        QStringLiteral("reg-control"), collisionDialog));
    QVERIFY(!collisionDialog);
    const auto* collisionBlock =
        regmap::findRegisterBlock(
            *controller->workspace(), "block-control");
    QVERIFY(collisionBlock != nullptr);
    QCOMPARE(collisionBlock->registers.size(), std::size_t{3});
    QCOMPARE(collisionBlock->registers[0].id,
             std::string("reg-status"));
    QCOMPARE(collisionBlock->registers[0].offset,
             std::uint64_t{0});
    QCOMPARE(collisionBlock->registers[1].id,
             std::string("reg-control"));
    QCOMPARE(collisionBlock->registers[1].offset,
             std::uint64_t{8});
    QCOMPARE(collisionBlock->registers[2].id,
             std::string("reg-third"));
    QCOMPARE(collisionBlock->registers[2].offset,
             std::uint64_t{4});
    QCOMPARE(controller->undoDepth(), collisionUndoDepth);
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Cannot delete and shift CONTROL")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("resulting Offsets")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("invalid Block/Page address layout")));
    QVERIFY(regmap::validateWorkspace(*controller->workspace()).empty());

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
    auto* controller = window.findChild<ProjectController*>();
    QVERIFY(registers != nullptr);
    QVERIFY(diff != nullptr);
    QVERIFY(conflictBar != nullptr);
    QVERIFY(conflictSummary != nullptr);
    QVERIFY(keepWorkbench != nullptr);
    QVERIFY(useRtl != nullptr);
    QVERIFY(state != nullptr);
    QVERIFY(controller != nullptr);
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

    bool cancelDialogSeen = false;
    bool cancelImpactDescribed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        cancelDialogSeen = true;
        auto* resolve = dialog->findChild<QPushButton*>(
            QStringLiteral("confirmConflictResolutionButton"));
        cancelImpactDescribed =
            dialog->windowTitle() ==
                QStringLiteral("Resolve RTL Conflicts") &&
            dialog->text().contains(
                QStringLiteral("Resolve 1 RTL conflict(s) using RTL values")) &&
            dialog->informativeText().contains(
                QStringLiteral(
                    "Workbench value for every listed conflict will be replaced")) &&
            dialog->informativeText().contains(
                QStringLiteral("Non-conflicting Workbench edits remain merged")) &&
            dialog->informativeText().contains(
                QStringLiteral("No file is changed if you cancel")) &&
            resolve != nullptr &&
            resolve->text() == QStringLiteral("Use RTL values") &&
            dialog->defaultButton() ==
                dialog->button(QMessageBox::Cancel);
        QTest::mouseClick(
            dialog->button(QMessageBox::Cancel), Qt::LeftButton);
    });
    QTest::mouseClick(useRtl, Qt::LeftButton);
    QVERIFY(cancelDialogSeen);
    QVERIFY(cancelImpactDescribed);
    QVERIFY(conflictBar->isVisible());
    QVERIFY(controller->hasConflicts());
    QCOMPARE(registers->model()->index(0, 1).data().toString(),
             QStringLiteral("0x4"));
    QVERIFY(state->text().startsWith(QStringLiteral("Conflict")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("Conflict resolution cancelled")));
    QVERIFY(window.statusBar()->currentMessage().contains(
        QStringLiteral("no file changed")));

    bool confirmDialogSeen = false;
    bool confirmImpactDescribed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog =
            qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        confirmDialogSeen = true;
        auto* resolve = dialog->findChild<QPushButton*>(
            QStringLiteral("confirmConflictResolutionButton"));
        confirmImpactDescribed =
            dialog->windowTitle() ==
                QStringLiteral("Resolve RTL Conflicts") &&
            dialog->text().contains(
                QStringLiteral("1 RTL conflict(s)")) &&
            resolve != nullptr &&
            resolve->text() == QStringLiteral("Use RTL values") &&
            dialog->defaultButton() ==
                dialog->button(QMessageBox::Cancel);
        if (resolve != nullptr) {
            QTest::mouseClick(resolve, Qt::LeftButton);
        } else {
            dialog->reject();
        }
    });
    QTest::mouseClick(useRtl, Qt::LeftButton);
    QVERIFY(confirmDialogSeen);
    QVERIFY(confirmImpactDescribed);
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
