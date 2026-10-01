#include "application_identity.hpp"
#include "main_window.hpp"
#include "project_controller.hpp"
#include "startup_options.hpp"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class IdentityTests final : public QObject {
    Q_OBJECT
    QTemporaryDir profile_;

private slots:
    void initTestCase()
    {
        QVERIFY(profile_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile_.path());
    }

    void keepsLegacyPreferencesAndRecentProjects()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        const QString manifest = project.filePath("legacy.regmap.yaml");
        ProjectController creator;
        QVERIFY(creator.createProject(manifest));

        QSettings legacy(QSettings::IniFormat, QSettings::UserScope,
                         "RegMapWorkbench", "Register Map Workbench");
        legacy.setValue("projects/recent", QStringList{manifest});
        legacy.setValue("ui/v1/showAdvancedFieldColumns", true);
        legacy.setValue("ui/v2/theme", "dark");
        legacy.sync();
        QCOMPARE(legacy.status(), QSettings::NoError);

        QCoreApplication::setApplicationName("Csrio");
        regmap::workbench::configureApplicationIdentity();
        QCOMPARE(QGuiApplication::applicationDisplayName(), QString("Csrio"));
        QSettings current;
        QCOMPARE(current.fileName(), legacy.fileName());
        QCOMPARE(current.value("ui/v2/theme").toString(), QString("dark"));
        QCOMPARE(current.value("projects/recent").toStringList(), QStringList{manifest});

        {
            MainWindow window;
            QCOMPARE(window.windowTitle(), QString("Csrio"));
            auto* advanced = window.findChild<QAction*>("showAdvancedFieldsAction");
            QVERIFY(advanced != nullptr);
            QVERIFY(advanced->isChecked());
            auto* recent = window.findChild<QMenu*>("recentProjectsMenu");
            QVERIFY(recent != nullptr);
            bool found = false;
            for (auto* action : recent->actions())
                if (action->objectName() == "recentProjectAction" && action->data().toString() == manifest)
                    found = true;
            QVERIFY(found);
            advanced->setChecked(false);
            QVERIFY(window.close());
        }
        legacy.sync();
        QVERIFY(!legacy.value("ui/v1/showAdvancedFieldColumns").toBool());
        QCOMPARE(legacy.value("projects/recent").toStringList(), QStringList{manifest});
    }

    void usesCsrioInStartupHelp()
    {
        regmap::workbench::configureApplicationIdentity();
        const auto usage = regmap::workbench::startupUsage();
        QVERIFY(usage.startsWith("Csrio\n"));
        QVERIFY(usage.contains("Csrio --project"));
        QVERIFY(!usage.contains("RegMapWorkbench"));
        QVERIFY(!usage.contains("Register Map Workbench"));
    }

    void restoresExistingProjectLocalRecoveryDraft()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        const QString manifest = project.filePath("legacy.regmap.yaml");
        {
            QCoreApplication::setOrganizationName("RegMapWorkbench");
            QCoreApplication::setApplicationName("Register Map Workbench");
            ProjectController previous;
            QVERIFY(previous.createProject(manifest));
            QVERIFY(previous.editWorkspace("Legacy pending edit", [](regmap::Workspace& workspace) {
                workspace.name = "Pending legacy draft";
            }));
            QVERIFY(QMetaObject::invokeMethod(&previous, "writeRecoveryDraft", Qt::DirectConnection));
        }
        QVERIFY(QFile::exists(project.filePath(".regmap-workbench/legacy.regmap.yaml.autosave.yaml")));
        QVERIFY(QFile::exists(project.filePath(".regmap-workbench/legacy.regmap.yaml.autosave.base.json")));
        QVERIFY(QFile::exists(project.filePath(".regmap-workbench/legacy.regmap.yaml.autosave.meta.json")));
        QFile original(manifest);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray saved = original.readAll();
        original.close();

        regmap::workbench::configureApplicationIdentity();
        ProjectController current;
        QVERIFY(current.openProject(manifest));
        QVERIFY(current.recoveryDraftAvailable());
        QVERIFY(current.restoreRecoveryDraft());
        QCOMPARE(current.workspace()->name, std::string("Pending legacy draft"));
        QVERIFY(current.isDirty());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), saved);
    }
};

QTEST_MAIN(IdentityTests)
#include "identity_tests.moc"
