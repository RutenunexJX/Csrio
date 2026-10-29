#include "main_window.hpp"
#include "project_controller.hpp"
#include "suite_integration.hpp"
#include "workbench_theme.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/project.hpp"
#include <suiteapp/transport.h>
#include <QApplication>
#include <QAction>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QProcess>
#include <QSettings>
#include <QScrollBar>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>
#include <chrono>
#include <future>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
#ifdef Q_OS_WIN
struct ReadLock {
    HANDLE handle;
    explicit ReadLock(const QString& path, DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE)
        : handle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
            sharing, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~ReadLock() { release(); }
    void release() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE; }
    ReadLock(const ReadLock&) = delete;
    ReadLock& operator=(const ReadLock&) = delete;
};
#endif
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
    void repeatedlySavesAndAdoptsWithoutReopening() {
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("连续协作.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window;
        window.show();
        QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window);
        QVERIFY(integration.startSession());
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller);
        const auto session = QCoreApplication::applicationPid();
        for (int round = 0; round < 6; ++round) {
            const QString before = revision(project);
            const QString name = QStringLiteral("Round %1 中文").arg(round);
            auto operations = round == 0 ? hierarchy() : QJsonArray{};
            operations.append(QJsonObject{{"op","set"},{"id","workspace-main"},
                {"property","name"},{"value",name}});
            const QString patch = writePatch(directory.path(),operations);
            QStringList arguments{"apply",project,patch,"--expect",before,"--session",QString::number(session)};
            const bool deferred = round % 2 != 0;
            if (deferred) arguments.append("--defer-generation");
            const auto applied = invoke(arguments);
            QVERIFY2(applied.value("ok").toBool(),QJsonDocument(applied).toJson().constData());
            QVERIFY(state(applied).value("saved").toBool());
            QVERIFY(state(applied).value("gui_adopted").toBool());
            QVERIFY(!state(applied).value("commit_id").toString().isEmpty());
            QCOMPARE(state(applied).value("generated").toBool(),!deferred);
            const QString after = applied.value("revision").toString();
            QCOMPARE(state(applied).value("saved_revision").toString(),after);
            QVERIFY(after != before);
            QCOMPARE(controller->adoptedRevision(),after);
            QCOMPARE(QString::fromStdString(controller->workspace()->name),name);
            QVERIFY(!controller->isDirty());
            const auto adoptedGeneration = controller->modelRevision();
            QTest::qWait(650);
            QCOMPARE(controller->modelRevision(),adoptedGeneration);
            if (deferred) {
                const auto generated = invoke({"generate",project,"--expect",after});
                QVERIFY2(generated.value("ok").toBool(),QJsonDocument(generated).toJson().constData());
                QCOMPARE(revision(project),after);
            }
            QVERIFY(invoke({"status",project,"--require-current"}).value("ok").toBool());
        }
        QVERIFY(window.close());
        const auto files = diskFiles(directory.path());
        for (auto it=files.begin();it!=files.end();++it) QFile::setPermissions(it.key(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
    void reportsLockedModelAndRecoversOnce_data() {
        QTest::addColumn<bool>("deferred");
        QTest::newRow("default-generation") << false;
        QTest::newRow("deferred-generation") << true;
    }
    void coordinatedCommitBlocksLocalState_data() {
        QTest::addColumn<QString>("blocked");
        for (const auto* code : {"dirty","active_editor","modal","session_unavailable"}) QTest::newRow(code) << QString::fromLatin1(code);
    }
    void coordinatedCommitBlocksLocalState() {
        QFETCH(QString,blocked);
        QTemporaryDir directory;
        const auto project = directory.filePath("guard.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window); QVERIFY(integration.startSession());
        auto* controller = window.findChild<ProjectController*>(); QVERIFY(controller);
        const auto before = revision(project); const auto bytes = readFile(project);
        QDialog dialog(&window);
        QLineEdit* editor = nullptr;
        if (blocked == "dirty") QVERIFY(controller->editWorkspace("Local edit",[](regmap::Workspace& model) { model.name = "Retain my edit"; }));
        if (blocked == "modal") { dialog.setModal(true); dialog.show(); QTRY_VERIFY(QApplication::activeModalWidget() == &dialog); }
        if (blocked == "active_editor") {
            auto* tree = window.findChild<QTreeView*>("hierarchyView"); QVERIFY(tree);
            tree->setFocus(); tree->edit(tree->model()->index(0,0));
            QTRY_VERIFY(qobject_cast<QLineEdit*>(QApplication::focusWidget()));
            editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
            editor->selectAll(); QTest::keyClicks(editor,"Retain draft editor");
        }
        const auto patch = writePatch(directory.path(),hierarchy());
        const auto session = blocked == "session_unavailable" ? QString("999999999") : QString::number(QCoreApplication::applicationPid());
        const auto response = invoke({"apply",project,patch,"--expect",before,"--session",session,"--defer-generation"});
        QVERIFY(!response.value("ok").toBool());
        QVERIFY(!state(response).value("saved").toBool());
        QCOMPARE(state(response).value("gui").toObject().value("error_code").toString(),blocked);
        QVERIFY(!state(response).value("gui").toObject().value("suggestion").toString().isEmpty());
        QCOMPARE(readFile(project),bytes);
        if (blocked == "dirty") { QCOMPARE(controller->workspace()->name,std::string("Retain my edit")); controller->undo(); }
        if (editor) { QCOMPARE(editor->text(),QStringLiteral("Retain draft editor")); QTest::keyClick(editor,Qt::Key_Escape); }
        dialog.close(); QVERIFY(window.close());
    }
    void generationGuardPreservesEditsAndRecoveryDoesNotReapply() {
        QTemporaryDir directory;
        const auto project = directory.filePath("race.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window); QVERIFY(integration.startSession());
        auto* controller = window.findChild<ProjectController*>(); QVERIFY(controller);
        const auto before = revision(project);
        const auto prepared = window.guiCommit({{"phase","begin"},{"project",project},{"expect",before}});
        QVERIFY(prepared.value("ok").toBool());
        QJsonObject request{{"project",project},{"token",prepared.value("token")},{"session_epoch",prepared.value("session_epoch")},{"phase","confirm"}};
        QVERIFY(window.guiCommit(request).value("ok").toBool());
        // Edit after confirmation but before disk commit. A watcher must not
        // adopt over the newly edited model, even if the CLI save succeeds.
        QVERIFY(controller->editWorkspace("Concurrent GUI edit",[](regmap::Workspace& model) { model.name = "New GUI edit"; }));
        QCOMPARE(window.guiCommit(request).value("error_code").toString(),QStringLiteral("gui_generation_changed"));
        const auto patch = writePatch(directory.path(),hierarchy());
        const auto saved = invoke({"apply",project,patch,"--expect",before,"--defer-generation"});
        QVERIFY(saved.value("ok").toBool());
        const auto after = saved.value("revision").toString(); const auto savedBytes = readFile(project);
        request.insert("phase","finish"); request.insert("expect",after);
        const auto blocked = window.guiCommit(request);
        QCOMPARE(blocked.value("error_code").toString(),QStringLiteral("gui_generation_changed"));
        QVERIFY(!blocked.value("gui_adopted").toBool());
        QCOMPARE(window.guiCommit(request),blocked);
        auto wrongRevision = request; wrongRevision.insert("expect",before);
        QCOMPARE(window.guiCommit(wrongRevision).value("error_code").toString(),QStringLiteral("revision_conflict"));
        QCOMPARE(controller->workspace()->name,std::string("New GUI edit"));
        QVERIFY(controller->isDirty());
        QTest::qWait(650); QCOMPARE(controller->workspace()->name,std::string("New GUI edit"));
        controller->undo();
        const auto recovered = gui(project,QCoreApplication::applicationPid(),{"--expect",after,"--adopt"});
        QVERIFY2(recovered.value("ok").toBool(),QJsonDocument(recovered).toJson().constData());
        QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),std::size_t(2));
        QCOMPARE(readFile(project),savedBytes);
        QCOMPARE(invoke({"apply",project,patch,"--expect",before,"--defer-generation"}).value("actual_exit").toInt(),3);
        QVERIFY(window.close());
    }
    void reconnectsAndRecoversSavedRevision() {
        QTemporaryDir directory;
        const auto project = directory.filePath("reconnect.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        auto integration = std::make_unique<regmap::workbench::RegMapSuiteIntegration>(&window);
        QVERIFY(integration->startSession());
        const auto before = revision(project);
        const auto prepared = window.guiCommit({{"phase","begin"},{"project",project},{"expect",before}});
        QVERIFY(prepared.value("ok").toBool());
        const auto patch = writePatch(directory.path(),hierarchy());
        const auto saved = invoke({"apply",project,patch,"--expect",before,"--defer-generation"});
        QVERIFY(saved.value("ok").toBool());
        const auto after = saved.value("revision").toString(); const auto bytes = readFile(project);
        integration.reset();
        const auto unavailable = gui(project,QCoreApplication::applicationPid(),{"--expect",after,"--adopt"});
        QCOMPARE(state(unavailable).value("error_code").toString(),QStringLiteral("session_unavailable"));
        QVERIFY(state(unavailable).contains("reconnect"));
        integration = std::make_unique<regmap::workbench::RegMapSuiteIntegration>(&window); QVERIFY(integration->startSession());
        const auto discovery = invoke({"gui",project,"--discover"});
        QVERIFY(discovery.value("ok").toBool());
        QCOMPARE(state(discovery).value("sessions").toArray().size(),1);
        const auto stale = window.guiCommit({{"phase","finish"},{"project",project},{"expect",after},
            {"token",prepared.value("token")},{"session_epoch",prepared.value("session_epoch")}});
        QCOMPARE(stale.value("error_code").toString(),QStringLiteral("session_expired"));
        QVERIFY(gui(project,QCoreApplication::applicationPid(),{"--expect",after,"--adopt"}).value("ok").toBool());
        QCOMPARE(readFile(project),bytes);
        auto* controller = window.findChild<ProjectController*>(); QVERIFY(controller);
        QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),std::size_t(2));
        QVERIFY(window.close());
    }
    void automaticallyAdoptsOfflineChangesOnce() {
        QTemporaryDir directory;
        const auto project = directory.filePath("automatic.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        auto* controller = window.findChild<ProjectController*>(); QVERIFY(controller);
        const auto patch = writePatch(directory.path(),hierarchy());
        const auto saved = invoke({"apply",project,patch,"--expect",revision(project),"--defer-generation"});
        QVERIFY(saved.value("ok").toBool());
        const auto after = saved.value("revision").toString();
        QTRY_COMPARE_WITH_TIMEOUT(controller->adoptedRevision(),after,5000);
        const auto generation = controller->modelRevision();
        QTest::qWait(650); QCOMPARE(controller->modelRevision(),generation);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(window.close());
    }
    void oneCallPreservesConcurrentEdits_data() {
        QTest::addColumn<QString>("phase");
        QTest::newRow("before-save") << QStringLiteral("confirm");
        QTest::newRow("after-save") << QStringLiteral("finish");
    }
    void oneCallPreservesConcurrentEdits() {
        QFETCH(QString,phase);
        QTemporaryDir directory;
        const auto project = directory.filePath("native-race.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        auto* controller = window.findChild<ProjectController*>(); QVERIFY(controller);
        bool injected = false, editSucceeded = false;
        // Inject a real GUI transaction at a deterministic protocol boundary.
        SuiteApp::JsonServer endpoint(QStringLiteral("csrio.session.%1").arg(QCoreApplication::applicationPid()),
            [&](const QJsonObject& request) {
                const auto params = request.value("params").toObject();
                if (request.value("method") == "regmap.commit") {
                    if (params.value("phase") == phase && !injected) {
                        injected = true;
                        editSucceeded = controller->editWorkspace("New user edit",[](regmap::Workspace& model) { model.name = "Preserve this new edit"; });
                    }
                    return SuiteApp::successResponse(request,window.guiCommit(params));
                }
                return SuiteApp::successResponse(request,window.guiSession(params));
            });
        QVERIFY(endpoint.start());
        const auto before = revision(project);
        const auto beforeBytes = readFile(project);
        const auto patch = writePatch(directory.path(),hierarchy());
        const auto response = invoke({"apply",project,patch,"--expect",before,"--session",QString::number(QCoreApplication::applicationPid()),"--defer-generation","--changes","summary"});
        QVERIFY(injected && editSucceeded);
        QCOMPARE(response.value("actual_exit").toInt(),3);
        QVERIFY(!state(response).value("gui_adopted").toBool());
        QCOMPARE(state(response).value("gui").toObject().value("error_code").toString(),QStringLiteral("gui_generation_changed"));
        QCOMPARE(controller->workspace()->name,std::string("Preserve this new edit"));
        if (phase == "confirm") {
            QVERIFY(!state(response).value("saved").toBool());
            QVERIFY(state(response).value("saved_revision").isNull());
            QCOMPARE(state(response).value("failure").toObject().value("stage").toString(),QStringLiteral("gui_confirm"));
            QVERIFY(!state(response).value("changes").toArray().isEmpty());
            QVERIFY(!state(response).contains("gui_recovery"));
            QCOMPARE(readFile(project),beforeBytes);
            controller->undo();
            const auto retry = invoke({"apply",project,patch,"--expect",before,"--session",QString::number(QCoreApplication::applicationPid()),"--defer-generation"});
            QVERIFY2(retry.value("ok").toBool(),QJsonDocument(retry).toJson().constData());
            QVERIFY(state(retry).value("saved").toBool() && state(retry).value("gui_adopted").toBool());
            QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),std::size_t(2));
            QVERIFY(window.close());
            return;
        }
        QVERIFY(state(response).value("saved").toBool());
        QCOMPARE(state(response).value("generation_state").toString(),QStringLiteral("deferred"));
        const auto after = state(response).value("saved_revision").toString();
        QCOMPARE(revision(project),after);
        QCOMPARE(controller->workspace()->name,std::string("Preserve this new edit"));
        const auto bytes = readFile(project);
        controller->undo();
        QStringList recovery;
        for (const auto& argument : state(response).value("gui_recovery").toObject().value("arguments").toArray()) recovery.append(argument.toString());
        QCOMPARE(recovery.front(),QStringLiteral("gui"));
        const auto recovered = invoke(recovery);
        QVERIFY2(recovered.value("ok").toBool(),QJsonDocument(recovered).toJson().constData());
        QCOMPARE(readFile(project),bytes);
        QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),std::size_t(2));
        QVERIFY(window.close());
    }
    void preservesViewAndLocatesSurvivingParent() {
        QTemporaryDir directory;
        const auto project = directory.filePath("navigation.regmap.yaml");
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        auto operations = hierarchy();
        for (int index = 0; index < 40; ++index) operations.append(QJsonObject{{"op","add"},{"kind","register"},
            {"id",QStringLiteral("reg-scroll-%1").arg(index)},{"parent_id","block-ch0"},
            {"value",QJsonObject{{"name",QStringLiteral("SCROLL_%1").arg(index)},{"offset","auto"}}}});
        auto patch = writePatch(directory.path(),operations);
        QVERIFY(invoke({"apply",project,patch,"--expect",revision(project),"--defer-generation"}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window); QVERIFY(integration.startSession());
        const auto session = QCoreApplication::applicationPid();
        QVERIFY(gui(project,session,{"--expect",revision(project),"--select","reg-scroll-30"}).value("ok").toBool());
        auto* table = window.findChild<QTableView*>("registerView"); QVERIFY(table);
        auto* tree = window.findChild<QTreeView*>("hierarchyView"); QVERIFY(tree);
        tree->expandAll();
        QTest::qWait(100);
        table->verticalScrollBar()->setValue(table->verticalScrollBar()->maximum()/2);
        const int scroll = table->verticalScrollBar()->value();
        const auto selected = state(gui(project,session)).value("selected_id");
        patch = writePatch(directory.path(),QJsonArray{QJsonObject{{"op","set"},{"id","workspace-main"},{"property","name"},{"value","Renamed workspace"}}});
        const auto renamed = invoke({"apply",project,patch,"--expect",revision(project),"--session",QString::number(session),"--defer-generation"});
        QVERIFY2(renamed.value("ok").toBool(),QJsonDocument(renamed).toJson().constData());
        QCOMPARE(state(gui(project,session)).value("selected_id"),selected);
        QCOMPARE(table->verticalScrollBar()->value(),scroll);
        QVERIFY(tree->isExpanded(tree->model()->index(0,0)));
        patch = writePatch(directory.path(),QJsonArray{QJsonObject{{"op","remove"},{"id","reg-scroll-30"}}});
        const auto removed = invoke({"apply",project,patch,"--expect",revision(project),"--session",QString::number(session),"--defer-generation"});
        QVERIFY2(removed.value("ok").toBool(),QJsonDocument(removed).toJson().constData());
        QCOMPARE(state(gui(project,session)).value("selected_id").toString(),QStringLiteral("block-ch0"));
        auto* action = window.findChild<QAction*>("checkReferencesAction"); QVERIFY(action); QVERIFY(action->shortcut().isEmpty());
        action->trigger();
        auto* dialog = window.findChild<QDialog*>("referenceChecksDialog"); QVERIFY(dialog); QVERIFY(!dialog->isModal());
        auto* references = dialog->findChild<QTableWidget*>("referenceChecksTable"); QVERIFY(references); QVERIFY(references->rowCount() > 0);
        dialog->close(); QVERIFY(window.close());
    }
    void reportsLockedModelAndRecoversOnce() {
#ifndef Q_OS_WIN
        QSKIP("Controlled Windows sharing locks are required.");
#else
        QFETCH(bool,deferred);
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("被占用的工程.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        MainWindow window;
        window.show();
        QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window);
        QVERIFY(integration.startSession());
        const QString before = revision(project);
        const auto original = readFile(project);
        const QString patch = writePatch(directory.path(),hierarchy());
        QStringList arguments{"apply",project,patch,"--expect",before,"--session",QString::number(QCoreApplication::applicationPid())};
        if (deferred) arguments.append("--defer-generation");
        ReadLock lock(project);
        QVERIFY(lock.handle != INVALID_HANDLE_VALUE);
        QVERIFY(invoke(arguments + QStringList{"--dry-run"}).value("ok").toBool());
        const auto failed = invoke(arguments);
        QCOMPARE(failed.value("actual_exit").toInt(),4);
        QVERIFY(!state(failed).value("saved").toBool());
        QCOMPARE(state(failed).value("failure").toObject().value("stage").toString(),QStringLiteral("save_model"));
        const auto diagnostic = failed.value("diagnostics").toArray().last().toObject();
        QCOMPARE(diagnostic.value("code").toString(),QStringLiteral("RM1103"));
        const auto error = diagnostic.value("file_error").toObject();
        qInfo().noquote() << "Locked model diagnostic:" << QJsonDocument(error).toJson(QJsonDocument::Compact);
        QCOMPARE(error.value("stage").toString(),QStringLiteral("replace"));
        QCOMPARE(error.value("role").toString(),QStringLiteral("target"));
        QCOMPARE(QFileInfo(error.value("target_path").toString()).absoluteFilePath(),project);
        QVERIFY(!error.value("temporary_path").toString().isEmpty());
        QVERIFY(error.value("native_error_code").toInt() == ERROR_ACCESS_DENIED ||
                error.value("native_error_code").toInt() == ERROR_SHARING_VIOLATION);
        QCOMPARE(error.value("attempts").toInt(),4);
        QVERIFY(!error.value("error_text").toString().isEmpty());
        QVERIFY(!error.value("recovery").toString().isEmpty());
        QCOMPARE(readFile(project),original);
        QCOMPARE(revision(project),before);
        QVERIFY(invoke({"validate",project}).value("ok").toBool());
        lock.release();
        const auto recovered = invoke(arguments);
        QVERIFY2(recovered.value("ok").toBool(),QJsonDocument(recovered).toJson().constData());
        const QString after = recovered.value("revision").toString();
        QVERIFY(gui(project,QCoreApplication::applicationPid(),{"--expect",after,"--adopt"}).value("ok").toBool());
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller);
        QCOMPARE(controller->workspace()->addressSpaces.front().blocks.size(),std::size_t(2));
        // A stale retry cannot apply the already successful patch twice.
        const auto stale = invoke(arguments);
        QCOMPARE(stale.value("actual_exit").toInt(),3);
        QVERIFY(!state(stale).value("saved").toBool());
        QVERIFY(!state(stale).value("gui_adopted").toBool());
        QVERIFY(state(stale).value("saved_revision").isNull());
        QCOMPARE(state(stale).value("gui").toObject().value("status").toString(),QStringLiteral("not_started"));
        QCOMPARE(revision(project),after);
        QVERIFY(window.close());
        const auto files = diskFiles(directory.path());
        for (auto it=files.begin();it!=files.end();++it) QFile::setPermissions(it.key(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
#endif
    }
    void rejectsExclusiveReadLockWithoutWrites() {
#ifndef Q_OS_WIN
        QSKIP("Controlled Windows sharing locks are required.");
#else
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("exclusive.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        const QString before = revision(project);
        const auto original = readFile(project);
        const QString patch = writePatch(directory.path(),hierarchy());
        ReadLock lock(project,0);
        QVERIFY(lock.handle != INVALID_HANDLE_VALUE);
        const auto failed = invoke({"apply",project,patch,"--expect",before});
        QVERIFY(!failed.value("ok").toBool());
        QVERIFY(!state(failed).value("saved").toBool());
        QCOMPARE(state(failed).value("failure").toObject().value("stage").toString(),QStringLiteral("load_project"));
        const auto error = failed.value("diagnostics").toArray().first().toObject().value("file_error").toObject();
        QCOMPARE(error.value("stage").toString(),QStringLiteral("open_read"));
        QCOMPARE(error.value("native_error_code").toInt(),int(ERROR_SHARING_VIOLATION));
        lock.release();
        QCOMPARE(readFile(project),original);
        QVERIFY(invoke({"validate",project}).value("ok").toBool());
#endif
    }
    void recoversOutputsWithoutRepeatingSavedMutation() {
#ifndef Q_OS_WIN
        QSKIP("Controlled Windows sharing locks are required.");
#else
        QTemporaryDir directory;
        const QString project = directory.filePath(QStringLiteral("output-recovery.regmap.yaml"));
        QVERIFY(invoke({"init",project,"--no-generate","--workspace-id","workspace-main"}).value("ok").toBool());
        QString patch = writePatch(directory.path(),hierarchy());
        QVERIFY(invoke({"apply",project,patch,"--expect",revision(project)}).value("ok").toBool());
        MainWindow window; window.show(); QVERIFY(window.openProjectPath(project,false));
        regmap::workbench::RegMapSuiteIntegration integration(&window); QVERIFY(integration.startSession());
        const auto opened = regmap::openProject(project.toStdWString());
        QVERIFY(opened.manifest);
        const QString output = QString::fromStdWString(opened.manifest->targets.front().path.resolved.wstring());
        const auto originalOutput = readFile(output);
        patch = writePatch(directory.path(),QJsonArray{QJsonObject{{"op","copy"},{"id","block-ch0"},
            {"new_id","block-extra"},{"parent_id","page-main"},{"value",QJsonObject{{"name","Extra"},{"base","auto"}}}}});
        const QString before = revision(project);
        ReadLock lock(output);
        QVERIFY(lock.handle != INVALID_HANDLE_VALUE);
        const auto failed = invoke({"apply",project,patch,"--expect",before,"--session",QString::number(QCoreApplication::applicationPid())});
        QCOMPARE(failed.value("actual_exit").toInt(),4);
        QVERIFY(state(failed).value("saved").toBool());
        QVERIFY(state(failed).value("gui_adopted").toBool());
        QVERIFY(!state(failed).value("generated").toBool());
        QCOMPARE(state(failed).value("failure").toObject().value("stage").toString(),QStringLiteral("write_outputs"));
        QCOMPARE(readFile(output),originalOutput);
        const auto savedModel = readFile(project);
        const QString after = revision(project);
        QVERIFY(before != after);
        const auto recovery = state(failed).value("recovery").toObject();
        QCOMPARE(recovery.value("command").toString(),QStringLiteral("generate"));
        QStringList arguments;
        for (const auto& argument : recovery.value("arguments").toArray()) arguments.append(argument.toString());
        lock.release();
        const auto recovered = invoke(arguments);
        QVERIFY2(recovered.value("ok").toBool(),QJsonDocument(recovered).toJson().constData());
        QCOMPARE(readFile(project),savedModel);
        QCOMPARE(revision(project),after);
        QCOMPARE(regmap::openProject(project.toStdWString()).workspace->addressSpaces.front().blocks.size(),std::size_t(3));
        QVERIFY(invoke({"status",project,"--require-current"}).value("ok").toBool());
        QVERIFY(window.close());
        const auto files = diskFiles(directory.path());
        for (auto it=files.begin();it!=files.end();++it) QFile::setPermissions(it.key(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
#endif
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
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller);
        QTRY_COMPARE_WITH_TIMEOUT(controller->adoptedRevision(),after,5000);
        auto response = gui(project,session);
        QVERIFY(!state(response).value("pending").toBool());
        const auto files = diskFiles(directory.path());
        const auto modelRevision = controller->modelRevision();
        response = gui(project,session,{"--expect",before,"--adopt","--select","block-ch1"});
        QCOMPARE(response.value("actual_exit").toInt(),3);
        response = gui(project,session,{"--expect",after,"--adopt","--select","missing-id"});
        QCOMPARE(response.value("actual_exit").toInt(),1);
        QCOMPARE(controller->modelRevision(),modelRevision);
        QCOMPARE(diskFiles(directory.path()),files);
        // The legacy public SDK route observes the automatically adopted snapshot.
        auto pending = std::async(std::launch::async,[&] {
            return SuiteApp::sendRequest(QStringLiteral("csrio.session.%1").arg(session),
                SuiteApp::makeRequest("action.invoke",{{"actionId","regmap.project.open"},
                    {"arguments",QJsonObject{{"projectPath",project},{"objectId","block-ch1"}}}}));
        });
        while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) QTest::qWait(5);
        const auto legacy = pending.get().response;
        QVERIFY2(legacy.value("ok").toBool(),QJsonDocument(legacy).toJson().constData());
        response = gui(project,session,{"--expect",after,"--adopt","--select","block-ch1"});
        QVERIFY2(response.value("ok").toBool(), QJsonDocument(response).toJson().constData());
        QVERIFY(!state(response).value("adopted").toBool());
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
        QTRY_COMPARE_WITH_TIMEOUT(state(gui(project,second.processId())).value("adopted_revision").toString(),after,5000);
        const auto other=gui(project,second.processId());
        QVERIFY(!state(other).value("pending").toBool());
        QVERIFY(state(other).value("selected_id").toString() != "block-ch1--reg-control");
        QCOMPARE(gui(directory.filePath("wrong.regmap.yaml"),first.processId()).value("actual_exit").toInt(),1);
        QCOMPARE(gui(project,999999999).value("actual_exit").toInt(),7);
        QVERIFY(invoke({"validate",project}).value("ok").toBool());
        QVERIFY(invoke({"status",project,"--require-current"}).value("ok").toBool());
        // Explicit navigation still targets only the named window.
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
            {"gui","file","--session","1","--unknown"},
            {"gui","file","--discover","--session","1"},{"gui","file","--discover","--adopt"},
            {"apply","file","patch","--session","0"},{"apply","file","patch","--session","1","--session","2"}})
            QCOMPARE(invoke(args).value("actual_exit").toInt(),2);
    }
};
QTEST_MAIN(CliSessionTests)
#include "cli_session_tests.moc"
