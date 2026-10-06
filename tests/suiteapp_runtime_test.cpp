#include "main_window.hpp"
#include "project_controller.hpp"
#include "suite_integration.hpp"
#include "workbench_theme.hpp"

#include <suiteapp/client.h>
#include <suiteapp/runtime.h>

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTreeView>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtTest>
#include <qscopeguard.h>

#include <chrono>
#include <future>
#include <memory>

namespace {
QString uniqueEndpoint()
{
    return QStringLiteral("csrio.test.%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString projectUri(const QString& path, const QString& object = {})
{
    QUrl uri;
    uri.setScheme(QStringLiteral("regmap"));
    uri.setHost(QStringLiteral("project"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("file"), path);
    if (!object.isEmpty())
        query.addQueryItem(QStringLiteral("object"), object);
    uri.setQuery(query);
    return uri.toString();
}

// The client runs outside the GUI thread so the real Provider can serve IPC.
template<typename Request>
QJsonObject ipc(Request request)
{
    auto pending = std::async(std::launch::async, std::move(request));
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        QTest::qWait(5);
    const auto response = pending.get();
    if (!response.hasResponse()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("transportError"), response.errorMessage}};
    }
    return response.response;
}

bool ok(const QJsonObject& response)
{
    if (!response.value(QStringLiteral("ok")).toBool())
        qWarning().noquote() << QJsonDocument(response).toJson(QJsonDocument::Compact);
    return response.value(QStringLiteral("ok")).toBool();
}

QJsonObject result(const QJsonObject& response)
{
    return response.value(QStringLiteral("result")).toObject();
}

QByteArray contents(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

class RuntimeProcess final : public QProcess {
public:
    ~RuntimeProcess() override
    {
        // This handle owns only the uniquely named runtime created by this test.
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(5000);
        }
    }
};
}

class RegMapSuiteAppRuntimeTest final : public QObject {
    Q_OBJECT
    QTemporaryDir profile_;

private slots:
    void initTestCase()
    {
        QVERIFY(profile_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile_.path());
        QCoreApplication::setOrganizationName(QStringLiteral("CsrioRuntimeTests"));
        QCoreApplication::setApplicationName(QStringLiteral("IsolatedSuiteAppTests"));
        QCoreApplication::setApplicationVersion(QStringLiteral(REGMAP_EXPECTED_VERSION));
        QGuiApplication::setApplicationDisplayName(QStringLiteral("Csrio"));
        WorkbenchTheme::apply(*qApp);
    }

    void routesRealRuntimeAndPreservesUnsavedModel()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = directory.filePath(QStringLiteral("first.regmap.yaml"));
        const QString second = directory.filePath(QStringLiteral("second.regmap.yaml"));
        ProjectController creator;
        QVERIFY(creator.createProject(first));
        const QString firstId = QString::fromStdString(creator.workspace()->id);
        const QString firstName = QString::fromStdString(creator.workspace()->name);
        QVERIFY(creator.createProject(second));
        const QString secondId = QString::fromStdString(creator.workspace()->id);
        const QByteArray original = contents(first);
        QVERIFY(!original.isEmpty());

        const QString endpoint = uniqueEndpoint();
        RuntimeProcess runtime;
        runtime.start(QStringLiteral(REGMAP_SUITE_RUNTIME_EXECUTABLE),
                      {QStringLiteral("--endpoint"), endpoint});
        QVERIFY2(runtime.waitForStarted(5000), qPrintable(runtime.errorString()));
        const SuiteApp::Client client(endpoint, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(client.request(QStringLiteral("health.ping")).hasResponse(), 5000);
        qInfo().noquote() << "Runtime PID" << runtime.processId() << "endpoint" << endpoint;

        MainWindow window;
        window.show();
        auto integration = std::make_unique<regmap::workbench::RegMapSuiteIntegration>(&window);
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = endpoint;
        options.startIfMissing = false;
        QString reason;
        QVERIFY2(integration->start(options, &reason), qPrintable(reason));
        QVERIFY(integration->isRegistered());
        QVERIFY(integration->start(options, &reason));
        const auto registry = ipc([&] { return client.listProviders(); });
        QVERIFY(ok(registry));
        const auto providers = result(registry).value(QStringLiteral("providers")).toArray();
        QCOMPARE(providers.size(), 1);
        const auto descriptor = providers.first().toObject();
        QCOMPARE(descriptor.value(QStringLiteral("appId")).toString(), QStringLiteral("regmap"));
        QCOMPARE(descriptor.value(QStringLiteral("displayName")).toString(), QStringLiteral("Csrio"));
        QCOMPARE(descriptor.value(QStringLiteral("version")).toString(), QStringLiteral(REGMAP_EXPECTED_VERSION));
        QCOMPARE(descriptor.value(QStringLiteral("processId")).toInteger(), QCoreApplication::applicationPid());
        const QString providerEndpoint = descriptor.value(QStringLiteral("endpoint")).toString();
        const QString firstUri = projectUri(first, firstId);
        const QString secondUri = projectUri(second, secondId);
        auto resolve = [&] { return ipc([&] { return client.resolveResource(firstUri); }); };
        auto response = resolve();
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("title")).toString(), firstName);
        QVERIFY(window.currentWorkspaceForSuite(first) == nullptr);
        QVERIFY(ok(ipc([&] { return client.invokeAction(QStringLiteral("regmap.project.open"), {}, firstUri); })));
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller != nullptr);
        QCOMPARE(QString::fromStdString(controller->workspace()->id), firstId);
        QVERIFY(controller->editWorkspace(QStringLiteral("Unsaved Suite edit"), [](regmap::Workspace& workspace) {
            workspace.name = "Unsaved IPC model";
        }));
        const auto revision = controller->modelRevision();
        const auto undoDepth = controller->undoDepth();
        response = resolve();
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("title")).toString(), QStringLiteral("Unsaved IPC model"));
        response = ipc([&] { return client.describeSurface(QStringLiteral("regmap.workbench"), firstUri); });
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("model")).toObject().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Unsaved IPC model"));
        QVERIFY(ok(ipc([&] { return client.openSurface(QStringLiteral("regmap.workbench"), firstUri); })));
        QVERIFY(ok(ipc([&] { return client.invokeAction(QStringLiteral("regmap.project.open"), {}, firstUri); })));
        QCOMPARE(controller->modelRevision(), revision);
        QCOMPARE(controller->undoDepth(), undoDepth);
        QVERIFY(controller->isDirty());
        QCOMPARE(contents(first), original);

        response = ipc([&] { return client.invokeAction(QStringLiteral("regmap.project.open"), {}, secondUri); });
        QVERIFY(!response.value(QStringLiteral("ok")).toBool());
        QVERIFY(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("Save or discard")));
        response = ipc([&] { return client.openSurface(QStringLiteral("regmap.workbench"), secondUri); });
        QVERIFY(!response.value(QStringLiteral("ok")).toBool());
        QCOMPARE(controller->modelRevision(), revision);
        QCOMPARE(controller->undoDepth(), undoDepth);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        response = ipc([&] { return client.resolveResource(projectUri(first, QStringLiteral("missing-object"))); });
        QVERIFY(!response.value(QStringLiteral("ok")).toBool());
        QCOMPARE(QString::fromStdString(controller->workspace()->id), firstId);
        QCOMPARE(contents(first), original);

        controller->undo();
        response = resolve();
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("title")).toString(), firstName);
        controller->redo();
        response = resolve();
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("title")).toString(), QStringLiteral("Unsaved IPC model"));
        auto* hierarchy = window.findChild<QTreeView*>(QStringLiteral("hierarchyView"));
        QVERIFY(hierarchy != nullptr);
        const auto root = hierarchy->model()->index(0, 0);
        QCOMPARE(root.data(Qt::UserRole + 1).toString(), firstId);
        hierarchy->setCurrentIndex(root);
        hierarchy->setFocus(Qt::OtherFocusReason);
        hierarchy->edit(root);
        QTRY_VERIFY_WITH_TIMEOUT(qobject_cast<QLineEdit*>(QApplication::focusWidget()) != nullptr, 2000);
        auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
        editor->selectAll();
        QTest::keyClicks(editor, "Committed IPC editor");
        QCOMPARE(controller->workspace()->name, std::string("Unsaved IPC model"));
        response = ipc([&] { return client.invokeAction(QStringLiteral("regmap.project.open"), {}, firstUri); });
        QVERIFY(ok(response));
        QCOMPARE(controller->workspace()->name, std::string("Committed IPC editor"));
        QCOMPARE(result(response).value(QStringLiteral("resource")).toObject().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Committed IPC editor"));
        controller->undo();
        QCOMPARE(controller->workspace()->name, std::string("Unsaved IPC model"));
        controller->save();
        QVERIFY(!controller->isDirty());
        QVERIFY(contents(first) != original);
        QVERIFY(ok(ipc([&] { return client.openSurface(QStringLiteral("regmap.workbench"), secondUri); })));
        QCOMPARE(QString::fromStdString(controller->workspace()->id), secondId);
        response = resolve();
        QVERIFY(ok(response));
        QCOMPARE(result(response).value(QStringLiteral("title")).toString(), QStringLiteral("Unsaved IPC model"));
        QVERIFY(ok(ipc([&] { return client.invokeAction(QStringLiteral("regmap.project.open"), {}, firstUri); })));
        QCOMPARE(QString::fromStdString(controller->workspace()->name), QStringLiteral("Unsaved IPC model"));
        QVERIFY(window.close());
        integration.reset();
        response = ipc([&] { return client.listProviders(); });
        QVERIFY(ok(response));
        QVERIFY(result(response).value(QStringLiteral("providers")).toArray().isEmpty());
        QVERIFY(!SuiteApp::sendRequest(providerEndpoint, SuiteApp::makeRequest(QStringLiteral("health.ping")), 100).hasResponse());
        QTRY_COMPARE_WITH_TIMEOUT(runtime.state(), QProcess::NotRunning, 5000);
        QCOMPARE(runtime.exitStatus(), QProcess::NormalExit);
        QCOMPARE(runtime.exitCode(), 0);
        qInfo() << "Verified real registry/resource/action/surface IPC, dirty model, undo/redo/save/reopen, unregister and idle exit";
    }

    void autostartsRuntimeOnUniqueEndpoint()
    {
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = uniqueEndpoint();
        options.executablePath = QStringLiteral(REGMAP_SUITE_RUNTIME_EXECUTABLE);
        const SuiteApp::Client client(options.endpoint, 500);
        QVERIFY(!client.request(QStringLiteral("health.ping")).hasResponse());
        // A failed registration must also let our detached runtime become idle.
        const auto cleanup = qScopeGuard([&] {
            if (client.request(QStringLiteral("health.ping")).hasResponse()) {
                client.registerProvider(regmap::workbench::RegMapSuiteIntegration::appDescriptor(
                    QStringLiteral(REGMAP_EXPECTED_VERSION)));
                client.unregisterProvider(QStringLiteral("regmap"));
            }
        });
        auto integration = std::make_unique<regmap::workbench::RegMapSuiteIntegration>(nullptr);
        QString reason;
        QVERIFY2(integration->start(options, &reason), qPrintable(reason));
        QVERIFY(integration->isRegistered());
        const auto registry = client.listProviders();
        QVERIFY(registry.hasResponse());
        QCOMPARE(result(registry.response).value(QStringLiteral("providers")).toArray().size(), 1);
        integration.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!client.request(QStringLiteral("health.ping")).hasResponse(), 6000);
        qInfo().noquote() << "Verified ensureRuntime auto-start and idle endpoint removal:" << options.endpoint;
    }

    void missingRuntimeAndLostRuntimeKeepStandaloneEditing()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("standalone.regmap.yaml"));
        ProjectController creator;
        QVERIFY(creator.createProject(path));
        const QString endpoint = uniqueEndpoint();
        RuntimeProcess runtime;
        MainWindow window;
        QVERIFY(window.openProjectPath(path));
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(controller != nullptr);
        auto integration = std::make_unique<regmap::workbench::RegMapSuiteIntegration>(&window);
        SuiteApp::RuntimeStartOptions options;
        options.endpoint = endpoint;
        options.startIfMissing = false;
        options.probeTimeoutMs = 30;
        QString reason;
        QVERIFY(!integration->start(options, &reason));
        QVERIFY(!reason.isEmpty());
        QVERIFY(!integration->isRegistered());
        {
            const QByteArray oldPath = qgetenv("PATH");
            const QByteArray oldRuntime = qgetenv("SUITEAPP_RUNTIME_EXECUTABLE");
            const auto restore = qScopeGuard([&] {
                qputenv("PATH", oldPath);
                qputenv("SUITEAPP_RUNTIME_EXECUTABLE", oldRuntime);
            });
            qputenv("PATH", directory.path().toUtf8());
            qunsetenv("SUITEAPP_RUNTIME_EXECUTABLE");
            options.startIfMissing = true;
            options.executablePath = directory.filePath(QStringLiteral("missing-runtime.exe"));
            QVERIFY(!integration->start(options, &reason));
            QVERIFY(reason.contains(QStringLiteral("could not be located")));
            QVERIFY(!integration->isRegistered());
        }
        QVERIFY(controller->editWorkspace(QStringLiteral("Standalone edit"), [](regmap::Workspace& workspace) {
            workspace.name = "Standalone survives";
        }));
        runtime.start(QStringLiteral(REGMAP_SUITE_RUNTIME_EXECUTABLE),
                      {QStringLiteral("--endpoint"), endpoint});
        QVERIFY(runtime.waitForStarted(5000));
        const SuiteApp::Client client(endpoint, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(client.request(QStringLiteral("health.ping")).hasResponse(), 5000);
        options.startIfMissing = false;
        QVERIFY2(integration->start(options, &reason), qPrintable(reason));
        QVERIFY(ok(ipc([&] { return client.resolveResource(projectUri(path)); })));
        runtime.kill();
        QVERIFY(runtime.waitForFinished(5000));
        controller->undo();
        QVERIFY(!controller->isDirty());
        controller->redo();
        QVERIFY(controller->isDirty());
        QCOMPARE(controller->workspace()->name, std::string("Standalone survives"));
        controller->save();
        QVERIFY(!controller->isDirty());
        integration.reset();
        QVERIFY(window.close());
        qInfo() << "Verified missing executable/unavailable runtime, retry registration, owned runtime loss and standalone save";
    }
};

QTEST_MAIN(RegMapSuiteAppRuntimeTest)
#include "suiteapp_runtime_test.moc"
