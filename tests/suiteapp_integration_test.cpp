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

    void resolvesRegisterAndFieldDeepLinks()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("deep-link.regmap.yaml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(
            "schema_version: 2\n"
            "workspace:\n"
            "  id: deep-workspace\n"
            "  name: Deep Workspace\n"
            "  address_spaces:\n"
            "    - id: space-main\n"
            "      name: Main\n"
            "      base: 0x0\n"
            "      address_width: 32\n"
            "      blocks:\n"
            "        - id: block-main\n"
            "          name: Main Block\n"
            "          base: 0x0\n"
            "          size: 0x100\n"
            "          registers:\n"
            "            - id: reg-control\n"
            "              name: CONTROL\n"
            "              offset: 0x0\n"
            "              width: 32\n"
            "              type: field\n"
            "              array:\n"
            "                count: 1\n"
            "                stride: 0x4\n"
            "              reset: 0x0\n"
            "              access: rw\n"
            "              fields:\n"
            "                - id: field-enable\n"
            "                  name: ENABLE\n"
            "                  msb: 0\n"
            "                  lsb: 0\n"
            "                  type: bool\n"
            "                  sw_access: rw\n"
            "                  hw_access: none\n"
            "                  read_side_effect: none\n"
            "                  write_side_effect: write\n"
            "                  enum_values: []\n"
            "rtl:\n"
            "  path: rtl/registers.sv\n"
            "  module: deep_registers\n"
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
        uri.setHost(QStringLiteral("register"));
        uri.setPath(QStringLiteral("/reg-control"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("file"), path);
        query.addQueryItem(QStringLiteral("field"),
                           QStringLiteral("field-enable"));
        uri.setQuery(query);

        regmap::workbench::RegMapSuiteIntegration integration(nullptr);
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"), uri.toString()}}));
        QVERIFY2(response.value(QStringLiteral("ok")).toBool(),
                 QJsonDocument(response)
                     .toJson(QJsonDocument::Compact)
                     .constData());
        const QJsonObject result =
            response.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("registerId")).toString(),
                 QStringLiteral("reg-control"));
        QCOMPARE(result.value(QStringLiteral("fieldId")).toString(),
                 QStringLiteral("field-enable"));
        QCOMPARE(result.value(QStringLiteral("object")).toObject()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("field"));

        uri.setPath(QStringLiteral("/missing-register"));
        uri.setQuery(query);
        const QJsonObject invalid = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"), uri.toString()}}));
        QVERIFY(!invalid.value(QStringLiteral("ok")).toBool());
        QVERIFY(QJsonDocument(invalid)
                    .toJson(QJsonDocument::Compact)
                    .contains("missing-register"));
    }
};

QTEST_APPLESS_MAIN(RegMapSuiteAppIntegrationTest)

#include "suiteapp_integration_test.moc"
