#include "cli_app.hpp"
#include "regmap/core/manifest.hpp"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

struct Invocation {
    int exitCode{0};
    QString standardOutput;
    QString standardError;
};

[[nodiscard]] Invocation invoke(
    const QStringList& arguments,
    const QString& standardInput = {})
{
    Invocation result;
    QString inputText =
        standardInput;
    QTextStream input(
        &inputText,
        QIODevice::ReadOnly);
    QTextStream output(
        &result.standardOutput);
    QTextStream error(
        &result.standardError);
    result.exitCode =
        regmap::cli::run(
            arguments,
            input,
            output,
            error);
    output.flush();
    error.flush();
    return result;
}

[[nodiscard]] Invocation invokeExecutable(
    const QStringList& arguments,
    const QProcessEnvironment& environment =
        QProcessEnvironment::systemEnvironment())
{
    Invocation result;
    QProcess process;
    process.setProgram(
        QString::fromUtf8(
            REGMAP_CLI_EXECUTABLE));
    process.setArguments(arguments);
    QProcessEnvironment processEnvironment = environment;
    QString inheritedPath = processEnvironment.value(QStringLiteral("PATH"));
    if (inheritedPath.isEmpty()) {
        inheritedPath = processEnvironment.value(QStringLiteral("Path"));
    }
    processEnvironment.remove(QStringLiteral("PATH"));
    processEnvironment.remove(QStringLiteral("Path"));
    processEnvironment.insert(
        QStringLiteral("PATH"),
        QString::fromUtf8(REGMAP_TEST_QT_RUNTIME_DIR) + QDir::listSeparator() +
            QString::fromUtf8(REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR) + QDir::listSeparator() +
            inheritedPath);
    process.setProcessEnvironment(
        processEnvironment);
    process.start();
    if (!process.waitForStarted(30000) ||
        !process.waitForFinished(30000)) {
        result.exitCode = -1;
        result.standardError =
            process.errorString();
        process.kill();
        process.waitForFinished();
        return result;
    }
    result.exitCode =
        process.exitCode();
    result.standardOutput =
        QString::fromUtf8(
            process.readAllStandardOutput());
    result.standardError =
        QString::fromUtf8(
            process.readAllStandardError());
    return result;
}

[[nodiscard]] QJsonObject json(
    const Invocation& invocation)
{
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            invocation.standardOutput
                .toUtf8(),
            &error);
    if (error.error !=
        QJsonParseError::NoError) {
        qWarning().noquote()
            << invocation.standardOutput;
        QTest::qFail(
            qPrintable(
                error.errorString()),
            __FILE__, __LINE__);
        return {};
    }
    if (!document.isObject()) {
        QTest::qFail(
            "CLI response is not a JSON object",
            __FILE__, __LINE__);
        return {};
    }
    return document.object();
}

[[nodiscard]] QString currentRevision(
    const QString& project)
{
    const Invocation summary =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    if (summary.exitCode != 0) {
        return {};
    }
    return json(summary)
        .value(
            QStringLiteral(
                "revision"))
        .toString();
}

[[nodiscard]] bool writePatch(
    const QString& path,
    const QJsonArray& operations,
    const QString& expectedRevision = {})
{
    QJsonObject patch{
        {QStringLiteral(
             "api_version"),
         regmap::cli::apiVersion},
        {QStringLiteral("operations"),
         operations},
    };
    if (!expectedRevision.isEmpty()) {
        patch.insert(
            QStringLiteral(
                "expected_revision"),
            expectedRevision);
    }
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray content =
        QJsonDocument(patch).toJson(
            QJsonDocument::Indented);
    return file.write(content) ==
        content.size();
}

[[nodiscard]] QString patchText(
    const QJsonArray& operations,
    const QString& expectedRevision = {})
{
    QJsonObject patch{
        {QStringLiteral(
             "api_version"),
         regmap::cli::apiVersion},
        {QStringLiteral("operations"),
         operations},
    };
    if (!expectedRevision.isEmpty()) {
        patch.insert(
            QStringLiteral(
                "expected_revision"),
            expectedRevision);
    }
    return QString::fromUtf8(
        QJsonDocument(patch).toJson(
            QJsonDocument::Compact));
}

[[nodiscard]] QByteArray readFile(
    const QString& path)
{
    QFile file(path);
    if (!file.open(
            QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

[[nodiscard]] QString createProject(
    const QString& directory)
{
    const QString path =
        QDir(directory).filePath(
            QStringLiteral(
                "device.regmap.yaml"));
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Text)) {
        return {};
    }
    const QByteArray content =
        QByteArrayLiteral(
            R"(schema_version: 2
workspace:
  id: workspace-main
  name: CLI Fixture
  address_spaces:
    - id: page-main
      name: Main
      base: 0x1000
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x20
          size: 0x100
          registers:
            - id: reg-control
              name: CONTROL
              offset: 0x4
              width: 32
              type: field
              initial: 0x1
              reset: 0x1
              access: rw
              tags: [control]
              description: Main control.
              enum_values: []
              fields:
                - id: field-enable
                  name: ENABLE
                  msb: 0
                  lsb: 0
                  type: bool
                  sw_access: rw
                  hw_access: ro
                  reset: 0x1
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []
rtl:
  path: rtl/device_registers.sv
  module: device_registers
generation:
  output_directory: generated
  targets:
    - kind: xlsx
      path: register-map.xlsx
    - kind: c-header
      path: device_regs.h
      options:
        guard: DEVICE_REGS_H
    - kind: markdown
      path: register-map.md
)");
    if (file.write(content) !=
        content.size()) {
        return {};
    }
    file.close();
    return path;
}

[[nodiscard]] QString
createOverlappingProject(
    const QString& directory)
{
    const QString path =
        createProject(directory);
    if (path.isEmpty()) {
        return {};
    }
    QByteArray content =
        readFile(path);
    const QByteArray marker =
        QByteArrayLiteral("rtl:");
    const QByteArray overlapping =
        QByteArrayLiteral(
            R"(            - id: reg-overlap
              name: OVERLAP
              offset: 0x4
              width: 32
              type: unsigned
              initial: 0x0
              reset: 0x0
              access: rw
              tags: []
              description: Overlapping register.
              enum_values: []
              fields: []
)");
    const qsizetype markerIndex =
        content.indexOf(marker);
    if (markerIndex < 0) {
        return {};
    }
    content.insert(
        markerIndex,
        overlapping);
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate)) {
        return {};
    }
    if (file.write(content) !=
        content.size()) {
        return {};
    }
    return path;
}

[[nodiscard]] QString createLargeQueryProject(
    const QString& directory)
{
    const QString path =
        createProject(directory);
    if (path.isEmpty()) {
        return {};
    }
    QByteArray content = readFile(path);
    content.replace(
        QByteArrayLiteral("size: 0x100"),
        QByteArrayLiteral("size: 0x1000"));
    QByteArray registers;
    for (int index = 0; index < 105;
         ++index) {
        registers.append(
            QStringLiteral(
                "            - id: reg-query-%1\n"
                "              name: QUERY_%2\n"
                "              offset: 0x%3\n"
                "              width: 32\n"
                "              type: unsigned\n"
                "              initial: 0x0\n"
                "              reset: 0x0\n"
                "              access: rw\n"
                "              tags: []\n"
                "              description: Query pagination fixture.\n"
                "              enum_values: []\n"
                "              fields: []\n")
                .arg(index)
                .arg(index, 3, 10,
                     QLatin1Char('0'))
                .arg(8 + index * 4,
                     0, 16)
                .toUtf8());
    }
    const qsizetype markerIndex =
        content.indexOf(
            QByteArrayLiteral("rtl:"));
    if (markerIndex < 0) {
        return {};
    }
    content.insert(markerIndex, registers);
    QFile file(path);
    if (!file.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate) ||
        file.write(content) !=
            content.size()) {
        return {};
    }
    return path;
}

void makeGeneratedFilesWritable(
    const QString& directory)
{
    QDir generated(
        QDir(directory).filePath(
            QStringLiteral(
                "generated")));
    for (const QFileInfo& info :
         generated.entryInfoList(
             QDir::Files |
             QDir::NoDotAndDotDot)) {
        QFile::setPermissions(
            info.absoluteFilePath(),
            info.permissions() |
                QFileDevice::WriteUser);
    }
}

} // namespace

class CliTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesMachineReadableSchema();
    void keepsHelpAndVersionMachineReadable();
    void keepsJsonTransportSingleDocument();
    void inspectsAndValidatesByStableId();
    void diffsProjectsReadOnly();
    void guardsGetByRevision();
    void getsManyObjectsInOneSnapshot();
    void findsObjectsByHumanFacingClues();
    void boundsListAndFindByDefault();
    void requiresOneFindResult();
    void queriesRecursiveObjectScopes();
    void previewsAndWritesReadOnlyOutputs();
    void reportsAndRepairsOutputStatusWithoutRewritingCurrentFiles();
    void keepsXlsxStatusStableAcrossProcesses();
    void runsCoreCommandsWithoutAPlatformPlugin();
    void supportsUnicodeProjectPathsAcrossProcesses();
    void returnsStableUsageAndProjectErrors();
    void previewsAtomicPatchWithoutWriting();
    void appliesRevisionGuardedPatchAndRegenerates();
    void rejectsUnsafePatchesWithoutPartialWrites();
    void rejectsIndependentFieldResetWrites();
    void reportsStructuredPrewriteFailures();
    void readsPatchFromStandardInput();
    void addsAndRemovesHierarchyAtomically();
    void createsHierarchyWithAutomaticIdsAndReferences();
    void usesEarlierOperationResultsAsObjectReferences();
    void usesNamedOperationReferences();
    void autoPlacesBlocksWithoutBaseArithmetic();
    void autoPlacesRegistersWithoutAddressArithmetic();
    void autoPlacesFieldsWithoutBitArithmetic();
    void copiesHierarchyWithDeterministicIds();
    void autoPlacesCopiedHierarchyRoots();
    void copiesWithAutomaticIdentityAndName();
    void movesObjectsBetweenParentsAtomically();
    void autoPlacesMovedHierarchyRoots();
    void reordersPagesAndBlocksWithBeforeId();
    void reordersRegistersAroundFixedAddresses();
    void repositionsFieldLsbWithoutChangingIdentity();
    void recoversAfterLockedGeneratedOutput();
    void repairsInvalidProjectAtomically();
    void avoidsWritingNetNoOpPatch();
    void initializesProjectWithoutOverwritingFiles();
};

void CliTests::exposesMachineReadableSchema()
{
    const Invocation invocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("schema")});
    QCOMPARE(
        invocation.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QVERIFY(
        invocation.standardError
            .isEmpty());
    const QJsonObject root =
        json(invocation);
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "api_version"))
            .toInt(),
        regmap::cli::apiVersion);
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "result"))
            .toObject()
            .value(
                QStringLiteral(
                    "cli_version"))
            .toString(),
        QString::fromLatin1(
            regmap::cli::cliVersion.data(),
            static_cast<qsizetype>(
                regmap::cli::cliVersion
                    .size())));
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "command"))
            .toString(),
        QStringLiteral("schema"));
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "exit_code"))
            .toInt(),
        0);
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("success"));
    QVERIFY(
        !root.contains(
            QStringLiteral(
                "error_code")));
    const QJsonObject result =
        root.value(
                QStringLiteral(
                    "result"))
            .toObject();
    const QJsonObject projectSchema =
        result.value(
                  QStringLiteral(
                      "project_schema"))
            .toObject();
    QCOMPARE(
        projectSchema.value(
                         QStringLiteral(
                             "current_version"))
            .toInt(),
        regmap::ProjectManifest::
            currentSchemaVersion);
    QCOMPARE(
        projectSchema.value(
                         QStringLiteral(
                             "supported_versions"))
            .toArray(),
        QJsonArray{
            regmap::ProjectManifest::
                currentSchemaVersion});
    const QJsonObject jsonTransport =
        result.value(
                  QStringLiteral(
                      "json_transport"))
            .toObject();
    QCOMPARE(
        jsonTransport
            .value(
                QStringLiteral(
                    "encoding"))
            .toString(),
        QStringLiteral("utf-8"));
    QCOMPARE(
        jsonTransport
            .value(
                QStringLiteral(
                    "response_stream"))
            .toString(),
        QStringLiteral("stdout"));
    QCOMPARE(
        jsonTransport
            .value(
                QStringLiteral(
                    "framing"))
            .toString(),
        QStringLiteral(
            "single_compact_json_object_lf"));
    QCOMPARE(
        jsonTransport
            .value(
                QStringLiteral(
                    "diagnostics_location"))
            .toString(),
        QStringLiteral("diagnostics"));
    QCOMPARE(
        jsonTransport
            .value(
                QStringLiteral(
                    "stdin_patch_token"))
            .toString(),
        QStringLiteral("-"));
    const QJsonObject navigationContract =
        result.value(
                  QStringLiteral(
                      "workbench_navigation_contract"))
            .toObject();
    QCOMPARE(
        navigationContract
            .value(
                QStringLiteral(
                    "member"))
            .toString(),
        QStringLiteral(
            "workbench_navigation"));
    QVERIFY(
        !navigationContract
             .value(
                 QStringLiteral(
                     "launches_process"))
             .toBool());
    QVERIFY(
        std::ranges::any_of(
            navigationContract
                .value(
                    QStringLiteral(
                        "present_on"))
                .toArray(),
            [](const QJsonValue& value) {
                return value.toString()
                    .startsWith(
                        QStringLiteral(
                            "diff change"));
            }));
    QCOMPARE(
        navigationContract
            .value(
                QStringLiteral(
                    "protocol_version"))
            .toInt(),
        1);
    QCOMPARE(
        navigationContract
            .value(
                QStringLiteral(
                    "fields"))
            .toArray(),
        (QJsonArray{
            QStringLiteral(
                "protocol_version"),
            QStringLiteral("project"),
            QStringLiteral(
                "stable_id"),
            QStringLiteral("kind"),
            QStringLiteral("path"),
            QStringLiteral(
                "arguments")}));
    QCOMPARE(
        navigationContract
            .value(
                QStringLiteral(
                    "argument_order"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            QStringLiteral(
                "<absolute-project-path>"),
            QStringLiteral("--select"),
            QStringLiteral("<stable-id>")}));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "stable_id_required"))
            .toBool());
    const QJsonArray commands =
        result.value(
                  QStringLiteral(
                      "commands"))
            .toArray();
    QVERIFY(commands.size() >= 7);
    const auto commandByName =
        [&commands](
            const QString& name) {
            const auto found =
                std::ranges::find_if(
                    commands,
                    [&name](
                        const QJsonValue&
                            value) {
                        return value
                                   .toObject()
                                   .value(
                                       QStringLiteral(
                                           "name"))
                                   .toString() ==
                               name;
                    });
            return found ==
                           commands.end()
                       ? QJsonObject{}
                       : (*found).toObject();
        };
    for (const QJsonValue& value :
         commands) {
        const QJsonObject command =
            value.toObject();
        QVERIFY(
            command.contains(
                QStringLiteral(
                    "project_access")));
        QVERIFY(
            command.contains(
                QStringLiteral(
                    "file_write_behavior")));
    }
    QCOMPARE(
        commandByName(
            QStringLiteral("version"))
            .value(
                QStringLiteral(
                    "project_access"))
            .toString(),
        QStringLiteral("none"));
    QCOMPARE(
        commandByName(
            QStringLiteral("summary"))
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("never"));
    QCOMPARE(
        commandByName(
            QStringLiteral("diff"))
            .value(
                QStringLiteral(
                    "project_access"))
            .toString(),
        QStringLiteral("read"));
    QCOMPARE(
        commandByName(
            QStringLiteral("diff"))
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("never"));
    QCOMPARE(
        commandByName(
            QStringLiteral("get-many"))
            .value(
                QStringLiteral(
                    "project_access"))
            .toString(),
        QStringLiteral("read"));
    QCOMPARE(
        commandByName(
            QStringLiteral("get-many"))
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("never"));
    QCOMPARE(
        commandByName(
            QStringLiteral("init"))
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("always"));
    for (const QString& commandName :
         {QStringLiteral("generate"),
          QStringLiteral("apply")}) {
        const QJsonObject command =
            commandByName(commandName);
        QCOMPARE(
            command.value(
                       QStringLiteral(
                           "project_access"))
                .toString(),
            QStringLiteral("write"));
        QCOMPARE(
            command.value(
                       QStringLiteral(
                           "file_write_behavior"))
                .toString(),
            QStringLiteral(
                "unless_dry_run"));
    }
    const QJsonObject
        commandDiscoveryContract =
            result.value(
                      QStringLiteral(
                          "command_discovery_contract"))
                .toObject();
    QVERIFY(
        commandDiscoveryContract
            .value(
                QStringLiteral(
                    "project_access"))
            .toObject()
            .contains(
                QStringLiteral("read")));
    QVERIFY(
        commandDiscoveryContract
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toObject()
            .contains(
                QStringLiteral(
                    "unless_dry_run")));
    QVERIFY(
        commandDiscoveryContract
            .value(
                QStringLiteral(
                    "argument_schema"))
            .toObject()
            .contains(
                QStringLiteral(
                    "positionals")));
    const QJsonArray globalOptions =
        result.value(
                  QStringLiteral(
                      "global_options"))
            .toArray();
    QCOMPARE(globalOptions.size(), 3);
    const auto globalOptionByName =
        [&globalOptions](
            const QString& name) {
            const auto found =
                std::ranges::find_if(
                    globalOptions,
                    [&name](
                        const QJsonValue& value) {
                        return value
                                   .toObject()
                                   .value(
                                       QStringLiteral(
                                           "name"))
                                   .toString() ==
                               name;
                    });
            return found ==
                           globalOptions.end()
                       ? QJsonObject{}
                       : found->toObject();
        };
    const QJsonObject jsonOption =
        globalOptionByName(
            QStringLiteral("--json"));
    QCOMPARE(
        jsonOption
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("--json"));
    QCOMPARE(
        jsonOption
            .value(
                QStringLiteral("position"))
            .toString(),
        QStringLiteral("any"));
    QVERIFY(
        jsonOption
            .value(
                QStringLiteral(
                    "repeatable"))
            .toBool());
    const QJsonObject helpOption =
        globalOptionByName(
            QStringLiteral("--help"));
    QVERIFY(
        helpOption
            .value(
                QStringLiteral("aliases"))
            .toArray()
            .contains(
                QStringLiteral("-h")));
    QVERIFY(
        helpOption
            .value(
                QStringLiteral("effect"))
            .toString()
            .contains(
                QStringLiteral(
                    "capability")));
    const QJsonObject terminator =
        globalOptionByName(
            QStringLiteral("--"));
    QCOMPARE(
        terminator
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("terminator"));
    QVERIFY(
        terminator
            .value(
                QStringLiteral("effect"))
            .toString()
            .contains(
                QStringLiteral(
                    "positional")));
    QVERIFY(
        commandByName(
            QStringLiteral("version"))
            .value(
                QStringLiteral("aliases"))
            .toArray()
            .contains(
                QStringLiteral(
                    "--version")));
    const QJsonObject
        commandArgumentSchemas =
            result.value(
                      QStringLiteral(
                          "command_argument_schemas"))
                .toObject();
    QCOMPARE(
        commandArgumentSchemas.size(),
        commands.size());
    for (const QJsonValue& value :
         commands) {
        const QString name =
            value.toObject()
                .value(
                    QStringLiteral("name"))
                .toString();
        QVERIFY2(
            commandArgumentSchemas
                .contains(name),
            qPrintable(
                QStringLiteral(
                    "Missing argument schema for %1")
                    .arg(name)));
        const QJsonObject arguments =
            commandArgumentSchemas
                .value(name)
                .toObject();
        QVERIFY(
            arguments.value(
                         QStringLiteral(
                             "positionals"))
                .isArray());
        QVERIFY(
            arguments.value(
                         QStringLiteral(
                             "options"))
                .isArray());
    }
    const QJsonObject initArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("init"))
            .toObject();
    QCOMPARE(
        initArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("project"));
    const auto optionByName =
        [](const QJsonArray& options,
           const QString& name) {
            const auto found =
                std::ranges::find_if(
                    options,
                    [&name](
                        const QJsonValue& value) {
                        return value
                                   .toObject()
                                   .value(
                                       QStringLiteral(
                                           "name"))
                                   .toString() ==
                               name;
                    });
            return found == options.end()
                       ? QJsonObject{}
                       : found->toObject();
        };
    QCOMPARE(
        optionByName(
            initArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral(
                "--no-generate"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    const QJsonObject initTargetOption =
        optionByName(
            initArguments
                .value(
                    QStringLiteral("options"))
                .toArray(),
            QStringLiteral("--target"));
    QVERIFY(
        initTargetOption
            .value(
                QStringLiteral("repeatable"))
            .toBool());
    QCOMPARE(
        initTargetOption
            .value(
                QStringLiteral(
                    "allowed_values"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("xlsx"),
            QStringLiteral("c-header"),
            QStringLiteral("markdown")}));
    QCOMPARE(
        optionByName(
            initArguments
                .value(
                    QStringLiteral("options"))
                .toArray(),
            QStringLiteral("--output-dir"))
            .value(
                QStringLiteral("value_type"))
            .toString(),
        QStringLiteral("relative_path"));
    for (const QString& commandName :
         {QStringLiteral("status"),
          QStringLiteral("generate")}) {
        const QJsonObject targetOption =
            optionByName(
                commandArgumentSchemas
                    .value(commandName)
                    .toObject()
                    .value(
                        QStringLiteral(
                            "options"))
                    .toArray(),
                QStringLiteral("--target"));
        QVERIFY2(
            targetOption
                .value(
                    QStringLiteral(
                        "repeatable"))
                .toBool(),
            qPrintable(commandName));
    }
    const QJsonObject findArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("find"))
            .toObject();
    QCOMPARE(
        findArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .size(),
        2);
    const QJsonObject kindOption =
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--kind"));
    QVERIFY(
        kindOption
            .value(
                QStringLiteral(
                    "allowed_values"))
            .toArray()
            .contains(
                QStringLiteral(
                    "register")));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--limit"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "positive_integer"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--all"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--offset"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "non_negative_integer"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--tag"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("string"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral(
                "--recursive"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral(
                "--require-one"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--exact"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    const QJsonObject listArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("list"))
            .toObject();
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--limit"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "positive_integer"));
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--all"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--offset"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "non_negative_integer"));
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--tag"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("string"));
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral(
                "--recursive"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            listArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--expect"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    const QJsonObject getManyArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("get-many"))
            .toObject();
    QCOMPARE(
        getManyArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .size(),
        2);
    const QJsonObject repeatedId =
        getManyArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(1)
            .toObject();
    QCOMPARE(
        repeatedId
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("stable-id"));
    QVERIFY(
        repeatedId
            .value(
                QStringLiteral(
                    "required"))
            .toBool());
    QVERIFY(
        repeatedId
            .value(
                QStringLiteral(
                    "repeatable"))
            .toBool());
    QCOMPARE(
        optionByName(
            getManyArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--expect"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    QCOMPARE(
        optionByName(
            findArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--expect"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    const QJsonObject statusArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("status"))
            .toObject();
    QCOMPARE(
        optionByName(
            statusArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral(
                "--require-current"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    const QJsonObject getArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("get"))
            .toObject();
    QVERIFY(
        !getArguments
             .value(
                 QStringLiteral(
                     "positionals"))
             .toArray()
             .at(1)
             .toObject()
             .value(
                 QStringLiteral(
                     "required"))
             .toBool());
    QCOMPARE(
        optionByName(
            getArguments
                .value(
                    QStringLiteral(
                        "options"))
                .toArray(),
            QStringLiteral("--expect"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    const QJsonObject diffArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("diff"))
            .toObject();
    QCOMPARE(
        diffArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .size(),
        2);
    QCOMPARE(
        diffArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "before_project"));
    QCOMPARE(
        diffArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(1)
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "after_project"));
    const QJsonArray diffOptions =
        diffArguments
            .value(
                QStringLiteral("options"))
            .toArray();
    QCOMPARE(diffOptions.size(), 6);
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral("--kind"))
            .value(
                QStringLiteral(
                    "allowed_values"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("workspace"),
            QStringLiteral("page"),
            QStringLiteral("block"),
            QStringLiteral("register"),
            QStringLiteral("field"),
            QStringLiteral("enum"),
            QStringLiteral("all")}));
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral("--offset"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "non_negative_integer"));
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral("--limit"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "positive_integer"));
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral(
                "--expect-before"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral(
                "--require-equal"))
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("flag"));
    QCOMPARE(
        optionByName(
            diffOptions,
            QStringLiteral(
                "--expect-after"))
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral("revision"));
    const QJsonObject diffContract =
        result.value(
                  QStringLiteral(
                      "diff_contract"))
            .toObject();
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "change_kinds"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("added"),
            QStringLiteral("removed"),
            QStringLiteral("modified")}));
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "stable_id_matching"))
            .toBool());
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "rechecks_both_revisions"))
            .toBool());
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "single_project_envelope_members_omitted"))
            .toBool());
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "result_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "writes_performed")));
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "result_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "kind_filter")));
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "strict_option"))
            .toString(),
        QStringLiteral(
            "--require-equal"));
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "strict_exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                differencesFound));
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "strict_error_code"))
            .toString(),
        QStringLiteral("RMC6001"));
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "change_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "property_changes")));
    QVERIFY(
        diffContract.value(
                        QStringLiteral(
                            "change_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "after_navigation")));
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "state_members"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("kind"),
            QStringLiteral("id"),
            QStringLiteral("parent_id"),
            QStringLiteral("order"),
            QStringLiteral("properties")}));
    QCOMPARE(
        diffContract.value(
                        QStringLiteral(
                            "property_change_members"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("property"),
            QStringLiteral(
                "before_present"),
            QStringLiteral("before"),
            QStringLiteral(
                "after_present"),
            QStringLiteral("after")}));
    const QJsonObject applyArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("apply"))
            .toObject();
    const QJsonArray expectedApplyExclusion{
        QStringLiteral("--expect"),
        QStringLiteral("--force")};
    QCOMPARE(
        applyArguments
            .value(
                QStringLiteral(
                    "mutually_exclusive_options"))
            .toArray()
            .at(0)
            .toArray(),
        expectedApplyExclusion);
    QCOMPARE(
        applyArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(1)
            .toObject()
            .value(
                QStringLiteral(
                    "value_type"))
            .toString(),
        QStringLiteral(
            "path_or_stdin"));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "write_requires_revision"))
            .toBool());
    const QJsonObject noCommandContract =
        result.value(
                  QStringLiteral(
                      "no_command_contract"))
            .toObject();
    QCOMPARE(
        noCommandContract
            .value(
                QStringLiteral(
                    "exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        noCommandContract
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("usage_error"));
    QCOMPARE(
        noCommandContract
            .value(
                QStringLiteral(
                    "explicit_help_exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    const QJsonObject
        commandHelpContract =
            result.value(
                      QStringLiteral(
                          "command_help_contract"))
                .toObject();
    QCOMPARE(
        commandHelpContract
            .value(
                QStringLiteral(
                    "project_access"))
            .toString(),
        QStringLiteral("none"));
    QCOMPARE(
        commandHelpContract
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("never"));
    QVERIFY(
        commandHelpContract
            .value(
                QStringLiteral(
                    "global_help_remains_complete"))
            .toBool());
    QVERIFY(
        commandHelpContract
            .value(
                QStringLiteral(
                    "help_option_with_command"))
            .toString()
            .contains(
                QStringLiteral(
                    "without opening")));
    QVERIFY(
        commandHelpContract
            .value(
                QStringLiteral(
                    "focused_result_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "argument_schema")));
    const QJsonObject helpArguments =
        commandArgumentSchemas
            .value(
                QStringLiteral("help"))
            .toObject();
    QCOMPARE(
        helpArguments
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "required"))
            .toBool(),
        false);
    const QJsonObject
        unknownCommandContract =
            result.value(
                      QStringLiteral(
                          "unknown_command_contract"))
                .toObject();
    QCOMPARE(
        unknownCommandContract
            .value(
                QStringLiteral(
                    "maximum_edit_distance"))
            .toInt(),
        2);
    QCOMPARE(
        unknownCommandContract
            .value(
                QStringLiteral(
                    "suggestion_member"))
            .toString(),
        QStringLiteral(
            "result.suggested_command"));
    const QJsonObject
        unknownOptionContract =
            result.value(
                      QStringLiteral(
                          "unknown_option_contract"))
                .toObject();
    QCOMPARE(
        unknownOptionContract
            .value(
                QStringLiteral(
                    "maximum_edit_distance"))
            .toInt(),
        2);
    QVERIFY(
        unknownOptionContract
            .value(
                QStringLiteral(
                    "allowed_option_source"))
            .toString()
            .contains(
                QStringLiteral(
                    "command_argument_schemas")));
    QCOMPARE(
        unknownOptionContract
            .value(
                QStringLiteral(
                    "suggestion_member"))
            .toString(),
        QStringLiteral(
            "result.suggested_option"));
    const QJsonObject
        responseEnvelopeContract =
            result.value(
                      QStringLiteral(
                          "response_envelope_contract"))
                .toObject();
    QVERIFY(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "always_present"))
            .toArray()
            .contains(
                QStringLiteral(
                    "exit_code")));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "exit_status_by_code"))
            .toObject()
            .value(
                QStringLiteral("3"))
            .toString(),
        QStringLiteral(
            "revision_conflict"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "exit_status_by_code"))
            .toObject()
            .value(
                QStringLiteral("5"))
            .toString(),
        QStringLiteral(
            "outputs_out_of_date"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "exit_status_by_code"))
            .toObject()
            .value(
                QStringLiteral("6"))
            .toString(),
        QStringLiteral(
            "differences_found"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "exit_status_by_code"))
            .toObject()
            .value(QStringLiteral("7"))
            .toString(),
        QStringLiteral("input_error"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "exit_status_by_code"))
            .toObject()
            .value(QStringLiteral("8"))
            .toString(),
        QStringLiteral(
            "generation_error"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "usage_recovery_member"))
            .toString(),
        QStringLiteral("usage"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "usage_recovery_present_when"))
            .toString(),
        QStringLiteral(
            "exit_status is usage_error"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "generic_error_codes"))
            .toObject()
            .value(
                QStringLiteral(
                    "usage_error"))
            .toString(),
        QStringLiteral("RMC1000"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "generic_error_codes"))
            .toObject()
            .value(
                QStringLiteral(
                    "outputs_out_of_date"))
            .toString(),
        QStringLiteral("RMC5000"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "generic_error_codes"))
            .toObject()
            .value(
                QStringLiteral(
                    "differences_found"))
            .toString(),
        QStringLiteral("RMC6000"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "generic_error_codes"))
            .toObject()
            .value(
                QStringLiteral(
                    "input_error"))
            .toString(),
        QStringLiteral("RMC7000"));
    QCOMPARE(
        responseEnvelopeContract
            .value(
                QStringLiteral(
                    "generic_error_codes"))
            .toObject()
            .value(
                QStringLiteral(
                    "generation_error"))
            .toString(),
        QStringLiteral("RMC8000"));
    const QJsonObject
        operationFailureContract =
            result.value(
                      QStringLiteral(
                          "apply_operation_failure_contract"))
                .toObject();
    QCOMPARE(
        operationFailureContract.value(
                                    QStringLiteral(
                                        "result_member"))
            .toString(),
        QStringLiteral("failure"));
    QCOMPARE(
        operationFailureContract.value(
                                    QStringLiteral(
                                        "indexing"))
            .toString(),
        QStringLiteral("zero-based"));
    QVERIFY(
        operationFailureContract.value(
                                    QStringLiteral(
                                        "members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "json_pointer")));
    QVERIFY(
        operationFailureContract.value(
                                    QStringLiteral(
                                        "completed_operations_rolled_back"))
            .toBool());
    const QJsonObject
        prewriteFailureContract =
            result.value(
                      QStringLiteral(
                          "apply_prewrite_failure_contract"))
                .toObject();
    QVERIFY(
        prewriteFailureContract.value(
                                    QStringLiteral(
                                        "stages"))
            .toArray()
            .contains(
                QStringLiteral(
                    "validation")));
    QVERIFY(
        prewriteFailureContract.value(
                                    QStringLiteral(
                                        "stages"))
            .toArray()
            .contains(
                QStringLiteral(
                    "generation")));
    QCOMPARE(
        prewriteFailureContract.value(
                                    QStringLiteral(
                                        "diagnostic_index_target"))
            .toString(),
        QStringLiteral(
            "top-level diagnostics array"));
    QVERIFY(
        prewriteFailureContract.value(
                                    QStringLiteral(
                                        "operation_index"))
            .isNull());
    const QJsonObject
        operationReferenceContract =
            result.value(
                      QStringLiteral(
                          "operation_reference_contract"))
                .toObject();
    QCOMPARE(
        operationReferenceContract.value(
                                      QStringLiteral(
                                          "definition_member"))
            .toString(),
        QStringLiteral("ref"));
    QVERIFY(
        operationReferenceContract.value(
                                      QStringLiteral(
                                          "reference_forms"))
            .toArray()
            .contains(
                QStringLiteral(
                    "{'ref': <earlier operation ref>}")));
    QVERIFY(
        operationReferenceContract.value(
                                      QStringLiteral(
                                          "case_sensitive"))
            .toBool());
    QVERIFY(
        operationReferenceContract.value(
                                      QStringLiteral(
                                          "unique"))
            .toBool());
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "partial_success_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "recovery_command"))
            .toString(),
        QStringLiteral("generate"));
    const QJsonObject partialSuccessContract =
        result.value(
                  QStringLiteral(
                      "partial_success_contract"))
            .toObject();
    QVERIFY(
        partialSuccessContract
            .value(
                QStringLiteral(
                    "condition"))
            .toString()
            .contains(
                QStringLiteral("init")));
    QVERIFY(
        partialSuccessContract
            .value(
                QStringLiteral(
                    "recovery_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "arguments")));
    QVERIFY(
        partialSuccessContract
            .value(
                QStringLiteral(
                    "revision_guard"))
            .toString()
            .contains(
                QStringLiteral(
                    "--expect")));
    const QJsonObject generationRevisionGuard =
        result.value(
                  QStringLiteral(
                      "generation_revision_guard"))
            .toObject();
    QVERIFY(
        generationRevisionGuard
            .value(
                QStringLiteral(
                    "automatic_recheck"))
            .toString()
            .contains(
                QStringLiteral(
                    "before and after")));
    QCOMPARE(
        generationRevisionGuard
            .value(
                QStringLiteral(
                    "recovery_member"))
            .toString(),
        QStringLiteral(
            "result.recovery"));
    QVERIFY(
        generationRevisionGuard
            .value(
                QStringLiteral(
                    "recovery_revision"))
            .toString()
            .contains(
                QStringLiteral("--expect")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject()
            .value(
                QStringLiteral("states"))
            .toArray()
            .contains(
                QStringLiteral(
                    "writable")));
    const QJsonObject outputStatusContract =
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject();
    for (const QString& member :
         {QStringLiteral("sha256"),
          QStringLiteral("state"),
          QStringLiteral("synchronized")}) {
        QVERIFY2(
            outputStatusContract
                .value(
                    QStringLiteral(
                        "artifact_members"))
                .toArray()
                .contains(member),
            qPrintable(member));
    }
    for (const QString& member :
         {QStringLiteral("written"),
          QStringLiteral("skipped"),
          QStringLiteral("failed")}) {
        QVERIFY2(
            outputStatusContract
                .value(
                    QStringLiteral(
                        "write_report_members"))
                .toArray()
                .contains(member),
            qPrintable(member));
    }
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "strict_option"))
            .toString(),
        QStringLiteral(
            "--require-current"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "strict_exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                outputsOutOfDate));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "recovery_member"))
            .toString(),
        QStringLiteral(
            "result.recovery"));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "output_status_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "recovery_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "expected_revision")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "find_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "searches"))
            .toArray()
            .contains(
                QStringLiteral("tags")));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "find_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "limit_option"))
            .toString(),
        QStringLiteral(
            "--limit <positive integer>"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "find_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "offset_option"))
            .toString(),
        QStringLiteral(
            "--offset <non-negative integer>"));
    const QJsonObject findContract =
        result.value(
                  QStringLiteral(
                      "find_contract"))
            .toObject();
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "exact_option"))
            .toString(),
        QStringLiteral("--exact"));
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "exact_match_ranks"))
            .toArray(),
        (QJsonArray{0, 1}));
    QVERIFY(
        findContract
            .value(
                QStringLiteral(
                    "exact_excludes"))
            .toArray()
            .contains(
                QStringLiteral(
                    "substring")));
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "require_one_option"))
            .toString(),
        QStringLiteral("--require-one"));
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "require_one_conflicts"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--offset"),
            QStringLiteral("--limit"),
            QStringLiteral("--all")}));
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "require_one_zero_error_code"))
            .toString(),
        QStringLiteral("RMC2001"));
    QCOMPARE(
        findContract
            .value(
                QStringLiteral(
                    "require_one_many_error_code"))
            .toString(),
        QStringLiteral("RMC2002"));
    QVERIFY(
        findContract
            .value(
                QStringLiteral(
                    "require_one_many_preserves_candidates"))
            .toBool());
    const QJsonObject paginationContract =
        result.value(
                  QStringLiteral(
                      "pagination_contract"))
            .toObject();
    QVERIFY(
        paginationContract
            .value(
                QStringLiteral(
                    "commands"))
            .toArray()
            .contains(
                QStringLiteral("list")));
    QVERIFY(
        paginationContract
            .value(
                QStringLiteral(
                    "commands"))
            .toArray()
            .contains(
                QStringLiteral("diff")));
    QCOMPARE(
        paginationContract
            .value(
                QStringLiteral(
                    "continuation_member"))
            .toString(),
        QStringLiteral(
            "result_metadata.next_offset"));
    const QJsonObject defaultLimits =
        paginationContract
            .value(
                QStringLiteral(
                    "default_limit"))
            .toObject();
    QCOMPARE(
        defaultLimits
            .value(QStringLiteral("list"))
            .toInt(),
        100);
    QCOMPARE(
        defaultLimits
            .value(QStringLiteral("find"))
            .toInt(),
        100);
    QVERIFY(
        defaultLimits
            .value(QStringLiteral("diff"))
            .isNull());
    QCOMPARE(
        paginationContract
            .value(
                QStringLiteral(
                    "unbounded_option"))
            .toString(),
        QStringLiteral("--all"));
    QCOMPARE(
        paginationContract
            .value(
                QStringLiteral(
                    "revision_guard_option"))
            .toString(),
        QStringLiteral(
            "--expect <sha256:...>"));
    QCOMPARE(
        paginationContract
            .value(
                QStringLiteral(
                    "diff_revision_guard_options"))
            .toArray(),
        (QJsonArray{
            QStringLiteral(
                "--expect-before <sha256:...>"),
            QStringLiteral(
                "--expect-after <sha256:...>")}));
    QCOMPARE(
        paginationContract
            .value(
                QStringLiteral(
                    "revision_conflict_exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QVERIFY(
        paginationContract
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toArray()
            .contains(
                QStringLiteral(
                    "has_more")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "find_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "match_result_members"))
            .toArray()
            .contains(
                QStringLiteral(
                    "match_field")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "get_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "omitted_stable_id"))
            .toString()
            .contains(
                QStringLiteral(
                    "root Workspace")));
    const QJsonObject getManyContract =
        result.value(
                  QStringLiteral(
                      "get_many_contract"))
            .toObject();
    QCOMPARE(
        getManyContract
            .value(
                QStringLiteral(
                    "result_members"))
            .toArray(),
        (QJsonArray{
            QStringLiteral(
                "requested_count"),
            QStringLiteral("found_count"),
            QStringLiteral("missing_count"),
            QStringLiteral(
                "duplicate_count"),
            QStringLiteral("items")}));
    QVERIFY(
        getManyContract
            .value(
                QStringLiteral(
                    "missing_behavior"))
            .toString()
            .contains(
                QStringLiteral("RMC2001")));
    QVERIFY(
        navigationContract
            .value(
                QStringLiteral("present_on"))
            .toArray()
            .contains(
                QStringLiteral(
                    "get-many found item object")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "query_filter_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "missing_parent"))
            .toString()
            .contains(
                QStringLiteral(
                    "project error")));
    const QJsonObject queryFilterContract =
        result.value(
                  QStringLiteral(
                      "query_filter_contract"))
            .toObject();
    QCOMPARE(
        queryFilterContract
            .value(
                QStringLiteral(
                    "recursive_option"))
            .toString(),
        QStringLiteral("--recursive"));
    QVERIFY(
        queryFilterContract
            .value(
                QStringLiteral(
                    "recursive_requires_parent"))
            .toBool());
    QVERIFY(
        queryFilterContract
            .value(
                QStringLiteral(
                    "recursive_scope"))
            .toString()
            .contains(
                QStringLiteral(
                    "all descendants")));
    const QJsonObject tagFilterContract =
        result.value(
                  QStringLiteral(
                      "tag_filter_contract"))
            .toObject();
    QCOMPARE(
        tagFilterContract
            .value(
                QStringLiteral("matching"))
            .toString(),
        QStringLiteral(
            "exact and case-sensitive"));
    QCOMPARE(
        tagFilterContract
            .value(
                QStringLiteral(
                    "catalog_member"))
            .toString(),
        QStringLiteral(
            "summary.result.tags"));
    QCOMPARE(
        tagFilterContract
            .value(
                QStringLiteral(
                    "catalog_item_members"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("name"),
            QStringLiteral(
                "register_count")}));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "invalid_project_repair_contract"))
            .toObject()
            .value(
                QStringLiteral(
                    "apply_can_start_from_validation_errors"))
            .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "writable_properties"))
            .toObject()
            .value(
                QStringLiteral(
                    "register"))
            .toArray()
            .contains(
                QStringLiteral(
                    "description")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "writable_properties"))
            .toObject()
            .value(
                QStringLiteral("field"))
            .toArray()
            .contains(
                QStringLiteral("lsb")));
    QVERIFY(
        !result.value(
                   QStringLiteral(
                       "writable_properties"))
             .toObject()
             .value(
                 QStringLiteral("field"))
             .toArray()
             .contains(
                 QStringLiteral("reset")));
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "writable_properties"))
            .toObject()
            .value(
                QStringLiteral("register"))
            .toArray()
            .contains(
                QStringLiteral("reset")));
    const QJsonObject operationSchemas =
        result.value(
                  QStringLiteral(
                      "operation_schemas"))
            .toObject();
    QVERIFY(
        operationSchemas.contains(
            QStringLiteral("set")));
    const QJsonObject setSchema =
        operationSchemas.value(
                            QStringLiteral(
                                "set"))
            .toObject();
    QVERIFY(
        setSchema.value(
                     QStringLiteral(
                         "id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QVERIFY(
        operationSchemas.contains(
            QStringLiteral("add")));
    const QJsonObject addSchema =
        operationSchemas.value(
                            QStringLiteral(
                                "add"))
            .toObject();
    QVERIFY(
        addSchema.value(
                     QStringLiteral(
                         "id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "exact string 'auto'")));
    QVERIFY(
        addSchema.value(
                     QStringLiteral(
                         "parent_id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QVERIFY(
        operationSchemas.contains(
            QStringLiteral("copy")));
    const QJsonObject copySchema =
        operationSchemas.value(
                            QStringLiteral(
                                "copy"))
            .toObject();
    QVERIFY(
        copySchema.value(
                      QStringLiteral(
                          "optional_members"))
            .toObject()
            .contains(
                QStringLiteral(
                    "unique_name")));
    QVERIFY(
        copySchema.value(
                      QStringLiteral(
                          "parent_id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QVERIFY(
        copySchema.value(
                      QStringLiteral(
                          "id_contract"))
            .toString()
            .startsWith(
                QStringLiteral(
                    "source stable ID")));
    QVERIFY(
        operationSchemas.contains(
            QStringLiteral("move")));
    const QJsonObject moveSchema =
        operationSchemas.value(
                            QStringLiteral(
                                "move"))
            .toObject();
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "kinds"))
            .toArray()
            .contains(
                QStringLiteral("page")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "optional_members"))
            .toObject()
            .contains(
                QStringLiteral(
                    "before_id")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "optional_members"))
            .toObject()
            .contains(
                QStringLiteral(
                    "placement")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "parent_id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "optional_members"))
            .toObject()
            .value(
                QStringLiteral(
                    "before_id"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier-operation")));
    QVERIFY(
        moveSchema.value(
                      QStringLiteral(
                          "optional_members"))
            .toObject()
            .value(
                QStringLiteral(
                    "before_id"))
            .toString()
            .contains(
                QStringLiteral(
                    "same-parent")));
    QVERIFY(
        operationSchemas.contains(
            QStringLiteral("remove")));
    QVERIFY(
        operationSchemas.value(
                            QStringLiteral(
                                "remove"))
            .toObject()
            .value(
                QStringLiteral(
                    "id_contract"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier zero-based")));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "register_offset_auto"))
            .toString(),
        QStringLiteral(
            "the exact string 'auto' selects the next aligned free range for add or copy register"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "field_lsb_auto"))
            .toString(),
        QStringLiteral(
            "the exact string 'auto' selects the lowest contiguous free bit range for add or copy field; width is accepted and msb is not"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "block_base_auto"))
            .toString(),
        QStringLiteral(
            "the exact string 'auto' selects the lowest free Page-relative range for add or copy block and requires a positive explicit or copied size"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "move_placement_auto"))
            .toString(),
        QStringLiteral(
            "move placement accepts the exact string 'auto' for block, register, or field; omission preserves the current position"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "register_before_id_reorder"))
            .toString(),
        QStringLiteral(
            "a movable register may use before_id within its existing block; movable registers exchange sorted offset slots while fixed register offsets remain unchanged"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "copy_new_id_auto"))
            .toString(),
        QStringLiteral(
            "copy new_id accepts the exact string 'auto' to select a deterministic free root and descendant ID family for the current revision"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "add_id_auto"))
            .toString(),
        QStringLiteral(
            "add id accepts the exact string 'auto' to select a deterministic free stable ID derived from kind and name for the current revision"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "operation_parent_reference"))
            .toString(),
        QStringLiteral(
            "add, copy, and move parent_id accept {'operation': N} to use the object ID returned by an earlier zero-based operation in the same patch"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "operation_object_reference"))
            .toString(),
        QStringLiteral(
            "set, copy, move, and remove id plus move before_id accept {'operation': N} to use the object ID returned by an earlier zero-based operation in the same patch"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "operation_named_reference"))
            .toString(),
        QStringLiteral(
            "every operation may define a unique case-sensitive ref; object reference members accept {'ref': <name>} for an earlier operation"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "value_contracts"))
            .toObject()
            .value(
                QStringLiteral(
                    "copy_unique_name"))
            .toString(),
        QStringLiteral(
            "copy unique_name true selects the first '<source> Copy' sibling name, adding a numeric suffix when needed"));
}

void CliTests::keepsHelpAndVersionMachineReadable()
{
    const QString expectedCliVersion =
        QString::fromLatin1(
            regmap::cli::cliVersion.data(),
            static_cast<qsizetype>(
                regmap::cli::cliVersion.size()));
    const Invocation schemaInvocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("schema")});
    QCOMPARE(schemaInvocation.exitCode, 0);
    const QJsonObject schemaResult =
        json(schemaInvocation)
            .value(
                QStringLiteral("result"))
            .toObject();

    for (const QString& versionToken :
         {QStringLiteral("version"),
          QStringLiteral("--version")}) {
        const Invocation invocation =
            invoke(
                {QStringLiteral("--json"),
                 versionToken});
        QCOMPARE(
            invocation.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::success));
        QVERIFY(
            invocation.standardError.isEmpty());
        const QJsonObject root =
            json(invocation);
        QCOMPARE(
            root.value(QStringLiteral("command")).toString(),
            QStringLiteral("version"));
        QVERIFY(
            root.value(QStringLiteral("ok")).toBool());
        const QJsonObject result =
            root.value(QStringLiteral("result")).toObject();
        QCOMPARE(
            result.value(QStringLiteral("cli_version")).toString(),
            expectedCliVersion);
        QCOMPARE(
            result.value(QStringLiteral("api_version")).toInt(),
            regmap::cli::apiVersion);
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "capabilities_command"))
                .toString(),
            QStringLiteral("schema"));
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "help_option"))
                .toString(),
            QStringLiteral("--help"));
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "help_command"))
                .toString(),
            QStringLiteral("help"));
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "project_schema"))
                .toObject()
                .value(
                    QStringLiteral(
                        "current_version"))
                .toInt(),
            regmap::ProjectManifest::
                currentSchemaVersion);
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "json_transport"))
                .toObject(),
            schemaResult
                .value(
                    QStringLiteral(
                        "json_transport"))
                .toObject());
    }

    for (const QStringList& arguments :
         {QStringList{QStringLiteral("--json"),
                      QStringLiteral("--help")}}) {
        const Invocation invocation =
            invoke(arguments);
        QCOMPARE(
            invocation.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::success));
        QVERIFY(
            invocation.standardError.isEmpty());
        const QJsonObject root =
            json(invocation);
        QCOMPARE(
            root.value(QStringLiteral("command")).toString(),
            QStringLiteral("help"));
        QVERIFY(
            root.value(QStringLiteral("ok")).toBool());
        const QJsonObject result =
            root.value(QStringLiteral("result")).toObject();
        QCOMPARE(
            result.value(QStringLiteral("cli_version")).toString(),
            expectedCliVersion);
        QCOMPARE(
            result.value(QStringLiteral("api_version")).toInt(),
            regmap::cli::apiVersion);
        QVERIFY(
            result.value(
                      QStringLiteral(
                          "project_schema"))
                .toObject()
                .value(
                    QStringLiteral(
                        "supported_versions"))
                .toArray()
                .contains(
                    regmap::ProjectManifest::
                        currentSchemaVersion));
        QVERIFY(
            result.value(QStringLiteral("usage")).toString().contains(
                QStringLiteral("regmapc [--json] apply")));
        const QJsonArray commands =
            result.value(QStringLiteral("commands")).toArray();
        QVERIFY(
            std::ranges::any_of(
                commands,
                [](const QJsonValue& value) {
                    return value.toObject()
                               .value(QStringLiteral("name"))
                               .toString() ==
                        QStringLiteral("version");
                }));
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "global_options")),
            schemaResult.value(
                QStringLiteral(
                    "global_options")));
        QCOMPARE(
            result.value(
                      QStringLiteral(
                          "command_argument_schemas")),
            schemaResult.value(
                QStringLiteral(
                    "command_argument_schemas")));
    }

    const Invocation focusedHelp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("help"),
             QStringLiteral("status")});
    QCOMPARE(
        focusedHelp.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QVERIFY(
        focusedHelp.standardError
            .isEmpty());
    const QJsonObject focusedHelpRoot =
        json(focusedHelp);
    QCOMPARE(
        focusedHelpRoot
            .value(
                QStringLiteral("command"))
            .toString(),
        QStringLiteral("help"));
    QVERIFY(
        focusedHelpRoot
            .value(QStringLiteral("ok"))
            .toBool());
    QVERIFY(
        !focusedHelpRoot.contains(
            QStringLiteral("project")));
    QVERIFY(
        !focusedHelpRoot.contains(
            QStringLiteral("revision")));
    const QJsonObject focusedHelpResult =
        focusedHelpRoot
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        focusedHelpResult
            .value(
                QStringLiteral(
                    "target_command"))
            .toString(),
        QStringLiteral("status"));
    QCOMPARE(
        focusedHelpResult
            .value(
                QStringLiteral(
                    "project_access"))
            .toString(),
        QStringLiteral("read"));
    QCOMPARE(
        focusedHelpResult
            .value(
                QStringLiteral(
                    "file_write_behavior"))
            .toString(),
        QStringLiteral("never"));
    QCOMPARE(
        focusedHelpResult
            .value(
                QStringLiteral(
                    "argument_schema")),
        schemaResult
            .value(
                QStringLiteral(
                    "command_argument_schemas"))
            .toObject()
            .value(
                QStringLiteral("status")));
    QVERIFY(
        focusedHelpResult
            .value(
                QStringLiteral("usage"))
            .toString()
            .contains(
                QStringLiteral(
                    "--require-current")));

    const Invocation optionFocusedHelp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             QStringLiteral(
                 "missing.regmap.yaml"),
             QStringLiteral("--help")});
    QCOMPARE(
        optionFocusedHelp.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QVERIFY(
        optionFocusedHelp.standardError
            .isEmpty());
    const QJsonObject
        optionFocusedHelpRoot =
            json(optionFocusedHelp);
    QCOMPARE(
        optionFocusedHelpRoot
            .value(
                QStringLiteral("command"))
            .toString(),
        QStringLiteral("help"));
    QCOMPARE(
        optionFocusedHelpRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "target_command"))
            .toString(),
        QStringLiteral("status"));
    QVERIFY(
        !optionFocusedHelpRoot.contains(
            QStringLiteral("project")));
    QVERIFY(
        !optionFocusedHelpRoot.contains(
            QStringLiteral("revision")));

    const Invocation textFocusedHelp =
        invoke(
            {QStringLiteral("help"),
             QStringLiteral("generate")});
    QCOMPARE(
        textFocusedHelp.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QVERIFY(
        textFocusedHelp.standardError
            .isEmpty());
    QVERIFY(
        textFocusedHelp.standardOutput
            .contains(
                QStringLiteral(
                    "Usage: regmapc [--json] generate")));
    QVERIFY(
        textFocusedHelp.standardOutput
            .contains(
                QStringLiteral(
                    "Project access: write")));
    QVERIFY(
        textFocusedHelp.standardOutput
            .contains(
                QStringLiteral(
                    "File writes: unless_dry_run")));
    QVERIFY(
        textFocusedHelp.standardOutput
            .contains(
                QStringLiteral("--dry-run")));
    QVERIFY(
        textFocusedHelp.standardOutput
            .contains(
                QStringLiteral("--expect <revision>")));

    const Invocation
        textOptionFocusedHelp =
            invoke(
                {QStringLiteral("generate"),
                 QStringLiteral(
                     "missing.regmap.yaml"),
                 QStringLiteral("--help")});
    QCOMPARE(
        textOptionFocusedHelp.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QVERIFY(
        textOptionFocusedHelp.standardError
            .isEmpty());
    QVERIFY(
        textOptionFocusedHelp.standardOutput
            .contains(
                QStringLiteral(
                    "Usage: regmapc [--json] generate")));
    QVERIFY(
        textOptionFocusedHelp.standardOutput
            .contains(
                QStringLiteral(
                    "File writes: unless_dry_run")));

    const Invocation aliasFocusedHelp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("help"),
             QStringLiteral("--version")});
    QCOMPARE(
        aliasFocusedHelp.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                success));
    QCOMPARE(
        json(aliasFocusedHelp)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "target_command"))
            .toString(),
        QStringLiteral("version"));

    const Invocation helpTypo =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("help"),
             QStringLiteral("validte")});
    QCOMPARE(
        helpTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject helpTypoRoot =
        json(helpTypo);
    QCOMPARE(
        helpTypoRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "suggested_command"))
            .toString(),
        QStringLiteral("validate"));
    QVERIFY(
        helpTypoRoot
            .value(QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "Did you mean 'validate'?")));
    QCOMPARE(
        helpTypoRoot
            .value(QStringLiteral("usage"))
            .toString(),
        QStringLiteral(
            "regmapc [--json] help [command]"));

    const Invocation missingJsonCommand =
        invoke(
            {QStringLiteral("--json")});
    QCOMPARE(
        missingJsonCommand.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        missingJsonCommand.standardError
            .isEmpty());
    const QJsonObject missingJsonRoot =
        json(missingJsonCommand);
    QVERIFY(
        !missingJsonRoot
             .value(QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        missingJsonRoot
            .value(QStringLiteral("command"))
            .toString(),
        QString{});
    QCOMPARE(
        missingJsonRoot
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("usage_error"));
    QCOMPARE(
        missingJsonRoot
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC1000"));
    QCOMPARE(
        missingJsonRoot
            .value(QStringLiteral("error"))
            .toString(),
        QStringLiteral(
            "No command was provided."));
    QVERIFY(
        missingJsonRoot
            .value(QStringLiteral("usage"))
            .toString()
            .contains(
                QStringLiteral(
                    "regmapc [--json] apply")));

    const Invocation missingTextCommand =
        invoke(QStringList{});
    QCOMPARE(
        missingTextCommand.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        missingTextCommand.standardOutput
            .isEmpty());
    QVERIFY(
        missingTextCommand.standardError
            .contains(
                QStringLiteral(
                    "No command was provided.")));
    QVERIFY(
        missingTextCommand.standardError
            .contains(
                QStringLiteral(
                    "regmapc [--json] apply")));

    const Invocation textVersion =
        invoke(
            {QStringLiteral("version")});
    QCOMPARE(
        textVersion.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::success));
    QCOMPARE(
        textVersion.standardOutput,
        QStringLiteral("regmapc %1\n")
            .arg(expectedCliVersion));
    QVERIFY(
        textVersion.standardError.isEmpty());
}

void CliTests::keepsJsonTransportSingleDocument()
{
    const Invocation success =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("version")});
    QCOMPARE(success.exitCode, 0);
    QVERIFY(success.standardError.isEmpty());
    QCOMPARE(
        success.standardOutput.count(
            QLatin1Char('\n')),
        1);
    QVERIFY(
        success.standardOutput.startsWith(
            QLatin1Char('{')));
    QVERIFY(
        success.standardOutput.endsWith(
            QStringLiteral("}\n")));
    QCOMPARE(
        json(success)
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("success"));

    const Invocation failure =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral(
                 "not-a-command")});
    QCOMPARE(
        failure.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(failure.standardError.isEmpty());
    QCOMPARE(
        failure.standardOutput.count(
            QLatin1Char('\n')),
        1);
    QVERIFY(
        failure.standardOutput.startsWith(
            QLatin1Char('{')));
    QVERIFY(
        failure.standardOutput.endsWith(
            QStringLiteral("}\n")));
    const QJsonObject failureRoot =
        json(failure);
    QVERIFY(
        !failureRoot
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        failureRoot
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("usage_error"));
    QVERIFY(
        failureRoot.contains(
            QStringLiteral(
                "error_code")));
    QVERIFY(
        failureRoot
            .value(
                QStringLiteral(
                    "diagnostics"))
            .isArray());
}

void CliTests::inspectsAndValidatesByStableId()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());

    const Invocation summary =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    QCOMPARE(summary.exitCode, 0);
    const QJsonObject summaryRoot =
        json(summary);
    QVERIFY(
        summaryRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    QVERIFY(
        summaryRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString()
            .startsWith(
                QStringLiteral(
                    "sha256:")));
    const QJsonObject counts =
        summaryRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("counts"))
            .toObject();
    QCOMPARE(
        counts.value(
                  QStringLiteral(
                      "registers"))
            .toInt(),
        1);
    QCOMPARE(
        counts.value(
                  QStringLiteral("fields"))
            .toInt(),
        1);
    QCOMPARE(
        counts.value(
                  QStringLiteral("tags"))
            .toInt(),
        1);
    const QJsonArray tags =
        summaryRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("tags"))
            .toArray();
    QCOMPARE(tags.size(), 1);
    QCOMPARE(
        tags.at(0)
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("control"));
    QCOMPARE(
        tags.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "register_count"))
            .toInt(),
        1);

    const Invocation rootGet =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project});
    QCOMPARE(rootGet.exitCode, 0);
    const QJsonObject rootGetRoot =
        json(rootGet);
    const QString openedProject =
        rootGetRoot
            .value(
                QStringLiteral(
                    "project"))
            .toString();
    const QJsonObject workspace =
        rootGetRoot
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        workspace.value(
                     QStringLiteral("kind"))
            .toString(),
        QStringLiteral("workspace"));
    QCOMPARE(
        workspace.value(
                     QStringLiteral("id"))
            .toString(),
        QStringLiteral("workspace-main"));
    QCOMPARE(
        workspace.value(
                     QStringLiteral("pages"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("blocks"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "registers"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    const QJsonObject
        workspaceNavigation =
            workspace
                .value(
                    QStringLiteral(
                        "workbench_navigation"))
                .toObject();
    QCOMPARE(
        workspaceNavigation
            .value(
                QStringLiteral(
                    "protocol_version"))
            .toInt(),
        1);
    QCOMPARE(
        workspaceNavigation
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("workspace"));
    QCOMPARE(
        workspaceNavigation
            .value(
                QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "CLI Fixture"));
    QCOMPARE(
        workspaceNavigation
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            openedProject,
            QStringLiteral("--select"),
            QStringLiteral(
                "workspace-main")}));

    const Invocation list =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--parent"),
             QStringLiteral(
                 "block-control")});
    QCOMPARE(list.exitCode, 0);
    const QJsonArray registers =
        json(list)
            .value(
                QStringLiteral("result"))
            .toArray();
    QCOMPARE(registers.size(), 1);
    QCOMPARE(
        registers.at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    const QJsonObject listNavigation =
        registers.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "workbench_navigation"))
            .toObject();
    QCOMPARE(
        listNavigation
            .value(
                QStringLiteral(
                    "stable_id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        listNavigation
            .value(
                QStringLiteral(
                    "protocol_version"))
            .toInt(),
        1);
    QCOMPARE(
        listNavigation
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("register"));
    QCOMPARE(
        listNavigation
            .value(
                QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "CLI Fixture/Main/Control/CONTROL"));
    QCOMPARE(
        listNavigation
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            openedProject,
            QStringLiteral("--select"),
            QStringLiteral(
                "reg-control")}));

    const Invocation get =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control")});
    QCOMPARE(get.exitCode, 0);
    const QJsonObject reg =
        json(get)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        reg.value(
               QStringLiteral("kind"))
            .toString(),
        QStringLiteral("register"));
    QCOMPARE(
        reg.value(
               QStringLiteral("address"))
            .toString(),
        QStringLiteral("0x1024"));
    QCOMPARE(
        reg.value(
               QStringLiteral("fields"))
            .toArray()
            .size(),
        1);
    QVERIFY(
        !reg.contains(
            QStringLiteral(
                "compatibility")));
    QVERIFY(
        !reg.contains(
            QStringLiteral(
                "array_count")));
    QVERIFY(
        !reg.contains(
            QStringLiteral(
                "array_stride")));
    QCOMPARE(
        reg.value(
               QStringLiteral(
                   "workbench_navigation"))
            .toObject()
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            openedProject,
            QStringLiteral("--select"),
            QStringLiteral(
                "reg-control")}));

    const Invocation validate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validate.exitCode, 0);
    QVERIFY(
        json(validate)
            .value(
                QStringLiteral("ok"))
            .toBool());

    const Invocation missing =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-missing")});
    QCOMPARE(
        missing.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject missingRoot =
        json(missing);
    QVERIFY(
        !missingRoot.value(
                        QStringLiteral(
                            "ok"))
             .toBool());
    QCOMPARE(
        missingRoot
            .value(
                QStringLiteral(
                    "diagnostics"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("code"))
            .toString(),
        QStringLiteral("RMC2001"));
}

void CliTests::diffsProjectsReadOnly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString beforeDirectory =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "基线 项目"));
    const QString afterDirectory =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "当前 项目"));
    QVERIFY(
        QDir().mkpath(
            beforeDirectory));
    QVERIFY(
        QDir().mkpath(
            afterDirectory));
    const QString beforeProject =
        createProject(
            beforeDirectory);
    const QString afterProject =
        createProject(
            afterDirectory);
    QVERIFY(!beforeProject.isEmpty());
    QVERIFY(!afterProject.isEmpty());
    const QString beforeAbsolute =
        QDir::toNativeSeparators(
            QFileInfo(beforeProject)
                .absoluteFilePath());
    const QString afterAbsolute =
        QDir::toNativeSeparators(
            QFileInfo(afterProject)
                .absoluteFilePath());
    const QByteArray beforeBytes =
        readFile(beforeProject);
    const QByteArray originalAfterBytes =
        readFile(afterProject);
    QVERIFY(!beforeBytes.isEmpty());
    QCOMPARE(
        originalAfterBytes,
        beforeBytes);

    const Invocation equalInvocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject});
    QCOMPARE(equalInvocation.exitCode, 0);
    QVERIFY(
        equalInvocation.standardError
            .isEmpty());
    const QJsonObject equalRoot =
        json(equalInvocation);
    QVERIFY(
        equalRoot.value(
                     QStringLiteral("ok"))
            .toBool());
    QVERIFY(
        !equalRoot.contains(
            QStringLiteral("project")));
    QVERIFY(
        !equalRoot.contains(
            QStringLiteral("revision")));
    const QJsonObject equalResult =
        equalRoot.value(
                     QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        equalResult.value(
                       QStringLiteral(
                           "before_project"))
            .toString(),
        beforeAbsolute);
    QCOMPARE(
        equalResult.value(
                       QStringLiteral(
                           "after_project"))
            .toString(),
        afterAbsolute);
    QVERIFY(
        equalResult.value(
                       QStringLiteral(
                           "before_revision"))
            .toString()
            .startsWith(
                QStringLiteral("sha256:")));
    QVERIFY(
        equalResult.value(
                       QStringLiteral(
                           "after_revision"))
            .toString()
            .startsWith(
                QStringLiteral("sha256:")));
    QVERIFY(
        !equalResult.value(
                        QStringLiteral(
                            "changed"))
             .toBool());
    QCOMPARE(
        equalResult.value(
                       QStringLiteral(
                           "change_count"))
            .toInt(),
        0);
    QCOMPARE(
        equalResult.value(
                       QStringLiteral(
                           "changes"))
            .toArray()
            .size(),
        0);
    QVERIFY(
        !equalResult.value(
                        QStringLiteral(
                            "writes_performed"))
             .toBool());
    const Invocation strictEqual =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral(
                 "--require-equal")});
    QCOMPARE(strictEqual.exitCode, 0);
    QVERIFY(
        json(strictEqual)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "require_equal"))
            .toBool());

    QByteArray changedAfterBytes =
        originalAfterBytes;
    QCOMPARE(
        changedAfterBytes.count(
            QByteArrayLiteral(
                "              name: CONTROL")),
        1);
    QCOMPARE(
        changedAfterBytes.count(
            QByteArrayLiteral(
                "                - id: field-enable")),
        1);
    changedAfterBytes.replace(
        QByteArrayLiteral(
            "              name: CONTROL"),
        QByteArrayLiteral(
            "              name: CONTROL_NEXT"));
    changedAfterBytes.replace(
        QByteArrayLiteral(
            "                - id: field-enable"),
        QByteArrayLiteral(
            "                - id: field-ready"));
    changedAfterBytes.replace(
        QByteArrayLiteral(
            "                  name: ENABLE"),
        QByteArrayLiteral(
            "                  name: READY"));
    {
        QFile afterFile(afterProject);
        QVERIFY(
            afterFile.open(
                QIODevice::WriteOnly |
                QIODevice::Truncate));
        QCOMPARE(
            afterFile.write(
                changedAfterBytes),
            changedAfterBytes.size());
    }

    const Invocation changedInvocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject});
    QCOMPARE(changedInvocation.exitCode, 0);
    QVERIFY(
        changedInvocation.standardError
            .isEmpty());
    const QJsonObject changedRoot =
        json(changedInvocation);
    QVERIFY(
        changedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QJsonObject changedResult =
        changedRoot.value(
                       QStringLiteral("result"))
            .toObject();
    QVERIFY(
        changedResult.value(
                         QStringLiteral(
                             "changed"))
            .toBool());
    QCOMPARE(
        changedResult.value(
                         QStringLiteral(
                             "change_count"))
            .toInt(),
        3);
    const QJsonObject counts =
        changedResult.value(
                         QStringLiteral("counts"))
            .toObject();
    QCOMPARE(
        counts.value(
                  QStringLiteral("added"))
            .toInt(),
        1);
    QCOMPARE(
        counts.value(
                  QStringLiteral("removed"))
            .toInt(),
        1);
    QCOMPARE(
        counts.value(
                  QStringLiteral("modified"))
            .toInt(),
        1);
    const QJsonArray changes =
        changedResult.value(
                         QStringLiteral("changes"))
            .toArray();
    QCOMPARE(changes.size(), 3);
    const auto changeById =
        [&changes](
            const QString& id) {
            const auto found =
                std::ranges::find_if(
                    changes,
                    [&id](
                        const QJsonValue& value) {
                        return value.toObject()
                                   .value(
                                       QStringLiteral(
                                           "id"))
                                   .toString() ==
                            id;
                    });
            return found == changes.end()
                ? QJsonObject{}
                : found->toObject();
        };
    const QJsonObject removed =
        changeById(
            QStringLiteral(
                "field-enable"));
    QCOMPARE(
        removed.value(
                   QStringLiteral("change"))
            .toString(),
        QStringLiteral("removed"));
    QCOMPARE(
        removed.value(
                   QStringLiteral(
                       "object_kind"))
            .toString(),
        QStringLiteral("field"));
    QVERIFY(
        removed.value(
                   QStringLiteral(
                       "before_source"))
            .isObject());
    QVERIFY(
        removed.value(
                   QStringLiteral(
                       "after_source"))
            .isNull());
    QCOMPARE(
        removed.value(
                   QStringLiteral(
                       "before_source"))
            .toObject()
            .value(
                QStringLiteral("file"))
            .toString(),
        beforeAbsolute);
    const QJsonObject removedState =
        removed.value(
                   QStringLiteral("before"))
            .toObject();
    QCOMPARE(
        removedState.value(
                        QStringLiteral("kind"))
            .toString(),
        QStringLiteral("field"));
    QCOMPARE(
        removedState.value(
                        QStringLiteral(
                            "parent_id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        removedState.value(
                        QStringLiteral(
                            "properties"))
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("ENABLE"));
    QVERIFY(
        removed.value(
                   QStringLiteral("after"))
            .isNull());
    QVERIFY(
        removed.value(
                   QStringLiteral(
                       "property_changes"))
            .toArray()
            .isEmpty());
    const QJsonObject removedNavigation =
        removed.value(
                   QStringLiteral(
                       "before_navigation"))
            .toObject();
    QCOMPARE(
        removedNavigation.value(
                             QStringLiteral(
                                 "project"))
            .toString(),
        beforeAbsolute);
    QCOMPARE(
        removedNavigation.value(
                             QStringLiteral(
                                 "stable_id"))
            .toString(),
        QStringLiteral(
            "field-enable"));
    QCOMPARE(
        removedNavigation.value(
                             QStringLiteral(
                                 "arguments"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            beforeAbsolute,
            QStringLiteral("--select"),
            QStringLiteral(
                "field-enable")}));
    QVERIFY(
        removed.value(
                   QStringLiteral(
                       "after_navigation"))
            .isNull());

    const QJsonObject added =
        changeById(
            QStringLiteral(
                "field-ready"));
    QCOMPARE(
        added.value(
                 QStringLiteral("change"))
            .toString(),
        QStringLiteral("added"));
    QVERIFY(
        added.value(
                 QStringLiteral(
                     "before_source"))
            .isNull());
    QCOMPARE(
        added.value(
                 QStringLiteral(
                     "after_source"))
            .toObject()
            .value(
                QStringLiteral("file"))
            .toString(),
        afterAbsolute);
    QVERIFY(
        added.value(
                 QStringLiteral("before"))
            .isNull());
    const QJsonObject addedState =
        added.value(
                 QStringLiteral("after"))
            .toObject();
    QCOMPARE(
        addedState.value(
                      QStringLiteral("kind"))
            .toString(),
        QStringLiteral("field"));
    QCOMPARE(
        addedState.value(
                      QStringLiteral(
                          "properties"))
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("READY"));
    QVERIFY(
        added.value(
                 QStringLiteral(
                     "before_navigation"))
            .isNull());
    QCOMPARE(
        added.value(
                 QStringLiteral(
                     "after_navigation"))
            .toObject()
            .value(
                QStringLiteral(
                    "stable_id"))
            .toString(),
        QStringLiteral(
            "field-ready"));

    const QJsonObject modified =
        changeById(
            QStringLiteral(
                "reg-control"));
    QCOMPARE(
        modified.value(
                    QStringLiteral("change"))
            .toString(),
        QStringLiteral("modified"));
    QCOMPARE(
        modified.value(
                    QStringLiteral(
                        "object_kind"))
            .toString(),
        QStringLiteral("register"));
    QCOMPARE(
        modified.value(
                    QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "CONTROL_NEXT"));
    QCOMPARE(
        modified.value(
                    QStringLiteral("summary"))
            .toString(),
        QStringLiteral(
            "Properties changed"));
    QVERIFY(
        modified.value(
                    QStringLiteral(
                        "before_source"))
            .isObject());
    QVERIFY(
        modified.value(
                    QStringLiteral(
                        "after_source"))
            .isObject());
    const QJsonObject
        modifiedBeforeState =
            modified.value(
                        QStringLiteral(
                            "before"))
                .toObject();
    const QJsonObject
        modifiedAfterState =
            modified.value(
                        QStringLiteral(
                            "after"))
                .toObject();
    QCOMPARE(
        modifiedBeforeState
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral("block-control"));
    QCOMPARE(
        modifiedBeforeState
            .value(
                QStringLiteral(
                    "properties"))
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("CONTROL"));
    QCOMPARE(
        modifiedAfterState
            .value(
                QStringLiteral(
                    "properties"))
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "CONTROL_NEXT"));
    const QJsonArray propertyChanges =
        modified.value(
                    QStringLiteral(
                        "property_changes"))
            .toArray();
    QCOMPARE(propertyChanges.size(), 1);
    const QJsonObject nameChange =
        propertyChanges.at(0)
            .toObject();
    QCOMPARE(
        nameChange.value(
                      QStringLiteral(
                          "property"))
            .toString(),
        QStringLiteral("name"));
    QVERIFY(
        nameChange.value(
                      QStringLiteral(
                          "before_present"))
            .toBool());
    QCOMPARE(
        nameChange.value(
                      QStringLiteral(
                          "before"))
            .toString(),
        QStringLiteral("CONTROL"));
    QVERIFY(
        nameChange.value(
                      QStringLiteral(
                          "after_present"))
            .toBool());
    QCOMPARE(
        nameChange.value(
                      QStringLiteral("after"))
            .toString(),
        QStringLiteral(
            "CONTROL_NEXT"));
    const QJsonObject
        modifiedBeforeNavigation =
            modified.value(
                        QStringLiteral(
                            "before_navigation"))
                .toObject();
    const QJsonObject
        modifiedAfterNavigation =
            modified.value(
                        QStringLiteral(
                            "after_navigation"))
                .toObject();
    QCOMPARE(
        modifiedBeforeNavigation
            .value(
                QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "CLI Fixture/Main/Control/CONTROL"));
    QCOMPARE(
        modifiedAfterNavigation
            .value(
                QStringLiteral("path"))
            .toString(),
        QStringLiteral(
            "CLI Fixture/Main/Control/CONTROL_NEXT"));
    QCOMPARE(
        modifiedAfterNavigation
            .value(
                QStringLiteral("kind"))
            .toString(),
        QStringLiteral("register"));
    QCOMPARE(
        changedResult.value(
                         QStringLiteral(
                             "kind_filter"))
            .toString(),
        QStringLiteral("all"));
    const QJsonObject allMetadata =
        changedRoot.value(
                       QStringLiteral(
                           "result_metadata"))
            .toObject();
    QCOMPARE(
        allMetadata.value(
                       QStringLiteral(
                           "total_count"))
            .toInteger(),
        3);
    QCOMPARE(
        allMetadata.value(
                       QStringLiteral(
                           "returned_count"))
            .toInteger(),
        3);
    QVERIFY(
        !allMetadata.value(
                        QStringLiteral(
                            "has_more"))
             .toBool());

    const QString beforeRevision =
        changedResult.value(
                         QStringLiteral(
                             "before_revision"))
            .toString();
    const QString afterRevision =
        changedResult.value(
                         QStringLiteral(
                             "after_revision"))
            .toString();
    const Invocation firstFieldPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral(
                 "--expect-before"),
             beforeRevision,
             QStringLiteral(
                 "--expect-after"),
             afterRevision});
    QCOMPARE(firstFieldPage.exitCode, 0);
    const QJsonObject firstFieldRoot =
        json(firstFieldPage);
    const QJsonObject firstFieldResult =
        firstFieldRoot.value(
                          QStringLiteral(
                              "result"))
            .toObject();
    QCOMPARE(
        firstFieldResult.value(
                            QStringLiteral(
                                "kind_filter"))
            .toString(),
        QStringLiteral("field"));
    QCOMPARE(
        firstFieldResult.value(
                            QStringLiteral(
                                "change_count"))
            .toInt(),
        2);
    const QJsonObject fieldCounts =
        firstFieldResult.value(
                            QStringLiteral("counts"))
            .toObject();
    QCOMPARE(
        fieldCounts.value(
                       QStringLiteral("added"))
            .toInt(),
        1);
    QCOMPARE(
        fieldCounts.value(
                       QStringLiteral("removed"))
            .toInt(),
        1);
    QCOMPARE(
        fieldCounts.value(
                       QStringLiteral("modified"))
            .toInt(),
        0);
    const QJsonArray firstFieldChanges =
        firstFieldResult.value(
                            QStringLiteral("changes"))
            .toArray();
    QCOMPARE(firstFieldChanges.size(), 1);
    QCOMPARE(
        firstFieldChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-enable"));
    const QJsonObject firstFieldMetadata =
        firstFieldRoot.value(
                          QStringLiteral(
                              "result_metadata"))
            .toObject();
    QCOMPARE(
        firstFieldMetadata.value(
                              QStringLiteral(
                                  "total_count"))
            .toInteger(),
        2);
    QCOMPARE(
        firstFieldMetadata.value(
                              QStringLiteral(
                                  "returned_count"))
            .toInteger(),
        1);
    QVERIFY(
        firstFieldMetadata.value(
                              QStringLiteral(
                                  "has_more"))
            .toBool());
    QCOMPARE(
        firstFieldMetadata.value(
                              QStringLiteral(
                                  "next_offset"))
            .toInteger(),
        1);

    const Invocation secondFieldPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral(
                 "--expect-before"),
             beforeRevision,
             QStringLiteral(
                 "--expect-after"),
             afterRevision});
    QCOMPARE(secondFieldPage.exitCode, 0);
    const QJsonObject secondFieldRoot =
        json(secondFieldPage);
    const QJsonArray secondFieldChanges =
        secondFieldRoot.value(
                           QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(secondFieldChanges.size(), 1);
    QCOMPARE(
        secondFieldChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-ready"));
    QVERIFY(
        !secondFieldRoot.value(
                            QStringLiteral(
                                "result_metadata"))
             .toObject()
             .value(
                 QStringLiteral(
                     "has_more"))
             .toBool());

    const QString staleRevision =
        QStringLiteral("sha256:%1")
            .arg(
                QString(
                    64,
                    QLatin1Char('0')));
    const Invocation staleGuard =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral(
                 "--expect-before"),
             staleRevision,
             QStringLiteral(
                 "--expect-after"),
             afterRevision});
    QCOMPARE(
        staleGuard.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    const QJsonObject staleRoot =
        json(staleGuard);
    QCOMPARE(
        staleRoot.value(
                     QStringLiteral(
                         "error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    const QJsonObject staleResult =
        staleRoot.value(
                       QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !staleResult.contains(
            QStringLiteral("changes")));
    QCOMPARE(
        staleResult.value(
                       QStringLiteral(
                           "expected_before_revision"))
            .toString(),
        staleRevision);
    QVERIFY(
        !staleResult.value(
                        QStringLiteral(
                            "writes_performed"))
             .toBool());
    const Invocation strictChanged =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral(
                 "--require-equal")});
    QCOMPARE(
        strictChanged.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                differencesFound));
    const QJsonObject strictChangedRoot =
        json(strictChanged);
    QVERIFY(
        !strictChangedRoot.value(
                              QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        strictChangedRoot.value(
                              QStringLiteral(
                                  "exit_status"))
            .toString(),
        QStringLiteral(
            "differences_found"));
    QCOMPARE(
        strictChangedRoot.value(
                              QStringLiteral(
                                  "error_code"))
            .toString(),
        QStringLiteral("RMC6001"));
    const QJsonObject strictChangedResult =
        strictChangedRoot.value(
                              QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        strictChangedResult.value(
                                QStringLiteral(
                                    "change_count"))
            .toInt(),
        2);
    QCOMPARE(
        strictChangedResult.value(
                                QStringLiteral(
                                    "changes"))
            .toArray()
            .size(),
        1);
    QVERIFY(
        strictChangedResult.value(
                                QStringLiteral(
                                    "require_equal"))
            .toBool());
    QVERIFY(
        !strictChangedResult.value(
                                 QStringLiteral(
                                     "writes_performed"))
             .toBool());
    const Invocation strictExecutable =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral(
                 "--require-equal")});
    QCOMPARE(
        strictExecutable.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                differencesFound));
    QVERIFY(
        strictExecutable.standardError
            .isEmpty());
    QCOMPARE(
        json(strictExecutable)
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral(
            "differences_found"));
    const Invocation strictFilteredEqual =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("enum"),
             QStringLiteral(
                 "--require-equal")});
    QCOMPARE(
        strictFilteredEqual.exitCode,
        0);
    QVERIFY(
        json(strictFilteredEqual)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "require_equal"))
            .toBool());

    QCOMPARE(
        readFile(beforeProject),
        beforeBytes);
    QCOMPARE(
        readFile(afterProject),
        changedAfterBytes);
    QVERIFY(
        !QFileInfo::exists(
            QDir(beforeDirectory)
                .filePath(
                    QStringLiteral(
                        "generated"))));
    QVERIFY(
        !QFileInfo::exists(
            QDir(afterDirectory)
                .filePath(
                    QStringLiteral(
                        "generated"))));

    const Invocation textInvocation =
        invoke(
            {QStringLiteral("diff"),
             beforeProject,
             afterProject});
    QCOMPARE(textInvocation.exitCode, 0);
    QVERIFY(
        textInvocation.standardError
            .isEmpty());
    QVERIFY(
        textInvocation.standardOutput
            .contains(
                QStringLiteral(
                    "Changes: 3 (1 added, 1 removed, 1 modified)")));
    QVERIFY(
        textInvocation.standardOutput
            .contains(
                QStringLiteral(
                    "removed\tfield\tfield-enable")));
    QVERIFY(
        textInvocation.standardOutput
            .contains(
                QStringLiteral(
                    "added\tfield\tfield-ready")));
    QVERIFY(
        textInvocation.standardOutput
            .contains(
                QStringLiteral(
                    "modified\tregister\treg-control")));
    QVERIFY(
        textInvocation.standardOutput
            .contains(
                QStringLiteral(
                    "  name: \"CONTROL\" -> \"CONTROL_NEXT\"")));
    const Invocation pagedText =
        invoke(
            {QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(pagedText.exitCode, 0);
    QVERIFY(
        pagedText.standardOutput
            .contains(
                QStringLiteral(
                    "Kind: field")));
    QVERIFY(
        pagedText.standardOutput
            .contains(
                QStringLiteral(
                    "Showing 1 of 2 changes from offset 0; use --offset 1 to continue.")));
    const Invocation strictText =
        invoke(
            {QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral(
                 "--require-equal")});
    QCOMPARE(
        strictText.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                differencesFound));
    QVERIFY(
        strictText.standardOutput
            .contains(
                QStringLiteral(
                    "Changes: 2 (1 added, 1 removed, 0 modified)")));
    QVERIFY(
        strictText.standardError
            .contains(
                QStringLiteral(
                    "ERROR RMC6001")));

    const Invocation missingArgument =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject});
    QCOMPARE(
        missingArgument.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject missingArgumentRoot =
        json(missingArgument);
    QCOMPARE(
        missingArgumentRoot.value(
                               QStringLiteral(
                                   "usage"))
            .toString(),
        QStringLiteral(
            "regmapc [--json] diff <before.regmap.yaml> <after.regmap.yaml> [--kind <kind>] [--offset <count>] [--limit <count>] [--expect-before <sha256:...>] [--expect-after <sha256:...>] [--require-equal]"));

    const Invocation invalidKind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--kind"),
             QStringLiteral("registers")});
    QCOMPARE(
        invalidKind.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(invalidKind)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "Expected workspace, page, block, register, field, enum, or all")));

    const Invocation optionTypo =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             afterProject,
             QStringLiteral("--limt"),
             QStringLiteral("1")});
    QCOMPARE(
        optionTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        json(optionTypo)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "suggested_option"))
            .toString(),
        QStringLiteral("--limit"));

    const QString missingProject =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "missing.regmap.yaml"));
    const Invocation missingFile =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("diff"),
             beforeProject,
             missingProject});
    QCOMPARE(
        missingFile.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject missingFileRoot =
        json(missingFile);
    QVERIFY(
        !missingFileRoot.value(
                            QStringLiteral("ok"))
             .toBool());
    QVERIFY(
        missingFileRoot.value(
                            QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "Both projects")));
    const QJsonObject missingFileResult =
        missingFileRoot.value(
                            QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !missingFileResult.value(
                              QStringLiteral(
                                  "writes_performed"))
             .toBool());
    QVERIFY(
        !missingFileResult.contains(
            QStringLiteral("changes")));
    QCOMPARE(
        readFile(beforeProject),
        beforeBytes);
    QCOMPARE(
        readFile(afterProject),
        changedAfterBytes);
}

void CliTests::guardsGetByRevision()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    QVERIFY(
        revision.startsWith(
            QStringLiteral(
                "sha256:")));

    const Invocation guarded =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(guarded.exitCode, 0);
    const QJsonObject guardedRoot =
        json(guarded);
    QCOMPARE(
        guardedRoot
            .value(
                QStringLiteral(
                    "revision"))
            .toString(),
        revision);
    QCOMPARE(
        guardedRoot
            .value(
                QStringLiteral(
                    "result"))
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "reg-control"));

    QFile externalEdit(project);
    QVERIFY(
        externalEdit.open(
            QIODevice::Append |
            QIODevice::Text));
    QCOMPARE(
        externalEdit.write(
            QByteArrayLiteral(
                "\n# external revision change\n")),
        qint64{
            QByteArrayLiteral(
                "\n# external revision change\n")
                .size()});
    externalEdit.close();
    const QByteArray changedProject =
        readFile(project);

    const Invocation stale =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QVERIFY(
        stale.standardError
            .isEmpty());
    QCOMPARE(
        readFile(project),
        changedProject);
    const QJsonObject staleRoot =
        json(stale);
    QVERIFY(
        !staleRoot
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        staleRoot
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral(
            "revision_conflict"));
    QCOMPARE(
        staleRoot
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    const QString current =
        staleRoot
            .value(
                QStringLiteral(
                    "revision"))
            .toString();
    QVERIFY(!current.isEmpty());
    QVERIFY(current != revision);
    const QJsonObject result =
        staleRoot
            .value(
                QStringLiteral(
                    "result"))
            .toObject();
    QCOMPARE(
        result
            .value(
                QStringLiteral(
                    "expected_revision"))
            .toString(),
        revision);
    QCOMPARE(
        result
            .value(
                QStringLiteral(
                    "current_revision"))
            .toString(),
        current);
    QVERIFY(
        !result
             .value(
                 QStringLiteral(
                     "writes_performed"))
             .toBool());
    QVERIFY(
        !result.contains(
            QStringLiteral("kind")));

    const Invocation missingRevision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control"),
             QStringLiteral("--expect")});
    QCOMPARE(
        missingRevision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(missingRevision)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "requires a revision")));

    const Invocation terminatedRevision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control"),
             QStringLiteral("--expect"),
             QStringLiteral("--"),
             revision});
    QCOMPARE(
        terminatedRevision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(terminatedRevision)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "requires a revision")));

    const Invocation terminatedId =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral("--"),
             QStringLiteral("--expect")});
    QCOMPARE(
        terminatedId.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        json(terminatedId)
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC2001"));
}

void CliTests::getsManyObjectsInOneSnapshot()
{
    const Invocation focusedHelp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("help"),
             QStringLiteral("get-many")});
    QCOMPARE(focusedHelp.exitCode, 0);
    const QJsonObject helpResult =
        json(focusedHelp)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        helpResult
            .value(
                QStringLiteral("usage"))
            .toString(),
        QStringLiteral(
            "regmapc [--json] get-many <project.regmap.yaml> <stable-id>... [--expect <sha256:...>]"));
    QVERIFY(
        helpResult
            .value(
                QStringLiteral(
                    "argument_schema"))
            .toObject()
            .value(
                QStringLiteral(
                    "positionals"))
            .toArray()
            .at(1)
            .toObject()
            .value(
                QStringLiteral(
                    "repeatable"))
            .toBool());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    QVERIFY(
        revision.startsWith(
            QStringLiteral("sha256:")));
    const QByteArray projectBefore =
        readFile(project);

    const Invocation successful =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project,
             QStringLiteral("reg-control"),
             QStringLiteral("field-enable"),
             QStringLiteral("reg-control"),
             QStringLiteral("block-control"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(successful.exitCode, 0);
    const QJsonObject successfulRoot =
        json(successful);
    QVERIFY(
        successfulRoot
            .value(
                QStringLiteral("ok"))
            .toBool());
    QCOMPARE(
        successfulRoot
            .value(
                QStringLiteral("command"))
            .toString(),
        QStringLiteral("get-many"));
    QCOMPARE(
        successfulRoot
            .value(
                QStringLiteral("revision"))
            .toString(),
        revision);
    const QJsonObject successfulResult =
        successfulRoot
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        successfulResult
            .value(
                QStringLiteral(
                    "requested_count"))
            .toInt(),
        4);
    QCOMPARE(
        successfulResult
            .value(
                QStringLiteral("found_count"))
            .toInt(),
        4);
    QCOMPARE(
        successfulResult
            .value(
                QStringLiteral("missing_count"))
            .toInt(),
        0);
    QCOMPARE(
        successfulResult
            .value(
                QStringLiteral(
                    "duplicate_count"))
            .toInt(),
        1);
    const QJsonArray successfulItems =
        successfulResult
            .value(
                QStringLiteral("items"))
            .toArray();
    QCOMPARE(successfulItems.size(), 4);
    const QJsonArray expectedIds{
        QStringLiteral("reg-control"),
        QStringLiteral("field-enable"),
        QStringLiteral("reg-control"),
        QStringLiteral("block-control"),
    };
    for (qsizetype index = 0;
         index < successfulItems.size();
         ++index) {
        const QJsonObject item =
            successfulItems.at(index)
                .toObject();
        QCOMPARE(
            item.value(
                    QStringLiteral(
                        "request_index"))
                .toInteger(),
            index);
        QCOMPARE(
            item.value(
                    QStringLiteral(
                        "requested_id"))
                .toString(),
            expectedIds.at(index)
                .toString());
        QVERIFY(
            item.value(
                    QStringLiteral("found"))
                .toBool());
        QVERIFY(
            item.value(
                    QStringLiteral(
                        "error_code"))
                .isNull());
        const QJsonObject object =
            item.value(
                    QStringLiteral("object"))
                .toObject();
        QCOMPARE(
            object.value(
                      QStringLiteral("id"))
                .toString(),
            expectedIds.at(index)
                .toString());
        QCOMPARE(
            object.value(
                      QStringLiteral(
                          "workbench_navigation"))
                .toObject()
                .value(
                    QStringLiteral(
                        "stable_id"))
                .toString(),
            expectedIds.at(index)
                .toString());
    }
    QVERIFY(
        successfulItems.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "duplicate_of_index"))
            .isNull());
    QCOMPARE(
        successfulItems.at(2)
            .toObject()
            .value(
                QStringLiteral(
                    "duplicate_of_index"))
            .toInt(),
        0);

    const Invocation partial =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project,
             QStringLiteral("reg-control"),
             QStringLiteral("reg-missing"),
             QStringLiteral("field-enable"),
             QStringLiteral("reg-missing")});
    QCOMPARE(
        partial.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject partialRoot =
        json(partial);
    QCOMPARE(
        partialRoot
            .value(
                QStringLiteral("error_code"))
            .toString(),
        QStringLiteral("RMC2001"));
    const QJsonObject partialResult =
        partialRoot
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        partialResult
            .value(
                QStringLiteral("found_count"))
            .toInt(),
        2);
    QCOMPARE(
        partialResult
            .value(
                QStringLiteral("missing_count"))
            .toInt(),
        2);
    QCOMPARE(
        partialResult
            .value(
                QStringLiteral(
                    "duplicate_count"))
            .toInt(),
        1);
    const QJsonArray partialItems =
        partialResult
            .value(
                QStringLiteral("items"))
            .toArray();
    QCOMPARE(partialItems.size(), 4);
    QVERIFY(
        partialItems.at(0)
            .toObject()
            .value(
                QStringLiteral("found"))
            .toBool());
    QVERIFY(
        !partialItems.at(1)
             .toObject()
             .value(
                 QStringLiteral("found"))
             .toBool());
    QVERIFY(
        partialItems.at(1)
            .toObject()
            .value(
                QStringLiteral("object"))
            .isNull());
    QCOMPARE(
        partialItems.at(1)
            .toObject()
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC2001"));
    QCOMPARE(
        partialItems.at(3)
            .toObject()
            .value(
                QStringLiteral(
                    "duplicate_of_index"))
            .toInt(),
        1);
    QCOMPARE(
        partialRoot
            .value(
                QStringLiteral("diagnostics"))
            .toArray()
            .size(),
        1);

    const QString staleRevision =
        QStringLiteral("sha256:%1")
            .arg(
                QString(
                    64,
                    QLatin1Char('0')));
    const Invocation stale =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project,
             QStringLiteral("reg-control"),
             QStringLiteral("field-enable"),
             QStringLiteral("--expect"),
             staleRevision});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    const QJsonObject staleRoot =
        json(stale);
    QCOMPARE(
        staleRoot
            .value(
                QStringLiteral("error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    const QJsonObject staleResult =
        staleRoot
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !staleResult.contains(
            QStringLiteral("items")));
    QVERIFY(
        !staleResult.contains(
            QStringLiteral("object")));

    const Invocation noIds =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project});
    QCOMPARE(
        noIds.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        json(noIds)
            .value(
                QStringLiteral("usage"))
            .toString(),
        QStringLiteral(
            "regmapc [--json] get-many <project.regmap.yaml> <stable-id>... [--expect <sha256:...>]"));

    const Invocation duplicateExpect =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project,
             QStringLiteral("reg-control"),
             QStringLiteral("--expect"),
             revision,
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(
        duplicateExpect.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));

    const Invocation terminatedId =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get-many"),
             project,
             QStringLiteral("--"),
             QStringLiteral("--expect")});
    QCOMPARE(
        terminatedId.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        json(terminatedId)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("items"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "requested_id"))
            .toString(),
        QStringLiteral("--expect"));

    const Invocation text =
        invoke(
            {QStringLiteral("get-many"),
             project,
             QStringLiteral("reg-control"),
             QStringLiteral("field-enable")});
    QCOMPARE(text.exitCode, 0);
    QVERIFY(
        text.standardOutput
            .contains(
                QStringLiteral(
                    "\"requested_count\": 2")));
    QCOMPARE(
        readFile(project),
        projectBefore);
    QVERIFY(
        !QFileInfo::exists(
            QDir(directory.path())
                .filePath(
                    QStringLiteral(
                        "generated"))));
}

void CliTests::findsObjectsByHumanFacingClues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString renamePatch =
        directory.filePath(
            QStringLiteral(
                "find-fixture.json"));
    QVERIFY(
        writePatch(
            renamePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("name")},
                    {QStringLiteral("value"),
                     QStringLiteral("CTRL")},
                },
            },
            currentRevision(project)));
    QCOMPARE(
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             renamePatch})
            .exitCode,
        0);
    const QString paginationRevision =
        currentRevision(project);
    QVERIFY(!paginationRevision.isEmpty());

    const auto find =
        [&project](
            const QString& query,
            const QStringList& options =
                QStringList{}) {
            QStringList arguments{
                QStringLiteral("--json"),
                QStringLiteral("find"),
                project,
                query,
            };
            arguments.append(options);
            return invoke(arguments);
        };

    const Invocation exactName =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(exactName.exitCode, 0);
    const QJsonArray exactResults =
        json(exactName)
            .value(
                QStringLiteral("result"))
            .toArray();
    QCOMPARE(exactResults.size(), 1);
    QCOMPARE(
        exactResults.at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        exactResults.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_rank"))
            .toInt(),
        0);
    QCOMPARE(
        exactResults.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_field"))
            .toString(),
        QStringLiteral("name"));
    QCOMPARE(
        exactResults.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "workbench_navigation"))
            .toObject()
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray(),
        (QJsonArray{
            QStringLiteral("--project"),
            json(exactName)
                .value(
                    QStringLiteral(
                        "project"))
                .toString(),
            QStringLiteral("--select"),
            QStringLiteral(
                "reg-control")}));

    const Invocation defaultPrefix =
        find(
            QStringLiteral("ctr"),
            {QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(defaultPrefix.exitCode, 0);
    QCOMPARE(
        json(defaultPrefix)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_rank"))
            .toInt(),
        2);
    const Invocation exactPrefix =
        find(
            QStringLiteral("ctr"),
            {QStringLiteral("--exact"),
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(exactPrefix.exitCode, 0);
    QVERIFY(
        json(exactPrefix)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());
    QCOMPARE(
        json(exactPrefix)
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        0);

    const Invocation tag =
        find(
            QStringLiteral("control"),
            {QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--parent"),
             QStringLiteral(
                 "block-control")});
    QCOMPARE(tag.exitCode, 0);
    QCOMPARE(
        json(tag)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_rank"))
            .toInt(),
        1);
    QCOMPARE(
        json(tag)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_field"))
            .toString(),
        QStringLiteral("tags"));
    QCOMPARE(
        json(tag)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("tags"))
            .toArray()
            .at(0)
            .toString(),
        QStringLiteral("control"));

    const Invocation exactTagQuery =
        find(
            QStringLiteral("CONTROL"),
            {QStringLiteral("--exact"),
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--tag"),
             QStringLiteral("control")});
    QCOMPARE(exactTagQuery.exitCode, 0);
    QCOMPARE(
        json(exactTagQuery)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_field"))
            .toString(),
        QStringLiteral("tags"));

    const Invocation exactTagFind =
        find(
            QStringLiteral("control"),
            {QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--tag"),
             QStringLiteral("control"),
             QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(exactTagFind.exitCode, 0);
    const QJsonObject exactTagFindRoot =
        json(exactTagFind);
    const QJsonArray exactTagFindResults =
        exactTagFindRoot
            .value(
                QStringLiteral("result"))
            .toArray();
    QCOMPARE(exactTagFindResults.size(), 1);
    QCOMPARE(
        exactTagFindResults
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    const QJsonObject exactTagFindMetadata =
        exactTagFindRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject();
    QCOMPARE(
        exactTagFindMetadata
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        1);
    QVERIFY(
        !exactTagFindMetadata
             .value(
                 QStringLiteral(
                     "has_more"))
             .toBool());

    const Invocation exactTagList =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--tag"),
             QStringLiteral("control")});
    QCOMPARE(exactTagList.exitCode, 0);
    const QJsonObject exactTagListRoot =
        json(exactTagList);
    const QJsonArray exactTagListResults =
        exactTagListRoot
            .value(
                QStringLiteral("result"))
            .toArray();
    QCOMPARE(exactTagListResults.size(), 1);
    QCOMPARE(
        exactTagListResults
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        exactTagListRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        1);

    const Invocation caseMismatchTag =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--tag"),
             QStringLiteral("Control")});
    QCOMPARE(caseMismatchTag.exitCode, 0);
    QVERIFY(
        json(caseMismatchTag)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());
    QCOMPARE(
        json(caseMismatchTag)
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        0);

    const Invocation duplicateTag =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--tag"),
             QStringLiteral("control"),
             QStringLiteral("--tag"),
             QStringLiteral("control")});
    QCOMPARE(
        duplicateTag.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(duplicateTag)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "--tag can be specified only once.")));

    const Invocation limited =
        find(
            QStringLiteral("control"),
            {QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(limited.exitCode, 0);
    const QJsonObject limitedRoot =
        json(limited);
    QCOMPARE(
        limitedRoot.value(
                       QStringLiteral(
                           "result"))
            .toArray()
            .size(),
        1);
    QCOMPARE(
        limitedRoot.value(
                       QStringLiteral(
                           "result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "block-control"));
    const QJsonObject limitedMetadata =
        limitedRoot.value(
                       QStringLiteral(
                           "result_metadata"))
            .toObject();
    QVERIFY(
        limitedMetadata.value(
                           QStringLiteral(
                               "total_count"))
                .toInt() >
            1);
    QCOMPARE(
        limitedMetadata.value(
                           QStringLiteral(
                               "returned_count"))
            .toInt(),
        1);
    QCOMPARE(
        limitedMetadata.value(
                           QStringLiteral(
                               "offset"))
            .toInt(),
        0);
    QVERIFY(
        limitedMetadata.value(
                           QStringLiteral(
                               "truncated"))
            .toBool());
    QVERIFY(
        limitedMetadata.value(
                           QStringLiteral(
                               "has_more"))
            .toBool());
    QCOMPARE(
        limitedMetadata.value(
                           QStringLiteral(
                               "next_offset"))
            .toInt(),
        1);
    QCOMPARE(
        limitedMetadata.value(
                           QStringLiteral(
                               "limit"))
            .toInt(),
        1);

    const Invocation nextFindPage =
        find(
            QStringLiteral("control"),
            {QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral("--expect"),
             paginationRevision});
    QCOMPARE(nextFindPage.exitCode, 0);
    const QJsonObject
        nextFindPageRoot =
            json(nextFindPage);
    QCOMPARE(
        nextFindPageRoot
            .value(
                QStringLiteral("result"))
            .toArray()
            .size(),
        1);
    QVERIFY(
        nextFindPageRoot
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString() !=
        limitedRoot
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString());
    QCOMPARE(
        nextFindPageRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(
                QStringLiteral("offset"))
            .toInt(),
        1);

    const Invocation listPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("2"),
             QStringLiteral("--expect"),
             paginationRevision});
    QCOMPARE(listPage.exitCode, 0);
    const QJsonObject listPageRoot =
        json(listPage);
    QCOMPARE(
        listPageRoot
            .value(
                QStringLiteral("result"))
            .toArray()
            .size(),
        2);
    const QJsonObject listPageMetadata =
        listPageRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject();
    QCOMPARE(
        listPageMetadata
            .value(
                QStringLiteral("offset"))
            .toInt(),
        1);
    QCOMPARE(
        listPageMetadata
            .value(
                QStringLiteral(
                    "returned_count"))
            .toInt(),
        2);
    QVERIFY(
        listPageMetadata
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt() >
        3);
    QCOMPARE(
        listPageMetadata
            .value(
                QStringLiteral(
                    "next_offset"))
            .toInt(),
        3);
    const Invocation beyondListEnd =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--offset"),
             QStringLiteral("999"),
             QStringLiteral("--limit"),
             QStringLiteral("2"),
             QStringLiteral("--expect"),
             paginationRevision});
    QCOMPARE(
        beyondListEnd.exitCode, 0);
    const QJsonObject beyondRoot =
        json(beyondListEnd);
    QVERIFY(
        beyondRoot
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());
    const QJsonObject beyondMetadata =
        beyondRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject();
    QCOMPARE(
        beyondMetadata
            .value(
                QStringLiteral("offset"))
            .toInt(),
        999);
    QVERIFY(
        !beyondMetadata
             .value(
                 QStringLiteral(
                     "has_more"))
             .toBool());
    QVERIFY(
        beyondMetadata
            .value(
                QStringLiteral(
                    "next_offset"))
            .isNull());
    const QString staleRevision =
        QStringLiteral("sha256:") +
        QString(64, QLatin1Char('0'));
    QVERIFY(staleRevision !=
            paginationRevision);
    const Invocation staleListPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("2"),
             QStringLiteral("--expect"),
             staleRevision});
    QCOMPARE(
        staleListPage.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    const QJsonObject staleListRoot =
        json(staleListPage);
    QCOMPARE(
        staleListRoot
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    QCOMPARE(
        staleListRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "current_revision"))
            .toString(),
        paginationRevision);
    QVERIFY(
        !staleListRoot
             .value(
                 QStringLiteral("result"))
             .toObject()
             .value(
                 QStringLiteral(
                     "writes_performed"))
             .toBool());

    const Invocation staleFindPage =
        find(
            QStringLiteral("control"),
            {QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral("--expect"),
             staleRevision});
    QCOMPARE(
        staleFindPage.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QCOMPARE(
        json(staleFindPage)
            .value(
                QStringLiteral(
                    "revision"))
            .toString(),
        paginationRevision);

    const Invocation address =
        find(
            QStringLiteral("0x1024"),
            {QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(address.exitCode, 0);
    const QJsonObject addressMatch =
        json(address)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject();
    QCOMPARE(
        addressMatch.value(
                        QStringLiteral(
                            "id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        addressMatch.value(
                        QStringLiteral(
                            "match_rank"))
            .toInt(),
        1);
    QCOMPARE(
        addressMatch.value(
                        QStringLiteral(
                            "match_field"))
            .toString(),
        QStringLiteral("address"));

    const Invocation exactAddress =
        find(
            QStringLiteral("0X1024"),
            {QStringLiteral("--exact"),
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--tag"),
             QStringLiteral("control"),
             QStringLiteral("--offset"),
             QStringLiteral("0"),
             QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral("--expect"),
             paginationRevision});
    QCOMPARE(exactAddress.exitCode, 0);
    const QJsonArray exactAddressResults =
        json(exactAddress)
            .value(
                QStringLiteral("result"))
            .toArray();
    QCOMPARE(exactAddressResults.size(), 1);
    QCOMPARE(
        exactAddressResults
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        exactAddressResults
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_rank"))
            .toInt(),
        1);

    const Invocation description =
        find(
            QStringLiteral(
                "main control"),
            {QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(description.exitCode, 0);
    QCOMPARE(
        json(description)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "description"))
            .toString(),
        QStringLiteral(
            "Main control."));
    QCOMPARE(
        json(description)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_field"))
            .toString(),
        QStringLiteral(
            "description"));
    const Invocation exactDescription =
        find(
            QStringLiteral(
                "main control"),
            {QStringLiteral("--exact"),
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(exactDescription.exitCode, 0);
    QVERIFY(
        json(exactDescription)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());

    const Invocation field =
        find(
            QStringLiteral("enable"),
            {QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(field.exitCode, 0);
    QCOMPARE(
        json(field)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-enable"));

    const Invocation missing =
        find(
            QStringLiteral(
                "definitely-missing"));
    QCOMPARE(missing.exitCode, 0);
    QVERIFY(
        json(missing)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());

    const Invocation optionLikeQuery =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             project,
             QStringLiteral("--"),
             QStringLiteral(
                 "--definitely-missing")});
    QCOMPARE(optionLikeQuery.exitCode, 0);
    QVERIFY(
        json(optionLikeQuery)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());

    const QByteArray beforeInvalidFilters =
        readFile(project);
    const Invocation missingParent =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--parent"),
             QStringLiteral(
                 "block-missing")});
    QCOMPARE(
        missingParent.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        json(missingParent)
            .value(
                QStringLiteral(
                    "diagnostics"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("code"))
            .toString(),
        QStringLiteral("RMC2001"));
    QVERIFY(
        json(missingParent)
            .value(
                QStringLiteral("result"))
            .toArray()
            .isEmpty());

    const Invocation missingListParent =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral(
                 "block-missing")});
    QCOMPARE(
        missingListParent.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        json(missingListParent)
            .value(
                QStringLiteral(
                    "diagnostics"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("code"))
            .toString(),
        QStringLiteral("RMC2001"));

    const Invocation duplicateListKind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(
        duplicateListKind.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidKind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral(
                 "registers")});
    QCOMPARE(
        invalidKind.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QString invalidKindError =
        json(invalidKind)
            .value(
                QStringLiteral("error"))
            .toString();
    QVERIFY(
        invalidKindError.contains(
            QStringLiteral(
                "workspace, page, block, register, field, enum, or all")));
    const Invocation duplicateFindParent =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--parent"),
             QStringLiteral(
                 "block-control"),
             QStringLiteral("--parent"),
             QStringLiteral(
                 "page-main")});
    QCOMPARE(
        duplicateFindParent.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidLimit =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--limit"),
             QStringLiteral("0")});
    QCOMPARE(
        invalidLimit.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation duplicateLimit =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--limit"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("2")});
    QCOMPARE(
        duplicateLimit.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidOffset =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--offset"),
             QStringLiteral("-1")});
    QCOMPARE(
        invalidOffset.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation duplicateOffset =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--offset"),
             QStringLiteral("0"),
             QStringLiteral("--offset"),
             QStringLiteral("1")});
    QCOMPARE(
        duplicateOffset.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidListLimit =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--limit"),
             QStringLiteral("0")});
    QCOMPARE(
        invalidListLimit.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidListOffset =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--offset"),
             QStringLiteral("-1")});
    QCOMPARE(
        invalidListOffset.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation invalidListRevision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--expect"),
             QStringLiteral("not-a-revision")});
    QCOMPARE(
        invalidListRevision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation duplicateFindRevision =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--expect"),
             paginationRevision,
             QStringLiteral("--expect"),
             paginationRevision});
    QCOMPARE(
        duplicateFindRevision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation duplicateExact =
        find(
            QStringLiteral("ctrl"),
            {QStringLiteral("--exact"),
             QStringLiteral("--exact")});
    QCOMPARE(
        duplicateExact.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(duplicateExact)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "--exact can be specified only once.")));
    QCOMPARE(
        readFile(project),
        beforeInvalidFilters);

    const Invocation text =
        invoke(
            {QStringLiteral("find"),
             project,
             QStringLiteral("0x1024"),
             QStringLiteral("--kind"),
             QStringLiteral(
                 "register")});
    QCOMPARE(text.exitCode, 0);
    QVERIFY(
        text.standardOutput
            .contains(
                QStringLiteral(
                    "register\treg-control")));
    QVERIFY(
        text.standardOutput
            .contains(
                QStringLiteral(
                    "\taddress\t1")));

    const Invocation limitedText =
        invoke(
            {QStringLiteral("find"),
             project,
             QStringLiteral("control"),
             QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(limitedText.exitCode, 0);
    QVERIFY(
        limitedText.standardOutput
            .contains(
                QStringLiteral(
                    "Showing 1 of ")));
    const Invocation listPageText =
        invoke(
            {QStringLiteral("list"),
             project,
             QStringLiteral("--offset"),
             QStringLiteral("1"),
             QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(listPageText.exitCode, 0);
    QVERIFY(
        listPageText.standardOutput
            .contains(
                QStringLiteral(
                    "Showing 1 of ")));
    QVERIFY(
        listPageText.standardOutput
            .contains(
                QStringLiteral(
                    "from offset 1")));

    const Invocation empty =
        find(QString{});
    QCOMPARE(
        empty.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::boundsListAndFindByDefault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createLargeQueryProject(
            directory.path());
    QVERIFY(!project.isEmpty());

    const Invocation boundedList =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(boundedList.exitCode, 0);
    const QJsonObject boundedListRoot =
        json(boundedList);
    QCOMPARE(
        boundedListRoot
            .value(QStringLiteral("result"))
            .toArray()
            .size(),
        100);
    const QJsonObject boundedListMetadata =
        boundedListRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject();
    QCOMPARE(
        boundedListMetadata
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        106);
    QCOMPARE(
        boundedListMetadata
            .value(QStringLiteral("limit"))
            .toInt(),
        100);
    QVERIFY(
        boundedListMetadata
            .value(
                QStringLiteral("has_more"))
            .toBool());

    const Invocation allList =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--all")});
    QCOMPARE(allList.exitCode, 0);
    const QJsonObject allListRoot =
        json(allList);
    QCOMPARE(
        allListRoot
            .value(QStringLiteral("result"))
            .toArray()
            .size(),
        106);
    QVERIFY(
        allListRoot
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(QStringLiteral("limit"))
            .isNull());

    const Invocation boundedFind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             project,
             QStringLiteral("QUERY_"),
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(boundedFind.exitCode, 0);
    QCOMPARE(
        json(boundedFind)
            .value(QStringLiteral("result"))
            .toArray()
            .size(),
        100);
    QCOMPARE(
        json(boundedFind)
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        105);

    const Invocation allFind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             project,
             QStringLiteral("QUERY_"),
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--all")});
    QCOMPARE(allFind.exitCode, 0);
    QCOMPARE(
        json(allFind)
            .value(QStringLiteral("result"))
            .toArray()
            .size(),
        105);
    QVERIFY(
        json(allFind)
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject()
            .value(QStringLiteral("limit"))
            .isNull());

    for (const QString& command :
         {QStringLiteral("list"),
          QStringLiteral("find")}) {
        QStringList arguments{
            QStringLiteral("--json"),
            command,
            project,
        };
        if (command == QStringLiteral("find")) {
            arguments.append(
                QStringLiteral("QUERY_"));
        }
        arguments
            << QStringLiteral("--all")
            << QStringLiteral("--limit")
            << QStringLiteral("1");
        const Invocation conflict =
            invoke(arguments);
        QCOMPARE(
            conflict.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        QVERIFY(
            json(conflict)
                .value(QStringLiteral("error"))
                .toString()
                .contains(
                    QStringLiteral(
                        "cannot be combined")));
    }
}

void CliTests::requiresOneFindResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    QVERIFY(!revision.isEmpty());
    const QByteArray before =
        readFile(project);

    const auto find =
        [&project](
            const QString& query,
            const QStringList& options =
                QStringList{}) {
            QStringList arguments{
                QStringLiteral("--json"),
                QStringLiteral("find"),
                project,
                query,
            };
            arguments.append(options);
            return invoke(arguments);
        };

    const Invocation one =
        find(
            QStringLiteral("0X1024"),
            {QStringLiteral("--exact"),
             QStringLiteral(
                 "--require-one"),
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--tag"),
             QStringLiteral("control"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(one.exitCode, 0);
    const QJsonObject oneRoot =
        json(one);
    QVERIFY(
        oneRoot.value(
                   QStringLiteral("ok"))
            .toBool());
    const QJsonArray oneResult =
        oneRoot.value(
                   QStringLiteral("result"))
            .toArray();
    QCOMPARE(oneResult.size(), 1);
    QCOMPARE(
        oneResult.at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("reg-control"));
    QCOMPARE(
        oneResult.at(0)
            .toObject()
            .value(
                QStringLiteral(
                    "match_field"))
            .toString(),
        QStringLiteral("address"));
    QCOMPARE(
        oneRoot.value(
                   QStringLiteral(
                       "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        1);

    const Invocation zero =
        find(
            QStringLiteral(
                "definitely-missing"),
            {QStringLiteral(
                "--require-one")});
    QCOMPARE(
        zero.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject zeroRoot =
        json(zero);
    QCOMPARE(
        zeroRoot.value(
                    QStringLiteral(
                        "error_code"))
            .toString(),
        QStringLiteral("RMC2001"));
    QVERIFY(
        zeroRoot.value(
                    QStringLiteral("result"))
            .isArray());
    QVERIFY(
        zeroRoot.value(
                    QStringLiteral("result"))
            .toArray()
            .isEmpty());
    QCOMPARE(
        zeroRoot.value(
                    QStringLiteral(
                        "result_metadata"))
            .toObject()
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        0);

    const Invocation many =
        find(
            QStringLiteral("control"),
            {QStringLiteral(
                "--require-one")});
    QCOMPARE(
        many.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject manyRoot =
        json(many);
    QCOMPARE(
        manyRoot.value(
                    QStringLiteral(
                        "error_code"))
            .toString(),
        QStringLiteral("RMC2002"));
    const QJsonArray manyResult =
        manyRoot.value(
                    QStringLiteral("result"))
            .toArray();
    const qint64 manyCount =
        static_cast<qint64>(
            manyResult.size());
    QVERIFY(manyCount > 1);
    const QJsonObject manyMetadata =
        manyRoot.value(
                    QStringLiteral(
                        "result_metadata"))
            .toObject();
    QCOMPARE(
        manyMetadata.value(
                        QStringLiteral(
                            "total_count"))
            .toInteger(),
        manyCount);
    QCOMPARE(
        manyMetadata.value(
                        QStringLiteral(
                            "returned_count"))
            .toInteger(),
        manyCount);
    QVERIFY(
        !manyMetadata.value(
                         QStringLiteral(
                             "has_more"))
             .toBool());

    const Invocation offsetConflict =
        find(
            QStringLiteral("control"),
            {QStringLiteral(
                 "--require-one"),
             QStringLiteral("--offset"),
             QStringLiteral("0")});
    QCOMPARE(
        offsetConflict.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation limitConflict =
        find(
            QStringLiteral("control"),
            {QStringLiteral(
                 "--require-one"),
             QStringLiteral("--limit"),
             QStringLiteral("1")});
    QCOMPARE(
        limitConflict.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const Invocation duplicate =
        find(
            QStringLiteral("control"),
            {QStringLiteral(
                 "--require-one"),
             QStringLiteral(
                 "--require-one")});
    QCOMPARE(
        duplicate.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));

    const QString staleRevision =
        QStringLiteral("sha256:") +
        QString(64, QLatin1Char('0'));
    QVERIFY(staleRevision != revision);
    const Invocation stale =
        find(
            QStringLiteral(
                "definitely-missing"),
            {QStringLiteral(
                 "--require-one"),
             QStringLiteral("--expect"),
             staleRevision});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    const QJsonObject staleRoot =
        json(stale);
    QCOMPARE(
        staleRoot.value(
                     QStringLiteral(
                         "error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    QVERIFY(
        staleRoot.value(
                     QStringLiteral("result"))
            .isObject());

    const Invocation textMany =
        invoke(
            {QStringLiteral("find"),
             project,
             QStringLiteral("control"),
             QStringLiteral(
                 "--require-one")});
    QCOMPARE(
        textMany.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QVERIFY(
        textMany.standardOutput
            .contains(
                QStringLiteral(
                    "block\tblock-control")));
    QVERIFY(
        textMany.standardOutput
            .contains(
                QStringLiteral(
                    "register\treg-control")));
    QVERIFY(
        textMany.standardError
            .contains(
                QStringLiteral("RMC2002")));

    QCOMPARE(readFile(project), before);
    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::queriesRecursiveObjectScopes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(directory.path());
    QVERIFY(!project.isEmpty());

    const auto add =
        [](const QString& kind,
           const QString& id,
           const QString& parent,
           QJsonObject value) {
            return QJsonObject{
                {QStringLiteral("op"),
                 QStringLiteral("add")},
                {QStringLiteral("kind"),
                 kind},
                {QStringLiteral("id"), id},
                {QStringLiteral("parent_id"),
                 parent},
                {QStringLiteral("value"),
                 std::move(value)},
            };
        };
    const QJsonArray operations{
        add(
            QStringLiteral("block"),
            QStringLiteral("block-status"),
            QStringLiteral("page-main"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("Status")},
                {QStringLiteral("base"),
                 QStringLiteral("0x200")},
                {QStringLiteral("size"),
                 QStringLiteral("0x100")},
            }),
        add(
            QStringLiteral("register"),
            QStringLiteral("reg-status"),
            QStringLiteral("block-status"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("STATUS")},
                {QStringLiteral("offset"),
                 QStringLiteral("0x0")},
                {QStringLiteral("type"),
                 QStringLiteral("field")},
            }),
        add(
            QStringLiteral("field"),
            QStringLiteral(
                "field-status-container"),
            QStringLiteral("reg-status"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral(
                     "STATUS_CONTAINER")},
                {QStringLiteral("lsb"), 0},
                {QStringLiteral("width"), 8},
                {QStringLiteral("type"),
                 QStringLiteral("field")},
            }),
        add(
            QStringLiteral("field"),
            QStringLiteral(
                "field-status-group"),
            QStringLiteral(
                "field-status-container"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral(
                     "STATUS_GROUP")},
                {QStringLiteral("lsb"), 0},
                {QStringLiteral("width"), 4},
                {QStringLiteral("type"),
                 QStringLiteral("field")},
            }),
        add(
            QStringLiteral("field"),
            QStringLiteral(
                "field-status-leaf"),
            QStringLiteral(
                "field-status-group"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("STATUS_LEAF")},
                {QStringLiteral("lsb"), 0},
                {QStringLiteral("width"), 1},
                {QStringLiteral("type"),
                 QStringLiteral("bool")},
            }),
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "recursive-scope.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            currentRevision(project)));
    const Invocation applied =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QString revision =
        json(applied)
            .value(
                QStringLiteral("revision"))
            .toString();
    QVERIFY(
        revision.startsWith(
            QStringLiteral("sha256:")));

    const auto ids =
        [](const Invocation& invocation) {
            QJsonArray result;
            for (const QJsonValue& value :
                 json(invocation)
                     .value(
                         QStringLiteral("result"))
                     .toArray()) {
                result.append(
                    value.toObject()
                        .value(
                            QStringLiteral("id")));
            }
            return result;
        };

    const Invocation directPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(directPage.exitCode, 0);
    QVERIFY(ids(directPage).isEmpty());

    const Invocation recursivePage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("register")});
    QCOMPARE(recursivePage.exitCode, 0);
    QCOMPARE(
        ids(recursivePage),
        (QJsonArray{
            QStringLiteral("reg-control"),
            QStringLiteral("reg-status")}));

    const Invocation recursiveBlock =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("block-status"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(recursiveBlock.exitCode, 0);
    QCOMPARE(
        ids(recursiveBlock),
        (QJsonArray{
            QStringLiteral(
                "field-status-container"),
            QStringLiteral(
                "field-status-group"),
            QStringLiteral(
                "field-status-leaf")}));

    const Invocation directField =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral(
                 "field-status-container"),
             QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(directField.exitCode, 0);
    QCOMPARE(
        ids(directField),
        (QJsonArray{
            QStringLiteral(
                "field-status-group")}));
    const Invocation recursiveField =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral(
                 "field-status-container"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(recursiveField.exitCode, 0);
    QCOMPARE(
        ids(recursiveField),
        (QJsonArray{
            QStringLiteral(
                "field-status-group"),
            QStringLiteral(
                "field-status-leaf")}));

    const Invocation recursiveFind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             project,
             QStringLiteral("status_leaf"),
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("field")});
    QCOMPARE(recursiveFind.exitCode, 0);
    QCOMPARE(
        ids(recursiveFind),
        (QJsonArray{
            QStringLiteral(
                "field-status-leaf")}));

    const Invocation firstPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--limit"),
             QStringLiteral("2"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(firstPage.exitCode, 0);
    const QJsonObject firstMetadata =
        json(firstPage)
            .value(
                QStringLiteral(
                    "result_metadata"))
            .toObject();
    QCOMPARE(
        firstMetadata
            .value(
                QStringLiteral(
                    "total_count"))
            .toInt(),
        4);
    QCOMPARE(ids(firstPage).size(), 2);
    QVERIFY(
        firstMetadata
            .value(
                QStringLiteral("has_more"))
            .toBool());
    QCOMPARE(
        firstMetadata
            .value(
                QStringLiteral(
                    "next_offset"))
            .toInt(),
        2);

    const Invocation secondPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--kind"),
             QStringLiteral("field"),
             QStringLiteral("--offset"),
             QStringLiteral("2"),
             QStringLiteral("--limit"),
             QStringLiteral("2"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(secondPage.exitCode, 0);
    QCOMPARE(
        ids(secondPage),
        (QJsonArray{
            QStringLiteral(
                "field-status-group"),
            QStringLiteral(
                "field-status-leaf")}));
    QVERIFY(
        !json(secondPage)
             .value(
                 QStringLiteral(
                     "result_metadata"))
             .toObject()
             .value(
                 QStringLiteral("has_more"))
             .toBool());

    const QString staleRevision =
        QStringLiteral("sha256:%1")
            .arg(
                QString(
                    64,
                    QLatin1Char('0')));
    const Invocation stale =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--expect"),
             staleRevision});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QCOMPARE(
        json(stale)
            .value(
                QStringLiteral("error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    QVERIFY(
        !json(stale).contains(
            QStringLiteral(
                "result_metadata")));

    for (const QString& command :
         {QStringLiteral("list"),
          QStringLiteral("find")}) {
        QStringList arguments{
            QStringLiteral("--json"),
            command,
            project,
        };
        if (command ==
            QStringLiteral("find")) {
            arguments.append(
                QStringLiteral("status"));
        }
        arguments.append(
            QStringLiteral("--recursive"));
        const Invocation withoutParent =
            invoke(arguments);
        QCOMPARE(
            withoutParent.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        QVERIFY(
            json(withoutParent)
                .value(
                    QStringLiteral("error"))
                .toString()
                .contains(
                    QStringLiteral(
                        "requires --parent")));
    }

    const Invocation missingParent =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             project,
             QStringLiteral("status"),
             QStringLiteral("--parent"),
             QStringLiteral("missing-parent"),
             QStringLiteral("--recursive")});
    QCOMPARE(
        missingParent.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        json(missingParent)
            .value(
                QStringLiteral("error_code"))
            .toString(),
        QStringLiteral("RMC2001"));

    const Invocation duplicate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--parent"),
             QStringLiteral("page-main"),
             QStringLiteral("--recursive"),
             QStringLiteral("--recursive")});
    QCOMPARE(
        duplicate.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(duplicate)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral("only once")));

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::previewsAndWritesReadOnlyOutputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString generated =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "generated"));
    const QString revision =
        currentRevision(project);
    QVERIFY(!revision.isEmpty());

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral(
                 "--dry-run"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(preview.exitCode, 0);
    const QJsonObject previewResult =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        previewResult.value(
                         QStringLiteral(
                             "dry_run"))
            .toBool());
    QVERIFY(
        !previewResult.value(
                          QStringLiteral(
                              "written"))
             .toBool());
    QVERIFY(
        previewResult.value(
                         QStringLiteral(
                             "outputs_current"))
            .isNull());
    QCOMPARE(
        previewResult.value(
                         QStringLiteral(
                             "generated_from_revision"))
            .toString(),
        revision);
    QCOMPARE(
        previewResult.value(
                         QStringLiteral(
                             "current_revision"))
            .toString(),
        revision);
    QCOMPARE(
        previewResult
            .value(
                QStringLiteral(
                    "artifacts"))
            .toArray()
            .size(),
        3);
    for (const QJsonValue& value :
         previewResult
             .value(
                 QStringLiteral(
                     "artifacts"))
             .toArray()) {
        const QJsonObject artifact =
            value.toObject();
        QVERIFY(
            artifact
                .value(
                    QStringLiteral("sha256"))
                .toString()
                .startsWith(
                    QStringLiteral(
                        "sha256:")));
        QCOMPARE(
            artifact
                .value(
                    QStringLiteral("written"))
                .toBool(),
            false);
        QCOMPARE(
            artifact
                .value(
                    QStringLiteral("failed"))
                .toBool(),
            false);
    }
    QVERIFY(!QDir(generated).exists());

    const Invocation targetedGenerate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--target"),
             QStringLiteral("markdown"),
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(targetedGenerate.exitCode, 0);
    const QJsonObject targetedResult =
        json(targetedGenerate)
            .value(QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        targetedResult
            .value(
                QStringLiteral(
                    "target_count"))
            .toInt(),
        1);
    const QJsonObject targetedArtifact =
        targetedResult
            .value(
                QStringLiteral(
                    "artifacts"))
            .toArray()
            .at(0)
            .toObject();
    QCOMPARE(
        targetedArtifact
            .value(QStringLiteral("kind"))
            .toString(),
        QStringLiteral("markdown"));
    QVERIFY(
        targetedArtifact
            .value(
                QStringLiteral("written"))
            .toBool());
    QVERIFY(
        QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "register-map.md"))));
    QVERIFY(
        !QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "register-map.xlsx"))));
    QVERIFY(
        !QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "device_regs.h"))));

    const Invocation targetedStatus =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project,
             QStringLiteral("--target"),
             QStringLiteral("markdown"),
             QStringLiteral(
                 "--require-current")});
    QCOMPARE(targetedStatus.exitCode, 0);
    const QJsonObject targetedStatusResult =
        json(targetedStatus)
            .value(QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        targetedStatusResult
            .value(
                QStringLiteral(
                    "output_count"))
            .toInt(),
        1);
    QVERIFY(
        targetedStatusResult
            .value(
                QStringLiteral(
                    "outputs_current"))
            .toBool());

    const Invocation generate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(generate.exitCode, 0);
    const QJsonObject generatedResult =
        json(generate)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        generatedResult.value(
                           QStringLiteral(
                               "written"))
            .toBool());
    QVERIFY(
        generatedResult.value(
                           QStringLiteral(
                              "outputs_current"))
            .toBool());
    QCOMPARE(
        generatedResult
            .value(
                QStringLiteral(
                    "artifacts"))
            .toArray()
            .size(),
        3);
    QVERIFY(
        std::ranges::any_of(
            generatedResult
                .value(
                    QStringLiteral(
                        "artifacts"))
                .toArray(),
            [](const QJsonValue& value) {
                return value.toObject()
                           .value(
                               QStringLiteral(
                                   "skipped"))
                           .toBool();
            }));
    QVERIFY(
        QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "register-map.xlsx"))));
    QVERIFY(
        QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "device_regs.h"))));
    QVERIFY(
        QFileInfo::exists(
            QDir(generated).filePath(
                QStringLiteral(
                    "register-map.md"))));

    const QString workbook =
        QDir(generated).filePath(
            QStringLiteral(
                "register-map.xlsx"));
    const QString header =
        QDir(generated).filePath(
            QStringLiteral(
                "device_regs.h"));
    const QString markdown =
        QDir(generated).filePath(
            QStringLiteral(
                "register-map.md"));
    const QByteArray workbookBefore =
        readFile(workbook);
    const QByteArray headerBefore =
        readFile(header);
    const QByteArray markdownBefore =
        readFile(markdown);
    const Invocation stale =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             QStringLiteral("sha256:") +
                 QString(
                     64,
                     QLatin1Char('0'))});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    const QJsonObject staleRoot =
        json(stale);
    QVERIFY(
        !staleRoot.value(
                      QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        staleRoot.value(
                     QStringLiteral(
                         "exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QCOMPARE(
        staleRoot.value(
                     QStringLiteral(
                         "exit_status"))
            .toString(),
        QStringLiteral(
            "revision_conflict"));
    QCOMPARE(
        staleRoot.value(
                     QStringLiteral(
                         "error_code"))
            .toString(),
        QStringLiteral("RMC3001"));
    const QJsonObject staleResult =
        staleRoot.value(
                      QStringLiteral(
                          "result"))
            .toObject();
    QVERIFY(
        !staleResult.value(
                         QStringLiteral(
                             "written"))
             .toBool());
    QVERIFY(
        !staleResult.value(
                         QStringLiteral(
                             "outputs_current"))
             .toBool());
    QCOMPARE(
        readFile(workbook),
        workbookBefore);
    QCOMPARE(
        readFile(header),
        headerBefore);
    QCOMPARE(
        readFile(markdown),
        markdownBefore);

    const Invocation invalidRevision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             QStringLiteral(
                 "not-a-revision")});
    QCOMPARE(
        invalidRevision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::reportsAndRepairsOutputStatusWithoutRewritingCurrentFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString generated =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "generated"));
    const QString workbook =
        QDir(generated).filePath(
            QStringLiteral(
                "register-map.xlsx"));
    const QString header =
        QDir(generated).filePath(
            QStringLiteral(
                "device_regs.h"));
    const QString markdown =
        QDir(generated).filePath(
            QStringLiteral(
                "register-map.md"));

    const auto artifactForKind =
        [](const QJsonArray& artifacts,
           const QString& kind) {
            for (const auto& value :
                 artifacts) {
                const QJsonObject artifact =
                    value.toObject();
                if (artifact.value(
                                QStringLiteral(
                                    "kind"))
                        .toString() ==
                    kind) {
                    return artifact;
                }
            }
            return QJsonObject{};
        };

    const Invocation missing =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project});
    QCOMPARE(missing.exitCode, 0);
    const QJsonObject missingResult =
        json(missing)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !missingResult.value(
                          QStringLiteral(
                              "outputs_current"))
             .toBool());
    QCOMPARE(
        missingResult.value(
                         QStringLiteral(
                             "output_count"))
            .toInt(),
        3);
    QCOMPARE(
        missingResult.value(
                         QStringLiteral(
                             "synchronized_count"))
            .toInt(),
        0);
    QCOMPARE(
        artifactForKind(
            missingResult.value(
                             QStringLiteral(
                                 "artifacts"))
                .toArray(),
            QStringLiteral("xlsx"))
            .value(
                QStringLiteral("state"))
            .toString(),
        QStringLiteral("missing"));
    const QJsonObject missingRecovery =
        missingResult.value(
                         QStringLiteral(
                             "recovery"))
            .toObject();
    QCOMPARE(
        missingRecovery.value(
                           QStringLiteral(
                               "command"))
            .toString(),
        QStringLiteral("generate"));
    QCOMPARE(
        missingRecovery.value(
                           QStringLiteral(
                               "project"))
            .toString(),
        QDir::toNativeSeparators(
            QFileInfo(project)
                .absoluteFilePath()));
    const QString recoveryRevision =
        missingRecovery.value(
                           QStringLiteral(
                               "expected_revision"))
            .toString();
    QVERIFY(
        recoveryRevision.startsWith(
            QStringLiteral(
                "sha256:")));
    const QJsonArray
        expectedRecoveryArguments{
            QStringLiteral(
                "generate"),
            QDir::toNativeSeparators(
                QFileInfo(project)
                    .absoluteFilePath()),
            QStringLiteral(
                "--expect"),
            recoveryRevision};
    QCOMPARE(
        missingRecovery.value(
                           QStringLiteral(
                               "arguments"))
            .toArray(),
        expectedRecoveryArguments);
    const Invocation strictMissing =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project,
             QStringLiteral(
                 "--require-current")});
    QCOMPARE(
        strictMissing.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                outputsOutOfDate));
    const QJsonObject strictMissingRoot =
        json(strictMissing);
    QVERIFY(
        !strictMissingRoot
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        strictMissingRoot
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral(
            "outputs_out_of_date"));
    QCOMPARE(
        strictMissingRoot
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC5001"));
    QCOMPARE(
        strictMissingRoot
            .value(
                QStringLiteral("result")),
        missingResult);
    QVERIFY(!QFileInfo::exists(workbook));
    QVERIFY(!QFileInfo::exists(header));
    QVERIFY(!QFileInfo::exists(markdown));

    for (const QStringList& invalidArguments :
         {QStringList{
              QStringLiteral("--json"),
              QStringLiteral("status"),
              project,
              QStringLiteral(
                  "--require-current"),
              QStringLiteral(
                  "--require-current")},
          QStringList{
              QStringLiteral("--json"),
              QStringLiteral("status"),
              project,
              QStringLiteral(
                  "--unknown")}}) {
        const Invocation invalid =
            invoke(invalidArguments);
        QCOMPARE(
            invalid.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        QCOMPARE(
            json(invalid)
                .value(
                    QStringLiteral(
                        "exit_status"))
                .toString(),
            QStringLiteral(
                "usage_error"));
    }

    const QString revision =
        currentRevision(project);
    QCOMPARE(
        recoveryRevision,
        revision);
    const Invocation generatedOutputs =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(
        generatedOutputs.exitCode,
        0);

    const Invocation synchronized =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project});
    QCOMPARE(
        synchronized.exitCode,
        0);
    const QJsonObject synchronizedResult =
        json(synchronized)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        synchronizedResult.value(
                              QStringLiteral(
                                  "outputs_current"))
            .toBool());
    QCOMPARE(
        synchronizedResult.value(
                              QStringLiteral(
                                  "synchronized_count"))
            .toInt(),
        3);
    QCOMPARE(
        synchronizedResult.value(
                              QStringLiteral(
                                  "checked_revision"))
            .toString(),
        revision);
    QVERIFY(
        !synchronizedResult.contains(
            QStringLiteral(
                "recovery")));
    const Invocation strictSynchronized =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project,
             QStringLiteral(
                 "--require-current")});
    QCOMPARE(
        strictSynchronized.exitCode,
        0);
    QVERIFY(
        json(strictSynchronized)
            .value(
                QStringLiteral("ok"))
            .toBool());

    const QDateTime workbookModified =
        QFileInfo(workbook)
            .lastModified();
    QVERIFY(workbookModified.isValid());
    QTest::qWait(25);
#ifdef Q_OS_WIN
    const HANDLE lockedWorkbook =
        CreateFileW(
            reinterpret_cast<LPCWSTR>(
                workbook.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    QVERIFY(
        lockedWorkbook !=
        INVALID_HANDLE_VALUE);
#endif
    const Invocation unchangedGenerate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             revision});
#ifdef Q_OS_WIN
    CloseHandle(lockedWorkbook);
#endif
    QCOMPARE(
        unchangedGenerate.exitCode,
        0);
    QCOMPARE(
        QFileInfo(workbook)
            .lastModified(),
        workbookModified);

    QVERIFY(
        QFile::setPermissions(
            workbook,
            QFileDevice::ReadOwner |
                QFileDevice::WriteOwner));
    QVERIFY(
        QFile::setPermissions(
            header,
            QFileDevice::ReadOwner |
                QFileDevice::WriteOwner));
    QFile modifiedHeader(header);
    QVERIFY(
        modifiedHeader.open(
            QIODevice::WriteOnly |
            QIODevice::Truncate));
    QCOMPARE(
        modifiedHeader.write(
            QByteArrayLiteral(
                "externally modified\n")),
        qint64{20});
    modifiedHeader.close();
    QVERIFY(
        QFile::setPermissions(
            markdown,
            QFileDevice::ReadOwner |
                QFileDevice::WriteOwner));
    QVERIFY(QFile::remove(markdown));

    const Invocation attention =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project});
    QCOMPARE(attention.exitCode, 0);
    const QJsonObject attentionResult =
        json(attention)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !attentionResult.value(
                            QStringLiteral(
                                "outputs_current"))
             .toBool());
    QCOMPARE(
        attentionResult.value(
                            QStringLiteral(
                                "attention_count"))
            .toInt(),
        3);
    const QJsonArray attentionArtifacts =
        attentionResult.value(
                           QStringLiteral(
                               "artifacts"))
            .toArray();
    QCOMPARE(
        artifactForKind(
            attentionArtifacts,
            QStringLiteral("xlsx"))
            .value(
                QStringLiteral("state"))
            .toString(),
        QStringLiteral("writable"));
    QCOMPARE(
        artifactForKind(
            attentionArtifacts,
            QStringLiteral("c-header"))
            .value(
                QStringLiteral("state"))
            .toString(),
        QStringLiteral("modified"));
    QCOMPARE(
        artifactForKind(
            attentionArtifacts,
            QStringLiteral("markdown"))
            .value(
                QStringLiteral("state"))
            .toString(),
        QStringLiteral("missing"));

    const Invocation textStatus =
        invoke(
            {QStringLiteral("status"),
             project});
    QCOMPARE(textStatus.exitCode, 0);
    QVERIFY(
        textStatus.standardOutput
            .contains(
                QStringLiteral(
                    "Outputs: 0/3 synchronized")));
    QVERIFY(
        textStatus.standardOutput
            .contains(
                QStringLiteral(
                    "writable\txlsx")));
    QVERIFY(
        textStatus.standardError
            .contains(
                QStringLiteral(
                    "Recovery: regmapc generate")));
    QVERIFY(
        textStatus.standardError
            .contains(
                QStringLiteral(
                    "--expect %1")
                    .arg(revision)));
    const Invocation strictTextStatus =
        invoke(
            {QStringLiteral("status"),
             project,
             QStringLiteral(
                 "--require-current")});
    QCOMPARE(
        strictTextStatus.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                outputsOutOfDate));
    QVERIFY(
        strictTextStatus.standardOutput
            .contains(
                QStringLiteral(
                    "Outputs: 0/3 synchronized")));
    QVERIFY(
        strictTextStatus.standardError
            .contains(
                QStringLiteral(
                    "ERROR RMC5001")));

    const Invocation repair =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--expect"),
             revision});
    QCOMPARE(repair.exitCode, 0);
    const Invocation repairedStatus =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project});
    QCOMPARE(repairedStatus.exitCode, 0);
    QVERIFY(
        json(repairedStatus)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "outputs_current"))
            .toBool());
    QVERIFY(
        (QFile::permissions(
             workbook) &
         QFileDevice::WriteOwner) == 0);
    QVERIFY(
        (QFile::permissions(
             header) &
         QFileDevice::WriteOwner) == 0);
    QVERIFY(
        (QFile::permissions(
             markdown) &
         QFileDevice::WriteOwner) == 0);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::keepsXlsxStatusStableAcrossProcesses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());

    const Invocation generate =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project});
    if (generate.exitCode != 0) {
        qWarning().noquote()
            << generate.standardError
            << generate.standardOutput;
    }
    QCOMPARE(generate.exitCode, 0);

    const auto verifySynchronized =
        [&](const Invocation& status) {
            if (status.exitCode != 0) {
                qWarning().noquote()
                    << status.standardError
                    << status.standardOutput;
            }
            QCOMPARE(status.exitCode, 0);
            const QJsonObject result =
                json(status)
                    .value(
                        QStringLiteral(
                            "result"))
                    .toObject();
            QVERIFY(
                result.value(
                          QStringLiteral(
                              "outputs_current"))
                    .toBool());
            for (const auto& value :
                 result.value(
                           QStringLiteral(
                               "artifacts"))
                     .toArray()) {
                QCOMPARE(
                    value.toObject()
                        .value(
                            QStringLiteral(
                                "state"))
                        .toString(),
                    QStringLiteral(
                        "synchronized"));
            }
        };

    verifySynchronized(
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project}));
    QTest::qWait(2200);
    verifySynchronized(
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project}));

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::runsCoreCommandsWithoutAPlatformPlugin()
{
    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    environment.insert(
        QStringLiteral("QT_QPA_PLATFORM"),
        QStringLiteral(
            "regmapc-invalid-platform"));

    const Invocation version =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("version")},
            environment);
    QCOMPARE(version.exitCode, 0);
    QVERIFY(version.standardError.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const Invocation generate =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project,
             QStringLiteral("--target"),
             QStringLiteral("markdown"),
             QStringLiteral("--dry-run")},
            environment);
    QCOMPARE(generate.exitCode, 0);
    QVERIFY(generate.standardError.isEmpty());
    QCOMPARE(
        json(generate)
            .value(QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "target_count"))
            .toInt(),
        1);

    const Invocation status =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project,
             QStringLiteral("--target"),
             QStringLiteral("markdown")},
            environment);
    QCOMPARE(status.exitCode, 0);
    QVERIFY(status.standardError.isEmpty());
}

void CliTests::supportsUnicodeProjectPathsAcrossProcesses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString projectDirectory =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "可用性 验收"));
    QVERIFY(
        QDir().mkpath(
            projectDirectory));
    const QString project =
        QDir(projectDirectory)
            .filePath(
                QStringLiteral(
                    "设备寄存器.regmap.yaml"));

    const Invocation initialized =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             project,
             QStringLiteral("--name"),
             QStringLiteral(
                 "设备寄存器")});
    if (initialized.exitCode != 0) {
        qWarning().noquote()
            << initialized.standardError
            << initialized.standardOutput;
    }
    QCOMPARE(initialized.exitCode, 0);
    QCOMPARE(
        json(initialized)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "workspace_name"))
            .toString(),
        QStringLiteral(
            "设备寄存器"));

    const Invocation summary =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    QCOMPARE(summary.exitCode, 0);
    const QJsonObject summaryRoot =
        json(summary);
    QCOMPARE(
        QFileInfo(
            summaryRoot.value(
                           QStringLiteral(
                               "project"))
                .toString())
            .canonicalFilePath(),
        QFileInfo(project)
            .canonicalFilePath());
    QCOMPARE(
        summaryRoot.value(
                       QStringLiteral(
                           "result"))
            .toObject()
            .value(
                QStringLiteral(
                    "workspace_name"))
            .toString(),
        QStringLiteral(
            "设备寄存器"));
    const QJsonArray targets =
        summaryRoot.value(
                       QStringLiteral(
                           "result"))
            .toObject()
            .value(
                QStringLiteral(
                    "generation_targets"))
            .toArray();
    const auto header =
        std::ranges::find_if(
            targets,
            [](const QJsonValue& value) {
                return value.toObject()
                           .value(
                               QStringLiteral(
                                   "kind"))
                           .toString() ==
                    QStringLiteral(
                        "c-header");
            });
    QVERIFY(header != targets.end());
    QCOMPARE(
        QFileInfo(
            header->toObject()
                .value(
                    QStringLiteral("path"))
                .toString())
            .fileName(),
        QStringLiteral(
            "register_map_regs.h"));

    const QString patchPath =
        QDir(projectDirectory)
            .filePath(
                QStringLiteral(
                    "新增页面.json"));
    QVERIFY(
        writePatch(
            patchPath,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "parent_id"),
                     summaryRoot.value(
                                    QStringLiteral(
                                        "result"))
                         .toObject()
                         .value(
                             QStringLiteral(
                                 "workspace_id"))
                         .toString()},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "主页面")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x0")},
                     }},
                },
            },
            summaryRoot.value(
                           QStringLiteral(
                               "revision"))
                .toString()));
    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patchPath});
    if (applied.exitCode != 0) {
        qWarning().noquote()
            << applied.standardError
            << applied.standardOutput;
    }
    QCOMPARE(applied.exitCode, 0);

    const Invocation reopened =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    QCOMPARE(reopened.exitCode, 0);
    QCOMPARE(
        json(reopened)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("counts"))
            .toObject()
            .value(
                QStringLiteral("pages"))
            .toInt(),
        1);
    const Invocation status =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("status"),
             project});
    QCOMPARE(status.exitCode, 0);
    QVERIFY(
        json(status)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "outputs_current"))
            .toBool());

    makeGeneratedFilesWritable(
        projectDirectory);
}

void CliTests::returnsStableUsageAndProjectErrors()
{
    const Invocation usage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("unknown")});
    QCOMPARE(
        usage.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject usageRoot =
        json(usage);
    QVERIFY(
        !usageRoot.value(
                      QStringLiteral("ok"))
             .toBool());
    QVERIFY(
        usageRoot.contains(
            QStringLiteral("error")));
    QCOMPARE(
        usageRoot.value(
                     QStringLiteral(
                         "exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        usageRoot.value(
                     QStringLiteral(
                         "exit_status"))
            .toString(),
        QStringLiteral("usage_error"));
    QCOMPARE(
        usageRoot.value(
                     QStringLiteral(
                         "error_code"))
            .toString(),
        QStringLiteral("RMC1000"));
    QVERIFY(
        usageRoot.value(
                     QStringLiteral("usage"))
            .toString()
            .contains(
                QStringLiteral(
                    "regmapc [--json] validate")));
    QVERIFY(
        usageRoot.value(
                     QStringLiteral("result"))
            .isNull());

    const Invocation typo =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validte"),
             QStringLiteral(
                 "project.regmap.yaml")});
    QCOMPARE(
        typo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject typoRoot =
        json(typo);
    QCOMPARE(
        typoRoot
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "suggested_command"))
            .toString(),
        QStringLiteral("validate"));
    QVERIFY(
        typoRoot
            .value(QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "Did you mean 'validate'?")));

    const Invocation textTypo =
        invoke(
            {QStringLiteral("genrate"),
             QStringLiteral(
                 "project.regmap.yaml")});
    QCOMPARE(
        textTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        textTypo.standardOutput
            .isEmpty());
    QVERIFY(
        textTypo.standardError
            .contains(
                QStringLiteral(
                    "Did you mean 'generate'?")));

    const Invocation optionTypo =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             QStringLiteral(
                 "project.regmap.yaml"),
             QStringLiteral(
                 "--dryrun")});
    QCOMPARE(
        optionTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(optionTypo)
            .value(QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "Did you mean '--dry-run'?")));
    QCOMPARE(
        json(optionTypo)
            .value(QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "suggested_option"))
            .toString(),
        QStringLiteral("--dry-run"));

    const Invocation findOptionTypo =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("find"),
             QStringLiteral(
                 "project.regmap.yaml"),
             QStringLiteral("query"),
             QStringLiteral("--lmit"),
             QStringLiteral("1")});
    QCOMPARE(
        findOptionTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        json(findOptionTypo)
            .value(QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "suggested_option"))
            .toString(),
        QStringLiteral("--limit"));

    const Invocation unrelatedOption =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             QStringLiteral(
                 "project.regmap.yaml"),
             QStringLiteral(
                 "--banana")});
    QCOMPARE(
        unrelatedOption.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(unrelatedOption)
            .value(QStringLiteral("result"))
            .isNull());

    const Invocation textOptionTypo =
        invoke(
            {QStringLiteral("status"),
             QStringLiteral(
                 "project.regmap.yaml"),
             QStringLiteral(
                 "--require-curent")});
    QCOMPARE(
        textOptionTypo.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        textOptionTypo.standardError
            .contains(
                QStringLiteral(
                    "Did you mean '--require-current'?")));

    const Invocation missingArgument =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary")});
    QCOMPARE(
        missingArgument.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject missingArgumentRoot =
        json(missingArgument);
    QCOMPARE(
        missingArgumentRoot.value(
                               QStringLiteral(
                                   "usage"))
            .toString(),
        QStringLiteral(
            "regmapc [--json] summary <project.regmap.yaml>"));
    QVERIFY(
        missingArgumentRoot.value(
                               QStringLiteral(
                                   "error"))
            .toString()
            .contains(
                QStringLiteral(
                    "exactly one project path")));

    const Invocation textMissingArgument =
        invoke(
            {QStringLiteral("summary")});
    QCOMPARE(
        textMissingArgument.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        textMissingArgument.standardOutput
            .isEmpty());
    QCOMPARE(
        textMissingArgument.standardError,
        QStringLiteral(
            "summary requires exactly one project path.\n"
            "Usage: regmapc [--json] summary <project.regmap.yaml>\n"));

    for (const QString& versionToken :
         {QStringLiteral("version"),
          QStringLiteral("--version")}) {
        const Invocation extraVersionArgument =
            invoke(
                {QStringLiteral("--json"),
                 versionToken,
                 QStringLiteral("extra")});
        QCOMPARE(
            extraVersionArgument.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        const QJsonObject extraVersionRoot =
            json(extraVersionArgument);
        QCOMPARE(
            extraVersionRoot.value(
                                 QStringLiteral(
                                     "command"))
                .toString(),
            QStringLiteral("version"));
        QCOMPARE(
            extraVersionRoot.value(
                                 QStringLiteral(
                                     "usage"))
                .toString(),
            QStringLiteral(
                "regmapc [--json] version"));
        QVERIFY(
            extraVersionRoot.value(
                                 QStringLiteral(
                                     "error"))
                .toString()
                .contains(
                    QStringLiteral(
                        "accepts no positional arguments")));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing =
        directory.filePath(
            QStringLiteral(
                "missing.regmap.yaml"));
    const QString initTarget =
        directory.filePath(
            QStringLiteral(
                "missing-value.regmap.yaml"));
    const QString missingPatch =
        directory.filePath(
            QStringLiteral(
                "missing-patch.json"));
    struct MissingOptionCase {
        QString command;
        QStringList arguments;
        QString error;
    };
    const std::vector<MissingOptionCase>
        missingOptionCases{
            {
                QStringLiteral("init"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("init"),
                    initTarget,
                    QStringLiteral("--name"),
                    QStringLiteral(
                        "--no-generate"),
                },
                QStringLiteral(
                    "--name requires a value."),
            },
            {
                QStringLiteral("init"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("init"),
                    initTarget,
                    QStringLiteral(
                        "--workspace-id"),
                    QStringLiteral(
                        "--no-generate"),
                },
                QStringLiteral(
                    "--workspace-id requires a value."),
            },
            {
                QStringLiteral("list"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("list"),
                    missing,
                    QStringLiteral("--kind"),
                    QStringLiteral("--parent"),
                    QStringLiteral("block-control"),
                },
                QStringLiteral(
                    "--kind requires a value."),
            },
            {
                QStringLiteral("list"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("list"),
                    missing,
                    QStringLiteral("--offset"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--offset requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--kind"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--kind requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--parent"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--parent requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--tag"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--tag requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--limit"),
                    QStringLiteral("--kind"),
                    QStringLiteral("register"),
                },
                QStringLiteral(
                    "--limit requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--offset"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--offset requires a value."),
            },
            {
                QStringLiteral("find"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("find"),
                    missing,
                    QStringLiteral("control"),
                    QStringLiteral("--expect"),
                    QStringLiteral("--limit"),
                    QStringLiteral("1"),
                },
                QStringLiteral(
                    "--expect requires a revision."),
            },
            {
                QStringLiteral("generate"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("generate"),
                    missing,
                    QStringLiteral("--expect"),
                    QStringLiteral("--dry-run"),
                },
                QStringLiteral(
                    "--expect requires a revision."),
            },
            {
                QStringLiteral("apply"),
                {
                    QStringLiteral("--json"),
                    QStringLiteral("apply"),
                    missing,
                    missingPatch,
                    QStringLiteral("--expect"),
                    QStringLiteral("--force"),
                },
                QStringLiteral(
                    "--expect requires a revision value."),
            },
        };
    for (const MissingOptionCase& testCase :
         missingOptionCases) {
        const Invocation invocation =
            invoke(testCase.arguments);
        QCOMPARE(
            invocation.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        const QJsonObject root =
            json(invocation);
        QCOMPARE(
            root.value(
                    QStringLiteral("command"))
                .toString(),
            testCase.command);
        QCOMPARE(
            root.value(
                    QStringLiteral("error"))
                .toString(),
            testCase.error);
        QVERIFY(
            root.value(
                    QStringLiteral("usage"))
                .toString()
                .startsWith(
                    QStringLiteral(
                        "regmapc [--json] %1")
                        .arg(
                            testCase.command)));
    }
    QVERIFY(
        !QFileInfo::exists(
            initTarget));
    QVERIFY(
        !QFileInfo::exists(
            missingPatch));

    const Invocation missingPatchInput =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             missing,
             missingPatch,
             QStringLiteral("--dry-run")});
    QCOMPARE(
        missingPatchInput.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                inputError));
    const QJsonObject missingPatchRoot =
        json(missingPatchInput);
    QCOMPARE(
        missingPatchRoot
            .value(
                QStringLiteral(
                    "exit_status"))
            .toString(),
        QStringLiteral("input_error"));
    QCOMPARE(
        missingPatchRoot
            .value(
                QStringLiteral(
                    "error_code"))
            .toString(),
        QStringLiteral("RMC7000"));
    QCOMPARE(
        missingPatchRoot
            .value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("stage"))
            .toString(),
        QStringLiteral("input"));

    const Invocation project =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             missing});
    QCOMPARE(
        project.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject projectRoot =
        json(project);
    QVERIFY(
        !projectRoot.value(
                        QStringLiteral("ok"))
             .toBool());
    QVERIFY(
        !projectRoot
             .value(
                 QStringLiteral(
                     "diagnostics"))
             .toArray()
             .isEmpty());
    QCOMPARE(
        projectRoot.value(
                       QStringLiteral(
                           "exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        projectRoot.value(
                       QStringLiteral(
                           "exit_status"))
            .toString(),
        QStringLiteral(
            "project_error"));
    const QJsonArray diagnostics =
        projectRoot.value(
                       QStringLiteral(
                           "diagnostics"))
            .toArray();
    QCOMPARE(
        projectRoot.value(
                       QStringLiteral(
                           "error_code"))
            .toString(),
        diagnostics.at(0)
            .toObject()
            .value(
                QStringLiteral("code"))
            .toString());
}

void CliTests::previewsAtomicPatchWithoutWriting()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    QVERIFY(
        revision.startsWith(
            QStringLiteral(
                "sha256:")));
    const QByteArray original =
        readFile(project);
    QVERIFY(!original.isEmpty());

    const QString patchPath =
        directory.filePath(
            QStringLiteral(
                "preview.json"));
    QVERIFY(
        writePatch(
            patchPath,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Previewed description.")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("tags")},
                    {QStringLiteral(
                         "value"),
                     QJsonArray{
                         QStringLiteral(
                             "control"),
                         QStringLiteral(
                             "startup")}},
                },
            }));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patchPath,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    const QJsonObject root =
        json(preview);
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "revision"))
            .toString(),
        revision);
    const QJsonObject result =
        root.value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "dry_run"))
            .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "changed"))
            .toBool());
    QVERIFY(
        !result.value(
                   QStringLiteral("saved"))
             .toBool());
    QVERIFY(
        !result.value(
                   QStringLiteral(
                       "generated"))
             .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "generation_previewed"))
            .toBool());
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "changes"))
            .toArray()
            .size(),
        2);
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "artifacts"))
            .toArray()
            .size(),
        3);
    QCOMPARE(
        readFile(project),
        original);
    QCOMPARE(
        currentRevision(project),
        revision);
    QVERIFY(
        !QDir(
             directory.filePath(
                 QStringLiteral(
                     "generated")))
             .exists());

    const Invocation get =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control")});
    QCOMPARE(get.exitCode, 0);
    QCOMPARE(
        json(get)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "description"))
            .toString(),
        QStringLiteral(
            "Main control."));
}

void CliTests::appliesRevisionGuardedPatchAndRegenerates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    QVERIFY(!revision.isEmpty());

    const QString patchPath =
        directory.filePath(
            QStringLiteral(
                "apply.json"));
    QVERIFY(
        writePatch(
            patchPath,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Applied description.")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "initial")},
                    {QStringLiteral(
                         "value"),
                     QJsonValue(
                         QJsonValue::Null)},
                },
            },
            revision));

    const Invocation apply =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patchPath});
    QCOMPARE(apply.exitCode, 0);
    const QJsonObject root =
        json(apply);
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    const QString newRevision =
        root.value(
                QStringLiteral(
                    "revision"))
            .toString();
    QVERIFY(!newRevision.isEmpty());
    QVERIFY(newRevision != revision);
    const QJsonObject result =
        root.value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        result.value(
                  QStringLiteral("saved"))
            .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "generated"))
            .toBool());
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "previous_revision"))
            .toString(),
        revision);
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "new_revision"))
            .toString(),
        newRevision);

    const Invocation get =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control")});
    QCOMPARE(get.exitCode, 0);
    const QJsonObject reg =
        json(get)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        reg.value(
               QStringLiteral(
                   "description"))
            .toString(),
        QStringLiteral(
            "Applied description."));
    QVERIFY(
        reg.value(
               QStringLiteral("initial"))
            .isNull());

    const QDir generated(
        directory.filePath(
            QStringLiteral(
                "generated")));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "register-map.xlsx"))));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "device_regs.h"))));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "register-map.md"))));
    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::rejectsUnsafePatchesWithoutPartialWrites()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    const QByteArray original =
        readFile(project);

    const QString stalePatch =
        directory.filePath(
            QStringLiteral(
                "stale.json"));
    QVERIFY(
        writePatch(
            stalePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Must not persist.")},
                }},
            QStringLiteral(
                "sha256:0000000000000000000000000000000000000000000000000000000000000000")));
    const Invocation stale =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             stalePatch});
    QCOMPARE(
        stale.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                revisionConflict));
    QVERIFY(
        !json(stale)
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        readFile(project),
        original);

    const QString invalidPatch =
        directory.filePath(
            QStringLiteral(
                "invalid.json"));
    QVERIFY(
        writePatch(
            invalidPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Partial write must not occur.")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "source")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "must-not-change")},
                },
            },
            revision));
    const Invocation invalid =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidPatch});
    QCOMPARE(
        invalid.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject invalidRoot =
        json(invalid);
    QVERIFY(
        !invalidRoot.value(
                        QStringLiteral("ok"))
             .toBool());
    QVERIFY(
        invalidRoot.value(
                       QStringLiteral(
                           "error"))
            .toString()
            .contains(
                QStringLiteral(
                    "not writable"),
                Qt::CaseInsensitive));
    QCOMPARE(
        readFile(project),
        original);
    QCOMPARE(
        currentRevision(project),
        revision);

    const QString invalidModelPatch =
        directory.filePath(
            QStringLiteral(
                "invalid-model.json"));
    QVERIFY(
        writePatch(
            invalidModelPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("width")},
                    {QStringLiteral(
                         "value"),
                     0},
                }},
            revision));
    const Invocation invalidModel =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidModelPatch});
    QCOMPARE(
        invalidModel.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject invalidModelRoot =
        json(invalidModel);
    QVERIFY(
        !invalidModelRoot
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QVERIFY(
        !invalidModelRoot
             .value(
                 QStringLiteral(
                     "diagnostics"))
             .toArray()
             .isEmpty());
    QCOMPARE(
        readFile(project),
        original);

    const QString unguardedPatch =
        directory.filePath(
            QStringLiteral(
                "unguarded.json"));
    QVERIFY(
        writePatch(
            unguardedPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Not guarded.")},
                }}));
    const Invocation unguarded =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             unguardedPatch});
    QCOMPARE(
        unguarded.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        readFile(project),
        original);
}

void CliTests::rejectsIndependentFieldResetWrites()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray original =
        readFile(project);

    const std::vector<QJsonObject> operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("property"),
             QStringLiteral("reset")},
            {QStringLiteral("value"),
             QStringLiteral("0x0")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral("field-extra")},
            {QStringLiteral("parent_id"),
             QStringLiteral("reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("EXTRA")},
                 {QStringLiteral("lsb"), 1},
                 {QStringLiteral("width"), 1},
                 {QStringLiteral("reset"),
                  QStringLiteral("0x0")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "field-enable-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("lsb"), 2},
                 {QStringLiteral("reset"),
                  QStringLiteral("0x0")},
             }},
        },
    };

    for (const QJsonObject& operation :
         operations) {
        const Invocation rejected =
            invoke(
                {QStringLiteral("--json"),
                 QStringLiteral("apply"),
                 project,
                 QStringLiteral("-"),
                 QStringLiteral("--dry-run")},
                patchText(
                    QJsonArray{operation}));
        QCOMPARE(
            rejected.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        QVERIFY(
            json(rejected)
                .value(QStringLiteral("error"))
                .toString()
                .contains(
                    QStringLiteral(
                        "parent Register")));
        QCOMPARE(
            readFile(project),
            original);
    }

    const QJsonArray copyToResetlessRegister{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("reg-resetless")},
            {QStringLiteral("parent_id"),
             QStringLiteral("block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("RESETLESS")},
                 {QStringLiteral("offset"),
                  QStringLiteral("0x8")},
                 {QStringLiteral("type"),
                  QStringLiteral("field")},
                 {QStringLiteral("reset"),
                  QJsonValue(
                      QJsonValue::Null)},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "field-resetless-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("reg-resetless")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("lsb"), 0},
             }},
        },
    };
    const Invocation copied =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                copyToResetlessRegister,
                currentRevision(project)));
    QCOMPARE(copied.exitCode, 0);
    const Invocation copiedField =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-resetless-copy")});
    QCOMPARE(copiedField.exitCode, 0);
    QVERIFY(
        json(copiedField)
            .value(QStringLiteral("result"))
            .toObject()
            .value(QStringLiteral("reset"))
            .isNull());

    const auto setRegisterReset =
        [&project](const QJsonValue& value) {
            const QJsonArray patch{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral("reg-control")},
                    {QStringLiteral("property"),
                     QStringLiteral("reset")},
                    {QStringLiteral("value"),
                     value},
                }};
            return invoke(
                {QStringLiteral("--json"),
                 QStringLiteral("apply"),
                 project,
                 QStringLiteral("-")},
                patchText(
                    patch,
                    currentRevision(project)));
        };
    const auto fieldReset =
        [&project]() {
            const Invocation get =
                invoke(
                    {QStringLiteral("--json"),
                     QStringLiteral("get"),
                     project,
                     QStringLiteral(
                         "field-enable")});
            if (get.exitCode != 0) {
                return QJsonValue{};
            }
            return json(get)
                .value(QStringLiteral("result"))
                .toObject()
                .value(QStringLiteral("reset"));
        };

    const Invocation cleared =
        setRegisterReset(
            QJsonValue(QJsonValue::Null));
    QCOMPARE(cleared.exitCode, 0);
    QVERIFY(fieldReset().isNull());

    const Invocation restored =
        setRegisterReset(
            QStringLiteral("0x1"));
    QCOMPARE(restored.exitCode, 0);
    QCOMPARE(
        fieldReset().toString(),
        QStringLiteral("0x1"));
    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::reportsStructuredPrewriteFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto verifyDiagnosticIndexes =
        [](const QJsonObject& failure,
           const QJsonArray& diagnostics) {
            const QJsonArray indexes =
                failure.value(
                           QStringLiteral(
                               "error_diagnostic_indexes"))
                    .toArray();
            if (indexes.isEmpty()) {
                return false;
            }
            for (const QJsonValue& value :
                 indexes) {
                const int index =
                    value.toInt(-1);
                if (index < 0 ||
                    index >=
                        diagnostics.size() ||
                    diagnostics.at(index)
                            .toObject()
                            .value(
                                QStringLiteral(
                                    "severity"))
                            .toString() !=
                        QStringLiteral(
                            "error")) {
                    return false;
                }
            }
            return true;
        };

    const QString validationDirectory =
        directory.filePath(
            QStringLiteral(
                "validation"));
    QVERIFY(
        QDir().mkpath(
            validationDirectory));
    const QString validationProject =
        createProject(
            validationDirectory);
    QVERIFY(
        !validationProject.isEmpty());
    const QByteArray validationOriginal =
        readFile(
            validationProject);
    const QString validationRevision =
        currentRevision(
            validationProject);
    const QString validationPatch =
        directory.filePath(
            QStringLiteral(
                "validation-failure.json"));
    QVERIFY(
        writePatch(
            validationPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("ref"),
                     QStringLiteral(
                         "invalid_width")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("width")},
                    {QStringLiteral("value"),
                     0},
                },
            },
            validationRevision));
    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             validationProject,
             validationPatch});
    QCOMPARE(
        validation.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject validationRoot =
        json(validation);
    QCOMPARE(
        validationRoot.value(
                          QStringLiteral(
                              "error"))
            .toString(),
        QStringLiteral(
            "Candidate validation failed; no files were written."));
    const QJsonArray
        validationDiagnostics =
            validationRoot.value(
                              QStringLiteral(
                                  "diagnostics"))
                .toArray();
    const QJsonObject validationFailure =
        validationRoot.value(
                          QStringLiteral(
                              "result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject();
    QCOMPARE(
        validationFailure.value(
                              QStringLiteral(
                                  "stage"))
            .toString(),
        QStringLiteral("validation"));
    QVERIFY(
        validationFailure.value(
                              QStringLiteral(
                                  "operation_index"))
            .isNull());
    QVERIFY(
        validationFailure.value(
                              QStringLiteral(
                                  "json_pointer"))
            .isNull());
    QCOMPARE(
        validationFailure.value(
                              QStringLiteral(
                                  "operation_count"))
            .toInt(),
        1);
    QCOMPARE(
        validationFailure.value(
                              QStringLiteral(
                                  "completed_operation_count"))
            .toInt(),
        1);
    QVERIFY(
        validationFailure.value(
                              QStringLiteral(
                                  "operation"))
            .isNull());
    QVERIFY(
        verifyDiagnosticIndexes(
            validationFailure,
            validationDiagnostics));
    QVERIFY(
        validationFailure.value(
                              QStringLiteral(
                                  "object_ids"))
            .toArray()
            .contains(
                QStringLiteral(
                    "reg-control")));
    QVERIFY(
        !validationFailure.value(
                               QStringLiteral(
                                   "error_codes"))
             .toArray()
             .isEmpty());
    QVERIFY(
        !validationFailure.value(
                               QStringLiteral(
                                   "writes_performed"))
             .toBool());
    QVERIFY(
        validationFailure.value(
                              QStringLiteral(
                                  "completed_operations_rolled_back"))
            .toBool());
    QCOMPARE(
        readFile(
            validationProject),
        validationOriginal);
    QCOMPARE(
        currentRevision(
            validationProject),
        validationRevision);
    QVERIFY(
        !QDir(
             QDir(validationDirectory)
                 .filePath(
                     QStringLiteral(
                         "generated")))
             .exists());

    const QString generationDirectory =
        directory.filePath(
            QStringLiteral(
                "generation"));
    QVERIFY(
        QDir().mkpath(
            generationDirectory));
    const QString generationProject =
        createProject(
            generationDirectory);
    QVERIFY(
        !generationProject.isEmpty());
    const QByteArray generationOriginal =
        readFile(
            generationProject);
    const QString generationRevision =
        currentRevision(
            generationProject);
    const Invocation modelValidation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             generationProject});
    QCOMPARE(
        modelValidation.exitCode,
        0);
    const QString generationPatch =
        directory.filePath(
            QStringLiteral(
                "generation-failure.json"));
    QVERIFY(
        writePatch(
            generationPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("ref"),
                     QStringLiteral(
                         "colliding_register")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control-lowercase")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "control")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "auto")},
                     }},
                },
            },
            generationRevision));
    const Invocation generation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             generationProject,
             generationPatch});
    QCOMPARE(
        generation.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                generationError));
    const QJsonObject generationRoot =
        json(generation);
    QCOMPARE(
        generationRoot.value(
                          QStringLiteral(
                              "error"))
            .toString(),
        QStringLiteral(
            "Candidate output generation failed; no files were written."));
    const QJsonArray
        generationDiagnostics =
            generationRoot.value(
                              QStringLiteral(
                                  "diagnostics"))
                .toArray();
    const QJsonObject generationFailure =
        generationRoot.value(
                          QStringLiteral(
                              "result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject();
    QCOMPARE(
        generationFailure.value(
                              QStringLiteral(
                                  "stage"))
            .toString(),
        QStringLiteral("generation"));
    QCOMPARE(
        generationFailure.value(
                              QStringLiteral(
                                  "completed_operation_count"))
            .toInt(),
        1);
    QVERIFY(
        verifyDiagnosticIndexes(
            generationFailure,
            generationDiagnostics));
    QVERIFY(
        generationFailure.value(
                              QStringLiteral(
                                  "error_codes"))
            .toArray()
            .contains(
                QStringLiteral(
                    "RM4001")));
    QVERIFY(
        !generationFailure.value(
                               QStringLiteral(
                                   "writes_performed"))
             .toBool());
    QVERIFY(
        generationFailure.value(
                              QStringLiteral(
                                  "completed_operations_rolled_back"))
            .toBool());
    QVERIFY(
        generationRoot.value(
                          QStringLiteral(
                              "result"))
            .toObject()
            .value(
                QStringLiteral(
                    "generation_previewed"))
            .toBool());
    QCOMPARE(
        readFile(
            generationProject),
        generationOriginal);
    QCOMPARE(
        currentRevision(
            generationProject),
        generationRevision);
    QVERIFY(
        !QDir(
             QDir(generationDirectory)
                 .filePath(
                     QStringLiteral(
                         "generated")))
             .exists());
}

void CliTests::readsPatchFromStandardInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    const QByteArray original =
        readFile(project);
    const QString input =
        patchText(
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Read from standard input.")},
                }},
            revision);

    const Invocation invocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-"),
             QStringLiteral(
                 "--dry-run")},
            input);
    QCOMPARE(invocation.exitCode, 0);
    const QJsonObject root =
        json(invocation);
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    const QJsonObject result =
        root.value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "dry_run"))
            .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "changed"))
            .toBool());
    QVERIFY(
        !result.value(
                   QStringLiteral("saved"))
             .toBool());
    QCOMPARE(
        readFile(project),
        original);
}

void CliTests::addsAndRemovesHierarchyAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());
    const QByteArray originalProject =
        readFile(project);

    const QString rejectedAddPatch =
        directory.filePath(
            QStringLiteral(
                "rejected-add.json"));
    QVERIFY(
        writePatch(
            rejectedAddPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-must-not-persist")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Must Not Persist")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-invalid")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-missing")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Invalid")}}},
                },
            },
            originalRevision));
    const Invocation rejectedAdd =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             rejectedAddPatch});
    QCOMPARE(
        rejectedAdd.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        readFile(project),
        originalProject);
    QCOMPARE(
        currentRevision(project),
        originalRevision);

    const QString addPatch =
        directory.filePath(
            QStringLiteral(
                "add-hierarchy.json"));
    QVERIFY(
        writePatch(
            addPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-automation")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Automation")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x2000")},
                         {QStringLiteral(
                              "address_width"),
                          32}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-automation")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-automation")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Registers")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x0")},
                         {QStringLiteral(
                              "size"),
                          QStringLiteral(
                              "0x100")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-automation-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-automation")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "CONTROL")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "0x0")},
                         {QStringLiteral(
                              "width"),
                          16},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "field")},
                         {QStringLiteral(
                              "access"),
                          QStringLiteral(
                              "rw")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("field")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-automation-enable")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-automation-control")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "ENABLE")},
                         {QStringLiteral(
                              "lsb"),
                          0},
                         {QStringLiteral(
                              "width"),
                          1}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-automation-mode")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-automation")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "MODE")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "0x4")},
                         {QStringLiteral(
                              "width"),
                          1},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "bool")},
                         {QStringLiteral(
                              "access"),
                          QStringLiteral(
                              "rw")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("enum")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "enum-automation-false")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-automation-mode")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "FALSE")},
                         {QStringLiteral(
                              "value"),
                          0}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("enum")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "enum-automation-true")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-automation-mode")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "TRUE")},
                         {QStringLiteral(
                              "value"),
                          1}}},
                },
            },
            originalRevision));

    const Invocation add =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             addPatch});
    QCOMPARE(add.exitCode, 0);
    const QJsonObject addRoot =
        json(add);
    QVERIFY(
        addRoot.value(
                   QStringLiteral("ok"))
            .toBool());
    const QJsonObject addResult =
        addRoot.value(
                   QStringLiteral(
                       "result"))
            .toObject();
    QVERIFY(
        addResult.value(
                     QStringLiteral(
                         "saved"))
            .toBool());
    QCOMPARE(
        addResult.value(
                     QStringLiteral(
                         "changes"))
            .toArray()
            .size(),
        7);
    const QString addedRevision =
        addRoot.value(
                   QStringLiteral(
                       "revision"))
            .toString();
    QVERIFY(
        addedRevision !=
        originalRevision);

    const Invocation summary =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    QCOMPARE(summary.exitCode, 0);
    const QJsonObject counts =
        json(summary)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("counts"))
            .toObject();
    QCOMPARE(
        counts.value(
                  QStringLiteral("pages"))
            .toInt(),
        2);
    QCOMPARE(
        counts.value(
                  QStringLiteral("blocks"))
            .toInt(),
        2);
    QCOMPARE(
        counts.value(
                  QStringLiteral(
                      "registers"))
            .toInt(),
        3);
    QCOMPARE(
        counts.value(
                  QStringLiteral("fields"))
            .toInt(),
        2);
    QCOMPARE(
        counts.value(
                  QStringLiteral(
                      "enum_values"))
            .toInt(),
        2);

    const QByteArray beforeRejectedRemove =
        readFile(project);
    const QString unsafeRemove =
        directory.filePath(
            QStringLiteral(
                "remove-without-cascade.json"));
    QVERIFY(
        writePatch(
            unsafeRemove,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral(
                         "remove")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-automation")},
                }},
            addedRevision));
    const Invocation rejected =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             unsafeRemove});
    QCOMPARE(
        rejected.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejected)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "cascade")));
    QCOMPARE(
        readFile(project),
        beforeRejectedRemove);

    const QString safeRemove =
        directory.filePath(
            QStringLiteral(
                "remove-with-cascade.json"));
    QVERIFY(
        writePatch(
            safeRemove,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral(
                         "remove")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-automation")},
                    {QStringLiteral(
                         "cascade"),
                     true},
                }},
            addedRevision));
    const Invocation removed =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             safeRemove});
    QCOMPARE(removed.exitCode, 0);
    const QJsonObject removeRoot =
        json(removed);
    QVERIFY(
        removeRoot.value(
                      QStringLiteral(
                          "ok"))
            .toBool());
    const QJsonObject removeResult =
        removeRoot.value(
                      QStringLiteral(
                          "result"))
            .toObject();
    QVERIFY(
        removeResult.value(
                         QStringLiteral(
                             "saved"))
            .toBool());
    const QJsonObject removeChange =
        removeResult.value(
                         QStringLiteral(
                             "changes"))
            .toArray()
            .at(0)
            .toObject();
    QCOMPARE(
        removeChange.value(
                        QStringLiteral(
                            "op"))
            .toString(),
        QStringLiteral("remove"));
    QVERIFY(
        removeChange.value(
                        QStringLiteral(
                            "cascade"))
            .toBool());
    QVERIFY(
        removeChange.value(
                        QStringLiteral(
                            "descendant_count"))
            .toString()
            .toULongLong() >= 6);

    const Invocation missing =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "page-automation")});
    QCOMPARE(
        missing.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const Invocation finalValidation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(finalValidation.exitCode, 0);
    QVERIFY(
        json(finalValidation)
            .value(
                QStringLiteral("ok"))
            .toBool());

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::createsHierarchyWithAutomaticIdsAndReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        directory.filePath(
            QStringLiteral(
                "batch-device.regmap.yaml"));
    const Invocation initialized =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             project,
             QStringLiteral("--name"),
             QStringLiteral("Batch Device"),
             QStringLiteral(
                 "--workspace-id"),
             QStringLiteral(
                 "workspace-batch"),
             QStringLiteral(
                 "--no-generate")});
    QCOMPARE(initialized.exitCode, 0);
    QVERIFY(QFileInfo::exists(project));
    const QByteArray emptyProject =
        readFile(project);
    const QString emptyRevision =
        currentRevision(project);
    QVERIFY(!emptyRevision.isEmpty());

    const auto prior =
        [](int operation) {
            return QJsonObject{
                {QStringLiteral(
                     "operation"),
                 operation},
            };
        };
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-batch")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Main")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(0)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Control")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(1)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("CONTROL")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  8},
                 {QStringLiteral("type"),
                  QStringLiteral("field")},
                 {QStringLiteral("initial"),
                  QStringLiteral("0x1")},
                 {QStringLiteral("reset"),
                  QStringLiteral("0x1")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(2)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("ENABLE")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("type"),
                  QStringLiteral("bool")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("enum")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(3)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("FALSE")},
                 {QStringLiteral("value"),
                  0},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("enum")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(3)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("TRUE")},
                 {QStringLiteral("value"),
                  1},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-status")},
            {QStringLiteral(
                 "parent_id"),
             prior(1)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "ID_OCCUPANT")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(1)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("STATUS")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-status")},
            {QStringLiteral("new_id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(1)},
            {QStringLiteral(
                 "unique_name"),
             true},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(0)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Diagnostics")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-status-2")},
            {QStringLiteral(
                 "parent_id"),
             prior(9)},
            {QStringLiteral(
                 "placement"),
             QStringLiteral("auto")},
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "create-hierarchy.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            emptyRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(readFile(project), emptyProject);
    QVERIFY(
        !QDir(
             directory.filePath(
                 QStringLiteral(
                     "generated")))
             .exists());
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(),
        operations.size());
    const QStringList expectedIds{
        QStringLiteral("page-main"),
        QStringLiteral("block-control"),
        QStringLiteral(
            "register-control"),
        QStringLiteral("field-enable"),
        QStringLiteral("enum-false"),
        QStringLiteral("enum-true"),
        QStringLiteral(
            "register-status"),
        QStringLiteral(
            "register-status-2"),
        QStringLiteral(
            "register-status-copy"),
        QStringLiteral(
            "block-diagnostics"),
        QStringLiteral(
            "register-status-2"),
    };
    for (qsizetype index = 0;
         index < expectedIds.size();
         ++index) {
        QCOMPARE(
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id"))
                .toString(),
            expectedIds.at(index));
    }
    for (int index :
         {0, 1, 2, 3, 4, 5, 7, 8, 9}) {
        QVERIFY(
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral(
                        "automatic_id"))
                .toBool());
    }
    QVERIFY(
        !previewChanges.at(6)
             .toObject()
             .value(
                 QStringLiteral(
                     "automatic_id"))
             .toBool());
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral("page-main"));
    QCOMPARE(
        previewChanges.at(3)
            .toObject()
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral(
            "register-control"));
    QCOMPARE(
        previewChanges.at(8)
            .toObject()
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral("block-control"));
    QCOMPARE(
        previewChanges.at(10)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toString(),
        QStringLiteral(
            "block-diagnostics"));
    QCOMPARE(
        previewChanges.at(10)
            .toObject()
            .value(
                QStringLiteral(
                    "placement_result"))
            .toObject()
            .value(
                QStringLiteral("value"))
            .toString(),
        QStringLiteral("0x0"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    QVERIFY(
        !appliedRoot.value(
                        QStringLiteral("result"))
             .toObject()
             .contains(
                 QStringLiteral(
                     "failure")));
    const QJsonObject appliedResult =
        appliedRoot.value(
                       QStringLiteral(
                           "result"))
            .toObject();
    QVERIFY(
        appliedResult.value(
                         QStringLiteral(
                             "saved"))
            .toBool());
    const QJsonArray appliedChanges =
        appliedResult.value(
                         QStringLiteral(
                             "changes"))
            .toArray();
    QCOMPARE(
        appliedChanges.size(),
        previewChanges.size());
    for (qsizetype index = 0;
         index < appliedChanges.size();
         ++index) {
        QCOMPARE(
            appliedChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")),
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")));
    }
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        !appliedRevision.isEmpty());
    QVERIFY(
        appliedRevision !=
        emptyRevision);

    const auto getDescriptor =
        [&project](const QString& id) {
            const Invocation objects =
                invokeExecutable(
                    {QStringLiteral(
                         "--json"),
                     QStringLiteral("list"),
                     project});
            if (objects.exitCode != 0) {
                return QJsonObject{};
            }
            const QJsonArray descriptors =
                json(objects)
                .value(
                    QStringLiteral(
                        "result"))
                .toArray();
            for (const QJsonValue&
                     descriptor :
                 descriptors) {
                if (descriptor
                        .toObject()
                        .value(
                            QStringLiteral(
                                "id"))
                        .toString() ==
                    id) {
                    return descriptor
                        .toObject();
                }
            }
            return QJsonObject{};
        };
    QCOMPARE(
        getDescriptor(
            QStringLiteral(
                "enum-true"))
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral("field-enable"));
    QCOMPARE(
        getDescriptor(
            QStringLiteral(
                "register-status-copy"))
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral("block-control"));
    QCOMPARE(
        getDescriptor(
            QStringLiteral(
                "register-status-2"))
            .value(
                QStringLiteral(
                    "parent_id"))
            .toString(),
        QStringLiteral(
            "block-diagnostics"));
    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);
    QVERIFY(
        QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "generated/register-map.xlsx"))));
    QVERIFY(
        QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "generated/register-map.md"))));
    QVERIFY(
        QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "generated/Batch_Device_regs.h"))));

    const QByteArray beforeRejected =
        readFile(project);
    const QList<QJsonValue>
        invalidReferences{
            QJsonObject{
                {QStringLiteral(
                     "operation"),
                 0}},
            QJsonObject{
                {QStringLiteral(
                     "operation"),
                 -1}},
            QJsonObject{
                {QStringLiteral(
                     "operation"),
                 0.5}},
            QJsonObject{
                {QStringLiteral(
                     "operation"),
                 1}},
        };
    for (const QJsonValue&
             invalidReference :
         invalidReferences) {
        const QJsonArray rejectedOperations{
            QJsonObject{
                {QStringLiteral("op"),
                 QStringLiteral("add")},
                {QStringLiteral("kind"),
                 QStringLiteral("page")},
                {QStringLiteral("id"),
                 QStringLiteral("auto")},
                {QStringLiteral(
                     "parent_id"),
                 invalidReference},
                {QStringLiteral("value"),
                 QJsonObject{
                     {QStringLiteral(
                          "name"),
                      QStringLiteral(
                          "Rejected")},
                 }},
            },
        };
        const Invocation rejected =
            invoke(
                {QStringLiteral("--json"),
                 QStringLiteral("apply"),
                 project,
                 QStringLiteral("-")},
                patchText(
                    rejectedOperations,
                    appliedRevision));
        QCOMPARE(
            rejected.exitCode,
            static_cast<int>(
                regmap::cli::ExitCode::
                    usageError));
        QVERIFY(
            json(rejected)
                .value(
                    QStringLiteral(
                        "error"))
                .toString()
                .contains(
                    QStringLiteral(
                        "earlier operation")));
        QCOMPARE(
            readFile(project),
            beforeRejected);
        QCOMPARE(
            currentRevision(project),
            appliedRevision);
    }

    const QJsonArray wrongParentKind{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral("block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "TEMP_PARENT")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(0)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "INVALID_CHILD")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
    };
    const Invocation rejectedKind =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                wrongParentKind,
                appliedRevision));
    QCOMPARE(
        rejectedKind.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedKind)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "existing block")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::usesEarlierOperationResultsAsObjectReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const auto prior =
        [](int operation) {
            return QJsonObject{
                {QStringLiteral(
                     "operation"),
                 operation},
            };
        };
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Automation")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x2000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             prior(0)},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral(
                 "Created and updated in one patch.")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(0)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Batch")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(2)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("STATUS")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             prior(3)},
            {QStringLiteral("new_id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(2)},
            {QStringLiteral(
                 "unique_name"),
             true},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             prior(0)},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Diagnostics")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             prior(4)},
            {QStringLiteral(
                 "parent_id"),
             prior(5)},
            {QStringLiteral(
                 "placement"),
             QStringLiteral("auto")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             prior(6)},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral(
                 "Moved through operation references.")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("remove")},
            {QStringLiteral("id"),
             prior(7)},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("First")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x3000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Second")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x4000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             prior(10)},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral(
                 "before_id"),
             prior(9)},
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "operation-references.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(),
        operations.size());
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "page-automation"));
    QCOMPARE(
        previewChanges.at(4)
            .toObject()
            .value(
                QStringLiteral(
                    "source_id"))
            .toString(),
        QStringLiteral(
            "register-status"));
    QCOMPARE(
        previewChanges.at(4)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "register-status-copy"));
    for (int index : {6, 7, 8}) {
        QCOMPARE(
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id"))
                .toString(),
            QStringLiteral(
                "register-status-copy"));
    }
    QCOMPARE(
        previewChanges.at(6)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toString(),
        QStringLiteral(
            "block-diagnostics"));
    QCOMPARE(
        previewChanges.at(11)
            .toObject()
            .value(
                QStringLiteral(
                    "placement_before_id"))
            .toString(),
        QStringLiteral("page-first"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QJsonArray appliedChanges =
        appliedRoot.value(
                       QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        appliedChanges.size(),
        previewChanges.size());
    for (qsizetype index = 0;
         index < appliedChanges.size();
         ++index) {
        QCOMPARE(
            appliedChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")),
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")));
    }
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const Invocation page =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "page-automation")});
    QCOMPARE(page.exitCode, 0);
    QCOMPARE(
        json(page)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "description"))
            .toString(),
        QStringLiteral(
            "Created and updated in one patch."));
    const Invocation removed =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "register-status-copy")});
    QCOMPARE(
        removed.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const Invocation originalRegister =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "register-status")});
    QCOMPARE(
        originalRegister.exitCode,
        0);

    const Invocation pages =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("page")});
    QCOMPARE(pages.exitCode, 0);
    QStringList pageIds;
    for (const QJsonValue& value :
         json(pages)
             .value(
                 QStringLiteral("result"))
             .toArray()) {
        pageIds.append(
            value.toObject()
                .value(
                    QStringLiteral("id"))
                .toString());
    }
    const qsizetype secondIndex =
        pageIds.indexOf(
            QStringLiteral(
                "page-second"));
    const qsizetype firstIndex =
        pageIds.indexOf(
            QStringLiteral(
                "page-first"));
    QVERIFY(secondIndex >= 0);
    QVERIFY(firstIndex >= 0);
    QVERIFY(secondIndex < firstIndex);

    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    const QByteArray beforeRejected =
        readFile(project);
    const QJsonArray forwardTarget{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Rollback")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             prior(2)},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral("Rejected")},
        },
    };
    const Invocation rejectedTarget =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                forwardTarget,
                appliedRevision));
    QCOMPARE(
        rejectedTarget.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedTarget)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier operation")));
    const QJsonObject targetFailure =
        json(rejectedTarget)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject();
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "stage"))
            .toString(),
        QStringLiteral("operation"));
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "operation_index"))
            .toInt(),
        1);
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "json_pointer"))
            .toString(),
        QStringLiteral(
            "/operations/1"));
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "operation_count"))
            .toInt(),
        2);
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "completed_operation_count"))
            .toInt(),
        1);
    QCOMPARE(
        targetFailure.value(
                         QStringLiteral(
                             "operation")),
        forwardTarget.at(1));
    QVERIFY(
        targetFailure.value(
                         QStringLiteral(
                             "message"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier operation")));
    QVERIFY(
        !targetFailure.value(
                          QStringLiteral(
                              "writes_performed"))
             .toBool());
    QVERIFY(
        targetFailure.value(
                         QStringLiteral(
                             "completed_operations_rolled_back"))
            .toBool());
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QJsonArray wrongBeforeKind{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "page-automation")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Wrong Anchor")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             QStringLiteral("page-first")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral(
                 "before_id"),
             prior(0)},
        },
    };
    const Invocation rejectedBefore =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                wrongBeforeKind,
                appliedRevision));
    QCOMPARE(
        rejectedBefore.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedBefore)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "before_id")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::usesNamedOperationReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const auto named =
        [](const QString& reference) {
            return QJsonObject{
                {QStringLiteral("ref"),
                 reference},
            };
        };
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "automation_page")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Automation")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x2000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "page_description")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "automation_page"))},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral(
                 "Named references keep patches readable.")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "source_block")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "automation_page"))},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Source")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "source_register")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "source_block"))},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("STATUS")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "copied_register")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "source_register"))},
            {QStringLiteral("new_id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "source_block"))},
            {QStringLiteral(
                 "unique_name"),
             true},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "destination_block")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "automation_page"))},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Destination")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "moved_register")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "copied_register"))},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "destination_block"))},
            {QStringLiteral(
                 "placement"),
             QStringLiteral("auto")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("remove")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "removed_register")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "moved_register"))},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "first_page")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("First")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x3000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "second_page")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Second")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x4000")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "reordered_page")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "second_page"))},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral(
                 "before_id"),
             named(
                 QStringLiteral(
                     "first_page"))},
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "named-references.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(),
        operations.size());
    for (qsizetype index = 0;
         index < operations.size();
         ++index) {
        QCOMPARE(
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("ref")),
            operations.at(index)
                .toObject()
                .value(
                    QStringLiteral("ref")));
    }
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "page-automation"));
    QCOMPARE(
        previewChanges.at(4)
            .toObject()
            .value(
                QStringLiteral(
                    "source_id"))
            .toString(),
        QStringLiteral(
            "register-status"));
    QCOMPARE(
        previewChanges.at(4)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "register-status-copy"));
    QCOMPARE(
        previewChanges.at(6)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "register-status-copy"));
    QCOMPARE(
        previewChanges.at(10)
            .toObject()
            .value(
                QStringLiteral(
                    "placement_before_id"))
            .toString(),
        QStringLiteral("page-first"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);
    const QJsonArray appliedChanges =
        appliedRoot.value(
                       QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        appliedChanges.size(),
        previewChanges.size());
    for (qsizetype index = 0;
         index < appliedChanges.size();
         ++index) {
        QCOMPARE(
            appliedChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")),
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("id")));
        QCOMPARE(
            appliedChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("ref")),
            previewChanges.at(index)
                .toObject()
                .value(
                    QStringLiteral("ref")));
    }

    const Invocation page =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "page-automation")});
    QCOMPARE(page.exitCode, 0);
    QCOMPARE(
        json(page)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "description"))
            .toString(),
        QStringLiteral(
            "Named references keep patches readable."));
    const Invocation removed =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "register-status-copy")});
    QCOMPARE(
        removed.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const Invocation pages =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("page")});
    QCOMPARE(pages.exitCode, 0);
    QStringList pageIds;
    for (const QJsonValue& value :
         json(pages)
             .value(
                 QStringLiteral("result"))
             .toArray()) {
        pageIds.append(
            value.toObject()
                .value(
                    QStringLiteral("id"))
                .toString());
    }
    const qsizetype secondIndex =
        pageIds.indexOf(
            QStringLiteral(
                "page-second"));
    const qsizetype firstIndex =
        pageIds.indexOf(
            QStringLiteral(
                "page-first"));
    QVERIFY(secondIndex >= 0);
    QVERIFY(firstIndex >= 0);
    QVERIFY(secondIndex < firstIndex);
    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    const QByteArray beforeRejected =
        readFile(project);
    const QJsonArray duplicateReferences{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("ref"),
             QStringLiteral("duplicate")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "page-automation")},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral("First")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("ref"),
             QStringLiteral("duplicate")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "page-automation")},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral("Second")},
        },
    };
    const Invocation duplicate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                duplicateReferences,
                appliedRevision));
    QCOMPARE(
        duplicate.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    const QJsonObject duplicateFailure =
        json(duplicate)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject();
    QCOMPARE(
        duplicateFailure.value(
                            QStringLiteral(
                                "operation_index"))
            .toInt(),
        1);
    QCOMPARE(
        duplicateFailure.value(
                            QStringLiteral(
                                "completed_operation_count"))
            .toInt(),
        1);
    QVERIFY(
        duplicateFailure.value(
                            QStringLiteral(
                                "message"))
            .toString()
            .contains(
                QStringLiteral("unique")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QJsonArray missingReference{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("ref"),
             QStringLiteral("consumer")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "Consumer"))},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral("Rejected")},
        },
    };
    const Invocation missing =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                missingReference,
                appliedRevision));
    QCOMPARE(
        missing.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(missing)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject()
            .value(
                QStringLiteral("message"))
            .toString()
            .contains(
                QStringLiteral(
                    "earlier operation ref")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QJsonArray whitespaceReference{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("ref"),
             QStringLiteral(" invalid ")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "page-automation")},
            {QStringLiteral("property"),
             QStringLiteral(
                 "description")},
            {QStringLiteral("value"),
             QStringLiteral("Rejected")},
        },
    };
    const Invocation whitespace =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                whitespaceReference,
                appliedRevision));
    QCOMPARE(
        whitespace.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(whitespace)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "whitespace")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::autoPlacesBlocksWithoutBaseArithmetic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "block-auto-gap")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "page-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Auto Gap")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral(
                      "0x20")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "block-auto-appended")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "page-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Auto Appended")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral(
                      "0x10")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "page-auto-full")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Auto Full")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x0")},
                 {QStringLiteral(
                      "address_width"),
                  8}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "block-auto-full")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "page-auto-full")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Auto Full Block")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral(
                      "0x100")}},
            },
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "auto-blocks.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(), 4);
    QCOMPARE(
        previewChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x0"));
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x120"));
    QCOMPARE(
        previewChanges.at(3)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x0"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const Invocation appended =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "block-auto-appended")});
    QCOMPARE(appended.exitCode, 0);
    QCOMPARE(
        json(appended)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x120"));

    const QByteArray beforeRejected =
        readFile(project);
    const QString fullPatch =
        directory.filePath(
            QStringLiteral(
                "auto-block-full.json"));
    QVERIFY(
        writePatch(
            fullPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-auto-rejected")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-auto-full")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Auto Rejected")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "auto")},
                         {QStringLiteral(
                              "size"),
                          QStringLiteral(
                              "0x1")}},
                    },
                },
            },
            appliedRevision));
    const Invocation full =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             fullPatch});
    QCOMPARE(
        full.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(full)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "found no free 1-byte range")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    const QString missingSizePatch =
        directory.filePath(
            QStringLiteral(
                "auto-block-missing-size.json"));
    QVERIFY(
        writePatch(
            missingSizePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-auto-no-size")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Auto No Size")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "auto")}},
                    },
                },
            },
            appliedRevision));
    const Invocation missingSize =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             missingSizePatch});
    QCOMPARE(
        missingSize.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(missingSize)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "requires a positive explicit 'size'")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::autoPlacesRegistersWithoutAddressArithmetic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral(
                 "property"),
             QStringLiteral("size")},
            {QStringLiteral("value"),
             QStringLiteral("0xC")},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral(
                 "register")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "reg-auto-appended")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_APPENDED")},
                 {QStringLiteral(
                      "offset"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  32},
                 {QStringLiteral("type"),
                  QStringLiteral(
                      "unsigned")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral(
                 "register")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "reg-auto-gap")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_GAP")},
                 {QStringLiteral(
                      "offset"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  32},
                 {QStringLiteral("type"),
                  QStringLiteral(
                      "unsigned")}},
            },
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "auto-registers.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(), 3);
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x8"));
    QCOMPARE(
        previewChanges.at(2)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x0"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    for (const auto& [id, offset] :
         std::vector<std::pair<
             QString, QString>>{
             {QStringLiteral(
                  "reg-auto-appended"),
              QStringLiteral("0x8")},
             {QStringLiteral(
                  "reg-auto-gap"),
              QStringLiteral("0x0")}}) {
        const Invocation object =
            invoke(
                {QStringLiteral("--json"),
                 QStringLiteral("get"),
                 project,
                 id});
        QCOMPARE(object.exitCode, 0);
        QCOMPARE(
            json(object)
                .value(
                    QStringLiteral(
                        "result"))
                .toObject()
                .value(
                    QStringLiteral(
                        "offset"))
                .toString(),
            offset);
    }

    const QByteArray fullProject =
        readFile(project);
    const QString rejectedPatch =
        directory.filePath(
            QStringLiteral(
                "auto-register-full.json"));
    QVERIFY(
        writePatch(
            rejectedPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-auto-rejected")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "AUTO_REJECTED")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "auto")},
                         {QStringLiteral(
                              "width"),
                          32},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "unsigned")}},
                    },
                },
            },
            appliedRevision));
    const Invocation rejected =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             rejectedPatch});
    QCOMPARE(
        rejected.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejected)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "no aligned 4-byte range")));
    QCOMPARE(
        readFile(project),
        fullProject);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::autoPlacesFieldsWithoutBitArithmetic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-auto-wide")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_WIDE")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  3},
                 {QStringLiteral("type"),
                  QStringLiteral(
                      "unsigned")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-auto-next")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_NEXT")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  2},
                 {QStringLiteral("type"),
                  QStringLiteral("bits")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-auto-container")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_CONTAINER")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  4},
                 {QStringLiteral("type"),
                  QStringLiteral("field")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-auto-member")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "field-auto-container")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_MEMBER")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("width"),
                  2},
                 {QStringLiteral("type"),
                  QStringLiteral("bits")}},
            },
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("field")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-auto-default-width")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "AUTO_DEFAULT_WIDTH")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")},
                 {QStringLiteral("type"),
                  QStringLiteral("bits")}},
            },
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "auto-fields.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(), 5);
    const auto verifyPreviewRange =
        [&previewChanges](
            int index,
            int lsb,
            int msb) {
            const QJsonObject after =
                previewChanges.at(index)
                    .toObject()
                    .value(
                        QStringLiteral(
                            "after"))
                    .toObject();
            QCOMPARE(
                after.value(
                         QStringLiteral(
                             "lsb"))
                    .toInt(),
                lsb);
            QCOMPARE(
                after.value(
                         QStringLiteral(
                             "msb"))
                    .toInt(),
                msb);
        };
    verifyPreviewRange(0, 1, 3);
    verifyPreviewRange(1, 4, 5);
    verifyPreviewRange(2, 6, 9);
    verifyPreviewRange(3, 0, 1);
    verifyPreviewRange(4, 10, 10);

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const Invocation nested =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-auto-container")});
    QCOMPARE(nested.exitCode, 0);
    const QJsonObject nestedField =
        json(nested)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        nestedField.value(
                       QStringLiteral("lsb"))
            .toInt(),
        6);
    QCOMPARE(
        nestedField.value(
                       QStringLiteral("msb"))
            .toInt(),
        9);
    QCOMPARE(
        nestedField.value(
                       QStringLiteral("members"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("lsb"))
            .toInt(),
        0);

    const QByteArray beforeRejected =
        readFile(project);
    const QString rejectedPatch =
        directory.filePath(
            QStringLiteral(
                "auto-field-full.json"));
    QVERIFY(
        writePatch(
            rejectedPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("field")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-auto-rejected")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "AUTO_REJECTED")},
                         {QStringLiteral(
                              "lsb"),
                          QStringLiteral(
                              "auto")},
                         {QStringLiteral(
                              "width"),
                          32},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "bits")}},
                    },
                },
            },
            appliedRevision));
    const Invocation rejected =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             rejectedPatch});
    QCOMPARE(
        rejected.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejected)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "no contiguous 32-bit range")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    const QString ambiguousPatch =
        directory.filePath(
            QStringLiteral(
                "auto-field-ambiguous.json"));
    QVERIFY(
        writePatch(
            ambiguousPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("field")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-auto-ambiguous")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "AUTO_AMBIGUOUS")},
                         {QStringLiteral(
                              "lsb"),
                          QStringLiteral(
                              "auto")},
                         {QStringLiteral(
                              "msb"),
                          12},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "bits")}},
                    },
                },
            },
            appliedRevision));
    const Invocation ambiguous =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             ambiguousPatch});
    QCOMPARE(
        ambiguous.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(ambiguous)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "accepts 'width', not an explicit 'msb'")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::copiesHierarchyWithDeterministicIds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString originalRevision =
        currentRevision(project);
    const QByteArray originalProject =
        readFile(project);

    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("page")},
            {QStringLiteral("id"),
             QStringLiteral("page-copy-target")},
            {QStringLiteral("parent_id"),
             QStringLiteral("workspace-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Copy Target")},
                 {QStringLiteral("base"),
                  QStringLiteral("0x2000")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("enum")},
            {QStringLiteral("id"),
             QStringLiteral("enum-enable-off")},
            {QStringLiteral("parent_id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("OFF")},
                 {QStringLiteral("value"), 0}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("enum")},
            {QStringLiteral("id"),
             QStringLiteral("enum-enable-on")},
            {QStringLiteral("parent_id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("ON")},
                 {QStringLiteral("value"), 1}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("reg-bool-target")},
            {QStringLiteral("parent_id"),
             QStringLiteral("block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("BOOL_TARGET")},
                 {QStringLiteral("offset"),
                  QStringLiteral("0x10")},
                 {QStringLiteral("width"), 1},
                 {QStringLiteral("type"),
                  QStringLiteral("bool")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("block-control")},
            {QStringLiteral("new_id"),
             QStringLiteral("block-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("page-copy-target")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("Copied Control")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("reg-control")},
            {QStringLiteral("new_id"),
             QStringLiteral("reg-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("CONTROL_COPY")},
                 {QStringLiteral("offset"),
                  QStringLiteral("0x8")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("field-enable")},
            {QStringLiteral("new_id"),
             QStringLiteral("field-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("ENABLE_COPY")},
                 {QStringLiteral("lsb"), 4}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral("enum-enable-off")},
            {QStringLiteral("new_id"),
             QStringLiteral("enum-copy")},
            {QStringLiteral("parent_id"),
             QStringLiteral("reg-bool-target")},
            {QStringLiteral("unique_name"),
             true},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "page-copy-target")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "page-copy-full")},
            {QStringLiteral("parent_id"),
             QStringLiteral(
                 "workspace-main")},
            {QStringLiteral("unique_name"),
             true},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("base"),
                  QStringLiteral(
                      "0x4000")}}},
        },
    };
    const QString copyPatch =
        directory.filePath(
            QStringLiteral(
                "copy-hierarchy.json"));
    QVERIFY(
        writePatch(
            copyPatch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             copyPatch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(),
        operations.size());
    const QJsonObject previewBlockMap =
        previewChanges.at(4)
            .toObject()
            .value(
                QStringLiteral("id_mapping"))
            .toObject();
    QCOMPARE(
        previewBlockMap.value(
                           QStringLiteral(
                               "block-control"))
            .toString(),
        QStringLiteral("block-copy"));
    QCOMPARE(
        previewBlockMap.value(
                           QStringLiteral(
                               "reg-control"))
            .toString(),
        QStringLiteral(
            "block-copy--reg-control"));
    QCOMPARE(
        previewBlockMap.value(
                           QStringLiteral(
                               "field-enable"))
            .toString(),
        QStringLiteral(
            "block-copy--field-enable"));
    QCOMPARE(
        previewBlockMap.value(
                           QStringLiteral(
                               "enum-enable-on"))
            .toString(),
        QStringLiteral(
            "block-copy--enum-enable-on"));
    const QJsonObject previewPageMap =
        previewChanges.at(8)
            .toObject()
            .value(
                QStringLiteral("id_mapping"))
            .toObject();
    QCOMPARE(
        previewPageMap.value(
                          QStringLiteral(
                              "page-copy-target"))
            .toString(),
        QStringLiteral(
            "page-copy-full"));
    QCOMPARE(
        previewPageMap.value(
                          QStringLiteral(
                              "block-copy"))
            .toString(),
        QStringLiteral(
            "page-copy-full--block-copy"));

    const Invocation applied =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             copyPatch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QJsonArray appliedChanges =
        appliedRoot.value(
                       QStringLiteral(
                           "result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        appliedChanges.at(4)
            .toObject()
            .value(
                QStringLiteral(
                    "id_mapping"))
            .toObject(),
        previewBlockMap);
    const QString copiedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        copiedRevision !=
        originalRevision);

    const Invocation copiedBlock =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral("block-copy")});
    QCOMPARE(copiedBlock.exitCode, 0);
    const QJsonObject block =
        json(copiedBlock)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        block.value(
                 QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "Copied Control"));
    QCOMPARE(
        block.value(
                 QStringLiteral(
                     "registers"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "block-copy--reg-control"));
    const Invocation originalBlock =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "block-control")});
    QCOMPARE(originalBlock.exitCode, 0);
    QVERIFY(
        block.value(
                 QStringLiteral("source"))
            .toObject()
            .value(
                QStringLiteral("cell"))
            .toString() !=
        json(originalBlock)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("source"))
            .toObject()
            .value(
                QStringLiteral("cell"))
            .toString());

    const Invocation copiedRegister =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral("reg-copy")});
    QCOMPARE(copiedRegister.exitCode, 0);
    const QJsonObject reg =
        json(copiedRegister)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        reg.value(
               QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x8"));
    QCOMPARE(
        reg.value(
               QStringLiteral("fields"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "reg-copy--field-enable"));

    const Invocation copiedField =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral("field-copy")});
    QCOMPARE(copiedField.exitCode, 0);
    const QJsonObject field =
        json(copiedField)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        field.value(
                 QStringLiteral("lsb"))
            .toInt(),
        4);
    QCOMPARE(
        field.value(
                 QStringLiteral(
                     "enum_values"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-copy--enum-enable-off"));

    const Invocation enumParent =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("enum"),
             QStringLiteral("--parent"),
             QStringLiteral(
                 "reg-bool-target")});
    QCOMPARE(enumParent.exitCode, 0);
    QCOMPARE(
        json(enumParent)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral("enum-copy"));
    QCOMPARE(
        json(enumParent)
            .value(
                QStringLiteral("result"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("OFF Copy"));

    const Invocation copiedPage =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "page-copy-full")});
    QCOMPARE(copiedPage.exitCode, 0);
    QCOMPARE(
        json(copiedPage)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("blocks"))
            .toArray()
            .at(0)
            .toObject()
            .value(
                QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "page-copy-full--block-copy"));
    QCOMPARE(
        json(copiedPage)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "Copy Target Copy"));

    const QByteArray beforeRejected =
        readFile(project);
    const QString collisionPatch =
        directory.filePath(
            QStringLiteral(
                "copy-id-collision.json"));
    QVERIFY(
        writePatch(
            collisionPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "new_id"),
                     QStringLiteral(
                         "reg-copy")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                }},
            copiedRevision));
    const Invocation collision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             collisionPatch});
    QCOMPARE(
        collision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QString invalidLayoutPatch =
        directory.filePath(
            QStringLiteral(
                "copy-invalid-layout.json"));
    QVERIFY(
        writePatch(
            invalidLayoutPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "new_id"),
                     QStringLiteral(
                         "field-invalid")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "INVALID")},
                         {QStringLiteral(
                              "lsb"),
                          0}}},
                }},
            copiedRevision));
    const Invocation invalidLayout =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidLayoutPatch});
    QCOMPARE(
        invalidLayout.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    const Invocation missingInvalid =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-invalid")});
    QCOMPARE(
        missingInvalid.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));

    const Invocation validation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::autoPlacesCopiedHierarchyRoots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "block-copy-auto")},
            {QStringLiteral("parent_id"),
             QStringLiteral(
                 "page-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Control Auto Copy")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "reg-copy-auto")},
            {QStringLiteral("parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "CONTROL_AUTO_COPY")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")}}},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("copy")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-enable")},
            {QStringLiteral("new_id"),
             QStringLiteral(
                 "field-copy-auto")},
            {QStringLiteral("parent_id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "ENABLE_AUTO_COPY")},
                 {QStringLiteral("lsb"),
                  QStringLiteral("auto")}}},
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "auto-copy.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(), 3);
    QCOMPARE(
        previewChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x120"));
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject()
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x8"));
    const QJsonObject previewField =
        previewChanges.at(2)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toObject();
    QCOMPARE(
        previewField.value(
                        QStringLiteral("lsb"))
            .toInt(),
        1);
    QCOMPARE(
        previewField.value(
                        QStringLiteral("msb"))
            .toInt(),
        1);
    QCOMPARE(
        previewField.value(
                        QStringLiteral("reset"))
            .toString(),
        QStringLiteral("0x0"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const auto verifyUnsignedProperty =
        [&](const QString& id,
            const QString& property,
            const QJsonValue& expected) {
            const Invocation object =
                invokeExecutable(
                    {QStringLiteral("--json"),
                     QStringLiteral("get"),
                     project,
                     id});
            QCOMPARE(object.exitCode, 0);
            QCOMPARE(
                json(object)
                    .value(
                        QStringLiteral("result"))
                    .toObject()
                    .value(property),
                expected);
        };
    verifyUnsignedProperty(
        QStringLiteral("block-copy-auto"),
        QStringLiteral("base"),
        QStringLiteral("0x120"));
    verifyUnsignedProperty(
        QStringLiteral("reg-copy-auto"),
        QStringLiteral("offset"),
        QStringLiteral("0x8"));
    verifyUnsignedProperty(
        QStringLiteral("field-copy-auto"),
        QStringLiteral("lsb"),
        1);

    const QByteArray beforeRejected =
        readFile(project);
    const QString ambiguousPatch =
        directory.filePath(
            QStringLiteral(
                "auto-copy-field-ambiguous.json"));
    QVERIFY(
        writePatch(
            ambiguousPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral("new_id"),
                     QStringLiteral(
                         "field-copy-ambiguous")},
                    {QStringLiteral("parent_id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral("name"),
                          QStringLiteral(
                              "AMBIGUOUS")},
                         {QStringLiteral("lsb"),
                          QStringLiteral("auto")},
                         {QStringLiteral("msb"),
                          4}}},
                },
            },
            appliedRevision));
    const Invocation ambiguous =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             ambiguousPatch});
    QCOMPARE(
        ambiguous.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(ambiguous)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "accepts 'width', not an explicit 'msb'")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    const QString missingSizePatch =
        directory.filePath(
            QStringLiteral(
                "auto-copy-block-no-size.json"));
    QVERIFY(
        writePatch(
            missingSizePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral("new_id"),
                     QStringLiteral(
                         "block-copy-no-size")},
                    {QStringLiteral("parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral("name"),
                          QStringLiteral(
                              "No Size")},
                         {QStringLiteral("base"),
                          QStringLiteral("auto")},
                         {QStringLiteral("size"),
                          QJsonValue(
                              QJsonValue::Null)}}},
                },
            },
            appliedRevision));
    const Invocation missingSize =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             missingSizePatch});
    QCOMPARE(
        missingSize.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(missingSize)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "positive explicit or copied 'size'")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::copiesWithAutomaticIdentityAndName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const auto automaticCopy =
        [](const QString& source,
           const QString& parent,
           const QJsonObject& value) {
            return QJsonObject{
                {QStringLiteral("op"),
                 QStringLiteral("copy")},
                {QStringLiteral("id"),
                 source},
                {QStringLiteral("new_id"),
                 QStringLiteral("auto")},
                {QStringLiteral("parent_id"),
                 parent},
                {QStringLiteral("unique_name"),
                 true},
                {QStringLiteral("value"),
                 value},
            };
        };
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "reg-control-copy--field-enable")},
            {QStringLiteral("parent_id"),
             QStringLiteral("block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "ID_COLLISION")},
                 {QStringLiteral("offset"),
                  QStringLiteral("0x8")},
                 {QStringLiteral("width"), 32},
                 {QStringLiteral("type"),
                  QStringLiteral("unsigned")},
             }},
        },
        automaticCopy(
            QStringLiteral("reg-control"),
            QStringLiteral("block-control"),
            QJsonObject{
                {QStringLiteral("offset"),
                 QStringLiteral("auto")},
            }),
        automaticCopy(
            QStringLiteral("reg-control"),
            QStringLiteral("block-control"),
            QJsonObject{
                {QStringLiteral("offset"),
                 QStringLiteral("auto")},
            }),
        automaticCopy(
            QStringLiteral("field-enable"),
            QStringLiteral("reg-control"),
            QJsonObject{
                {QStringLiteral("lsb"),
                 QStringLiteral("auto")},
            }),
        automaticCopy(
            QStringLiteral("block-control"),
            QStringLiteral("page-main"),
            QJsonObject{
                {QStringLiteral("base"),
                 QStringLiteral("auto")},
            }),
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "copy-workbench-defaults.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.size(),
        operations.size());

    const auto copiedAfter =
        [&previewChanges](int index) {
            const QJsonObject change =
                previewChanges.at(index)
                    .toObject();
            if (!change.value(
                           QStringLiteral(
                               "automatic_id"))
                     .toBool() ||
                !change.value(
                           QStringLiteral(
                               "unique_name"))
                     .toBool()) {
                return QJsonObject{};
            }
            return change.value(
                             QStringLiteral(
                                 "after"))
                .toObject();
        };
    const QJsonObject first =
        copiedAfter(1);
    QCOMPARE(
        first.value(
                 QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "reg-control-copy-2"));
    QCOMPARE(
        first.value(
                 QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "CONTROL Copy"));
    QCOMPARE(
        first.value(
                 QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0xC"));
    QCOMPARE(
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral(
                    "id_mapping"))
            .toObject()
            .value(
                QStringLiteral(
                    "field-enable"))
            .toString(),
        QStringLiteral(
            "reg-control-copy-2--field-enable"));

    const QJsonObject second =
        copiedAfter(2);
    QCOMPARE(
        second.value(
                  QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "reg-control-copy-3"));
    QCOMPARE(
        second.value(
                  QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "CONTROL Copy 2"));
    QCOMPARE(
        second.value(
                  QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x10"));

    const QJsonObject field =
        copiedAfter(3);
    QCOMPARE(
        field.value(
                 QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-enable-copy"));
    QCOMPARE(
        field.value(
                 QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "ENABLE Copy"));
    QCOMPARE(
        field.value(
                 QStringLiteral("lsb"))
            .toInt(),
        1);
    QCOMPARE(
        field.value(
                 QStringLiteral("reset"))
            .toString(),
        QStringLiteral("0x0"));

    const QJsonObject block =
        copiedAfter(4);
    QCOMPARE(
        block.value(
                 QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "block-control-copy"));
    QCOMPARE(
        block.value(
                 QStringLiteral("name"))
            .toString(),
        QStringLiteral(
            "Control Copy"));
    QCOMPARE(
        block.value(
                 QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x120"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QJsonArray appliedChanges =
        appliedRoot.value(
                       QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        appliedChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("id")),
        previewChanges.at(1)
            .toObject()
            .value(
                QStringLiteral("id")));
    QCOMPARE(
        appliedChanges.at(2)
            .toObject()
            .value(
                QStringLiteral("id")),
        previewChanges.at(2)
            .toObject()
            .value(
                QStringLiteral("id")));
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const auto object =
        [&project](const QString& id) {
            const Invocation result =
                invokeExecutable(
                    {QStringLiteral("--json"),
                     QStringLiteral("get"),
                     project,
                     id});
            if (result.exitCode != 0) {
                return QJsonObject{};
            }
            return json(result)
                .value(
                    QStringLiteral("result"))
                .toObject();
        };
    QCOMPARE(
        object(
            QStringLiteral(
                "reg-control-copy-2"))
            .value(
                QStringLiteral("name"))
            .toString(),
        QStringLiteral("CONTROL Copy"));
    QCOMPARE(
        object(
            QStringLiteral(
                "field-enable-copy"))
            .value(
                QStringLiteral("lsb"))
            .toInt(),
        1);

    const QByteArray beforeRejected =
        readFile(project);
    const QString conflictingNamePatch =
        directory.filePath(
            QStringLiteral(
                "copy-conflicting-name-mode.json"));
    QVERIFY(
        writePatch(
            conflictingNamePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("new_id"),
                     QStringLiteral("auto")},
                    {QStringLiteral("parent_id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral("unique_name"),
                     true},
                    {QStringLiteral("value"),
                     QJsonObject{
                         {QStringLiteral("name"),
                          QStringLiteral(
                              "MANUAL")},
                         {QStringLiteral("offset"),
                          QStringLiteral(
                              "auto")},
                     }},
                },
            },
            appliedRevision));
    const Invocation conflictingName =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             conflictingNamePatch});
    QCOMPARE(
        conflictingName.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(conflictingName)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "cannot be combined")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);

    const QString invalidModePatch =
        directory.filePath(
            QStringLiteral(
                "copy-invalid-name-mode.json"));
    QVERIFY(
        writePatch(
            invalidModePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("copy")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("new_id"),
                     QStringLiteral("auto")},
                    {QStringLiteral("parent_id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral("unique_name"),
                     QStringLiteral("yes")},
                },
            },
            appliedRevision));
    const Invocation invalidMode =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidModePatch});
    QCOMPARE(
        invalidMode.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(invalidMode)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "must be a boolean")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::movesObjectsBetweenParentsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QString movePatch =
        directory.filePath(
            QStringLiteral(
                "move-hierarchy.json"));
    QVERIFY(
        writePatch(
            movePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "page-destination")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Destination")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x2000")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-destination")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "Destination")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x200")},
                         {QStringLiteral(
                              "size"),
                          QStringLiteral(
                              "0x100")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-destination")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-destination")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "DEST_STRUCT")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "0x0")},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "field")},
                         {QStringLiteral(
                              "initial"),
                          QStringLiteral(
                              "0x1")},
                         {QStringLiteral(
                              "reset"),
                          QStringLiteral(
                              "0x1")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-bool-destination")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-destination")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "DEST_BOOL")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "0x8")},
                         {QStringLiteral(
                              "width"),
                          1},
                         {QStringLiteral(
                              "type"),
                          QStringLiteral(
                              "bool")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("enum")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "enum-movable")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "OFF")},
                         {QStringLiteral(
                              "value"),
                          0}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "enum-movable")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-bool-destination")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-destination")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-destination")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-destination")},
                },
            },
            originalRevision));

    const Invocation moved =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             movePatch});
    QCOMPARE(moved.exitCode, 0);
    const QJsonObject movedRoot =
        json(moved);
    QVERIFY(
        movedRoot.value(
                     QStringLiteral("ok"))
            .toBool());
    const QJsonObject movedResult =
        movedRoot.value(
                     QStringLiteral(
                         "result"))
            .toObject();
    QVERIFY(
        movedResult.value(
                       QStringLiteral(
                           "saved"))
            .toBool());
    QCOMPARE(
        movedResult.value(
                       QStringLiteral(
                           "changes"))
            .toArray()
            .size(),
        9);
    const QString movedRevision =
        movedRoot.value(
                     QStringLiteral(
                         "revision"))
            .toString();
    QVERIFY(
        movedRevision !=
        originalRevision);

    const auto parentFor =
        [&project](
            const QString& id) {
            const Invocation list =
                invoke(
                    {QStringLiteral(
                         "--json"),
                     QStringLiteral("list"),
                     project});
            if (list.exitCode != 0) {
                return QString{};
            }
            const QJsonArray objects =
                json(list)
                    .value(
                        QStringLiteral(
                            "result"))
                    .toArray();
            for (const QJsonValue&
                     value : objects) {
                const QJsonObject object =
                    value.toObject();
                if (object.value(
                              QStringLiteral(
                                  "id"))
                        .toString() ==
                    id) {
                    return object.value(
                                     QStringLiteral(
                                         "parent_id"))
                        .toString();
                }
            }
            return QString{};
        };
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "block-control")),
        QStringLiteral(
            "page-destination"));
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "reg-control")),
        QStringLiteral(
            "block-destination"));
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "field-enable")),
        QStringLiteral(
            "reg-destination"));
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "enum-movable")),
        QStringLiteral(
            "reg-bool-destination"));

    const QByteArray beforeNoOp =
        readFile(project);
    const QString noOpPatch =
        directory.filePath(
            QStringLiteral(
                "move-no-op.json"));
    QVERIFY(
        writePatch(
            noOpPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-destination")},
                }},
            movedRevision));
    const Invocation noOp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             noOpPatch});
    QCOMPARE(noOp.exitCode, 0);
    const QJsonObject noOpResult =
        json(noOp)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !noOpResult.value(
                       QStringLiteral(
                           "changed"))
             .toBool());
    QVERIFY(
        !noOpResult.value(
                       QStringLiteral(
                           "saved"))
             .toBool());
    QCOMPARE(
        readFile(project),
        beforeNoOp);

    const QString rejectedPatch =
        directory.filePath(
            QStringLiteral(
                "move-rejected.json"));
    QVERIFY(
        writePatch(
            rejectedPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "field-enable")},
                },
            },
            movedRevision));
    const Invocation rejected =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             rejectedPatch});
    QCOMPARE(
        rejected.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejected)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "itself")));
    QCOMPARE(
        readFile(project),
        beforeNoOp);
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "reg-control")),
        QStringLiteral(
            "block-destination"));

    const QString conflictPatch =
        directory.filePath(
            QStringLiteral(
                "move-address-conflict.json"));
    QVERIFY(
        writePatch(
            conflictPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral(
                         "register")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-conflict")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "CONFLICT")},
                         {QStringLiteral(
                              "offset"),
                          QStringLiteral(
                              "0x4")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "block-control")},
                },
            },
            movedRevision));
    const Invocation conflict =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             conflictPatch});
    QCOMPARE(
        conflict.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QVERIFY(
        !json(conflict)
             .value(
                 QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        readFile(project),
        beforeNoOp);
    QCOMPARE(
        parentFor(
            QStringLiteral(
                "reg-control")),
        QStringLiteral(
            "block-destination"));
    const Invocation missingConflict =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-conflict")});
    QCOMPARE(
        missingConflict.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));

    const Invocation validation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);
    QVERIFY(
        json(validation)
            .value(
                QStringLiteral("ok"))
            .toBool());

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::autoPlacesMovedHierarchyRoots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const auto add =
        [](const QString& kind,
           const QString& id,
           const QString& parent,
           const QJsonObject& value) {
            return QJsonObject{
                {QStringLiteral("op"),
                 QStringLiteral("add")},
                {QStringLiteral("kind"),
                 kind},
                {QStringLiteral("id"),
                 id},
                {QStringLiteral("parent_id"),
                 parent},
                {QStringLiteral("value"),
                 value},
            };
        };
    const auto moveAutomatically =
        [](const QString& id,
           const QString& parent) {
            return QJsonObject{
                {QStringLiteral("op"),
                 QStringLiteral("move")},
                {QStringLiteral("id"),
                 id},
                {QStringLiteral("parent_id"),
                 parent},
                {QStringLiteral("placement"),
                 QStringLiteral("auto")},
            };
        };

    const QJsonArray operations{
        add(
            QStringLiteral("page"),
            QStringLiteral("page-move-auto"),
            QStringLiteral("workspace-main"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral(
                     "Move Target")},
                {QStringLiteral("base"),
                 QStringLiteral("0x4000")},
                {QStringLiteral("address_width"),
                 16},
            }),
        add(
            QStringLiteral("block"),
            QStringLiteral(
                "block-page-occupied"),
            QStringLiteral("page-move-auto"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("Occupied")},
                {QStringLiteral("base"),
                 QStringLiteral("0x0")},
                {QStringLiteral("size"),
                 QStringLiteral("0x100")},
            }),
        moveAutomatically(
            QStringLiteral("block-control"),
            QStringLiteral("page-move-auto")),
        add(
            QStringLiteral("block"),
            QStringLiteral(
                "block-register-target"),
            QStringLiteral("page-move-auto"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral(
                     "Register Target")},
                {QStringLiteral("base"),
                 QStringLiteral("0x300")},
                {QStringLiteral("size"),
                 QStringLiteral("0x100")},
            }),
        add(
            QStringLiteral("register"),
            QStringLiteral(
                "reg-target-occupied"),
            QStringLiteral(
                "block-register-target"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("OCCUPIED")},
                {QStringLiteral("offset"),
                 QStringLiteral("0x0")},
                {QStringLiteral("width"), 32},
                {QStringLiteral("type"),
                 QStringLiteral("unsigned")},
            }),
        moveAutomatically(
            QStringLiteral("reg-control"),
            QStringLiteral(
                "block-register-target")),
        add(
            QStringLiteral("field"),
            QStringLiteral(
                "field-remains"),
            QStringLiteral("reg-control"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("REMAINS")},
                {QStringLiteral("lsb"), 2},
                {QStringLiteral("width"), 1},
                {QStringLiteral("type"),
                 QStringLiteral("bits")},
            }),
        add(
            QStringLiteral("register"),
            QStringLiteral(
                "reg-field-target"),
            QStringLiteral(
                "block-register-target"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral(
                     "FIELD_TARGET")},
                {QStringLiteral("offset"),
                 QStringLiteral("0x8")},
                {QStringLiteral("width"), 32},
                {QStringLiteral("type"),
                 QStringLiteral("field")},
                {QStringLiteral("initial"), 0},
                {QStringLiteral("reset"), 0},
            }),
        add(
            QStringLiteral("field"),
            QStringLiteral(
                "field-target-occupied"),
            QStringLiteral(
                "reg-field-target"),
            QJsonObject{
                {QStringLiteral("name"),
                 QStringLiteral("OCCUPIED")},
                {QStringLiteral("lsb"), 0},
                {QStringLiteral("width"), 1},
                {QStringLiteral("type"),
                 QStringLiteral("bits")},
            }),
        moveAutomatically(
            QStringLiteral("field-enable"),
            QStringLiteral(
                "reg-field-target")),
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "auto-move.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray changes =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        changes.size(),
        operations.size());
    const auto placementFor =
        [&changes](int index) {
            const QJsonObject change =
                changes.at(index)
                    .toObject();
            if (!change.value(
                           QStringLiteral(
                               "automatic_placement"))
                     .toBool()) {
                return QJsonObject{};
            }
            return change.value(
                             QStringLiteral(
                                 "placement_result"))
                .toObject();
        };
    QCOMPARE(
        placementFor(2)
            .value(
                QStringLiteral("value"))
            .toString(),
        QStringLiteral("0x100"));
    QCOMPARE(
        placementFor(5)
            .value(
                QStringLiteral("value"))
            .toString(),
        QStringLiteral("0x4"));
    QCOMPARE(
        placementFor(9)
            .value(
                QStringLiteral("value"))
            .toInt(),
        1);
    QCOMPARE(
        placementFor(9)
            .value(
                QStringLiteral("msb"))
            .toInt(),
        1);

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const auto object =
        [&project](const QString& id) {
            const Invocation result =
                invokeExecutable(
                    {QStringLiteral("--json"),
                     QStringLiteral("get"),
                     project,
                     id});
            if (result.exitCode != 0) {
                return QJsonObject{};
            }
            return json(result)
                .value(
                    QStringLiteral("result"))
                .toObject();
        };
    QCOMPARE(
        object(
            QStringLiteral("block-control"))
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x100"));
    QCOMPARE(
        object(
            QStringLiteral("reg-control"))
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x4"));
    const QJsonObject movedField =
        object(
            QStringLiteral("field-enable"));
    QCOMPARE(
        movedField.value(
                      QStringLiteral("lsb"))
            .toInt(),
        1);
    QCOMPARE(
        movedField.value(
                      QStringLiteral("reset"))
            .toString(),
        QStringLiteral("0x0"));

    const QByteArray beforeRejected =
        readFile(project);
    const QString fullPatch =
        directory.filePath(
            QStringLiteral(
                "auto-move-full.json"));
    QVERIFY(
        writePatch(
            fullPatch,
            QJsonArray{
                add(
                    QStringLiteral("page"),
                    QStringLiteral(
                        "page-move-full"),
                    QStringLiteral(
                        "workspace-main"),
                    QJsonObject{
                        {QStringLiteral("name"),
                         QStringLiteral("Full")},
                        {QStringLiteral(
                             "address_width"),
                         8},
                    }),
                add(
                    QStringLiteral("block"),
                    QStringLiteral(
                        "block-move-full"),
                    QStringLiteral(
                        "page-move-full"),
                    QJsonObject{
                        {QStringLiteral("name"),
                         QStringLiteral(
                             "Full Block")},
                        {QStringLiteral("base"),
                         QStringLiteral("0x0")},
                        {QStringLiteral("size"),
                         QStringLiteral("0x100")},
                    }),
                moveAutomatically(
                    QStringLiteral(
                        "block-control"),
                    QStringLiteral(
                        "page-move-full")),
            },
            appliedRevision));
    const Invocation full =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             fullPatch});
    QCOMPARE(
        full.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(full)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "found no free 256-byte range")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);
    QCOMPARE(
        object(
            QStringLiteral("block-control"))
            .value(
                QStringLiteral("base"))
            .toString(),
        QStringLiteral("0x100"));

    const QString invalidPlacementPatch =
        directory.filePath(
            QStringLiteral(
                "auto-move-invalid-token.json"));
    QVERIFY(
        writePatch(
            invalidPlacementPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral("parent_id"),
                     QStringLiteral(
                         "block-register-target")},
                    {QStringLiteral("placement"),
                     QStringLiteral("AUTO")},
                },
            },
            appliedRevision));
    const Invocation invalidPlacement =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidPlacementPatch});
    QCOMPARE(
        invalidPlacement.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(invalidPlacement)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "exact string 'auto'")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::reordersPagesAndBlocksWithBeforeId()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString originalRevision =
        currentRevision(project);

    const QString reorderPatch =
        directory.filePath(
            QStringLiteral(
                "reorder-hierarchy.json"));
    QVERIFY(
        writePatch(
            reorderPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral("page-b")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral("B")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x2000")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("page")},
                    {QStringLiteral("id"),
                     QStringLiteral("page-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral("C")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x3000")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral("block-b")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral("B")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x200")},
                         {QStringLiteral(
                              "size"),
                          QStringLiteral(
                              "0x100")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("block")},
                    {QStringLiteral("id"),
                     QStringLiteral("block-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral("C")},
                         {QStringLiteral(
                              "base"),
                          QStringLiteral(
                              "0x400")},
                         {QStringLiteral(
                              "size"),
                          QStringLiteral(
                              "0x100")}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("page-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "before_id"),
                     QStringLiteral(
                         "page-main")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("block-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "before_id"),
                     QStringLiteral(
                         "block-control")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("block-b")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-c")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "block-control")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "before_id"),
                     QStringLiteral(
                         "block-c")},
                },
            },
            originalRevision));
    const Invocation reordered =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             reorderPatch});
    QCOMPARE(reordered.exitCode, 0);
    const QJsonObject reorderedRoot =
        json(reordered);
    QVERIFY(
        reorderedRoot.value(
                          QStringLiteral(
                              "ok"))
            .toBool());
    QCOMPARE(
        reorderedRoot.value(
                          QStringLiteral(
                              "result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray()
            .size(),
        8);
    const QString reorderedRevision =
        reorderedRoot.value(
                          QStringLiteral(
                              "revision"))
            .toString();
    QVERIFY(
        reorderedRevision !=
        originalRevision);

    const auto idsFor =
        [&project](
            const QString& kind,
            const QString& parent) {
            const Invocation list =
                invoke(
                    {QStringLiteral(
                         "--json"),
                     QStringLiteral("list"),
                     project,
                     QStringLiteral(
                         "--kind"),
                     kind,
                     QStringLiteral(
                         "--parent"),
                     parent});
            QStringList ids;
            if (list.exitCode != 0) {
                return ids;
            }
            for (const QJsonValue&
                     value :
                 json(list)
                     .value(
                         QStringLiteral(
                             "result"))
                     .toArray()) {
                ids.push_back(
                    value.toObject()
                        .value(
                            QStringLiteral(
                                "id"))
                        .toString());
            }
            return ids;
        };
    QCOMPARE(
        idsFor(
            QStringLiteral("page"),
            QStringLiteral(
                "workspace-main")),
        QStringList({
            QStringLiteral("page-c"),
            QStringLiteral("page-main"),
            QStringLiteral("page-b")}));
    QCOMPARE(
        idsFor(
            QStringLiteral("block"),
            QStringLiteral("page-main")),
        QStringList({
            QStringLiteral(
                "block-control"),
            QStringLiteral("block-c")}));
    QCOMPARE(
        idsFor(
            QStringLiteral("block"),
            QStringLiteral("page-c")),
        QStringList({
            QStringLiteral("block-b")}));

    const QString moveToEndPatch =
        directory.filePath(
            QStringLiteral(
                "page-to-end.json"));
    QVERIFY(
        writePatch(
            moveToEndPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("page-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                    {QStringLiteral(
                         "before_id"),
                     QJsonValue(
                         QJsonValue::Null)},
                }},
            reorderedRevision));
    const Invocation movedToEnd =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             moveToEndPatch});
    QCOMPARE(movedToEnd.exitCode, 0);
    const QString endRevision =
        json(movedToEnd)
            .value(
                QStringLiteral(
                    "revision"))
            .toString();
    QCOMPARE(
        idsFor(
            QStringLiteral("page"),
            QStringLiteral(
                "workspace-main")),
        QStringList({
            QStringLiteral("page-main"),
            QStringLiteral("page-b"),
            QStringLiteral("page-c")}));

    const QByteArray beforeNoOp =
        readFile(project);
    const QString noOpPatch =
        directory.filePath(
            QStringLiteral(
                "page-same-parent.json"));
    QVERIFY(
        writePatch(
            noOpPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("page-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "workspace-main")},
                }},
            endRevision));
    const Invocation noOp =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             noOpPatch});
    QCOMPARE(noOp.exitCode, 0);
    QVERIFY(
        !json(noOp)
             .value(
                 QStringLiteral("result"))
             .toObject()
             .value(
                 QStringLiteral("changed"))
             .toBool());
    QCOMPARE(
        readFile(project),
        beforeNoOp);

    const QString invalidPatch =
        directory.filePath(
            QStringLiteral(
                "block-invalid-before.json"));
    QVERIFY(
        writePatch(
            invalidPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("move")},
                    {QStringLiteral("id"),
                     QStringLiteral("block-c")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "page-main")},
                    {QStringLiteral(
                         "before_id"),
                     QStringLiteral("block-b")},
                }},
            endRevision));
    const Invocation invalid =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             invalidPatch});
    QCOMPARE(
        invalid.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(invalid)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "destination parent")));
    QCOMPARE(
        readFile(project),
        beforeNoOp);
    QCOMPARE(
        idsFor(
            QStringLiteral("block"),
            QStringLiteral("page-main")),
        QStringList({
            QStringLiteral(
                "block-control"),
            QStringLiteral("block-c")}));

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::reordersRegistersAroundFixedAddresses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray originalProject =
        readFile(project);
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const auto named =
        [](const QString& reference) {
            return QJsonObject{
                {QStringLiteral("ref"),
                 reference},
            };
        };
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "register_a")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("A")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "fixed_register")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("LOCKED")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
                 {QStringLiteral("fixed"),
                  true},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "register_b")},
            {QStringLiteral("kind"),
             QStringLiteral("register")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral("B")},
                 {QStringLiteral("offset"),
                  QStringLiteral("auto")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "reordered_b")},
            {QStringLiteral("id"),
             named(
                 QStringLiteral(
                     "register_b"))},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral(
                 "before_id"),
             named(
                 QStringLiteral(
                     "register_a"))},
        },
    };
    const QString patch =
        directory.filePath(
            QStringLiteral(
                "register-reorder.json"));
    QVERIFY(
        writePatch(
            patch,
            operations,
            originalRevision));

    const Invocation preview =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(previewChanges.size(), 4);
    const QJsonObject reorderChange =
        previewChanges.at(3)
            .toObject();
    QCOMPARE(
        reorderChange.value(
                         QStringLiteral("id"))
            .toString(),
        QStringLiteral("register-b"));
    QCOMPARE(
        reorderChange.value(
                         QStringLiteral(
                             "placement_before_id"))
            .toString(),
        QStringLiteral("register-a"));
    QVERIFY(
        !reorderChange.value(
                          QStringLiteral(
                              "automatic_placement"))
             .toBool());
    QCOMPARE(
        reorderChange.value(
                         QStringLiteral(
                             "placement_result"))
            .toObject()
            .value(
                QStringLiteral("value"))
            .toString(),
        QStringLiteral("0x8"));

    const Invocation applied =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    const QString appliedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        appliedRevision !=
        originalRevision);

    const auto object =
        [&project](const QString& id) {
            const Invocation result =
                invokeExecutable(
                    {QStringLiteral("--json"),
                     QStringLiteral("get"),
                     project,
                     id});
            if (result.exitCode != 0) {
                return QJsonObject{};
            }
            return json(result)
                .value(
                    QStringLiteral("result"))
                .toObject();
        };
    QCOMPARE(
        object(
            QStringLiteral("register-b"))
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x8"));
    const QJsonObject fixed =
        object(
            QStringLiteral(
                "register-locked"));
    QCOMPARE(
        fixed.value(
                 QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0xC"));
    QVERIFY(
        fixed.value(
                 QStringLiteral("fixed"))
            .toBool());
    QCOMPARE(
        object(
            QStringLiteral("register-a"))
            .value(
                QStringLiteral("offset"))
            .toString(),
        QStringLiteral("0x10"));

    const Invocation listed =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("list"),
             project,
             QStringLiteral("--kind"),
             QStringLiteral("register"),
             QStringLiteral("--parent"),
             QStringLiteral(
                 "block-control")});
    QCOMPARE(listed.exitCode, 0);
    QStringList orderedIds;
    for (const QJsonValue& value :
         json(listed)
             .value(
                 QStringLiteral("result"))
             .toArray()) {
        orderedIds.append(
            value.toObject()
                .value(
                    QStringLiteral("id"))
                .toString());
    }
    const QStringList expectedOrder{
        QStringLiteral("reg-control"),
        QStringLiteral("register-b"),
        QStringLiteral(
            "register-locked"),
        QStringLiteral("register-a"),
    };
    QCOMPARE(
        orderedIds,
        expectedOrder);

    const QByteArray beforeRejected =
        readFile(project);
    const QJsonArray fixedMove{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-locked")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral(
                 "before_id"),
             QStringLiteral(
                 "register-b")},
        },
    };
    const Invocation rejectedFixed =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                fixedMove,
                appliedRevision));
    QCOMPARE(
        rejectedFixed.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedFixed)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("failure"))
            .toObject()
            .value(
                QStringLiteral("message"))
            .toString()
            .contains(
                QStringLiteral(
                    "fixed address")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QJsonArray ambiguousMove{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-a")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral(
                 "block-control")},
            {QStringLiteral(
                 "before_id"),
             QStringLiteral(
                 "register-b")},
            {QStringLiteral(
                 "placement"),
             QStringLiteral("auto")},
        },
    };
    const Invocation rejectedAmbiguous =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                ambiguousMove,
                appliedRevision));
    QCOMPARE(
        rejectedAmbiguous.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedAmbiguous)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "cannot be combined")));
    QCOMPARE(
        readFile(project),
        beforeRejected);

    const QJsonArray crossBlockMove{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("add")},
            {QStringLiteral("ref"),
             QStringLiteral(
                 "destination_block")},
            {QStringLiteral("kind"),
             QStringLiteral("block")},
            {QStringLiteral("id"),
             QStringLiteral("auto")},
            {QStringLiteral(
                 "parent_id"),
             QStringLiteral("page-main")},
            {QStringLiteral("value"),
             QJsonObject{
                 {QStringLiteral("name"),
                  QStringLiteral(
                      "Destination")},
                 {QStringLiteral("base"),
                  QStringLiteral("auto")},
                 {QStringLiteral("size"),
                  QStringLiteral("0x100")},
             }},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("move")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "register-a")},
            {QStringLiteral(
                 "parent_id"),
             named(
                 QStringLiteral(
                     "destination_block"))},
            {QStringLiteral(
                 "before_id"),
             QStringLiteral(
                 "register-b")},
        },
    };
    const Invocation rejectedCrossBlock =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             QStringLiteral("-")},
            patchText(
                crossBlockMove,
                appliedRevision));
    QCOMPARE(
        rejectedCrossBlock.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(rejectedCrossBlock)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "existing parent block")));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    QCOMPARE(
        currentRevision(project),
        appliedRevision);
    const Invocation missingDestination =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "block-destination")});
    QCOMPARE(
        missingDestination.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));

    const Invocation validation =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(validation.exitCode, 0);
    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::repositionsFieldLsbWithoutChangingIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString originalRevision =
        currentRevision(project);
    QVERIFY(!originalRevision.isEmpty());

    const QString repositionPatch =
        directory.filePath(
            QStringLiteral(
                "reposition-field.json"));
    const QJsonArray operations{
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "field-enable")},
            {QStringLiteral(
                 "property"),
             QStringLiteral("lsb")},
            {QStringLiteral("value"),
             4},
        },
        QJsonObject{
            {QStringLiteral("op"),
             QStringLiteral("set")},
            {QStringLiteral("id"),
             QStringLiteral(
                 "reg-control")},
            {QStringLiteral(
                 "property"),
             QStringLiteral("reset")},
            {QStringLiteral("value"),
             QStringLiteral(
                 "0x10")},
        },
    };
    QVERIFY(
        writePatch(
            repositionPatch,
            operations,
            originalRevision));

    const QByteArray originalProject =
        readFile(project);
    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             repositionPatch,
             QStringLiteral(
                 "--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    QCOMPARE(
        readFile(project),
        originalProject);
    const QJsonArray previewChanges =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("changes"))
            .toArray();
    QCOMPARE(
        previewChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("before"))
            .toInt(),
        0);
    QCOMPARE(
        previewChanges.at(0)
            .toObject()
            .value(
                QStringLiteral("after"))
            .toInt(),
        4);

    const Invocation applied =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             repositionPatch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QString repositionedRevision =
        appliedRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        repositionedRevision !=
        originalRevision);

    const Invocation get =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-enable")});
    QCOMPARE(get.exitCode, 0);
    const QJsonObject field =
        json(get)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        field.value(
                 QStringLiteral("id"))
            .toString(),
        QStringLiteral(
            "field-enable"));
    QCOMPARE(
        field.value(
                 QStringLiteral("lsb"))
            .toInt(),
        4);
    QCOMPARE(
        field.value(
                 QStringLiteral("msb"))
            .toInt(),
        4);
    QCOMPARE(
        field.value(
                 QStringLiteral("width"))
            .toInt(),
        1);

    const QByteArray beforeRejected =
        readFile(project);
    const QString overlapPatch =
        directory.filePath(
            QStringLiteral(
                "reposition-overlap.json"));
    QVERIFY(
        writePatch(
            overlapPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "reset")},
                    {QStringLiteral("value"),
                     QStringLiteral(
                         "0x100")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("add")},
                    {QStringLiteral("kind"),
                     QStringLiteral("field")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-overlap")},
                    {QStringLiteral(
                         "parent_id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "value"),
                     QJsonObject{
                         {QStringLiteral(
                              "name"),
                          QStringLiteral(
                              "OVERLAP")},
                         {QStringLiteral(
                              "lsb"),
                          8}}},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "lsb")},
                    {QStringLiteral("value"),
                     8},
                },
            },
            repositionedRevision));
    const Invocation overlap =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             overlapPatch});
    QCOMPARE(
        overlap.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    const Invocation missingOverlap =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-overlap")});
    QCOMPARE(
        missingOverlap.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));

    const QString outOfRangePatch =
        directory.filePath(
            QStringLiteral(
                "reposition-out-of-range.json"));
    QVERIFY(
        writePatch(
            outOfRangePatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "field-enable")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "lsb")},
                    {QStringLiteral("value"),
                     32},
                }},
            repositionedRevision));
    const Invocation outOfRange =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             outOfRangePatch});
    QCOMPARE(
        outOfRange.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    QCOMPARE(
        readFile(project),
        beforeRejected);
    const Invocation unchangedField =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "field-enable")});
    QCOMPARE(
        json(unchangedField)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("lsb"))
            .toInt(),
        4);

    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::recoversAfterLockedGeneratedOutput()
{
#ifndef Q_OS_WIN
    QSKIP(
        "The generated-output sharing violation is verified on Windows.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());

    const Invocation initialGeneration =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project});
    QCOMPARE(
        initialGeneration.exitCode,
        0);
    const QString workbook =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "generated/register-map.xlsx"));
    QVERIFY(
        QFileInfo::exists(workbook));
    QVERIFY(
        (QFile::permissions(
             workbook) &
         QFileDevice::WriteOwner) == 0);

    const QString revision =
        currentRevision(project);
    QVERIFY(!revision.isEmpty());
    const HANDLE lockedWorkbook =
        CreateFileW(
            reinterpret_cast<LPCWSTR>(
                workbook.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    QVERIFY(
        lockedWorkbook !=
        INVALID_HANDLE_VALUE);

    const QString lockedPatch =
        directory.filePath(
            QStringLiteral(
                "locked-output.json"));
    QVERIFY(
        writePatch(
            lockedPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Saved while XLSX was locked.")},
                }},
            revision));
    const Invocation lockedApply =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             lockedPatch});
    const Invocation lockedGenerate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project});
    CloseHandle(lockedWorkbook);

    QCOMPARE(
        lockedApply.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    const QJsonObject lockedRoot =
        json(lockedApply);
    QCOMPARE(
        lockedGenerate.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    const QJsonObject
        lockedGenerateRoot =
            json(lockedGenerate);
    const QJsonObject
        lockedGenerateResult =
            lockedGenerateRoot
                .value(
                    QStringLiteral(
                        "result"))
                .toObject();
    QVERIFY(
        !lockedGenerateResult
             .value(
                 QStringLiteral(
                     "outputs_current"))
             .toBool());
    const QJsonObject
        lockedGenerateRecovery =
            lockedGenerateResult
                .value(
                    QStringLiteral(
                        "recovery"))
                .toObject();
    QCOMPARE(
        lockedGenerateRecovery
            .value(
                QStringLiteral(
                    "command"))
            .toString(),
        QStringLiteral("generate"));
    QCOMPARE(
        lockedGenerateRecovery
            .value(
                QStringLiteral(
                    "reason"))
            .toString(),
        QStringLiteral(
            "generated_outputs_incomplete"));
    QCOMPARE(
        lockedGenerateRecovery
            .value(
                QStringLiteral(
                    "expected_revision"))
            .toString(),
        lockedGenerateRoot
            .value(
                QStringLiteral(
                    "revision"))
            .toString());
    QCOMPARE(
        lockedGenerateRecovery
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray()
            .size(),
        4);
    QVERIFY(
        !lockedRoot.value(
                       QStringLiteral("ok"))
             .toBool());
    QCOMPARE(
        lockedRoot.value(
                      QStringLiteral(
                          "exit_code"))
            .toInt(),
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    QCOMPARE(
        lockedRoot.value(
                      QStringLiteral(
                          "exit_status"))
            .toString(),
        QStringLiteral("write_error"));
    QVERIFY(
        !lockedRoot.value(
                       QStringLiteral(
                           "error_code"))
             .toString()
             .isEmpty());
    const QJsonObject lockedResult =
        lockedRoot.value(
                      QStringLiteral(
                          "result"))
            .toObject();
    QVERIFY(
        lockedResult.value(
                        QStringLiteral(
                            "saved"))
            .toBool());
    QVERIFY(
        !lockedResult.value(
                         QStringLiteral(
                             "generated"))
             .toBool());
    QVERIFY(
        !lockedResult.value(
                         QStringLiteral(
                             "outputs_current"))
             .toBool());
    const QJsonObject recovery =
        lockedResult.value(
                        QStringLiteral(
                            "recovery"))
            .toObject();
    QCOMPARE(
        recovery.value(
                    QStringLiteral(
                        "command"))
            .toString(),
        QStringLiteral("generate"));
    QCOMPARE(
        QFileInfo(
            recovery.value(
                        QStringLiteral(
                            "project"))
                .toString())
            .canonicalFilePath(),
        QFileInfo(project)
            .canonicalFilePath());
    const QString savedRevision =
        lockedRoot.value(
                      QStringLiteral(
                          "revision"))
            .toString();
    QVERIFY(
        !savedRevision.isEmpty());
    QCOMPARE(
        recovery.value(
                    QStringLiteral(
                        "expected_revision"))
            .toString(),
        savedRevision);
    const QJsonArray recoveryArguments =
        recovery.value(
                    QStringLiteral(
                        "arguments"))
            .toArray();
    QCOMPARE(
        recoveryArguments.size(),
        4);
    QCOMPARE(
        recoveryArguments.at(0)
            .toString(),
        QStringLiteral(
            "generate"));
    QCOMPARE(
        QFileInfo(
            recoveryArguments.at(1)
                .toString())
            .canonicalFilePath(),
        QFileInfo(project)
            .canonicalFilePath());
    QCOMPARE(
        recoveryArguments.at(2)
            .toString(),
        QStringLiteral(
            "--expect"));
    QCOMPARE(
        recoveryArguments.at(3)
            .toString(),
        savedRevision);
    QVERIFY(
        lockedRoot.value(
                      QStringLiteral(
                          "revision"))
            .toString() !=
        revision);
    QVERIFY(
        (QFile::permissions(
             workbook) &
         QFileDevice::WriteOwner) == 0);

    const Invocation get =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-control")});
    QCOMPARE(get.exitCode, 0);
    QCOMPARE(
        json(get)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "description"))
            .toString(),
        QStringLiteral(
            "Saved while XLSX was locked."));

    const QString textRevision =
        currentRevision(project);
    QVERIFY(!textRevision.isEmpty());
    const HANDLE textLockedWorkbook =
        CreateFileW(
            reinterpret_cast<LPCWSTR>(
                workbook.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    QVERIFY(
        textLockedWorkbook !=
        INVALID_HANDLE_VALUE);
    const QString textPatch =
        directory.filePath(
            QStringLiteral(
                "locked-output-text.json"));
    QVERIFY(
        writePatch(
            textPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Saved from text mode while XLSX was locked.")},
                }},
            textRevision));
    const Invocation textLockedApply =
        invoke(
            {QStringLiteral("apply"),
             project,
             textPatch});
    CloseHandle(
        textLockedWorkbook);
    QCOMPARE(
        textLockedApply.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    QVERIFY(
        textLockedApply.standardError
            .contains(
                QStringLiteral(
                    "Recovery: regmapc generate")));
    QVERIFY(
        textLockedApply.standardError
            .contains(
                QFileInfo(project)
                    .fileName()));
    QVERIFY(
        textLockedApply.standardError
            .contains(
                QStringLiteral(
                    "--expect sha256:")));

    const Invocation repair =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("generate"),
             project});
    QCOMPARE(repair.exitCode, 0);
    QVERIFY(
        json(repair)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "written"))
            .toBool());
    QVERIFY(
        (QFile::permissions(
             workbook) &
         QFileDevice::WriteOwner) == 0);
    QVERIFY(
        readFile(
            QDir(directory.path())
                .filePath(
                    QStringLiteral(
                        "generated/register-map.md")))
            .contains(
                QByteArrayLiteral(
                    "Saved from text mode while XLSX was locked.")));

    const QString secondRevision =
        currentRevision(project);
    const QString secondPatch =
        directory.filePath(
            QStringLiteral(
                "second-apply.json"));
    QVERIFY(
        writePatch(
            secondPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Second successful edit.")},
                }},
            secondRevision));
    const Invocation secondApply =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             secondPatch});
    QCOMPARE(secondApply.exitCode, 0);
    const QJsonObject secondResult =
        json(secondApply)
            .value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        secondResult.value(
                        QStringLiteral(
                            "saved"))
            .toBool());
    QVERIFY(
        secondResult.value(
                        QStringLiteral(
                            "generated"))
            .toBool());
    QVERIFY(
        secondResult.value(
                        QStringLiteral(
                            "outputs_current"))
            .toBool());
    QVERIFY(
        secondResult.value(
                        QStringLiteral(
                            "outputs_written"))
            .toBool());
    QVERIFY(
        readFile(
            QDir(directory.path())
                .filePath(
                    QStringLiteral(
                        "generated/register-map.md")))
            .contains(
                QByteArrayLiteral(
                    "Second successful edit.")));
    QVERIFY(
        (QFile::permissions(
             workbook) &
         QFileDevice::WriteOwner) == 0);

    makeGeneratedFilesWritable(
        directory.path());
#endif
}

void CliTests::repairsInvalidProjectAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createOverlappingProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QByteArray original =
        readFile(project);
    QVERIFY(!original.isEmpty());

    const Invocation invalid =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(
        invalid.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject invalidRoot =
        json(invalid);
    const QString revision =
        invalidRoot.value(
                       QStringLiteral(
                           "revision"))
            .toString();
    QVERIFY(
        revision.startsWith(
            QStringLiteral(
                "sha256:")));
    QVERIFY(
        invalidRoot.value(
                       QStringLiteral(
                           "diagnostics"))
            .toArray()
            .size() >
        0);

    const QString partialPatch =
        directory.filePath(
            QStringLiteral(
                "partial-repair.json"));
    QVERIFY(
        writePatch(
            partialPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-overlap")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral("value"),
                     QStringLiteral(
                         "Still overlapping.")},
                }},
            revision));
    const Invocation partial =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             partialPatch});
    QCOMPARE(
        partial.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject partialRoot =
        json(partial);
    QVERIFY(
        partialRoot.value(
                       QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "leaves validation errors")));
    const QJsonObject partialResult =
        partialRoot.value(
                       QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !partialResult.value(
                          QStringLiteral(
                              "saved"))
             .toBool());
    const QJsonObject partialRepair =
        partialResult.value(
                         QStringLiteral(
                             "repair"))
            .toObject();
    QVERIFY(
        partialRepair.value(
                         QStringLiteral(
                             "baseline_invalid"))
            .toBool());
    QVERIFY(
        !partialRepair.value(
                          QStringLiteral(
                              "candidate_valid"))
             .toBool());
    QCOMPARE(
        partialRepair.value(
                         QStringLiteral(
                             "introduced_problem_count"))
            .toInt(),
        0);
    QVERIFY(
        partialRepair.value(
                         QStringLiteral(
                             "after_error_count"))
                .toInt() >
            0);
    QCOMPARE(
        readFile(project),
        original);

    const QString replacementPatch =
        directory.filePath(
            QStringLiteral(
                "replacement-problem.json"));
    QVERIFY(
        writePatch(
            replacementPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-overlap")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("offset")},
                    {QStringLiteral("value"),
                     QStringLiteral("0x200")},
                }},
            revision));
    const Invocation replacement =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             replacementPatch});
    QCOMPARE(
        replacement.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                projectError));
    const QJsonObject replacementRoot =
        json(replacement);
    QVERIFY(
        replacementRoot.value(
                           QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "introduces a different")));
    QVERIFY(
        replacementRoot.value(
                           QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("repair"))
            .toObject()
            .value(
                QStringLiteral(
                    "introduced_problem_count"))
                .toInt() >
            0);
    QCOMPARE(
        readFile(project),
        original);

    const QString repairPatch =
        directory.filePath(
            QStringLiteral(
                "repair.json"));
    QVERIFY(
        writePatch(
            repairPatch,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-overlap")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral("offset")},
                    {QStringLiteral("value"),
                     QStringLiteral("0x8")},
                }},
            revision));

    const Invocation preview =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             repairPatch,
             QStringLiteral("--dry-run")});
    QCOMPARE(preview.exitCode, 0);
    const QJsonObject previewResult =
        json(preview)
            .value(
                QStringLiteral("result"))
            .toObject();
    const QJsonObject previewRepair =
        previewResult.value(
                         QStringLiteral(
                             "repair"))
            .toObject();
    QVERIFY(
        previewRepair.value(
                         QStringLiteral(
                             "baseline_invalid"))
            .toBool());
    QVERIFY(
        previewRepair.value(
                         QStringLiteral(
                             "candidate_valid"))
            .toBool());
    QVERIFY(
        previewRepair.value(
                         QStringLiteral(
                             "resolved_problem_count"))
                .toInt() >
            0);
    QCOMPARE(
        previewRepair.value(
                         QStringLiteral(
                             "after_error_count"))
            .toInt(),
        0);
    QCOMPARE(
        previewRepair.value(
                         QStringLiteral(
                             "introduced_problem_count"))
            .toInt(),
        0);
    QCOMPARE(
        readFile(project),
        original);

    const Invocation textPreview =
        invoke(
            {QStringLiteral("apply"),
             project,
             repairPatch,
             QStringLiteral("--dry-run")});
    QCOMPARE(textPreview.exitCode, 0);
    QVERIFY(
        textPreview.standardOutput
            .contains(
                QStringLiteral(
                    "Repair: ")));
    QVERIFY(
        textPreview.standardOutput
            .contains(
                QStringLiteral(
                    "introduced=0")));
    QCOMPARE(
        readFile(project),
        original);

    const Invocation applied =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             repairPatch});
    QCOMPARE(applied.exitCode, 0);
    const QJsonObject appliedRoot =
        json(applied);
    QVERIFY(
        appliedRoot.value(
                       QStringLiteral("ok"))
            .toBool());
    const QJsonObject appliedResult =
        appliedRoot.value(
                       QStringLiteral("result"))
            .toObject();
    QVERIFY(
        appliedResult.value(
                         QStringLiteral("saved"))
            .toBool());
    QVERIFY(
        appliedResult.value(
                         QStringLiteral(
                             "outputs_current"))
            .toBool());
    QVERIFY(
        appliedResult.value(
                         QStringLiteral("repair"))
            .toObject()
            .value(
                QStringLiteral(
                    "candidate_valid"))
            .toBool());
    QVERIFY(
        readFile(project) !=
        original);

    const Invocation valid =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("validate"),
             project});
    QCOMPARE(valid.exitCode, 0);
    const Invocation repairedObject =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("get"),
             project,
             QStringLiteral(
                 "reg-overlap")});
    QCOMPARE(
        repairedObject.exitCode,
        0);
    QCOMPARE(
        json(repairedObject)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("address"))
            .toString(),
        QStringLiteral("0x1028"));
    QVERIFY(
        !QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "rtl/device_registers.sv"))));
    QVERIFY(
        QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "generated/register-map.xlsx"))));
    makeGeneratedFilesWritable(
        directory.path());
}

void CliTests::avoidsWritingNetNoOpPatch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        createProject(
            directory.path());
    QVERIFY(!project.isEmpty());
    const QString revision =
        currentRevision(project);
    const QByteArray original =
        readFile(project);
    const QString patchPath =
        directory.filePath(
            QStringLiteral(
                "net-no-op.json"));
    QVERIFY(
        writePatch(
            patchPath,
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Temporary value.")},
                },
                QJsonObject{
                    {QStringLiteral("op"),
                     QStringLiteral("set")},
                    {QStringLiteral("id"),
                     QStringLiteral(
                         "reg-control")},
                    {QStringLiteral(
                         "property"),
                     QStringLiteral(
                         "description")},
                    {QStringLiteral(
                         "value"),
                     QStringLiteral(
                         "Main control.")},
                },
            },
            revision));

    const Invocation invocation =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("apply"),
             project,
             patchPath});
    QCOMPARE(invocation.exitCode, 0);
    const QJsonObject root =
        json(invocation);
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    QCOMPARE(
        root.value(
                QStringLiteral(
                    "revision"))
            .toString(),
        revision);
    const QJsonObject result =
        root.value(
                QStringLiteral("result"))
            .toObject();
    QVERIFY(
        !result.value(
                   QStringLiteral(
                       "changed"))
             .toBool());
    QVERIFY(
        !result.value(
                   QStringLiteral("saved"))
             .toBool());
    QVERIFY(
        !result.value(
                   QStringLiteral(
                       "generated"))
             .toBool());
    QCOMPARE(
        readFile(project),
        original);
    QVERIFY(
        !QDir(
             directory.filePath(
                 QStringLiteral(
                     "generated")))
             .exists());
}

void CliTests::initializesProjectWithoutOverwritingFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString project =
        directory.filePath(
            QStringLiteral(
                "fresh-device.regmap.yaml"));
    const Invocation initialized =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             project,
             QStringLiteral("--name"),
             QStringLiteral(
                 "Fresh Device"),
             QStringLiteral(
                 "--workspace-id"),
             QStringLiteral(
                 "workspace-fresh")});
    QCOMPARE(initialized.exitCode, 0);
    const QJsonObject root =
        json(initialized);
    QVERIFY(
        root.value(
                QStringLiteral("ok"))
            .toBool());
    QVERIFY(
        root.value(
                QStringLiteral(
                    "revision"))
            .toString()
            .startsWith(
                QStringLiteral(
                    "sha256:")));
    const QJsonObject result =
        root.value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "workspace_id"))
            .toString(),
        QStringLiteral(
            "workspace-fresh"));
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "workspace_name"))
            .toString(),
        QStringLiteral(
            "Fresh Device"));
    QVERIFY(
        result.value(
                  QStringLiteral("saved"))
            .toBool());
    QVERIFY(
        result.value(
                  QStringLiteral(
                      "generated"))
            .toBool());
    QCOMPARE(
        result.value(
                  QStringLiteral(
                      "artifacts"))
            .toArray()
            .size(),
        3);
    QVERIFY(
        QFileInfo::exists(project));
    const QDir generated(
        directory.filePath(
            QStringLiteral(
                "generated")));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "register-map.xlsx"))));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "Fresh_Device_regs.h"))));
    QVERIFY(
        QFileInfo::exists(
            generated.filePath(
                QStringLiteral(
                    "register-map.md"))));
    QVERIFY(
        !QFileInfo::exists(
            directory.filePath(
                QStringLiteral(
                    "rtl/Fresh_Device_registers.sv"))));

    const Invocation summary =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             project});
    QCOMPARE(summary.exitCode, 0);
    const QJsonObject counts =
        json(summary)
            .value(
                QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral("counts"))
            .toObject();
    QCOMPARE(
        counts.value(
                  QStringLiteral("pages"))
            .toInt(),
        0);
    const QByteArray original =
        readFile(project);
    const Invocation overwrite =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             project});
    QCOMPARE(
        overwrite.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    QVERIFY(
        json(overwrite)
            .value(
                QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral(
                    "overwrite")));
    QCOMPARE(
        readFile(project),
        original);

    const QString noGenerateProject =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "no-generate/other-device.regmap.yaml"));
    const Invocation noGenerate =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             noGenerateProject,
             QStringLiteral(
                 "--no-generate")});
    QCOMPARE(noGenerate.exitCode, 0);
    const QJsonObject noGenerateResult =
        json(noGenerate)
            .value(
                QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        noGenerateResult.value(
                            QStringLiteral(
                                "workspace_name"))
            .toString(),
        QStringLiteral(
            "other-device"));
    QVERIFY(
        noGenerateResult.value(
                            QStringLiteral(
                                "generation_skipped"))
            .toBool());
    QVERIFY(
        !QDir(
             QFileInfo(
                 noGenerateProject)
                 .absoluteDir()
                 .filePath(
                     QStringLiteral(
                         "generated")))
             .exists());

    const QString configuredProject =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "configured/configured.regmap.yaml"));
    const Invocation configured =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             configuredProject,
             QStringLiteral("--output-dir"),
             QStringLiteral("exports"),
             QStringLiteral("--xlsx-file"),
             QStringLiteral("map.xlsx"),
             QStringLiteral("--c-header-file"),
             QStringLiteral("registers.h"),
             QStringLiteral("--markdown-file"),
             QStringLiteral("map.md"),
             QStringLiteral("--target"),
             QStringLiteral("markdown")});
    QCOMPARE(configured.exitCode, 0);
    const QJsonObject configuredResult =
        json(configured)
            .value(QStringLiteral("result"))
            .toObject();
    QCOMPARE(
        configuredResult
            .value(
                QStringLiteral(
                    "artifacts"))
            .toArray()
            .size(),
        1);
    QCOMPARE(
        configuredResult
            .value(
                QStringLiteral(
                    "artifacts"))
            .toArray()
            .at(0)
            .toObject()
            .value(QStringLiteral("kind"))
            .toString(),
        QStringLiteral("markdown"));
    const QDir exports(
        QFileInfo(configuredProject)
            .absoluteDir()
            .filePath(
                QStringLiteral("exports")));
    QVERIFY(
        QFileInfo::exists(
            exports.filePath(
                QStringLiteral("map.md"))));
    QVERIFY(
        !QFileInfo::exists(
            exports.filePath(
                QStringLiteral("map.xlsx"))));
    QVERIFY(
        !QFileInfo::exists(
            exports.filePath(
                QStringLiteral("registers.h"))));

    const Invocation configuredSummary =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("summary"),
             configuredProject});
    QCOMPARE(configuredSummary.exitCode, 0);
    const QJsonArray configuredTargets =
        json(configuredSummary)
            .value(QStringLiteral("result"))
            .toObject()
            .value(
                QStringLiteral(
                    "generation_targets"))
            .toArray();
    QCOMPARE(configuredTargets.size(), 3);
    QSet<QString> configuredNames;
    for (const QJsonValue& target :
         configuredTargets) {
        configuredNames.insert(
            QFileInfo(
                target.toObject()
                    .value(
                        QStringLiteral("path"))
                    .toString())
                .fileName());
    }
    QCOMPARE(
        configuredNames,
        (QSet<QString>{
            QStringLiteral("map.xlsx"),
            QStringLiteral("registers.h"),
            QStringLiteral("map.md")}));

    const QString duplicateTargetProject =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "invalid-duplicate/device.regmap.yaml"));
    const Invocation duplicateTarget =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             duplicateTargetProject,
             QStringLiteral("--output-dir"),
             QStringLiteral("exports"),
             QStringLiteral("--target"),
             QStringLiteral("markdown"),
             QStringLiteral("--xlsx-file"),
             QStringLiteral("same.out"),
             QStringLiteral("--c-header-file"),
             QStringLiteral("same.out")});
    QCOMPARE(
        duplicateTarget.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(duplicateTarget)
            .value(QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral("unique")));
    QVERIFY(
        !QFileInfo::exists(
            duplicateTargetProject));
    QVERIFY(
        !QDir(
             QFileInfo(
                 duplicateTargetProject)
                 .absoluteDir()
                 .filePath(
                     QStringLiteral("exports")))
             .exists());

    const QString manifestCollisionProject =
        QDir(directory.path())
            .filePath(
                QStringLiteral(
                    "invalid-manifest/device.regmap.yaml"));
    const Invocation manifestCollision =
        invokeExecutable(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             manifestCollisionProject,
             QStringLiteral("--output-dir"),
             QStringLiteral("."),
             QStringLiteral("--target"),
             QStringLiteral("markdown"),
             QStringLiteral("--xlsx-file"),
             QStringLiteral("device.regmap.yaml")});
    QCOMPARE(
        manifestCollision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                usageError));
    QVERIFY(
        json(manifestCollision)
            .value(QStringLiteral("error"))
            .toString()
            .contains(
                QStringLiteral("manifest")));
    QVERIFY(
        !QFileInfo::exists(
            manifestCollisionProject));
    QVERIFY(
        !QFileInfo::exists(
            QFileInfo(
                manifestCollisionProject)
                .absoluteDir()
                .filePath(
                    QStringLiteral(
                        "register-map.md"))));

    const QString collisionDirectory =
        directory.filePath(
            QStringLiteral(
                "collision"));
    QVERIFY(
        QDir().mkpath(
            QDir(collisionDirectory)
                .filePath(
                    QStringLiteral(
                        "generated"))));
    const QString existingOutput =
        QDir(collisionDirectory)
            .filePath(
                QStringLiteral(
                    "generated/register-map.xlsx"));
    QFile existing(existingOutput);
    QVERIFY(
        existing.open(
            QIODevice::WriteOnly));
    QCOMPARE(
        existing.write(
            QByteArrayLiteral(
                "existing")),
        qint64{8});
    existing.close();
    const QString collisionProject =
        QDir(collisionDirectory)
            .filePath(
                QStringLiteral(
                    "collision.regmap.yaml"));
    const Invocation collision =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             collisionProject});
    QCOMPARE(
        collision.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    QVERIFY(
        !QFileInfo::exists(
            collisionProject));
    QCOMPARE(
        readFile(existingOutput),
        QByteArrayLiteral(
            "existing"));

    const QString partialDirectory =
        directory.filePath(
            QStringLiteral(
                "partial"));
    QVERIFY(
        QDir().mkpath(
            partialDirectory));
    QFile blockedGeneratedDirectory(
        QDir(partialDirectory)
            .filePath(
                QStringLiteral(
                    "generated")));
    QVERIFY(
        blockedGeneratedDirectory.open(
            QIODevice::WriteOnly));
    QCOMPARE(
        blockedGeneratedDirectory.write(
            QByteArrayLiteral(
                "blocks output directory")),
        qint64{23});
    blockedGeneratedDirectory.close();
    const QString partialProject =
        QDir(partialDirectory)
            .filePath(
                QStringLiteral(
                    "partial.regmap.yaml"));
    const Invocation partial =
        invoke(
            {QStringLiteral("--json"),
             QStringLiteral("init"),
             partialProject});
    QCOMPARE(
        partial.exitCode,
        static_cast<int>(
            regmap::cli::ExitCode::
                writeError));
    const QJsonObject partialRoot =
        json(partial);
    QVERIFY(
        QFileInfo::exists(
            partialProject));
    const QString partialRevision =
        partialRoot
            .value(
                QStringLiteral(
                    "revision"))
            .toString();
    QVERIFY(
        partialRevision.startsWith(
            QStringLiteral(
                "sha256:")));
    const QJsonObject partialResult =
        partialRoot
            .value(
                QStringLiteral(
                    "result"))
            .toObject();
    QVERIFY(
        partialResult
            .value(
                QStringLiteral(
                    "saved"))
            .toBool());
    QVERIFY(
        !partialResult
             .value(
                 QStringLiteral(
                     "generated"))
             .toBool());
    const QJsonObject partialRecovery =
        partialResult
            .value(
                QStringLiteral(
                    "recovery"))
            .toObject();
    QCOMPARE(
        partialRecovery
            .value(
                QStringLiteral(
                    "command"))
            .toString(),
        QStringLiteral("generate"));
    QCOMPARE(
        partialRecovery
            .value(
                QStringLiteral(
                    "expected_revision"))
            .toString(),
        partialRevision);
    QCOMPARE(
        partialRecovery
            .value(
                QStringLiteral(
                    "reason"))
            .toString(),
        QStringLiteral(
            "generated_outputs_incomplete"));
    const QJsonArray partialArguments =
        partialRecovery
            .value(
                QStringLiteral(
                    "arguments"))
            .toArray();
    QCOMPARE(
        partialArguments.size(),
        4);
    QCOMPARE(
        partialArguments.at(0)
            .toString(),
        QStringLiteral("generate"));
    QCOMPARE(
        QFileInfo(
            partialArguments.at(1)
                .toString())
            .canonicalFilePath(),
        QFileInfo(
            partialProject)
            .canonicalFilePath());
    QCOMPARE(
        partialArguments.at(2)
            .toString(),
        QStringLiteral("--expect"));
    QCOMPARE(
        partialArguments.at(3)
            .toString(),
        partialRevision);

    makeGeneratedFilesWritable(
        directory.path());
}

QTEST_MAIN(CliTests)

#include "cli_tests.moc"
