#include "main_window.hpp"
#include "project_controller.hpp"
#include "suite_integration.hpp"
#include "workbench_theme.hpp"
#include "regmap/core/serialization.hpp"
#include <suiteapp/transport.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>
#include <chrono>
#include <future>

namespace {
QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QMap<QString, QByteArray> diskFiles(const QString& directory) {
    QMap<QString, QByteArray> files;
    QDirIterator iterator(directory, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        files.insert(path, QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256));
    }
    return files;
}
QJsonObject invoke(QStringList args) {
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("QT_QPA_PLATFORM"));
    environment.remove(QStringLiteral("QT_HASH_SEED"));
    process.setProcessEnvironment(environment);
    args.prepend(QStringLiteral("--json"));
    process.start(QStringLiteral(REGMAP_CLI_EXECUTABLE), args);
    if (!process.waitForStarted(5000)) return {{"process_error", process.errorString()}};
    QElapsedTimer timeout; timeout.start();
    while (process.state() != QProcess::NotRunning && timeout.elapsed() < 20000) QTest::qWait(5);
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); }
    auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    result.insert(QStringLiteral("actual_exit"), process.exitCode());
    if (result.isEmpty() || !result.contains(QStringLiteral("ok")))
        qWarning() << args << process.readAllStandardError();
    return result;
}
QJsonObject gui(const QString& project, qint64 session, QStringList options = {}) {
    return invoke(QStringList{"gui",project,"--session",QString::number(session)} + options);
}
QJsonObject state(const QJsonObject& response) { return response.value("result").toObject(); }
QString revision(const QString& project) { return invoke({"summary",project}).value("revision").toString(); }
QString writePatch(const QString& directory, const QJsonArray& operations) {
    const QString path = directory + QStringLiteral("/patch.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(QJsonObject{{"api_version",1},{"operations",operations}}).toJson());
    return path;
}
QJsonArray hierarchy() {
    return QJsonArray{
        QJsonObject{{"op","add"},{"kind","page"},{"id","page-main"},{"parent_id","workspace-main"},{"value",QJsonObject{{"name","Main"}}}},
        QJsonObject{{"op","add"},{"kind","block"},{"id","block-ch0"},{"parent_id","page-main"},{"value",QJsonObject{{"name","Channel 0"},{"base","auto"},{"size","0x100"}}}},
        QJsonObject{{"op","add"},{"kind","register"},{"id","reg-control"},{"parent_id","block-ch0"},{"value",QJsonObject{{"name","CONTROL"},{"type","field"},{"offset","auto"},{"reset","0x1"}}}},
        QJsonObject{{"op","add"},{"kind","field"},{"id","field-mode"},{"parent_id","reg-control"},{"value",QJsonObject{{"name","MODE"},{"type","enum"},{"lsb",0},{"width",2}}}},
        QJsonObject{{"op","add"},{"kind","enum"},{"id","enum-on"},{"parent_id","field-mode"},{"value",QJsonObject{{"name","ON"},{"value",1}}}},
        QJsonObject{{"op","copy"},{"id","block-ch0"},{"new_id","block-ch1"},{"parent_id","page-main"},{"value",QJsonObject{{"name","Channel 1"},{"base","auto"}}}}};
}
struct OwnedWindow : QProcess {
    ~OwnedWindow() override {
        // Only the child created by this test. These tests leave no GUI edits.
        if (state() != NotRunning) { terminate(); if (!waitForFinished(5000)) { kill(); waitForFinished(); } }
    }
};
}

class CliSessionTests : public QObject {
    Q_OBJECT
    QTemporaryDir profile_;
private slots:
    void initTestCase() {
        QVERIFY(profile_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,profile_.path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,profile_.path());
        QCoreApplication::setOrganizationName("CsrioCliSessionTests");
        QCoreApplication::setApplicationName("IsolatedGui");
        WorkbenchTheme::apply(*qApp);
    }
    void publicCliProtectsWindowEdits() {
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("设备 寄存器.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window;
        window.show();
        QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window);
        QVERIFY(integration.startSession());
        const auto session = QCoreApplication::applicationPid();
        const QString before = revision(project);
        QCOMPARE(state(gui(project,session)).value("adopted_revision").toString(),before);
        const QString patch = writePatch(directory.path(),hierarchy());
        QVERIFY(invoke({"apply",project,patch,"--expect",before,"--dry-run"}).value("ok").toBool());
        QCOMPARE(revision(project),before);
        QVERIFY(invoke({"apply",project,patch,"--expect",before}).value("ok").toBool());
        const QString after = revision(project);
        QVERIFY(after != before);
        auto response = gui(project,session);
        QVERIFY(state(response).value("pending").toBool());
        QCOMPARE(state(response).value("adopted_revision").toString(),before);
        const auto files = diskFiles(directory.path());
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller);
        const auto modelRevision = controller->modelRevision();
        response = gui(project,session,{"--expect",before,"--adopt","--select","block-ch1"});
        QCOMPARE(response.value("actual_exit").toInt(),3);
        response = gui(project,session,{"--expect",after,"--adopt","--select","missing-id"});
        QCOMPARE(response.value("actual_exit").toInt(),1);
        QCOMPARE(controller->modelRevision(),modelRevision);
        QCOMPARE(diskFiles(directory.path()),files);
        // Exercise the existing public SDK route over real IPC before adoption.
        auto pending = std::async(std::launch::async,[&] {
            return SuiteApp::sendRequest(QStringLiteral("csrio.session.%1").arg(session),
                SuiteApp::makeRequest("action.invoke",{{"actionId","regmap.project.open"},
                    {"arguments",QJsonObject{{"projectPath",project},{"objectId","block-ch1"}}}}));
        });
        while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) QTest::qWait(5);
        const auto legacy = pending.get().response;
        QVERIFY(!legacy.value("ok").toBool());
        QCOMPARE(legacy.value("error").toObject().value("code").toString(),QStringLiteral("revision_conflict"));
        qInfo().noquote() << "Pending legacy route:" << QJsonDocument(legacy).toJson(QJsonDocument::Compact);
        response = gui(project,session,{"--expect",after,"--adopt","--select","block-ch1"});
        QVERIFY2(response.value("ok").toBool(), QJsonDocument(response).toJson().constData());
        QVERIFY(state(response).value("adopted").toBool());
        QVERIFY(state(response).value("located").toBool());
        QCOMPARE(state(response).value("selected_id").toString(),QStringLiteral("block-ch1"));
        QCOMPARE(state(response).value("adopted_revision").toString(),after);
        QCOMPARE(controller->artifacts().size(),std::size_t(3));
        for (std::size_t i=0; i<controller->artifacts().size(); ++i)
            QVERIFY(controller->generatedArtifactIsCurrent(i));
        QCOMPARE(diskFiles(directory.path()),files);
        response = gui(project,session,{"--expect",after,"--adopt","--select","block-ch1--enum-on"});
        QVERIFY2(response.value("ok").toBool(), QJsonDocument(response).toJson().constData());
        QVERIFY(!state(response).value("adopted").toBool());
        QCOMPARE(state(response).value("selected_id").toString(),QStringLiteral("block-ch1--enum-on"));
        auto guarded = std::async(std::launch::async,[&] {
            return SuiteApp::sendRequest(QStringLiteral("csrio.session.%1").arg(session),
                SuiteApp::makeRequest("action.invoke",{{"actionId","regmap.project.open"},
                    {"arguments",QJsonObject{{"projectPath",project},{"objectId","block-ch1"},
                        {"expected_revision",after},{"adopt",true}}}}));
        });
        while (guarded.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) QTest::qWait(5);
        const auto guardedResponse = guarded.get().response;
        QVERIFY(guardedResponse.value("ok").toBool());
        QCOMPARE(guardedResponse.value("result").toObject().value("session").toObject().value("selected_id").toString(),QStringLiteral("block-ch1"));
        QDialog modal(&window);
        modal.setModal(true); modal.show();
        QTRY_VERIFY(QApplication::activeModalWidget() == &modal);
        response=gui(project,session,{"--expect",after,"--adopt"});
        QCOMPARE(response.value("actual_exit").toInt(),3);
        QVERIFY(state(response).value("modal").toBool());
        modal.close(); window.activateWindow();
        QVERIFY(gui(project,session,{"--expect",after,"--select","workspace-main"}).value("ok").toBool());
        auto* tree = window.findChild<QTreeView*>("hierarchyView");
        QVERIFY(tree);
        tree->setFocus();
        tree->edit(tree->model()->index(0,0));
        QTRY_VERIFY_WITH_TIMEOUT(qobject_cast<QLineEdit*>(QApplication::focusWidget()),2000);
        auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
        editor->selectAll(); QTest::keyClicks(editor,"Uncommitted UI name");
        const auto cleanRevision = controller->modelRevision();
        response = gui(project,session,{"--expect",after,"--adopt","--select","block-ch1"});
        QCOMPARE(response.value("actual_exit").toInt(),3);
        QCOMPARE(state(response).value("error_code").toString(),QStringLiteral("active_editor"));
        QCOMPARE(editor->text(),QStringLiteral("Uncommitted UI name"));
        QCOMPARE(controller->modelRevision(),cleanRevision);
        QCOMPARE(diskFiles(directory.path()),files);
        QTest::keyClick(editor,Qt::Key_Return);
        QTRY_VERIFY(controller->isDirty());
        response = gui(project,session,{"--expect",after,"--adopt","--select","block-ch1"});
        QCOMPARE(response.value("actual_exit").toInt(),3);
        QCOMPARE(state(response).value("error_code").toString(),QStringLiteral("dirty"));
        QCOMPARE(QString::fromStdString(controller->workspace()->name),QStringLiteral("Uncommitted UI name"));
        controller->undo(); // undo the real editor transaction, never replace the private model
        QVERIFY(!controller->isDirty());
        QVERIFY(gui(project,session,{"--expect",after,"--adopt","--select","reg-control"}).value("ok").toBool());
        QVERIFY(window.close());
        for (auto it=files.begin();it!=files.end();++it) QFile::setPermissions(it.key(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
    void unchangedAdoptionPreservesHistoryAndValidatesIdentity() {
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("adoption.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        const QString patch = writePatch(directory.path(),hierarchy());
        QVERIFY(invoke({"apply",project,patch,"--expect",revision(project)}).value("ok").toBool());
        ProjectController controller;
        QVERIFY(controller.openProject(project));
        QVERIFY(controller.editWorkspace("Nested identity fixture", [](regmap::Workspace& workspace) {
            auto& blocks = workspace.addressSpaces.front().blocks;
            auto& field = blocks.front().registers.front().fields.front();
            auto member = field;
            regmap::Field container;
            container.id = "field-container"; container.name = "GROUP";
            container.type = regmap::FieldType::structure;
            container.msb = member.msb; container.lsb = member.lsb;
            container.members.push_back(std::move(member));
            field = std::move(container);
            auto& reg = blocks.back().registers.front();
            reg.type = regmap::FieldType::enumeration; reg.fields.clear();
            reg.initialValue = regmap::UnsignedValue{1};
            regmap::EnumValue value;
            value.id = "enum-register"; value.name = "ON"; value.value = regmap::UnsignedValue{1};
            reg.enumValues = {value};
        }));
        QStringList diagnostics;
        for (const auto& diagnostic : controller.diagnostics())
            diagnostics.append(QString::fromStdString(diagnostic.code + ": " + diagnostic.message));
        QVERIFY2(!controller.hasProjectErrors(), qPrintable(diagnostics.join("\n")));
        controller.save();
        QVERIFY(!controller.isDirty());
        const QString expected = controller.diskRevision();
        const auto files = diskFiles(directory.path());
        const auto modelRevision = controller.modelRevision();
        for (const auto& id : QStringList{"", "workspace-main", "page-main", "block-ch0", "reg-control",
                 "field-container", "field-mode", "enum-on", "enum-register"}) {
            QString reason;
            QCOMPARE(controller.adoptDiskRevision(expected,id,&reason),QString{});
            QCOMPARE(controller.modelRevision(),modelRevision);
        }
        QCOMPARE(controller.adoptDiskRevision(expected,"missing-id"),QStringLiteral("object_not_found"));
        QCOMPARE(controller.adoptDiskRevision("sha256:wrong","reg-control"),QStringLiteral("revision_conflict"));
        QCOMPARE(diskFiles(directory.path()),files);
        QVERIFY(controller.editWorkspace("Temporary edit", [](regmap::Workspace& workspace) {
            workspace.name = "Temporarily edited";
        }));
        QCOMPARE(controller.adoptDiskRevision(expected,{}),QStringLiteral("dirty"));
        controller.undo();
        QVERIFY(!controller.isDirty()); QVERIFY(controller.canRedo());
        const auto restoredRevision = controller.modelRevision();
        QCOMPARE(controller.adoptDiskRevision(expected,"enum-on"),QString{});
        QCOMPARE(controller.modelRevision(),restoredRevision);
        QVERIFY(controller.canRedo());
        controller.redo();
        QCOMPARE(controller.workspace()->name,std::string("Temporarily edited"));
        controller.undo();
        QCOMPARE(diskFiles(directory.path()),files);

        const auto beforeInvalidRevision = controller.modelRevision();
        auto invalid = *controller.workspace();
        invalid.addressSpaces.front().blocks.front().registers.front().width = 0;
        const auto encoded = regmap::serializeProjectText(*controller.manifest(),invalid);
        QVERIFY(encoded.text.has_value());
        { QFile file(project); QVERIFY(file.open(QIODevice::WriteOnly));
          QCOMPARE(file.write(QByteArray::fromStdString(*encoded.text)),qint64(encoded.text->size())); }
        const QString invalidRevision = controller.diskRevision();
        QCOMPARE(controller.adoptDiskRevision(expected,{}),QStringLiteral("revision_conflict"));
        QCOMPARE(controller.adoptDiskRevision(invalidRevision,{}),QStringLiteral("invalid_project"));
        QCOMPARE(controller.modelRevision(),beforeInvalidRevision);
        QVERIFY(controller.openProject(project));
        QVERIFY(!controller.isDirty()); QVERIFY(controller.hasProjectErrors());
        QCOMPARE(controller.adoptedRevision(),controller.diskRevision());
        const auto invalidModelRevision = controller.modelRevision();
        QCOMPARE(controller.adoptDiskRevision(controller.diskRevision(),{}),QStringLiteral("invalid_project"));
        QCOMPARE(controller.modelRevision(),invalidModelRevision);
    }
    void actualExecutableTargetsOneSession() {
        QTemporaryDir directory;
        const QString project=directory.filePath(QStringLiteral("实际 主窗口.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        auto start = [&](OwnedWindow& process,const QString& profile) {
            auto environment=QProcessEnvironment::systemEnvironment();
            environment.remove("QT_QPA_PLATFORM"); environment.remove("QT_HASH_SEED");
            process.setProcessEnvironment(environment);
            process.start(QStringLiteral(REGMAP_GUI_EXECUTABLE),{"--no-suite-runtime","--profile-dir",profile,"--project",project});
            return process.waitForStarted(5000);
        };
        OwnedWindow first,second;
        QVERIFY(start(first,directory.filePath("profile1")));
        QTRY_VERIFY_WITH_TIMEOUT(gui(project,first.processId()).value("ok").toBool(),15000);
        QVERIFY(start(second,directory.filePath("profile2")));
        QTRY_VERIFY_WITH_TIMEOUT(gui(project,second.processId()).value("ok").toBool(),15000);
        const QString before=revision(project);
        const QString patch=writePatch(directory.path(),hierarchy());
        QVERIFY(invoke({"apply",project,patch,"--expect",before}).value("ok").toBool());
        const QString after=revision(project);
        const auto response=gui(project,first.processId(),{"--expect",after,"--adopt","--select","block-ch1--reg-control"});
        QVERIFY2(response.value("ok").toBool(),QJsonDocument(response).toJson().constData());
        QCOMPARE(state(response).value("adopted_revision").toString(),after);
        QCOMPARE(state(response).value("selected_id").toString(),QStringLiteral("block-ch1--reg-control"));
        qInfo().noquote()<<"Real Csrio.exe:"<<QJsonDocument(response).toJson(QJsonDocument::Compact);
        const auto other=gui(project,second.processId());
        QVERIFY(state(other).value("pending").toBool());
        QCOMPARE(state(other).value("adopted_revision").toString(),before);
        QCOMPARE(gui(directory.filePath("wrong.regmap.yaml"),first.processId()).value("actual_exit").toInt(),1);
        QCOMPARE(gui(project,999999999).value("actual_exit").toInt(),7);
        QVERIFY(invoke({"validate",project}).value("ok").toBool());
        QVERIFY(invoke({"status",project,"--require-current"}).value("ok").toBool());
        // Reopen the same persisted project in the second real window through adoption.
        QVERIFY(gui(project,second.processId(),{"--expect",after,"--adopt","--select","block-ch1"}).value("ok").toBool());
        const auto files=diskFiles(directory.path());
        for (auto it=files.begin();it!=files.end();++it) QFile::setPermissions(it.key(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
    void discoversAndRejectsBadArguments() {
        const auto schema=invoke({"schema"});
        QVERIFY(state(schema).value("gui_session_contract").toObject().value("available").toBool());
        const auto help=invoke({"help","gui"});
        QVERIFY(state(help).value("usage").toString().contains("--session"));
        for(const auto& args:QList<QStringList>{{"gui"},{"gui","file","--session","0"},
            {"gui","file","--session","1","--adopt"},{"gui","file","--session","1","--select","id"},
            {"gui","file","--session","1","--expect","wrong"},{"gui","file","--session","1","--session","2"},
            {"gui","file","--session","1","--unknown"}})
            QCOMPARE(invoke(args).value("actual_exit").toInt(),2);
    }
};
QTEST_MAIN(CliSessionTests)
#include "cli_session_tests.moc"
