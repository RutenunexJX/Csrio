#include "suite_integration.hpp"

#include <suiteapp/protocol.h>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

class RegMapSuiteAppIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void publishesStableContract()
    {
        const QJsonObject descriptor =
            regmap::workbench::RegMapSuiteIntegration::appDescriptor(
                QStringLiteral("1.2.3"), QStringLiteral("test.regmap"));
        QString reason;
        QVERIFY2(SuiteApp::validateAppDescriptor(descriptor, &reason),
                 qPrintable(reason));
        QVERIFY(SuiteApp::descriptorOwnsScheme(
            descriptor, QStringLiteral("regmap")));
        QVERIFY(SuiteApp::descriptorOwnsAction(
            descriptor, QStringLiteral("regmap.project.open")));
        QVERIFY(SuiteApp::descriptorOwnsSurface(
            descriptor, QStringLiteral("regmap.workbench")));
    }

    void resolvesProjectThroughCoreApi()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("project.regmap.yaml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(
            "schema_version: 2\n"
            "workspace:\n"
            "  id: suite-workspace\n"
            "  name: Suite Workspace\n"
            "  address_spaces: []\n"
            "rtl:\n"
            "  path: rtl/registers.sv\n"
            "  module: suite_registers\n"
            "generation:\n"
            "  output_directory: generated\n"
            "  targets:\n"
            "    - kind: xlsx\n"
            "      path: register-map.xlsx\n"
            "    - kind: c-header\n"
            "      path: registers.h\n"
            "    - kind: markdown\n"
            "      path: register-map.md\n");
        file.close();

        QUrl uri;
        uri.setScheme(QStringLiteral("regmap"));
        uri.setHost(QStringLiteral("project"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("file"), path);
        query.addQueryItem(QStringLiteral("object"),
                           QStringLiteral("suite-workspace"));
        uri.setQuery(query);

        regmap::workbench::RegMapSuiteIntegration integration(nullptr);
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"), uri.toString()}}));
        QVERIFY2(response.value(QStringLiteral("ok")).toBool(),
                 QJsonDocument(response).toJson(QJsonDocument::Compact).constData());
        const QJsonObject result =
            response.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("regmap-project"));
        QCOMPARE(result.value(QStringLiteral("object")).toObject()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("workspace"));
    }
};

QTEST_APPLESS_MAIN(RegMapSuiteAppIntegrationTest)

#include "suiteapp_integration_test.moc"
