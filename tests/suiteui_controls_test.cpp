#define main unusedSnapshotMain
#include "ui_snapshot.cpp"
#undef main
#include "suiteui_adapter.hpp"
#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QFontDatabase>
#include <QFontInfo>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

class Contracts final : public QObject {
    Q_OBJECT
private slots:
    void realControlsAndThemeLifecycle() {
        const auto mode = qEnvironmentVariable("REGMAP_TEST_THEME") == "dark"
            ? WorkbenchTheme::Mode::dark : WorkbenchTheme::Mode::light;
        WorkbenchTheme::apply(*qApp, mode);
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4('A'));
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4('0'));
        qInfo() << "Fonts:" << QFontInfo(qApp->font()).family()
                << QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family();
        const bool expectsSdk = REGMAP_TEST_SUITEUI_ENABLED
            && qEnvironmentVariable("REGMAP_UI_STYLE") != "classic";
        QCOMPARE(RegMapSuiteUi::enabled(), expectsSdk);
        QStyle* backend = expectsSdk ? RegMapSuiteUi::install(*qApp) : nullptr;
        if (expectsSdk) {
            QVERIFY(backend);
            QCOMPARE(backend->objectName(), QStringLiteral("RegMapSuiteUi"));
        }
        qInfo() << "Renderer:" << (backend ? backend->objectName() : QStringLiteral("classic"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto manifest = directory.filePath("pilot.regmap.yaml");
        QVERIFY(createFixture(manifest));
        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1440, 900); window.show(); QTest::qWait(50);
        auto* controller = window.findChild<ProjectController*>();
        auto* registers = window.findChild<QTableView*>("registerView");
        auto* fileBadge = window.findChild<QLabel*>("fileStateBadge");
        auto* syncBadge = window.findChild<QLabel*>("syncStateBadge");
        QVERIFY(controller && registers && fileBadge && syncBadge);
        const auto tableFont = registers->font();
        const auto headerFont = registers->horizontalHeader()->font();
        const auto columns = registers->model()->columnCount();
        for (auto other : {WorkbenchTheme::Mode::dark, WorkbenchTheme::Mode::light, mode}) {
            WorkbenchTheme::apply(*qApp, other); QTest::qWait(30);
            if (backend) QCOMPARE(RegMapSuiteUi::install(*qApp), backend);
            QCOMPARE(registers->font(), tableFont);
            QCOMPARE(registers->horizontalHeader()->font(), headerFont);
            QCOMPARE(registers->model()->columnCount(), columns);
        }
        const auto output = qEnvironmentVariable("REGMAP_UI_ARTIFACT_DIR", directory.filePath("screenshots"));
        QDir().mkpath(output);
        for (const auto size : {QSize(1440, 900), QSize(960, 720)}) {
            window.resize(size); QTest::qWait(30); QCOMPARE(window.size(), size);
            auto* header = window.findChild<QWidget*>("pageHeader"); QVERIFY(header);
            QList<QRect> occupied;
            for (const auto* name : {"generateButton", "synchronizeButton", "saveSyncButton", "fileStateBadge", "syncStateBadge"}) {
                auto* widget = window.findChild<QWidget*>(name); QVERIFY(widget);
                if (!widget->isVisibleTo(&window)) continue;
                QRect bounds(widget->mapTo(header, QPoint(0, 0)), widget->size());
                QVERIFY2(header->rect().contains(bounds), name);
                for (const auto& prior : occupied) QVERIFY2(!prior.intersects(bounds), name);
                occupied.push_back(bounds);
                QVERIFY2(widget->width() >= widget->minimumSizeHint().width(), name);
                QVERIFY2(widget->height() >= widget->minimumSizeHint().height(), name);
            }
            QVERIFY(window.grab().save(output + QString("/window-%1.png").arg(size.width())));
        }
        auto* generate = window.findChild<QToolButton*>("generateButton"); QVERIFY(generate);
        QSignalSpy clicks(generate, &QToolButton::clicked);
        generate->setEnabled(false);
        QTest::mouseClick(generate, Qt::LeftButton); QTest::keyClick(generate, Qt::Key_Space);
        QCOMPARE(clicks.count(), 0);
        generate->setEnabled(true);
        QTest::mousePress(generate, Qt::LeftButton);
        QTest::mouseRelease(generate, Qt::LeftButton, Qt::NoModifier, QPoint(-8, -8));
        QCOMPARE(clicks.count(), 0);
        generate->setFocus(Qt::TabFocusReason); QTest::qWait(10); QVERIFY(generate->hasFocus());
        QTest::keyClick(generate, Qt::Key_Space); QCOMPARE(clicks.count(), 1);
        QTest::mouseClick(generate, Qt::LeftButton); QCOMPARE(clicks.count(), 2);

        registers->setCurrentIndex(registers->model()->index(0, 0));
        registers->selectionModel()->select(QItemSelection(registers->model()->index(0, 0),
            registers->model()->index(1, columns - 1)), QItemSelectionModel::ClearAndSelect);
        registers->setFocus(); QTest::qWait(30);
        auto* batch = window.findChild<QToolButton*>("registerBatchEditButton");
        QVERIFY(batch && batch->isEnabled());
        const auto undoDepth = controller->undoDepth();
        bool checked = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto close = qScopeGuard([dialog] { dialog->reject(); });
            dialog->activateWindow(); QTest::qWait(25);
            auto* check = dialog->findChild<QCheckBox*>("batchAccessApply");
            auto* combo = dialog->findChild<QComboBox*>("batchAccessEditor");
            auto* buttons = dialog->findChild<QDialogButtonBox*>("batchEditButtons");
            QVERIFY(check && combo && buttons);
            QVERIFY(!check->isChecked()); QVERIFY(!combo->isEnabled());
            QSignalSpy toggles(check, &QCheckBox::toggled);
            check->setFocus(Qt::TabFocusReason); QTest::qWait(10); QVERIFY(check->hasFocus());
            QTest::keyClick(check, Qt::Key_Space);
            QCOMPARE(toggles.count(), 1); QVERIFY(check->isChecked()); QVERIFY(combo->isEnabled());
            QTest::qWait(250);
            QVERIFY(dialog->grab().save(output + "/batch-checked.png"));
            QTest::keyClick(check, Qt::Key_Tab); QVERIFY(combo->hasFocus());
            QTest::keyClick(combo, Qt::Key_Down); QVERIFY(combo->currentIndex() > 0);
            QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, QPoint(8, check->height()/2));
            QCOMPARE(toggles.count(), 2); QVERIFY(!combo->isEnabled());
            check->setEnabled(false); QTest::keyClick(check, Qt::Key_Space);
            QCOMPARE(toggles.count(), 2);
            QTest::qWait(250);
            QVERIFY(dialog->grab().save(output + "/batch-disabled.png"));
            check->setEnabled(true);
            check->setTristate(true); check->setCheckState(Qt::PartiallyChecked);
            QTest::qWait(250);
            QVERIFY(dialog->grab().save(output + "/batch-partial.png"));
            check->setCheckState(Qt::Unchecked); check->setTristate(false);
            QTest::qWait(250);
            QVERIFY(dialog->grab().save(output + "/batch.png"));
            checked = true;
            QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        });
        QTest::mouseClick(batch, Qt::LeftButton);
        QVERIFY(checked); QCOMPARE(controller->undoDepth(), undoDepth);
        QVERIFY(controller->editWorkspace("Pilot long status", [](regmap::Workspace& workspace) {
            auto* reg = regmap::findRegister(workspace, "reg-control");
            if (reg) reg->description = "Pilot status lifecycle update";
        }));
        QTRY_COMPARE(fileBadge->property("state").toString(), QString("dirty"));
        QVERIFY(!fileBadge->toolTip().isEmpty());
        auto* accessible = QAccessible::queryAccessibleInterface(fileBadge);
        QVERIFY(accessible); QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
        QCOMPARE(fileBadge->focusPolicy(), Qt::NoFocus);
        QVERIFY(window.grab().save(output + "/dirty.png"));
        controller->save();
        QTRY_COMPARE(fileBadge->property("state").toString(), QString("saved"));
        QVERIFY(!controller->isDirty());
        QVERIFY(!syncBadge->accessibleName().isEmpty());
        QVERIFY(window.grab().save(output + "/saved.png"));
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    app.setOrganizationName("RegMapTests"); app.setApplicationName("RegMapSuiteUiContracts");
    if (qEnvironmentVariableIsSet("REGMAP_UI_REVIEW")) {
        WorkbenchTheme::apply(app, WorkbenchTheme::Mode::light);
        QTemporaryDir project;
        const auto manifest = project.filePath("pilot.regmap.yaml");
        if (!project.isValid() || !createFixture(manifest)) return 3;
        MainWindow window;
        if (!window.openProjectPath(manifest)) return 4;
        window.resize(1150, 780);
        window.setWindowTitle("RegMap SuiteUi SDK - temporary fixture");
        window.show();
        QTimer::singleShot(300000, &app, &QApplication::quit);
        return app.exec();
    }
    Contracts tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "suiteui_controls_test.moc"
