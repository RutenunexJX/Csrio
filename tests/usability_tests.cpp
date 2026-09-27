#include "main_window.hpp"
#include "project_controller.hpp"
#include "workbench_theme.hpp"
#include "workspace_inspection.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace {
QString makeProject(const QString& directory)
{
    const QString path = directory + "/project.regmap.yaml";
    ProjectController creator;
    if (!creator.createProject(path)) return {};
    if (!creator.editWorkspace("Fixture", [](regmap::Workspace& workspace) {
            workspace.name = "Usability fixture";
            regmap::AddressSpace page;
            page.id = "page"; page.name = "Main"; page.addressWidth = 32;
            for (int b = 0; b < 2; ++b) {
                regmap::RegisterBlock block;
                block.id = "block-" + std::to_string(b);
                block.name = "Block_" + std::to_string(b);
                block.baseAddress = static_cast<std::uint64_t>(b) * 0x1000;
                block.size = 0x1000;
                for (int i = 0; i < 30; ++i) {
                    const int number = b * 30 + i;
                    regmap::Register reg;
                    reg.id = "reg-" + std::to_string(number);
                    reg.name = "REG_" + std::to_string(number);
                    reg.offset = static_cast<std::uint64_t>(i) * 4;
                    reg.width = 32;
                    reg.type = i == 0 ? regmap::FieldType::structure : regmap::FieldType::unsignedInteger;
                    reg.description = "Saved description";
                    reg.resetValue = regmap::UnsignedValue(0);
                    if (i == 0) {
                        regmap::Field field;
                        field.id = "field-" + std::to_string(number); field.name = "ENABLE";
                        field.type = regmap::FieldType::boolean;
                        reg.fields.push_back(field);
                    }
                    block.registers.push_back(reg);
                }
                page.blocks.push_back(block);
            }
            workspace.addressSpaces = {page};
        })) return {};
    creator.save();
    return creator.isDirty() || creator.hasProjectErrors() ? QString{} : path;
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
int column(QTableView* table, const QString& name)
{
    for (int i = 0; i < table->model()->columnCount(); ++i)
        if (table->model()->headerData(i, Qt::Horizontal).toString() == name) return i;
    return -1;
}
}

class UsabilityTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
private slots:
    void initTestCase()
    {
        QVERIFY(settings_.isValid());
        QCoreApplication::setOrganizationName("RegMapUsabilityTests");
        QCoreApplication::setApplicationName("RegMapUsabilityTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::light);
    }
    void init() { QSettings{}.clear(); }
    void simplifiesHeaderAndCompactEditing()
    {
        QTemporaryDir directory;
        const auto path = makeProject(directory.path());
        QVERIFY(!path.isEmpty());
        MainWindow window;
        QVERIFY(window.openProjectPath(path));
        window.resize(960, 720); window.show();
        auto* registers = window.findChild<QTableView*>("registerView");
        auto* fields = window.findChild<QTableView*>("fieldView");
        auto* more = window.findChild<QToolButton*>("moreProjectButton");
        QVERIFY(more && more->menu());
        QVERIFY(more->menu()->actions().contains(window.findChild<QAction*>("generateAction")));
        QVERIFY(!window.findChild<QToolButton*>("generateButton")->isVisible());
        QVERIFY(!window.findChild<QToolButton*>("synchronizeButton")->isVisible());
        QVERIFY(!window.findChild<QLabel*>("fileStateBadge")->isVisible());
        QVERIFY(window.findChild<QLabel*>("syncStateBadge")->isVisible());
        auto* map = window.findChild<QToolButton*>("addressMapToggle");
        QVERIFY(map->isVisible());
        QVERIFY(!window.findChild<QScrollArea*>("addressSpaceScroll")->isVisible());
        map->click();
        QVERIFY(window.findChild<QScrollArea*>("addressSpaceScroll")->isVisible());
        const auto open = registers->model()->index(0, 5);
        registers->setCurrentIndex(open);
        Q_EMIT registers->clicked(open);
        QTRY_VERIFY(fields->isVisible());
        QTRY_VERIFY(!registers->isVisible());
        QVERIFY(fields->viewport()->height() >= 100);
        auto* values = window.findChild<QToolButton*>("enumValuesToggle");
        QVERIFY(values->isVisible());
        QVERIFY(!window.findChild<QTableView*>("enumView")->isVisible());
        values->click();
        QVERIFY(window.findChild<QTableView*>("enumView")->isVisible());
        auto* back = window.findChild<QPushButton*>("closeFieldsButton");
        QCOMPARE(back->text(), QString("Back to Registers"));
        back->click();
        QTRY_VERIFY(registers->isVisible());
        QCOMPARE(registers->currentIndex().row(), 0);
        Q_EMIT registers->clicked(registers->model()->index(0, 5));
        window.resize(1440, 900);
        QTRY_VERIFY(registers->isVisible() && fields->isVisible());
    }
    void previewsPropertyValuesAndLocatesCells()
    {
        QTemporaryDir directory;
        const auto path = makeProject(directory.path());
        QVERIFY(!path.isEmpty());
        MainWindow window; QVERIFY(window.openProjectPath(path));
        window.show();
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller->editWorkspace("Describe change", [](regmap::Workspace& model) {
            regmap::findRegister(model, "reg-1")->description = "Updated description";
        }));
        auto* results = window.findChild<QToolButton*>("resultsToggleButton");
        if (!results->isChecked()) results->click();
        window.findChild<QTabWidget*>("resultTabs")->setCurrentIndex(2);
        auto* details = window.findChild<QTableView*>("diffDetailsView");
        QTRY_COMPARE(details->model()->rowCount(), 1);
        QCOMPARE(details->model()->index(0, 1).data().toString(), QString("description"));
        QCOMPARE(details->model()->index(0, 2).data().toString(), QString("Saved description"));
        QCOMPARE(details->model()->index(0, 3).data().toString(), QString("Updated description"));
        QVERIFY(details->model()->index(0, 0).data().toString().endsWith("Block_0 / REG_1"));
        Q_EMIT details->doubleClicked(details->model()->index(0, 1));
        auto* registers = window.findChild<QTableView*>("registerView");
        QCOMPARE(registers->currentIndex().row(), 1);
        QCOMPARE(registers->currentIndex().column(), column(registers, "Description"));
        QVERIFY(controller->editWorkspace("Hardware access", [](regmap::Workspace& model) {
            regmap::findField(model, "field-0")->hardwareAccess = regmap::AccessMode::readOnly;
        }));
        auto* changes = window.findChild<QTableView*>("diffView");
        int changedField = -1;
        for (int row = 0; row < changes->model()->rowCount(); ++row)
            if (changes->model()->index(row, 0).data(Qt::UserRole + 1).toString() == "field-0")
                changedField = row;
        QVERIFY(changedField >= 0);
        changes->setCurrentIndex(changes->model()->index(changedField, 0));
        QTRY_COMPARE(details->model()->index(0, 1).data().toString(), QString("hw_access"));
        Q_EMIT details->doubleClicked(details->model()->index(0, 1));
        auto* fields = window.findChild<QTableView*>("fieldView");
        const int hardwareAccess = column(fields, "HW Access");
        QVERIFY(hardwareAccess >= 0);
        QVERIFY(!fields->isColumnHidden(hardwareAccess));
        QCOMPARE(fields->currentIndex().column(), hardwareAccess);
    }
    void navigatesBackWithCellAndScrollContext()
    {
        QTemporaryDir directory;
        const auto path = makeProject(directory.path());
        QVERIFY(!path.isEmpty());
        MainWindow window; QVERIFY(window.openProjectPath(path));
        window.resize(1100, 760); window.show();
        auto* registers = window.findChild<QTableView*>("registerView");
        const auto original = registers->model()->index(20, column(registers, "Description"));
        registers->setCurrentIndex(original); registers->scrollTo(original); registers->setFocus();
        QTest::qWait(50);
        const int vertical = registers->verticalScrollBar()->value();
        const int horizontal = registers->horizontalScrollBar()->value();
        auto* search = window.findChild<QLineEdit*>("globalSearchEdit");
        search->setText("REG_50"); Q_EMIT search->returnPressed();
        QTRY_COMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(), QString("reg-50"));
        auto* back = window.findChild<QAction*>("navigateBackAction");
        QVERIFY(back->isEnabled()); back->trigger();
        QTRY_COMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(), QString("reg-20"));
        QCOMPARE(registers->currentIndex().column(), column(registers, "Description"));
        QCOMPARE(registers->verticalScrollBar()->value(), vertical);
        QCOMPARE(registers->horizontalScrollBar()->value(), horizontal);
        auto* forward = window.findChild<QAction*>("navigateForwardAction");
        QVERIFY(forward->isEnabled()); forward->trigger();
        QTRY_COMPARE(registers->currentIndex().data(Qt::UserRole + 1).toString(), QString("reg-50"));
        QTemporaryDir another;
        const auto secondPath = makeProject(another.path());
        QVERIFY(!secondPath.isEmpty()); QVERIFY(window.openProjectPath(secondPath));
        QVERIFY(!back->isEnabled() && !forward->isEnabled());
    }
    void listsAllSearchResultsAndFiltersBlocks()
    {
        QTemporaryDir directory;
        const auto path = makeProject(directory.path());
        QVERIFY(!path.isEmpty());
        MainWindow window; QVERIFY(window.openProjectPath(path)); window.show();
        window.findChild<QLineEdit*>("globalSearchEdit")->setText("REG_");
        bool inspected = false;
        QString selected;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("allSearchResultsDialog");
            if (!dialog) return;
            auto* type = dialog->findChild<QComboBox*>("allResultsType");
            auto* block = dialog->findChild<QComboBox*>("allResultsBlock");
            auto* table = dialog->findChild<QTableView*>("allResultsTable");
            type->setCurrentIndex(type->findData("register"));
            if (table->model()->rowCount() != 60) { dialog->reject(); return; }
            block->setCurrentIndex(block->findData("block-1"));
            if (table->model()->rowCount() != 30) { dialog->reject(); return; }
            block->setCurrentIndex(0);
            const auto last = table->model()->index(59, 0);
            selected = last.data(Qt::UserRole + 1).toString();
            table->setCurrentIndex(last); inspected = true;
            Q_EMIT table->doubleClicked(last);
        });
        QTimer::singleShot(5000, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>("allSearchResultsDialog")) dialog->reject();
        });
        window.findChild<QAction*>("allSearchResultsAction")->trigger();
        QVERIFY(inspected);
        QCOMPARE(window.findChild<QTableView*>("registerView")->currentIndex()
                     .data(Qt::UserRole + 1).toString(), selected);
    }
    void reviewsDeferredRecoveryWithoutOverwritingIt()
    {
        QTemporaryDir directory;
        const auto path = makeProject(directory.path());
        QVERIFY(!path.isEmpty());
        {
            ProjectController editing; QVERIFY(editing.openProject(path));
            QVERIFY(editing.editWorkspace("Draft edit", [](regmap::Workspace& model) {
                regmap::findRegister(model, "reg-1")->description = "Draft description";
            }));
            QVERIFY(QMetaObject::invokeMethod(&editing, "writeRecoveryDraft", Qt::DirectConnection));
        }
        MainWindow window;
        bool previewed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) return;
            previewed = dialog->detailedText().contains("Draft description") &&
                        dialog->detailedText().contains("Saved description");
            dialog->findChild<QPushButton*>("keepRecoveryDraftButton")->click();
        });
        QVERIFY(window.openProjectPath(path)); QVERIFY(previewed); window.show();
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller->editWorkspace("Current edit", [](regmap::Workspace& model) {
            regmap::findRegister(model, "reg-1")->description = "Current description";
        }));
        QVERIFY(QMetaObject::invokeMethod(controller, "writeRecoveryDraft", Qt::DirectConnection));
        const auto info = controller->recoveryDraftInfo(true);
        QVERIFY(info.has_value()); QCOMPARE(info->conflictCount, std::size_t{1});
        QVERIFY(controller->editWorkspace("Newer current edit", [](regmap::Workspace& model) {
            regmap::findRegister(model, "reg-2")->description = "Another pending edit";
        }));
        QVERIFY(!controller->restoreRecoveryDraft(regmap::MergePreference::workbench, info->revision));
        QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-1")->description,
                 std::string("Current description"));
        const auto projectBeforeRestore = bytes(path);
        bool conflictPreviewed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!dialog) return;
            conflictPreviewed = dialog->detailedText().contains("CONFLICT:") &&
                dialog->detailedText().contains("Current description") &&
                dialog->detailedText().contains("Draft description");
            dialog->findChild<QPushButton*>("restoreRecoveryKeepDraftButton")->click();
        });
        window.findChild<QAction*>("recoveryDraftAction")->trigger();
        QVERIFY(conflictPreviewed);
        QCOMPARE(regmap::findRegister(*controller->workspace(), "reg-1")->description,
                 std::string("Draft description"));
        QVERIFY(controller->isDirty()); QCOMPARE(bytes(path), projectBeforeRestore);
    }
};
QTEST_MAIN(UsabilityTests)
#include "usability_tests.moc"
