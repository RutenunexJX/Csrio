#include "regmap/core/generation.hpp"
#include "regmap/core/external_changes.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"
#include "regmap/core/model_tokens.hpp"
#include "regmap/core/project.hpp"
#include "regmap/core/project_creation.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/sync_diff.hpp"
#include "regmap/core/unsigned_value.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/xlsx_export.hpp"
#include "regmap/core/workspace_store.hpp"
#include "regmap/core/three_way_merge.hpp"

#include <xlsxcell.h>
#include <xlsxcellrange.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesUnsignedValues();
    void rejectsInvalidUnsignedValues();
    void slicesUnsignedValues();
    void parsesModelTokens();
    void createsSharedDefaultProjectDefinition();
    void loadsSchemaV2Manifest();
    void reportsInvalidManifest();
    void rejectsUnsafeManifestPaths();
    void rejectsPlatformOutputPathCollisions();
    void loadsStableProjectFixture();
    void loadsOneSnapshotWithEquivalentDiagnostics_data();
    void loadsOneSnapshotWithEquivalentDiagnostics();
    void adoptsOnlyUnmodifiedLoadSnapshots();
    void roundTripsProjectFile();
    void roundTripsExtendedModel();
    void normalizesFixedRegisterSlotsAndLegacyArrays();
    void derivesFieldResetsFromRegister();
    void tracksTransactionsAndStableIds();
    void handlesLargeWorkspaceWithCachedSaveState();
    void squashesTransactionsIntoSingleUndoStep();
    void boundsTransactionHistory();
    void roundTripsManagedRtl();
    void rejectsInvalidManagedRtlStructure();
    void preservesUnmanagedRtlText();
    void refusesUnmanagedRtlOverwrite();
    void mergesDisjointChangesAndReportsConflicts();
    void roundTripsSynchronizationBaseline();
    void rejectsCorruptSynchronizationBaseline();
    void validatesModelConflicts();
    void validatesNumericRangeBoundaries();
    void validatesBlockAllocationRanges();
    void generatesReadOnlyArtifacts();
    void reportsGeneratedIdentifierCollisions();
    void sanitizesXlsxWorksheetNames();
    void diffsByStableId();
    void plansExternalChangesWithDependenciesAndValidation();
};

namespace {

void writeTextFile(const QString& path, const QByteArray& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write(text), text.size());
    file.close();
}

[[nodiscard]] regmap::Workspace stableWorkspace()
{
    regmap::Workspace workspace;
    workspace.id = "minimal-example";
    workspace.name = "Minimal Example";

    regmap::AddressSpace page;
    page.id = "space-main";
    page.name = "Main";
    page.baseAddress = 0x43C00000;
    page.addressWidth = 32;

    regmap::RegisterBlock block;
    block.id = "block-control";
    block.name = "Control";
    block.baseAddress = 0xF000;
    block.size = 0x1000;
    block.description = "Control and status registers.";

    regmap::Register control;
    control.id = "reg-control";
    control.name = "CONTROL";
    control.offset = 0;
    control.width = 32;
    control.type = regmap::FieldType::structure;
    control.initialValue = regmap::UnsignedValue(0x221);
    control.resetValue = regmap::UnsignedValue(0);
    control.access = regmap::AccessMode::readWrite;
    control.tags = {"test"};
    control.description = "Global enable and operating mode.";

    regmap::Field enable;
    enable.id = "field-enable";
    enable.name = "ENABLE";
    enable.msb = 0;
    enable.lsb = 0;
    enable.type = regmap::FieldType::boolean;
    enable.softwareAccess = regmap::AccessMode::readWrite;
    enable.hardwareAccess = regmap::AccessMode::readOnly;
    enable.resetValue = regmap::UnsignedValue(0);
    enable.description = "Enables the block.";

    regmap::Field mode;
    mode.id = "field-mode";
    mode.name = "MODE";
    mode.msb = 2;
    mode.lsb = 1;
    mode.type = regmap::FieldType::enumeration;
    mode.softwareAccess = regmap::AccessMode::readWrite;
    mode.hardwareAccess = regmap::AccessMode::readOnly;
    mode.resetValue = regmap::UnsignedValue(0);
    regmap::EnumValue idle;
    idle.id = "enum-mode-idle";
    idle.name = "IDLE";
    idle.value = regmap::UnsignedValue(0);
    regmap::EnumValue run;
    run.id = "enum-mode-run";
    run.name = "RUN";
    run.value = regmap::UnsignedValue(1);
    mode.enumValues = {idle, run};
    control.fields = {enable, mode};

    regmap::Register status;
    status.id = "reg-status";
    status.name = "STATUS";
    status.offset = 4;
    status.width = 32;
    status.type = regmap::FieldType::structure;
    status.initialValue = regmap::UnsignedValue(1);
    status.resetValue = regmap::UnsignedValue(1);
    status.access = regmap::AccessMode::readOnly;
    status.tags = {"status"};
    status.description = "Current hardware status.";

    regmap::Field ready;
    ready.id = "field-ready";
    ready.name = "READY";
    ready.msb = 0;
    ready.lsb = 0;
    ready.type = regmap::FieldType::boolean;
    ready.softwareAccess = regmap::AccessMode::readOnly;
    ready.hardwareAccess = regmap::AccessMode::writeOnly;
    ready.resetValue = regmap::UnsignedValue(1);
    ready.writeSideEffect = regmap::WriteSideEffect::none;
    ready.description = "Hardware is ready.";

    regmap::Field error;
    error.id = "field-error";
    error.name = "ERROR";
    error.msb = 1;
    error.lsb = 1;
    error.type = regmap::FieldType::boolean;
    error.softwareAccess = regmap::AccessMode::readOnly;
    error.hardwareAccess = regmap::AccessMode::writeOnly;
    error.resetValue = regmap::UnsignedValue(0);
    error.writeSideEffect = regmap::WriteSideEffect::none;
    error.description = "Hardware error indicator.";
    status.fields = {ready, error};

    regmap::Register irq;
    irq.id = "reg-irq-status";
    irq.name = "IRQ_STATUS";
    irq.offset = 8;
    irq.width = 32;
    irq.type = regmap::FieldType::structure;
    irq.resetValue = regmap::UnsignedValue(0);
    irq.access = regmap::AccessMode::readWrite;
    irq.description = "Pending interrupt bits.";

    regmap::Field pending;
    pending.id = "field-pending";
    pending.name = "PENDING";
    pending.msb = 15;
    pending.lsb = 12;
    pending.softwareAccess = regmap::AccessMode::readWrite;
    pending.hardwareAccess = regmap::AccessMode::writeOnly;
    pending.resetValue = regmap::UnsignedValue(0);
    pending.writeSideEffect = regmap::WriteSideEffect::oneToClear;
    pending.description = "Pending interrupt sources.";

    regmap::Field irqReserved;
    irqReserved.id = "field-irq-reserved";
    irqReserved.name = "RESERVED";
    irqReserved.msb = 31;
    irqReserved.lsb = 27;
    irqReserved.type = regmap::FieldType::reserved;
    irqReserved.softwareAccess = regmap::AccessMode::none;
    irqReserved.hardwareAccess = regmap::AccessMode::none;
    irqReserved.resetValue = regmap::UnsignedValue(0);
    irqReserved.writeSideEffect = regmap::WriteSideEffect::none;
    irqReserved.description = "Reserved; keep zero.";

    regmap::Field irqFlag;
    irqFlag.id = "field-irq-flag";
    irqFlag.name = "IRQ_FLAG";
    irqFlag.msb = 0;
    irqFlag.lsb = 0;
    irqFlag.softwareAccess = regmap::AccessMode::readWrite;
    irqFlag.resetValue = regmap::UnsignedValue(0);
    irq.fields = {pending, irqReserved, irqFlag};

    regmap::Register scalar;
    scalar.id = "reg-scalar";
    scalar.name = "SCALAR";
    scalar.offset = 0xC;
    scalar.width = 32;
    scalar.type = regmap::FieldType::unsignedInteger;
    scalar.minimumValue = "1";
    scalar.maximumValue = "295";
    scalar.resetValue = regmap::UnsignedValue(0);
    scalar.description = "Scalar register.";

    regmap::Register threshold;
    threshold.id = "reg-threshold";
    threshold.name = "THRESHOLD";
    threshold.offset = 0x10;
    threshold.width = 32;
    threshold.type = regmap::FieldType::unsignedInteger;
    threshold.minimumValue = "1";
    threshold.maximumValue = "545";
    threshold.resetValue = regmap::UnsignedValue(0);

    regmap::Register data;
    data.id = "reg-data";
    data.name = "DATA";
    data.offset = 0x14;
    data.width = 32;
    data.type = regmap::FieldType::unsignedInteger;
    data.resetValue = regmap::UnsignedValue(0);

    regmap::Register extension;
    extension.id = "reg-extension";
    extension.name = "EXTENSION";
    extension.offset = 0x18;
    extension.width = 32;
    extension.type = regmap::FieldType::structure;
    extension.resetValue = regmap::UnsignedValue(0);
    regmap::Field extensionField;
    extensionField.id = "field-extension";
    extensionField.name = "VALUE";
    extensionField.msb = 0;
    extensionField.lsb = 0;
    extensionField.resetValue = regmap::UnsignedValue(0);
    extension.fields = {extensionField};

    block.registers = {control, status, irq, scalar, threshold, data, extension};
    page.blocks.push_back(std::move(block));
    workspace.addressSpaces.push_back(std::move(page));
    return workspace;
}

[[nodiscard]] regmap::ProjectManifest stableManifest(const std::filesystem::path& manifestPath)
{
    regmap::ProjectManifest manifest;
    manifest.manifestPath = manifestPath;
    manifest.workspaceId = "minimal-example";
    manifest.workspaceName = "Minimal Example";
    manifest.rtl.path.declared = "rtl/minimal_registers.sv";
    manifest.rtl.path.resolved = manifestPath.parent_path() / manifest.rtl.path.declared;
    manifest.rtl.moduleName = "minimal_registers";
    manifest.outputDirectory.declared = "generated";
    manifest.outputDirectory.resolved = manifestPath.parent_path() / "generated";

    const auto addTarget = [&](regmap::GenerationTargetKind kind,
                               std::filesystem::path name) {
        regmap::GenerationTargetConfig target;
        target.kind = kind;
        target.path.declared = std::move(name);
        target.path.resolved = manifest.outputDirectory.resolved / target.path.declared;
        manifest.targets.push_back(std::move(target));
    };
    addTarget(regmap::GenerationTargetKind::xlsx, "register-map.xlsx");
    addTarget(regmap::GenerationTargetKind::cHeader, "minimal_regs.h");
    addTarget(regmap::GenerationTargetKind::markdown, "register-map.md");
    return manifest;
}

[[nodiscard]] regmap::ProjectOpenResult openStableProject(QTemporaryDir& directory)
{
    const std::filesystem::path manifestPath = std::filesystem::path(
        directory.filePath(QStringLiteral("project.regmap.yaml")).toStdWString());
    auto workspace = stableWorkspace();
    workspace.manifestPath = manifestPath;
    auto manifest = stableManifest(manifestPath);
    const auto diagnostics = regmap::saveProjectFile(manifest, workspace);
    if (!diagnostics.empty()) {
        regmap::ProjectOpenResult result;
        result.diagnostics = diagnostics;
        return result;
    }
    return regmap::openProject(manifestPath);
}

} // namespace

void CoreTests::parsesUnsignedValues()
{
    const auto value = regmap::UnsignedValue::parse("0x1_FFFF_FFFF_FFFF_FFFF");
    QVERIFY(value.has_value());
    QCOMPARE(value->bitWidth(), std::size_t{65});
    QCOMPARE(value->toHexString(), std::string("0x1FFFFFFFFFFFFFFFF"));
    QCOMPARE(value->toDecimalString(), std::string("36893488147419103231"));
}

void CoreTests::rejectsInvalidUnsignedValues()
{
    QVERIFY(!regmap::UnsignedValue::parse("").has_value());
    QVERIFY(!regmap::UnsignedValue::parse("-1").has_value());
    QVERIFY(!regmap::UnsignedValue::parse("0x").has_value());
    QVERIFY(!regmap::UnsignedValue::parse("0b102").has_value());
    QVERIFY(!regmap::UnsignedValue::parse("1__0").has_value());
}

void CoreTests::slicesUnsignedValues()
{
    const auto value = regmap::UnsignedValue::parse("0x123456789ABCDEF0");
    QVERIFY(value.has_value());
    QCOMPARE(value->slice(8, 16).toHexString(), std::string("0xBCDE"));
    QVERIFY(value->fitsInBits(64));
    QVERIFY(!value->fitsInBits(60));

    const auto base = regmap::UnsignedValue::parse("0xFFFF");
    const auto replacement = regmap::UnsignedValue::parse("0x5");
    QVERIFY(base.has_value());
    QVERIFY(replacement.has_value());
    const auto replaced = base->replacingSlice(4, 4, *replacement);
    QVERIFY(replaced.has_value());
    QCOMPARE(replaced->toHexString(), std::string("0xFF5F"));
    const auto cleared =
        base->replacingSlice(4, 4, regmap::UnsignedValue(0));
    QVERIFY(cleared.has_value());
    QCOMPARE(cleared->toHexString(), std::string("0xFF0F"));
    QVERIFY(!base->replacingSlice(4, 2, *replacement).has_value());
    QVERIFY(!base
                 ->replacingSlice(std::numeric_limits<std::size_t>::max(), 2,
                                  regmap::UnsignedValue(0))
                 .has_value());

    regmap::Field field;
    field.msb = 31;
    field.lsb = 16;
    QCOMPARE(field.width(), std::uint64_t{16});
}

void CoreTests::parsesModelTokens()
{
    QVERIFY(regmap::parseAccessMode("read-write") == regmap::AccessMode::readWrite);
    QVERIFY(regmap::parseFieldType("boolean") == regmap::FieldType::boolean);
    QVERIFY(regmap::parseFieldType("compound") == regmap::FieldType::structure);
    QVERIFY(regmap::parseReadSideEffect("rc") == regmap::ReadSideEffect::clear);
    QVERIFY(regmap::parseWriteSideEffect("W1C") == regmap::WriteSideEffect::oneToClear);
    QCOMPARE(regmap::toString(regmap::WriteSideEffect::toggle), std::string_view("toggle"));
}

void CoreTests::createsSharedDefaultProjectDefinition()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path path =
        std::filesystem::path(
            directory.filePath(
                QStringLiteral(
                    "device.regmap.yaml"))
                .toStdWString());
    const regmap::NewProject project =
        regmap::makeDefaultProject(
            path,
            "42 Device Map",
            regmap::ObjectId{
                "workspace-device"});

    QCOMPARE(
        project.workspace.id,
        std::string(
            "workspace-device"));
    QCOMPARE(
        project.workspace.name,
        std::string(
            "42 Device Map"));
    QVERIFY(
        project.workspace
            .addressSpaces.empty());
    QCOMPARE(
        project.manifest.workspaceId,
        project.workspace.id);
    QCOMPARE(
        project.manifest
            .rtl.moduleName,
        std::string(
            "_42_Device_Map_registers"));
    QCOMPARE(
        project.manifest.targets.size(),
        std::size_t{3});
    QVERIFY(
        project.manifest.targets[0]
            .kind ==
        regmap::GenerationTargetKind::
            xlsx);
    QCOMPARE(
        project.manifest.targets[0]
            .path.declared,
        std::filesystem::path(
            "register-map.xlsx"));
    QCOMPARE(
        project.manifest.targets[1]
            .path.declared,
        std::filesystem::path(
            "_42_Device_Map_regs.h"));
    QVERIFY(
        regmap::validateWorkspace(
            project.workspace)
            .empty());
}

void CoreTests::loadsSchemaV2Manifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("project.regmap.yaml"));
    writeTextFile(path,
                  R"(schema_version: 2
workspace:
  id: test-workspace
  name: Test Workspace
  address_spaces: []
rtl:
  path: rtl/test_registers.sv
  module: test_registers
generation:
  output_directory: output
  targets:
    - kind: xlsx
      path: registers.xlsx
    - kind: c-header
      path: registers.h
    - kind: markdown
      path: registers.md
)");

    const auto result = regmap::loadProjectManifest(path.toStdWString());
    QVERIFY(!result.hasErrors());
    QVERIFY(result.manifest.has_value());
    QCOMPARE(result.manifest->schemaVersion, 2);
    QCOMPARE(result.manifest->workspaceId, std::string("test-workspace"));
    QCOMPARE(result.manifest->rtl.moduleName, std::string("test_registers"));
    QCOMPARE(result.manifest->rtl.path.resolved,
             (std::filesystem::path(directory.path().toStdWString()) / "rtl/test_registers.sv")
                 .lexically_normal());
    QCOMPARE(result.manifest->targets.size(), std::size_t{3});
    QVERIFY(result.manifest->targets.front().kind == regmap::GenerationTargetKind::xlsx);
}

void CoreTests::reportsInvalidManifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("invalid.regmap.yaml"));
    writeTextFile(path,
                  R"(schema_version: 1
workspace:
  id: invalid id
rtl:
  path: C:/absolute/registers.sv
  module: invalid module
generation:
  targets:
    - kind: unknown
      path: C:/output.txt
)");

    const auto result = regmap::loadProjectManifest(path.toStdWString());
    QVERIFY(result.hasErrors());
    QVERIFY(!result.manifest.has_value());
    QVERIFY(result.diagnostics.size() >= std::size_t{6});
}

void CoreTests::rejectsUnsafeManifestPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("project.regmap.yaml"));
    writeTextFile(path,
                  R"(schema_version: 2
workspace:
  id: test-workspace
  name: Test
  address_spaces: []
rtl:
  path: rtl/registers.sv
  module: test_registers
generation:
  output_directory: .
  targets:
    - kind: xlsx
      path: ../escaped.xlsx
      options:
        editable: "true"
    - kind: c-header
      path: project.regmap.yaml
    - kind: markdown
      path: registers.md
)");

    const auto result = regmap::loadProjectManifest(path.toStdWString());
    QVERIFY(result.hasErrors());
    QVERIFY(!result.manifest.has_value());
    const auto count =
        std::ranges::count_if(result.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1002";
        });
    QVERIFY(count >= 3);
}

void CoreTests::rejectsPlatformOutputPathCollisions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString projectPath =
        directory.filePath(QStringLiteral("project.regmap.yaml"));
    const QByteArray projectText = R"(schema_version: 2
workspace:
  id: test-workspace
  name: Test
  address_spaces: []
rtl:
  path: rtl/registers.sv
  module: test_registers
generation:
  output_directory: .
  targets:
    - kind: xlsx
      path: register-map.xlsx
    - kind: c-header
      path: PROJECT.REGMAP.YAML
    - kind: markdown
      path: registers.md
)";
    writeTextFile(projectPath, projectText);
    QFile original(projectPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();

    const auto protectedPath =
        regmap::loadProjectManifest(projectPath.toStdWString());
#ifdef Q_OS_WIN
    QVERIFY(protectedPath.hasErrors());
    QVERIFY(!protectedPath.manifest.has_value());
    QVERIFY(std::ranges::any_of(
        protectedPath.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1002" &&
                   diagnostic.message.find("must not overwrite") != std::string::npos;
        }));
#else
    QVERIFY(!protectedPath.hasErrors());
    QVERIFY(protectedPath.manifest.has_value());
#endif

    const QString duplicatePath =
        directory.filePath(QStringLiteral("duplicates.regmap.yaml"));
    writeTextFile(duplicatePath,
                  R"(schema_version: 2
workspace:
  id: duplicate-workspace
  name: Duplicate
  address_spaces: []
rtl:
  path: rtl/registers.sv
  module: duplicate_registers
generation:
  output_directory: generated
  targets:
    - kind: xlsx
      path: Map.output
    - kind: c-header
      path: map.OUTPUT
    - kind: markdown
      path: registers.md
)");
    const auto duplicateTargets =
        regmap::loadProjectManifest(duplicatePath.toStdWString());
#ifdef Q_OS_WIN
    QVERIFY(duplicateTargets.hasErrors());
    QVERIFY(!duplicateTargets.manifest.has_value());
    QVERIFY(std::ranges::any_of(
        duplicateTargets.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1004" &&
                   diagnostic.message.find("paths must be unique") != std::string::npos;
        }));
#else
    QVERIFY(!duplicateTargets.hasErrors());
    QVERIFY(duplicateTargets.manifest.has_value());
#endif

    regmap::Workspace workspace;
    workspace.id = "test-workspace";
    workspace.name = "Test";
    regmap::ProjectManifest directManifest;
    directManifest.manifestPath =
        std::filesystem::path(projectPath.toStdWString());
    directManifest.rtl.path.resolved =
        directManifest.manifestPath.parent_path() / "rtl/registers.sv";
    regmap::GenerationTargetConfig target;
    target.kind = regmap::GenerationTargetKind::cHeader;
    target.path.declared = "PROJECT.REGMAP.YAML";
    target.path.resolved =
        directManifest.manifestPath.parent_path() / target.path.declared;
    directManifest.targets.push_back(std::move(target));

    const auto generation =
        regmap::generateArtifacts(workspace, directManifest);
#ifdef Q_OS_WIN
    QVERIFY(generation.hasErrors());
    QVERIFY(generation.artifacts.empty());
    QVERIFY(std::ranges::any_of(
        generation.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM4001" &&
                   diagnostic.message.find("project source file") != std::string::npos;
        }));
    QVERIFY(regmap::writeGeneratedArtifacts(generation.artifacts).empty());
#else
    QVERIFY(!generation.hasErrors());
    QCOMPARE(generation.artifacts.size(), std::size_t{1});
#endif

    regmap::ProjectManifest directDuplicates;
    directDuplicates.manifestPath = directManifest.manifestPath;
    directDuplicates.rtl.path.resolved = directManifest.rtl.path.resolved;
    regmap::GenerationTargetConfig firstTarget;
    firstTarget.kind = regmap::GenerationTargetKind::cHeader;
    firstTarget.path.resolved =
        directManifest.manifestPath.parent_path() / "generated/Map.output";
    regmap::GenerationTargetConfig secondTarget;
    secondTarget.kind = regmap::GenerationTargetKind::markdown;
    secondTarget.path.resolved =
        directManifest.manifestPath.parent_path() / "generated/map.OUTPUT";
    directDuplicates.targets = {std::move(firstTarget), std::move(secondTarget)};
    const auto directDuplicateGeneration =
        regmap::generateArtifacts(workspace, directDuplicates);
#ifdef Q_OS_WIN
    QVERIFY(directDuplicateGeneration.hasErrors());
    QCOMPARE(directDuplicateGeneration.artifacts.size(), std::size_t{1});
    QVERIFY(std::ranges::any_of(
        directDuplicateGeneration.diagnostics,
        [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM4001" &&
                   diagnostic.message.find("Multiple generation targets") != std::string::npos;
        }));
#else
    QVERIFY(!directDuplicateGeneration.hasErrors());
    QCOMPARE(directDuplicateGeneration.artifacts.size(), std::size_t{2});
#endif

    QFile unchanged(projectPath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), originalBytes);
}

void CoreTests::loadsStableProjectFixture()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto result = openStableProject(directory);
    QVERIFY(!result.hasErrors());
    QVERIFY(result.manifest.has_value());
    QVERIFY(result.workspace.has_value());
    QCOMPARE(result.workspace->addressSpaces.size(), std::size_t{1});
    const auto& blocks = result.workspace->addressSpaces.front().blocks;
    QVERIFY(!blocks.empty());
    const auto* controlBlock = regmap::findRegisterBlock(*result.workspace, "block-control");
    QVERIFY(controlBlock != nullptr);
    QVERIFY(controlBlock->registers.size() >= std::size_t{3});
    const auto* control = regmap::findRegister(*result.workspace, "reg-control");
    QVERIFY(control != nullptr);
    QVERIFY(!control->fields.empty());
    QVERIFY(control->fields.front().type == regmap::FieldType::boolean);
    const auto validation = regmap::validateWorkspace(*result.workspace);
    QVERIFY(std::ranges::none_of(validation, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.severity == regmap::DiagnosticSeverity::error;
    }));
    QVERIFY(std::ranges::any_of(validation, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3052" &&
               diagnostic.severity == regmap::DiagnosticSeverity::warning &&
               diagnostic.message.find("reset value lies outside") != std::string::npos;
    }));
    QCOMPARE(controlBlock->registers.front().propertySources.at("offset").cell,
             std::string("workspace.address_spaces[0].blocks[0].registers[0].offset"));

    const auto rtlPath = std::filesystem::path(
        directory.filePath(QStringLiteral("minimal_registers.sv")).toStdWString());
    QVERIFY(regmap::writeManagedRtl(rtlPath, "minimal_registers", *result.workspace).empty());
    const auto rtl = regmap::parseManagedRtl(rtlPath);
    QVERIFY(!rtl.hasErrors());
    QVERIFY(rtl.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*result.workspace, *rtl.workspace).empty());
}

void CoreTests::loadsOneSnapshotWithEquivalentDiagnostics_data()
{
    QTest::addColumn<QString>("kind");
    for (const char* kind : {"valid", "syntax", "root", "schema", "workspace", "model", "missing"})
        QTest::newRow(kind) << QString::fromLatin1(kind);
}

void CoreTests::loadsOneSnapshotWithEquivalentDiagnostics()
{
    QFETCH(QString, kind);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QString::fromUtf8("单次加载.regmap.yaml"));
    auto workspace = stableWorkspace();
    const auto manifest = stableManifest(std::filesystem::path(path.toStdWString()));
    if (kind == "model") {
        auto& registers = workspace.addressSpaces.front().blocks.front().registers;
        registers.back().id = registers.front().id; // Syntactically valid, semantically invalid.
    }
    const auto serialized = regmap::serializeProjectText(manifest, workspace);
    QVERIFY(serialized.text.has_value());
    QByteArray bytes = QByteArray::fromStdString(*serialized.text);
    if (kind == "syntax") bytes = "schema_version: [unterminated";
    if (kind == "root") bytes = "- sequence\n";
    if (kind == "schema") {
        QVERIFY(bytes.contains("schema_version: 2"));
        bytes.replace("schema_version: 2", "schema_version: 99");
    }
    if (kind == "workspace") {
        QVERIFY(bytes.contains("address_spaces:"));
        bytes.replace("address_spaces:", "unexpected_spaces:");
    }
    if (kind != "missing") writeTextFile(path, bytes);

    // Reconstruct the previous public API composition, including exact order
    // and SourceLocation fields; do not merely compare diagnostic counts.
    auto previousManifest = regmap::loadProjectManifest(manifest.manifestPath);
    auto expectedDiagnostics = previousManifest.diagnostics;
    std::optional<regmap::Workspace> expectedWorkspace;
    if (previousManifest.manifest) {
        auto previousModel = regmap::loadWorkspaceFromProjectFile(manifest.manifestPath);
        expectedDiagnostics.insert(expectedDiagnostics.end(),
            previousModel.diagnostics.begin(), previousModel.diagnostics.end());
        expectedWorkspace = std::move(previousModel.workspace);
        if (expectedWorkspace) {
            const auto validation = regmap::validateWorkspace(*expectedWorkspace);
            expectedDiagnostics.insert(expectedDiagnostics.end(), validation.begin(), validation.end());
        }
    }
    const auto opened = regmap::openProject(manifest.manifestPath);
    if (kind == "model") {
        QVERIFY(opened.workspace.has_value());
        QVERIFY(opened.hasErrors());
    }
    QVERIFY(opened.diagnostics == expectedDiagnostics);
    QCOMPARE(opened.workspace.has_value(), expectedWorkspace.has_value());
    auto snapshot = regmap::loadProjectSnapshot(manifest.manifestPath);
    QCOMPARE(snapshot.metrics().fileReads, kind == "missing" ? std::size_t{0} : std::size_t{1});
    QCOMPARE(snapshot.metrics().yamlParses, kind == "missing" ? std::size_t{0} : std::size_t{1});
    QCOMPARE(snapshot.metrics().modelValidations, expectedWorkspace ? std::size_t{1} : std::size_t{0});
    if (kind != "missing") {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(snapshot.sourceSha256(), QCryptographicHash::hash(file.readAll(),
            QCryptographicHash::Sha256).toHex().toStdString());
    }
    if (expectedWorkspace) {
        QCOMPARE(regmap::serializeWorkspaceState(*snapshot.workspace(), true),
                 regmap::serializeWorkspaceState(*expectedWorkspace, true));
        regmap::WorkspaceStore store;
        QVERIFY(store.resetLoadedProject(std::move(snapshot)));
        QCOMPARE(store.validationCount(), std::uint64_t{0});
        QVERIFY(store.diagnostics() == regmap::validateWorkspace(*store.workspace()));
        QVERIFY(!store.dirty());
    }
}

void CoreTests::adoptsOnlyUnmodifiedLoadSnapshots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto opened = openStableProject(directory);
    QVERIFY(opened.manifest && opened.workspace);
    auto snapshot = regmap::loadProjectSnapshot(opened.manifest->manifestPath);
    static_assert(std::is_same_v<decltype(snapshot.workspace()), const std::optional<regmap::Workspace>&>);
    auto moved = std::move(snapshot);
    QVERIFY(!snapshot.workspace());
    regmap::WorkspaceStore store;
    QVERIFY(!store.resetLoadedProject(std::move(snapshot)));
    QVERIFY(store.resetLoadedProject(std::move(moved)));
    QVERIFY(!moved.workspace());
    QCOMPARE(store.validationCount(), std::uint64_t{0});
    QVERIFY(!store.resetLoadedProject(std::move(moved)));
    QVERIFY(store.workspace());
    auto invalid = *store.workspace();
    invalid.addressSpaces.front().blocks.front().registers.front().width = 0;
    store.reset(std::move(invalid));
    QCOMPARE(store.validationCount(), std::uint64_t{1});
    QVERIFY(!store.diagnostics().empty());
    QVERIFY(store.transact("Description", [](regmap::Workspace& model) { model.name += " edited"; }));
    QCOMPARE(store.validationCount(), std::uint64_t{2});
    QVERIFY(!store.diagnostics().empty());
}

void CoreTests::roundTripsProjectFile()
{
    QTemporaryDir sourceDirectory;
    QVERIFY(sourceDirectory.isValid());
    auto source = openStableProject(sourceDirectory);
    QVERIFY(source.manifest.has_value());
    QVERIFY(source.workspace.has_value());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto manifest = *source.manifest;
    manifest.manifestPath = std::filesystem::path(
        directory.filePath(QStringLiteral("project.regmap.yaml")).toStdWString());
    manifest.rtl.path.declared = "rtl/registers.sv";
    manifest.rtl.path.resolved = manifest.manifestPath.parent_path() / manifest.rtl.path.declared;
    manifest.outputDirectory.declared = "generated";
    manifest.outputDirectory.resolved = manifest.manifestPath.parent_path() / "generated";
    for (auto& target : manifest.targets) {
        target.path.resolved = manifest.outputDirectory.resolved / target.path.declared;
    }

    const auto saveDiagnostics = regmap::saveProjectFile(manifest, *source.workspace);
    QVERIFY(saveDiagnostics.empty());
    const auto loaded = regmap::openProject(manifest.manifestPath);
    QVERIFY(!loaded.hasErrors());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*source.workspace, *loaded.workspace).empty());
    QCOMPARE(loaded.manifest->rtl.path.declared, std::filesystem::path("rtl/registers.sv"));
}

void CoreTests::roundTripsExtendedModel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path projectPath = std::filesystem::path(
        directory.filePath(QStringLiteral("extended.regmap.yaml")).toStdWString());

    regmap::Workspace workspace;
    workspace.id = "extended-workspace";
    workspace.name = "Extended Workspace";
    workspace.manifestPath = projectPath;
    regmap::AddressSpace page;
    page.id = "page-main";
    page.name = "Main";
    page.baseAddress = 0x1000;
    page.addressWidth = 32;
    regmap::RegisterBlock block;
    block.id = "block-control";
    block.name = "Control";
    block.baseAddress = 0x200;
    block.size = 0x100;

    regmap::Register active;
    active.id = "reg-active";
    active.name = "ACTIVE";
    active.offset = 0;
    active.addressFixed = true;
    active.width = 32;
    active.array.stride = 4;
    active.type = regmap::FieldType::structure;
    active.initialValue = regmap::UnsignedValue(0);
    active.resetValue = regmap::UnsignedValue(0);
    active.tags = {"control", "startup"};

    regmap::Field numeric;
    numeric.id = "field-count";
    numeric.name = "COUNT";
    numeric.msb = 7;
    numeric.lsb = 0;
    numeric.type = regmap::FieldType::unsignedInteger;
    numeric.resetValue = regmap::UnsignedValue(0);
    numeric.minimumValue = "0";
    numeric.maximumValue = "255";

    regmap::Field compound;
    compound.id = "field-payload";
    compound.name = "PAYLOAD";
    compound.msb = 15;
    compound.lsb = 8;
    compound.type = regmap::FieldType::structure;
    compound.softwareAccess = regmap::AccessMode::none;
    compound.hardwareAccess = regmap::AccessMode::none;
    compound.resetValue = regmap::UnsignedValue(0);
    compound.writeSideEffect = regmap::WriteSideEffect::none;
    regmap::Field member;
    member.id = "field-payload-code";
    member.name = "CODE";
    member.msb = 7;
    member.lsb = 0;
    member.resetValue = regmap::UnsignedValue(0);
    compound.members.push_back(member);

    regmap::Field enabled;
    enabled.id = "field-enabled";
    enabled.name = "ENABLED";
    enabled.msb = 16;
    enabled.lsb = 16;
    enabled.type = regmap::FieldType::boolean;
    enabled.resetValue = regmap::UnsignedValue(0);
    active.fields = {numeric, compound, enabled};

    regmap::Register reserved;
    reserved.id = "reg-reserved";
    reserved.name = "RESERVED_04";
    reserved.offset = 4;
    reserved.width = 32;
    reserved.array.stride = 4;
    reserved.type = regmap::FieldType::reserved;
    reserved.initialValue = regmap::UnsignedValue(0);
    reserved.resetValue = regmap::UnsignedValue(0);
    reserved.access = regmap::AccessMode::none;
    reserved.reserved = true;
    reserved.tags = {"reserved"};

    regmap::Register scalar;
    scalar.id = "reg-scalar";
    scalar.name = "SCALAR";
    scalar.offset = 8;
    scalar.width = 16;
    scalar.array.stride = 2;
    scalar.type = regmap::FieldType::unsignedInteger;
    scalar.minimumValue = "0";
    scalar.maximumValue = "100";
    scalar.initialValue = regmap::UnsignedValue(3);
    scalar.resetValue = regmap::UnsignedValue(0);

    regmap::Register mode;
    mode.id = "reg-mode";
    mode.name = "MODE";
    mode.offset = 12;
    mode.width = 2;
    mode.array.stride = 1;
    mode.type = regmap::FieldType::enumeration;
    mode.initialValue = regmap::UnsignedValue(0);
    mode.resetValue = regmap::UnsignedValue(0);
    regmap::EnumValue idle;
    idle.id = "enum-mode-idle";
    idle.name = "IDLE";
    idle.value = regmap::UnsignedValue(0);
    regmap::EnumValue run;
    run.id = "enum-mode-run";
    run.name = "RUN";
    run.value = regmap::UnsignedValue(1);
    mode.enumValues = {idle, run};

    block.registers = {active, reserved, scalar, mode};
    page.blocks.push_back(block);
    workspace.addressSpaces.push_back(page);
    QVERIFY(regmap::validateWorkspace(workspace).empty());

    regmap::ProjectManifest manifest;
    manifest.manifestPath = projectPath;
    manifest.workspaceId = workspace.id;
    manifest.workspaceName = workspace.name;
    manifest.rtl.path.declared = "rtl/extended.sv";
    manifest.rtl.path.resolved = projectPath.parent_path() / manifest.rtl.path.declared;
    manifest.rtl.moduleName = "extended_registers";
    manifest.outputDirectory.declared = "generated";
    manifest.outputDirectory.resolved = projectPath.parent_path() / "generated";

    const auto serialized = regmap::serializeProjectText(manifest, workspace);
    QVERIFY(!serialized.hasErrors());
    QVERIFY(serialized.text.has_value());
    QVERIFY(serialized.text->find("workspace:") != std::string::npos);
    const auto loadedFromText = regmap::loadWorkspaceFromProjectText(
        *serialized.text, std::filesystem::path("clipboard.regmap.yaml"));
    QVERIFY(!loadedFromText.hasErrors());
    QVERIFY(loadedFromText.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *loadedFromText.workspace).empty());
    QCOMPARE(loadedFromText.workspace->manifestPath,
             std::filesystem::path("clipboard.regmap.yaml"));

    QVERIFY(regmap::saveProjectFile(manifest, workspace).empty());

    const auto loaded = regmap::loadWorkspaceFromProjectFile(projectPath);
    QVERIFY(!loaded.hasErrors());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *loaded.workspace).empty());
    const auto* loadedActive = regmap::findRegister(*loaded.workspace, "reg-active");
    QVERIFY(loadedActive != nullptr);
    QCOMPARE(loadedActive->tags.size(), std::size_t{2});
    QVERIFY(loadedActive->type == regmap::FieldType::structure);
    QVERIFY(loadedActive->addressFixed);
    QVERIFY(loadedActive->initialValue.has_value());
    const auto* loadedScalar = regmap::findRegister(*loaded.workspace, "reg-scalar");
    QVERIFY(loadedScalar != nullptr);
    QVERIFY(loadedScalar->type == regmap::FieldType::unsignedInteger);
    QVERIFY(loadedScalar->minimumValue == std::optional<std::string>("0"));
    QVERIFY(loadedScalar->maximumValue == std::optional<std::string>("100"));
    QVERIFY(loadedScalar->initialValue == std::optional(regmap::UnsignedValue(3)));
    const auto* loadedMode = regmap::findRegister(*loaded.workspace, "reg-mode");
    QVERIFY(loadedMode != nullptr);
    QVERIFY(loadedMode->type == regmap::FieldType::enumeration);
    QCOMPARE(loadedMode->enumValues.size(), std::size_t{2});
    QVERIFY(regmap::findEnumValue(*loaded.workspace, "enum-mode-run") != nullptr);
    const auto* loadedCompound = regmap::findField(*loaded.workspace, "field-payload");
    QVERIFY(loadedCompound != nullptr);
    QCOMPARE(loadedCompound->members.size(), std::size_t{1});

    const std::filesystem::path rtlPath = projectPath.parent_path() / "extended.sv";
    QVERIFY(regmap::writeManagedRtl(rtlPath, "extended_registers", workspace).empty());
    const auto parsedRtl = regmap::parseManagedRtl(rtlPath);
    QVERIFY(!parsedRtl.hasErrors());
    QVERIFY(parsedRtl.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *parsedRtl.workspace).empty());
    const auto* parsedRtlActive =
        regmap::findRegister(*parsedRtl.workspace, "reg-active");
    QVERIFY(parsedRtlActive != nullptr);
    QVERIFY(parsedRtlActive->addressFixed);

    auto invalid = workspace;
    regmap::findField(invalid, "field-count")->maximumValue = "256";
    const auto diagnostics = regmap::validateWorkspace(invalid);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3052";
    }));

    auto invalidTag = workspace;
    regmap::findRegister(invalidTag, "reg-active")->tags.push_back("bad,tag");
    const auto tagDiagnostics = regmap::validateWorkspace(invalidTag);
    QVERIFY(std::ranges::any_of(tagDiagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3051" &&
               diagnostic.message.find("must not contain a comma") != std::string::npos;
    }));

    auto duplicateTag = workspace;
    regmap::findRegister(duplicateTag, "reg-active")->tags.push_back("CONTROL");
    const auto duplicateTagDiagnostics = regmap::validateWorkspace(duplicateTag);
    QVERIFY(std::ranges::any_of(
        duplicateTagDiagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM3051" &&
                   diagnostic.message.find("duplicated") != std::string::npos;
        }));

    auto paddedTag = workspace;
    regmap::findRegister(paddedTag, "reg-active")->tags.push_back(" padded ");
    const auto paddedTagDiagnostics = regmap::validateWorkspace(paddedTag);
    QVERIFY(std::ranges::any_of(
        paddedTagDiagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM3051" &&
                   diagnostic.message.find("leading or trailing whitespace") != std::string::npos;
        }));
}

void CoreTests::normalizesFixedRegisterSlotsAndLegacyArrays()
{
    const std::string legacyProject = R"YAML(
schema_version: 2
workspace:
  id: workspace-fixed-slots
  name: Fixed Slots
  address_spaces:
    - id: page-main
      name: Main
      base: 0x1000
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x8
          registers:
            - id: reg-first
              name: FIRST
              offset: 0x0
              width: 8
              array:
                count: 7
                stride: 0x20
              type: unsigned
              access: rw
              enum_values: []
              fields: []
            - id: reg-second
              name: SECOND
              offset: 0x4
              width: 8
              type: unsigned
              access: rw
              enum_values: []
              fields: []
)YAML";

    const auto loaded = regmap::loadWorkspaceFromProjectText(
        legacyProject, std::filesystem::path("legacy-array.regmap.yaml"));
    QVERIFY(!loaded.hasErrors());
    QVERIFY(loaded.workspace.has_value());
    const auto* first = regmap::findRegister(*loaded.workspace, "reg-first");
    QVERIFY(first != nullptr);
    QCOMPARE(first->array.count, std::uint32_t{1});
    QCOMPARE(first->array.stride, std::uint64_t{4});
    QVERIFY(regmap::validateWorkspace(*loaded.workspace).empty());

    auto ignoredArrayChange = *loaded.workspace;
    auto* changedFirst = regmap::findRegister(ignoredArrayChange, "reg-first");
    QVERIFY(changedFirst != nullptr);
    changedFirst->array.count = 99;
    changedFirst->array.stride = 1;
    QVERIFY(regmap::diffWorkspaces(*loaded.workspace, ignoredArrayChange).empty());

    regmap::ProjectManifest manifest;
    manifest.workspaceId = loaded.workspace->id;
    manifest.workspaceName = loaded.workspace->name;
    const auto serialized = regmap::serializeProjectText(manifest, ignoredArrayChange);
    QVERIFY(!serialized.hasErrors());
    QVERIFY(serialized.text.has_value());
    QVERIFY(serialized.text->find("array:") == std::string::npos);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    regmap::GenerationTargetConfig headerTarget;
    headerTarget.kind = regmap::GenerationTargetKind::cHeader;
    headerTarget.path.declared = "fixed_slots.h";
    headerTarget.path.resolved = std::filesystem::path(directory.path().toStdWString()) /
        headerTarget.path.declared;
    manifest.targets = {headerTarget};
    const auto header = regmap::generateArtifacts(ignoredArrayChange, manifest);
    QVERIFY(!header.hasErrors());
    QCOMPARE(header.artifacts.size(), std::size_t{1});
    QVERIFY(header.artifacts.front().content.find("_COUNT") == std::string::npos);
    QVERIFY(header.artifacts.front().content.find("_STRIDE") == std::string::npos);

    auto invalid = *loaded.workspace;
    auto* second = regmap::findRegister(invalid, "reg-second");
    QVERIFY(second != nullptr);
    second->offset = 2;
    auto diagnostics = regmap::validateWorkspace(invalid);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3026" && diagnostic.objectId == "reg-second";
    }));
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3024" && diagnostic.objectId == "reg-second";
    }));

    second->offset = 4;
    second->width = 64;
    diagnostics = regmap::validateWorkspace(invalid);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3020" && diagnostic.objectId == "reg-second";
    }));
}

void CoreTests::derivesFieldResetsFromRegister()
{
    const std::string legacyProject = R"YAML(
schema_version: 2
workspace:
  id: workspace-reset-source
  name: Reset Source
  address_spaces:
    - id: page-main
      name: Main
      base: 0x0
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x4
          registers:
            - id: reg-control
              name: CONTROL
              offset: 0x0
              width: 8
              type: field
              reset: 0xA5
              access: rw
              enum_values: []
              fields:
                - id: field-low
                  name: LOW
                  msb: 3
                  lsb: 0
                  type: bits
                  sw_access: rw
                  hw_access: none
                  reset: 0x0
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []
                - id: field-high
                  name: HIGH
                  msb: 7
                  lsb: 4
                  type: field
                  sw_access: none
                  hw_access: none
                  reset: 0x0
                  read_side_effect: none
                  write_side_effect: none
                  enum_values: []
                  members:
                    - id: field-high-low
                      name: HIGH_LOW
                      msb: 1
                      lsb: 0
                      type: bits
                      sw_access: rw
                      hw_access: none
                      reset: 0x0
                      read_side_effect: none
                      write_side_effect: write
                      enum_values: []
)YAML";

    const auto loaded = regmap::loadWorkspaceFromProjectText(
        legacyProject, std::filesystem::path("legacy-field-reset.regmap.yaml"));
    QVERIFY(!loaded.hasErrors());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(std::ranges::any_of(
        loaded.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1105" &&
                   diagnostic.severity == regmap::DiagnosticSeverity::warning;
        }));
    const auto* low = regmap::findField(*loaded.workspace, "field-low");
    const auto* high = regmap::findField(*loaded.workspace, "field-high");
    const auto* highLow = regmap::findField(*loaded.workspace, "field-high-low");
    QVERIFY(low != nullptr);
    QVERIFY(high != nullptr);
    QVERIFY(highLow != nullptr);
    QVERIFY(low->resetValue == std::optional(regmap::UnsignedValue(0x5)));
    QVERIFY(high->resetValue == std::optional(regmap::UnsignedValue(0xA)));
    QVERIFY(highLow->resetValue == std::optional(regmap::UnsignedValue(0x2)));
    QCOMPARE(low->propertySources.at("reset").cell,
             std::string("workspace.address_spaces[0].blocks[0].registers[0].reset"));

    regmap::ProjectManifest manifest;
    manifest.workspaceId = loaded.workspace->id;
    manifest.workspaceName = loaded.workspace->name;
    const auto serialized = regmap::serializeProjectText(manifest, *loaded.workspace);
    QVERIFY(!serialized.hasErrors());
    QVERIFY(serialized.text.has_value());
    const std::size_t registerReset = serialized.text->find("reset:");
    QVERIFY(registerReset != std::string::npos);
    QVERIFY(serialized.text->find("reset:", registerReset + 1) == std::string::npos);
    const auto reloaded = regmap::loadWorkspaceFromProjectText(
        *serialized.text, std::filesystem::path("canonical-reset.regmap.yaml"));
    QVERIFY(!reloaded.hasErrors());
    QVERIFY(reloaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*loaded.workspace, *reloaded.workspace).empty());

    const std::string fieldOnlyReset = R"YAML(
workspace:
  id: workspace-field-only-reset
  name: Field Only Reset
  address_spaces:
    - id: page-main
      name: Main
      base: 0x0
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x4
          registers:
            - id: reg-control
              name: CONTROL
              offset: 0x0
              width: 8
              type: field
              access: rw
              enum_values: []
              fields:
                - id: field-low
                  name: LOW
                  msb: 3
                  lsb: 0
                  type: bits
                  sw_access: rw
                  hw_access: none
                  reset: 0x3
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []
)YAML";
    const auto fieldOnlyLoaded = regmap::loadWorkspaceFromProjectText(
        fieldOnlyReset, std::filesystem::path("field-only-reset.regmap.yaml"));
    QVERIFY(!fieldOnlyLoaded.hasErrors());
    QVERIFY(fieldOnlyLoaded.workspace.has_value());
    const auto* migratedRegister =
        regmap::findRegister(*fieldOnlyLoaded.workspace, "reg-control");
    QVERIFY(migratedRegister != nullptr);
    QVERIFY(migratedRegister->resetValue ==
            std::optional(regmap::UnsignedValue(0x3)));
    const auto* normalizedField =
        regmap::findField(*fieldOnlyLoaded.workspace, "field-low");
    QVERIFY(normalizedField != nullptr);
    QVERIFY(normalizedField->resetValue ==
            std::optional(regmap::UnsignedValue(0x3)));
    QVERIFY(std::ranges::any_of(
        fieldOnlyLoaded.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1104" &&
                   diagnostic.severity == regmap::DiagnosticSeverity::warning;
        }));

    auto resetNoise = *fieldOnlyLoaded.workspace;
    regmap::findField(resetNoise, "field-low")->resetValue = regmap::UnsignedValue(0x10);
    QVERIFY(regmap::diffWorkspaces(*fieldOnlyLoaded.workspace, resetNoise).empty());
    const auto resetNoiseDiagnostics = regmap::validateWorkspace(resetNoise);
    QVERIFY(std::ranges::none_of(
        resetNoiseDiagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM3034" && diagnostic.objectId == "field-low";
        }));
    const auto fieldOnlySerialized = regmap::serializeProjectText(manifest, resetNoise);
    QVERIFY(!fieldOnlySerialized.hasErrors());
    QVERIFY(fieldOnlySerialized.text.has_value());
    const std::size_t migratedReset = fieldOnlySerialized.text->find("reset:");
    QVERIFY(migratedReset != std::string::npos);
    QVERIFY(fieldOnlySerialized.text->find("reset:", migratedReset + 1) ==
            std::string::npos);

    const std::string conflictingLegacyResets = R"YAML(
workspace:
  id: workspace-conflicting-reset
  name: Conflicting Reset
  address_spaces:
    - id: page-main
      name: Main
      base: 0x0
      address_width: 32
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x4
          registers:
            - id: reg-control
              name: CONTROL
              offset: 0x0
              width: 8
              type: field
              access: rw
              enum_values: []
              fields:
                - id: field-first
                  name: FIRST
                  msb: 3
                  lsb: 0
                  type: bits
                  sw_access: rw
                  hw_access: none
                  reset: 0x0
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []
                - id: field-second
                  name: SECOND
                  msb: 3
                  lsb: 0
                  type: bits
                  sw_access: rw
                  hw_access: none
                  reset: 0xF
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []
)YAML";
    const auto conflictingLoaded = regmap::loadWorkspaceFromProjectText(
        conflictingLegacyResets,
        std::filesystem::path("conflicting-field-reset.regmap.yaml"));
    QVERIFY(conflictingLoaded.hasErrors());
    QVERIFY(!conflictingLoaded.workspace.has_value());
    QVERIFY(std::ranges::any_of(
        conflictingLoaded.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM1105" &&
                   diagnostic.severity == regmap::DiagnosticSeverity::error;
        }));
}

void CoreTests::tracksTransactionsAndStableIds()
{
    regmap::WorkspaceStore store(stableWorkspace());
    QVERIFY(!store.dirty());
    QVERIFY(!store.canUndo());

    const auto firstId = regmap::makeStableObjectId(*store.workspace(), "register");
    const auto secondId = regmap::makeStableObjectId(*store.workspace(), "register");
    QVERIFY(!firstId.empty());
    QVERIFY(firstId != secondId);

    const std::string registerId = "reg-control";
    QVERIFY(store.transact("Move CONTROL", [&](regmap::Workspace& workspace) {
        regmap::findRegister(workspace, registerId)->offset = 0x20;
    }));
    QVERIFY(store.dirty());
    QVERIFY(store.canUndo());
    QCOMPARE(regmap::findRegister(*store.workspace(), registerId)->offset, std::uint64_t{0x20});
    QVERIFY(store.undo());
    QCOMPARE(regmap::findRegister(*store.workspace(), registerId)->offset, std::uint64_t{0});
    QVERIFY(!store.dirty());
    QVERIFY(store.redo());
    QCOMPARE(regmap::findRegister(*store.workspace(), registerId)->offset, std::uint64_t{0x20});
    store.markSaved();
    QVERIFY(!store.dirty());

    const auto& savedRegisters =
        store.workspace()->addressSpaces.front().blocks.front().registers;
    QVERIFY(savedRegisters.size() >= std::size_t{2});
    const std::string savedFirstId = savedRegisters.front().id;
    const std::string savedLastId = savedRegisters.back().id;
    QVERIFY(store.transact("Reverse register order", [](regmap::Workspace& workspace) {
        auto& registers = workspace.addressSpaces.front().blocks.front().registers;
        std::reverse(registers.begin(), registers.end());
    }));
    QVERIFY(store.dirty());
    QVERIFY(store.canUndo());
    QCOMPARE(store.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             savedLastId);
    QVERIFY(store.undo());
    QVERIFY(!store.dirty());
    QCOMPARE(store.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             savedFirstId);
    QVERIFY(store.redo());
    QVERIFY(store.dirty());
    QCOMPARE(store.workspace()->addressSpaces.front().blocks.front().registers.front().id,
             savedLastId);
}

void CoreTests::handlesLargeWorkspaceWithCachedSaveState()
{
    constexpr std::size_t registerCount = 10'000;
    regmap::Workspace workspace;
    workspace.id = "large-workspace";
    workspace.name = "Large Workspace";

    regmap::AddressSpace page;
    page.id = "space-main";
    page.name = "Main";
    page.addressWidth = 32;

    regmap::RegisterBlock block;
    block.id = "block-main";
    block.name = "Main Block";
    block.size = static_cast<std::uint64_t>(registerCount) * UINT64_C(4);
    block.registers.reserve(registerCount);
    for (std::size_t index = 0; index < registerCount; ++index) {
        regmap::Register reg;
        reg.id = "reg-" + std::to_string(index);
        reg.name = "REGISTER_" + std::to_string(index);
        reg.offset = static_cast<std::uint64_t>(index) * UINT64_C(4);
        reg.width = 32;
        reg.type = regmap::FieldType::structure;
        reg.resetValue = regmap::UnsignedValue(0);

        regmap::Field field;
        field.id = "field-" + std::to_string(index);
        field.name = "VALUE";
        field.type = regmap::FieldType::boolean;
        reg.fields.push_back(std::move(field));
        block.registers.push_back(std::move(reg));
    }
    page.blocks.push_back(std::move(block));
    workspace.addressSpaces.push_back(std::move(page));

    const auto constructStarted = std::chrono::steady_clock::now();
    regmap::WorkspaceStore store(std::move(workspace));
    const auto constructFinished = std::chrono::steady_clock::now();
    QVERIFY(store.workspace() != nullptr);
    QVERIFY(store.savedWorkspace() != nullptr);
    QCOMPARE(store.workspace()->addressSpaces.front().blocks.front().registers.size(),
             registerCount);
    QVERIFY(!store.dirty());

    const auto dirtyStarted = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < 100'000; ++iteration) {
        QVERIFY(!store.dirty());
    }
    const auto dirtyFinished = std::chrono::steady_clock::now();

    const auto editStarted = std::chrono::steady_clock::now();
    QVERIFY(store.transact(
        "Describe final register",
        [](regmap::Workspace& candidate) {
            candidate.addressSpaces.front()
                .blocks.front()
                .registers.back()
                .description = "Edited in a large workspace";
        }));
    const auto editFinished = std::chrono::steady_clock::now();
    QVERIFY(store.dirty());

    const auto diffStarted = std::chrono::steady_clock::now();
    const auto changes = regmap::diffWorkspaces(
        *store.savedWorkspace(), *store.workspace());
    const auto diffFinished = std::chrono::steady_clock::now();
    QCOMPARE(changes.size(), std::size_t{1});
    QCOMPARE(changes.front().id, std::string{"reg-9999"});

    QVERIFY(store.undo());
    QVERIFY(!store.dirty());
    QVERIFY(store.redo());
    QVERIFY(store.dirty());
    store.markSaved();
    QVERIFY(!store.dirty());
    QCOMPARE(regmap::findRegister(*store.savedWorkspace(), "reg-9999")->description,
             std::string{"Edited in a large workspace"});

    const auto milliseconds = [](const auto begin, const auto end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - begin)
            .count();
    };
    const auto constructMs = milliseconds(constructStarted, constructFinished);
    const auto dirtyMs = milliseconds(dirtyStarted, dirtyFinished);
    const auto editMs = milliseconds(editStarted, editFinished);
    const auto diffMs = milliseconds(diffStarted, diffFinished);
    qInfo().nospace()
        << "large-map benchmark registers=" << registerCount
        << " fields=" << registerCount
        << " construct_ms=" << constructMs
        << " dirty_100k_ms=" << dirtyMs
        << " edit_ms=" << editMs
        << " diff_ms=" << diffMs;

    QVERIFY2(constructMs < 30'000, "10,000-register store construction regressed");
    QVERIFY2(dirtyMs < 2'000, "Cached dirty-state checks regressed");
    QVERIFY2(editMs < 30'000, "10,000-register atomic edit regressed");
    QVERIFY2(diffMs < 30'000, "10,000-register structured diff regressed");
}

void CoreTests::squashesTransactionsIntoSingleUndoStep()
{
    regmap::WorkspaceStore store(stableWorkspace());

    const std::string registerId = "reg-control";
    const auto* original = regmap::findRegister(*store.workspace(), registerId);
    QVERIFY(original != nullptr);
    const std::uint64_t originalOffset = original->offset;
    const std::string originalDescription = original->description;

    const std::size_t startingDepth = store.undoDepth();
    QVERIFY(!store.squashUndoSince(startingDepth, "Empty group"));
    QVERIFY(!store.squashUndoSince(startingDepth + 1U, "Out of bounds"));

    QVERIFY(store.transact("Move CONTROL", [&](regmap::Workspace& workspace) {
        regmap::findRegister(workspace, registerId)->offset = 0x20;
    }));
    QVERIFY(store.transact("Describe CONTROL", [&](regmap::Workspace& workspace) {
        regmap::findRegister(workspace, registerId)->description = "Grouped edit";
    }));
    QCOMPARE(store.undoDepth(), startingDepth + 2U);

    QVERIFY(store.squashUndoSince(startingDepth, "Edit CONTROL"));
    QCOMPARE(store.undoDepth(), startingDepth + 1U);
    QCOMPARE(store.undoText(), std::string_view{"Edit CONTROL"});

    const auto* edited = regmap::findRegister(*store.workspace(), registerId);
    QVERIFY(edited != nullptr);
    QCOMPARE(edited->offset, std::uint64_t{0x20});
    QCOMPARE(edited->description, std::string{"Grouped edit"});

    QVERIFY(store.undo());
    QCOMPARE(store.undoDepth(), startingDepth);
    QVERIFY(!store.canUndo());
    const auto* undone = regmap::findRegister(*store.workspace(), registerId);
    QVERIFY(undone != nullptr);
    QCOMPARE(undone->offset, originalOffset);
    QCOMPARE(undone->description, originalDescription);

    QVERIFY(store.redo());
    QCOMPARE(store.undoDepth(), startingDepth + 1U);
    QVERIFY(!store.canRedo());
    const auto* redone = regmap::findRegister(*store.workspace(), registerId);
    QVERIFY(redone != nullptr);
    QCOMPARE(redone->offset, std::uint64_t{0x20});
    QCOMPARE(redone->description, std::string{"Grouped edit"});
}

void CoreTests::boundsTransactionHistory()
{
    regmap::Workspace workspace;
    workspace.id = "workspace";
    workspace.name = "0";
    regmap::WorkspaceStore store(workspace);

    constexpr std::size_t extraTransactions = 5;
    constexpr std::size_t transactionCount =
        regmap::WorkspaceStore::historyLimit + extraTransactions;
    for (std::size_t index = 1; index <= transactionCount; ++index) {
        QVERIFY(store.transact("Rename workspace", [index](regmap::Workspace& candidate) {
            candidate.name = std::to_string(index);
        }));
    }
    QCOMPARE(store.undoDepth(), regmap::WorkspaceStore::historyLimit);
    QVERIFY(store.dirty());
    store.markSaved();
    QVERIFY(!store.dirty());

    std::size_t undoCount = 0;
    while (store.undo()) {
        ++undoCount;
    }
    QCOMPARE(undoCount, regmap::WorkspaceStore::historyLimit);
    QCOMPARE(store.undoDepth(), std::size_t{0});
    QVERIFY(!store.canUndo());
    QVERIFY(store.canRedo());
    QCOMPARE(store.workspace()->name, std::to_string(extraTransactions));
    QVERIFY(store.dirty());

    std::size_t redoCount = 0;
    while (store.redo()) {
        ++redoCount;
    }
    QCOMPARE(redoCount, regmap::WorkspaceStore::historyLimit);
    QCOMPARE(store.undoDepth(), regmap::WorkspaceStore::historyLimit);
    QVERIFY(!store.canRedo());
    QCOMPARE(store.workspace()->name, std::to_string(transactionCount));
    QVERIFY(!store.dirty());

    store.reset(workspace);
    QCOMPARE(store.undoDepth(), std::size_t{0});
    QVERIFY(!store.canUndo());
    QVERIFY(!store.canRedo());
    QVERIFY(!store.dirty());

    regmap::WorkspaceStore squashStore(workspace);
    for (std::size_t index = 1; index < regmap::WorkspaceStore::historyLimit; ++index) {
        QVERIFY(squashStore.transact(
            "Fill history", [index](regmap::Workspace& candidate) {
                candidate.name = std::to_string(index);
            }));
    }
    const std::size_t startingDepth = squashStore.undoDepth();
    QCOMPARE(startingDepth, regmap::WorkspaceStore::historyLimit - 1U);
    QVERIFY(squashStore.transact("Group first", [](regmap::Workspace& candidate) {
        candidate.name = "group-first";
    }));
    QVERIFY(squashStore.transact("Group second", [](regmap::Workspace& candidate) {
        candidate.name = "group-second";
    }));
    QCOMPARE(squashStore.undoDepth(), regmap::WorkspaceStore::historyLimit);
    QVERIFY(!squashStore.squashUndoSince(startingDepth, "Unsafe truncated group"));
    QCOMPARE(squashStore.undoDepth(), regmap::WorkspaceStore::historyLimit);
    QVERIFY(squashStore.undo());
    QCOMPARE(squashStore.workspace()->name, std::string{"group-first"});
}

void CoreTests::roundTripsManagedRtl()
{
    const auto workspace = stableWorkspace();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path rtlPath =
        std::filesystem::path(directory.filePath(QStringLiteral("registers.sv")).toStdWString());

    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", workspace).empty());
    auto parsed = regmap::parseManagedRtl(rtlPath);
    for (const auto& diagnostic : parsed.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(!parsed.hasErrors());
    QVERIFY(parsed.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *parsed.workspace).empty());

    QFile file(QString::fromStdWString(rtlPath.wstring()));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    file.close();
    bool changed = false;
    for (QString& line : lines) {
        if (!line.contains(QStringLiteral("\"id\":\"reg-control\"")) ||
            !line.contains(QStringLiteral("\"property\":\"offset\""))) {
            continue;
        }
        QVERIFY(line.contains(QStringLiteral("logic [63:0]")));
        const qsizetype equals = line.indexOf('=');
        const qsizetype semicolon = line.indexOf(';', equals);
        QVERIFY(equals >= 0 && semicolon > equals);
        line.replace(equals + 1, semicolon - equals - 1, QStringLiteral(" 64'h100"));
        changed = true;
        break;
    }
    QVERIFY(changed);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    const QByteArray modified = lines.join('\n').toUtf8();
    QCOMPARE(file.write(modified), modified.size());
    file.close();

    parsed = regmap::parseManagedRtl(rtlPath);
    for (const auto& diagnostic : parsed.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(!parsed.hasErrors());
    QVERIFY(parsed.workspace.has_value());
    const auto* reg = regmap::findRegister(*parsed.workspace, "reg-control");
    QVERIFY(reg != nullptr);
    QCOMPARE(reg->offset, std::uint64_t{0x100});
    QVERIFY(reg->propertySources.at("offset").row.has_value());
}

void CoreTests::rejectsInvalidManagedRtlStructure()
{
    const auto workspace = stableWorkspace();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("registers.sv"));
    const std::filesystem::path rtlPath(path.toStdWString());
    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", workspace).empty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray text = file.readAll();
    file.close();
    QVERIFY(text.contains(R"("parent":"reg-control")"));
    text.replace(R"("parent":"reg-control")", R"("parent":"missing-register")");
    writeTextFile(path, text);

    const auto parsed = regmap::parseManagedRtl(rtlPath);
    QVERIFY(!parsed.workspace.has_value());
    QVERIFY(std::ranges::any_of(parsed.diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM5002";
    }));
}

void CoreTests::preservesUnmanagedRtlText()
{
    const auto workspace = stableWorkspace();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path rtlPath =
        std::filesystem::path(directory.filePath(QStringLiteral("registers.sv")).toStdWString());
    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", workspace).empty());

    QFile file(QString::fromStdWString(rtlPath.wstring()));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    text.replace(QStringLiteral("endmodule"),
                 QStringLiteral("  logic user_owned_signal;\nendmodule"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
    file.close();

    regmap::Workspace changed = workspace;
    regmap::findRegister(changed, "reg-control")->offset = 0x10;
    const auto writeDiagnostics = regmap::writeManagedRtl(rtlPath, "test_registers", changed);
    for (const auto& diagnostic : writeDiagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(writeDiagnostics.empty());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString updated = QString::fromUtf8(file.readAll());
    QVERIFY(updated.contains(QStringLiteral("logic user_owned_signal;")));
    QVERIFY(updated.contains(QStringLiteral("'h10")));
}

void CoreTests::refusesUnmanagedRtlOverwrite()
{
    const auto workspace = stableWorkspace();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString qtPath = directory.filePath(QStringLiteral("user_owned.sv"));
    const QByteArray original = "module user_owned;\n"
                                "  string fake_begin = \"// RMW:BEGIN schema=1\";\n"
                                "  string fake_end = \"// RMW:END\";\n"
                                "  logic must_survive;\n"
                                "endmodule\n";
    writeTextFile(qtPath, original);
    const std::filesystem::path path(qtPath.toStdWString());

    const auto diagnostics = regmap::writeManagedRtl(path, "test_registers", workspace);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM5001";
    }));

    QFile file(qtPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(file.readAll(), original);
}

void CoreTests::mergesDisjointChangesAndReportsConflicts()
{
    const regmap::Workspace base = stableWorkspace();
    regmap::Workspace workbench = base;
    regmap::Workspace rtl = base;
    regmap::findRegister(workbench, "reg-control")->offset = 4;
    regmap::findField(rtl, "field-ready")->name = "READY_FROM_RTL";

    auto merged = regmap::mergeWorkspaces(base, workbench, rtl);
    QVERIFY(merged.conflicts.empty());
    QVERIFY(merged.merged.has_value());
    QCOMPARE(regmap::findRegister(*merged.merged, "reg-control")->offset, std::uint64_t{4});
    QCOMPARE(regmap::findField(*merged.merged, "field-ready")->name, std::string("READY_FROM_RTL"));

    rtl = base;
    regmap::findRegister(rtl, "reg-control")->offset = 8;
    merged = regmap::mergeWorkspaces(base, workbench, rtl, regmap::MergePreference::workbench);
    QCOMPARE(merged.conflicts.size(), std::size_t{1});
    QCOMPARE(merged.conflicts.front().objectId, std::string("reg-control"));
    QCOMPARE(merged.conflicts.front().property, std::string("offset"));
    QCOMPARE(regmap::findRegister(*merged.merged, "reg-control")->offset, std::uint64_t{4});

    merged = regmap::mergeWorkspaces(base, workbench, rtl, regmap::MergePreference::rtl);
    QCOMPARE(regmap::findRegister(*merged.merged, "reg-control")->offset, std::uint64_t{8});

    workbench = base;
    rtl = base;
    regmap::Register added = *regmap::findRegister(workbench, "reg-status");
    added.id = "reg-added";
    added.name = "ADDED";
    added.offset = 0x10;
    added.fields.clear();
    workbench.addressSpaces.front().blocks.front().registers.push_back(std::move(added));
    regmap::findField(rtl, "field-ready")->name = "READY_FROM_RTL";
    merged = regmap::mergeWorkspaces(base, workbench, rtl);
    QVERIFY(merged.conflicts.empty());
    QVERIFY(regmap::findRegister(*merged.merged, "reg-added") != nullptr);
    QCOMPARE(regmap::findField(*merged.merged, "field-ready")->name, std::string("READY_FROM_RTL"));

    workbench = base;
    rtl = base;
    QVERIFY(regmap::removeObject(workbench, "reg-status"));
    merged = regmap::mergeWorkspaces(base, workbench, rtl);
    QVERIFY(merged.conflicts.empty());
    QVERIFY(regmap::findRegister(*merged.merged, "reg-status") == nullptr);

    rtl = base;
    regmap::findRegister(rtl, "reg-status")->name = "STATUS_FROM_RTL";
    merged = regmap::mergeWorkspaces(base, workbench, rtl, regmap::MergePreference::workbench);
    QVERIFY(std::ranges::any_of(merged.conflicts, [](const regmap::MergeConflict& conflict) {
        return conflict.objectId == "reg-status" && conflict.property == "<presence>";
    }));
    QVERIFY(regmap::findRegister(*merged.merged, "reg-status") == nullptr);

    workbench = base;
    rtl = base;
    auto* workbenchStatus = regmap::findRegister(workbench, "reg-status");
    auto* rtlStatus = regmap::findRegister(rtl, "reg-status");
    QVERIFY(workbenchStatus != nullptr);
    QVERIFY(rtlStatus != nullptr);
    workbenchStatus->array.count = 9;
    workbenchStatus->array.stride = 0x20;
    rtlStatus->array.count = 3;
    rtlStatus->array.stride = 0x10;
    regmap::findField(workbench, "field-ready")->resetValue = regmap::UnsignedValue(0);
    regmap::findField(rtl, "field-ready")->resetValue = regmap::UnsignedValue(1);
    merged = regmap::mergeWorkspaces(base, workbench, rtl);
    QVERIFY(merged.conflicts.empty());
    QVERIFY(merged.merged.has_value());
    const auto* normalizedStatus = regmap::findRegister(*merged.merged, "reg-status");
    const auto* normalizedReady = regmap::findField(*merged.merged, "field-ready");
    QVERIFY(normalizedStatus != nullptr);
    QVERIFY(normalizedReady != nullptr);
    QCOMPARE(normalizedStatus->array.count, std::uint32_t{1});
    QCOMPARE(normalizedStatus->array.stride, std::uint64_t{4});
    QVERIFY(normalizedReady->resetValue == std::optional(regmap::UnsignedValue(1)));
}

void CoreTests::roundTripsSynchronizationBaseline()
{
    const auto workspace = stableWorkspace();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path path = std::filesystem::path(
        directory.filePath(QStringLiteral(".regmap.sync.json")).toStdWString());
    QVERIFY(regmap::saveSyncBaseline(path, workspace).empty());
    const auto loaded = regmap::loadSyncBaseline(path);
    QVERIFY(loaded.diagnostics.empty());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *loaded.workspace).empty());

    const std::string canonicalState = regmap::serializeWorkspaceState(workspace, false);
    QJsonDocument legacyDocument = QJsonDocument::fromJson(
        QByteArray(canonicalState.data(), static_cast<qsizetype>(canonicalState.size())));
    QVERIFY(legacyDocument.isObject());
    QJsonObject legacyRoot = legacyDocument.object();
    QJsonArray objects = legacyRoot.value(QStringLiteral("objects")).toArray();
    bool injectedRegister = false;
    bool injectedField = false;
    for (qsizetype index = 0; index < objects.size(); ++index) {
        QJsonObject object = objects.at(index).toObject();
        QJsonObject properties = object.value(QStringLiteral("properties")).toObject();
        if (object.value(QStringLiteral("id")).toString() == QStringLiteral("reg-status")) {
            properties.insert(QStringLiteral("array_count"), QStringLiteral("9"));
            properties.insert(QStringLiteral("stride"), QStringLiteral("0x20"));
            injectedRegister = true;
        } else if (object.value(QStringLiteral("id")).toString() ==
                   QStringLiteral("field-ready")) {
            properties.insert(QStringLiteral("reset"), QStringLiteral("0x0"));
            injectedField = true;
        }
        object.insert(QStringLiteral("properties"), properties);
        objects.replace(index, object);
    }
    QVERIFY(injectedRegister);
    QVERIFY(injectedField);
    legacyRoot.insert(QStringLiteral("objects"), objects);
    legacyDocument.setObject(legacyRoot);
    const QByteArray legacyBytes = legacyDocument.toJson(QJsonDocument::Compact);
    const auto legacyLoaded = regmap::parseWorkspaceState(
        std::string_view(legacyBytes.constData(), static_cast<std::size_t>(legacyBytes.size())),
        path);
    QVERIFY(legacyLoaded.diagnostics.empty());
    QVERIFY(legacyLoaded.workspace.has_value());
    const auto* legacyStatus = regmap::findRegister(*legacyLoaded.workspace, "reg-status");
    const auto* legacyReady = regmap::findField(*legacyLoaded.workspace, "field-ready");
    QVERIFY(legacyStatus != nullptr);
    QVERIFY(legacyReady != nullptr);
    QCOMPARE(legacyStatus->array.count, std::uint32_t{1});
    QCOMPARE(legacyStatus->array.stride, std::uint64_t{4});
    QVERIFY(legacyReady->resetValue == std::optional(regmap::UnsignedValue(1)));

    const std::string normalizedState =
        regmap::serializeWorkspaceState(*legacyLoaded.workspace, false);
    const QJsonDocument normalizedDocument = QJsonDocument::fromJson(
        QByteArray(normalizedState.data(), static_cast<qsizetype>(normalizedState.size())));
    for (const QJsonValue& value :
         normalizedDocument.object().value(QStringLiteral("objects")).toArray()) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties =
            object.value(QStringLiteral("properties")).toObject();
        if (object.value(QStringLiteral("kind")).toString() == QStringLiteral("register")) {
            QVERIFY(!properties.contains(QStringLiteral("array_count")));
            QVERIFY(!properties.contains(QStringLiteral("stride")));
        }
        if (object.value(QStringLiteral("kind")).toString() == QStringLiteral("field")) {
            QVERIFY(!properties.contains(QStringLiteral("reset")));
        }
    }
}

void CoreTests::rejectsCorruptSynchronizationBaseline()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("project.regmap.yaml.sync.json"));
    writeTextFile(path, QByteArrayLiteral("{not valid json"));

    const auto loaded = regmap::loadSyncBaseline(std::filesystem::path(path.toStdWString()));
    QVERIFY(!loaded.workspace.has_value());
    QVERIFY(std::ranges::any_of(loaded.diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM5200";
    }));
}

void CoreTests::validatesModelConflicts()
{
    regmap::Workspace workspace;
    workspace.id = "workspace";
    workspace.name = "Workspace";

    regmap::AddressSpace addressSpace;
    addressSpace.id = "space";
    addressSpace.name = "Main";
    addressSpace.addressWidth = 16;

    regmap::RegisterBlock block;
    block.id = "block";
    block.name = "Control";
    block.size = 8;

    regmap::Register first;
    first.id = "reg-first";
    first.name = "FIRST";
    first.offset = 0;
    first.width = 32;
    first.type = regmap::FieldType::structure;
    first.resetValue = *regmap::UnsignedValue::parse("0x1");

    regmap::Field firstField;
    firstField.id = "field-mode";
    firstField.name = "MODE";
    firstField.msb = 1;
    firstField.lsb = 0;
    firstField.type = regmap::FieldType::enumeration;
    firstField.resetValue = *regmap::UnsignedValue::parse("0x2");
    regmap::EnumValue enumZero;
    enumZero.id = "enum-zero";
    enumZero.name = "ZERO";
    enumZero.value = regmap::UnsignedValue(0);
    firstField.enumValues.push_back(enumZero);

    regmap::Field overlappingField;
    overlappingField.id = "field-overlap";
    overlappingField.name = "OVERLAP";
    overlappingField.msb = 3;
    overlappingField.lsb = 1;
    first.fields.push_back(firstField);
    first.fields.push_back(overlappingField);

    regmap::Register second;
    second.id = "reg-second";
    second.name = "SECOND";
    second.offset = 2;
    second.width = 32;
    block.registers.push_back(first);
    block.registers.push_back(second);
    addressSpace.blocks.push_back(block);
    workspace.addressSpaces.push_back(addressSpace);

    const auto diagnostics = regmap::validateWorkspace(workspace);
    const auto hasCode = [&](std::string_view code) {
        return std::ranges::any_of(diagnostics, [&](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
    };
    QVERIFY(hasCode("RM3024"));
    QVERIFY(hasCode("RM3026"));
    QVERIFY(hasCode("RM3031"));
    QVERIFY(!hasCode("RM3035"));
    QVERIFY(hasCode("RM3043"));
}

void CoreTests::validatesNumericRangeBoundaries()
{
    regmap::Workspace workspace;
    workspace.id = "workspace";
    workspace.name = "Workspace";

    regmap::AddressSpace addressSpace;
    addressSpace.id = "space";
    addressSpace.name = "Main";
    addressSpace.addressWidth = 16;

    regmap::RegisterBlock block;
    block.id = "block";
    block.name = "Control";
    block.size = 1;

    regmap::Register value;
    value.id = "reg-value";
    value.name = "VALUE";
    value.width = 7;
    value.type = regmap::FieldType::unsignedInteger;
    value.minimumValue = "0";
    value.maximumValue = "127";
    block.registers.push_back(value);
    addressSpace.blocks.push_back(block);
    workspace.addressSpaces.push_back(addressSpace);

    const auto hasRangeDiagnostic = [](const regmap::Workspace& candidate) {
        const auto diagnostics = regmap::validateWorkspace(candidate);
        return std::ranges::any_of(
            diagnostics, [](const regmap::Diagnostic& diagnostic) {
                return diagnostic.code == "RM3052" &&
                       diagnostic.objectId == "reg-value";
            });
    };

    QVERIFY(!hasRangeDiagnostic(workspace));
    workspace.addressSpaces[0].blocks[0].registers[0].maximumValue = "128";
    QVERIFY(hasRangeDiagnostic(workspace));
    workspace.addressSpaces[0].blocks[0].registers[0].maximumValue = "127";
    workspace.addressSpaces[0].blocks[0].registers[0].minimumValue = "-1";
    QVERIFY(hasRangeDiagnostic(workspace));

    auto& numeric = workspace.addressSpaces[0].blocks[0].registers[0];
    numeric.width = 8;
    numeric.type = regmap::FieldType::signedInteger;
    numeric.minimumValue = "-128";
    numeric.maximumValue = "127";
    QVERIFY(!hasRangeDiagnostic(workspace));
    numeric.minimumValue = "-129";
    QVERIFY(hasRangeDiagnostic(workspace));
    numeric.minimumValue = "-128";
    numeric.maximumValue = "128";
    QVERIFY(hasRangeDiagnostic(workspace));

    numeric.width = 1;
    numeric.minimumValue = "-1";
    numeric.maximumValue = "0";
    QVERIFY(!hasRangeDiagnostic(workspace));
    numeric.minimumValue = "-2";
    QVERIFY(hasRangeDiagnostic(workspace));
    numeric.minimumValue = "-1";
    numeric.maximumValue = "1";
    QVERIFY(hasRangeDiagnostic(workspace));

    const auto valueRangeDiagnostic = [](const regmap::Workspace& candidate,
                                         std::string_view objectId,
                                         std::string_view message) {
        const auto diagnostics = regmap::validateWorkspace(candidate);
        const auto iterator = std::ranges::find_if(
            diagnostics, [&](const regmap::Diagnostic& diagnostic) {
                return diagnostic.code == "RM3052" && diagnostic.objectId == objectId &&
                       diagnostic.message.find(message) != std::string::npos;
            });
        return iterator == diagnostics.end() ? std::optional<regmap::Diagnostic>{}
                                             : std::optional<regmap::Diagnostic>{*iterator};
    };

    numeric.width = 8;
    numeric.type = regmap::FieldType::signedInteger;
    numeric.minimumValue = "-10";
    numeric.maximumValue = "10";
    numeric.initialValue = regmap::UnsignedValue(0xF6);
    numeric.resetValue = regmap::UnsignedValue(0x0A);
    numeric.propertySources["initial"].cell = "register.initial";
    numeric.propertySources["reset"].cell = "register.reset";
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "reset value lies"));

    numeric.initialValue = regmap::UnsignedValue(0xF5);
    auto initialDiagnostic =
        valueRangeDiagnostic(workspace, "reg-value", "initial value lies");
    QVERIFY(initialDiagnostic.has_value());
    QCOMPARE(initialDiagnostic->severity, regmap::DiagnosticSeverity::error);
    QCOMPARE(initialDiagnostic->source.cell, std::string("register.initial"));
    numeric.initialValue = regmap::UnsignedValue(0x0B);
    QVERIFY(valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));
    numeric.initialValue = regmap::UnsignedValue(0xF6);

    numeric.resetValue = regmap::UnsignedValue(0xF5);
    auto resetDiagnostic =
        valueRangeDiagnostic(workspace, "reg-value", "reset value lies");
    QVERIFY(resetDiagnostic.has_value());
    QCOMPARE(resetDiagnostic->severity, regmap::DiagnosticSeverity::warning);
    QCOMPARE(resetDiagnostic->source.cell, std::string("register.reset"));
    numeric.resetValue = regmap::UnsignedValue(0x0B);
    QVERIFY(valueRangeDiagnostic(workspace, "reg-value", "reset value lies"));
    numeric.resetValue = regmap::UnsignedValue(0x0A);

    numeric.initialValue = regmap::UnsignedValue(0x7F);
    numeric.minimumValue = "invalid";
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));
    numeric.minimumValue = "-129";
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));
    numeric.minimumValue = "10";
    numeric.maximumValue = "-10";
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));
    numeric.type = regmap::FieldType::bits;
    numeric.minimumValue = "0";
    numeric.maximumValue = "10";
    QVERIFY(!valueRangeDiagnostic(workspace, "reg-value", "initial value lies"));

    numeric.type = regmap::FieldType::structure;
    numeric.minimumValue.reset();
    numeric.maximumValue.reset();
    numeric.initialValue.reset();
    numeric.resetValue = regmap::UnsignedValue(0xE0);
    regmap::Field field;
    field.id = "field-value";
    field.name = "VALUE_FIELD";
    field.msb = 7;
    field.lsb = 4;
    field.type = regmap::FieldType::signedInteger;
    field.minimumValue = "-2";
    field.maximumValue = "2";
    field.propertySources["reset"].cell = "field.reset";
    numeric.fields = {field};
    QVERIFY(!valueRangeDiagnostic(workspace, "field-value", "reset value lies"));

    numeric.resetValue = regmap::UnsignedValue(0xD0);
    auto inheritedResetDiagnostic =
        valueRangeDiagnostic(workspace, "field-value", "reset value lies");
    QVERIFY(inheritedResetDiagnostic.has_value());
    QCOMPARE(inheritedResetDiagnostic->severity, regmap::DiagnosticSeverity::warning);
    QCOMPARE(inheritedResetDiagnostic->source.cell, std::string("register.reset"));

    numeric.resetValue = regmap::UnsignedValue(0x30);
    numeric.fields.front().resetValue = regmap::UnsignedValue(0x0);
    auto explicitResetDiagnostic =
        valueRangeDiagnostic(workspace, "field-value", "reset value lies");
    QVERIFY(explicitResetDiagnostic.has_value());
    QCOMPARE(explicitResetDiagnostic->severity, regmap::DiagnosticSeverity::warning);
    QCOMPARE(explicitResetDiagnostic->source.cell, std::string("register.reset"));
    numeric.resetValue = regmap::UnsignedValue(0xE0);
    numeric.fields.front().resetValue = regmap::UnsignedValue(0xE);
    QVERIFY(!valueRangeDiagnostic(workspace, "field-value", "reset value lies"));

    numeric.resetValue = regmap::UnsignedValue(0x30);
    numeric.fields.front().resetValue = regmap::UnsignedValue(0x3);
    numeric.fields.front().minimumValue = "-9";
    QVERIFY(!valueRangeDiagnostic(workspace, "field-value", "reset value lies"));
}

void CoreTests::validatesBlockAllocationRanges()
{
    regmap::Workspace workspace;
    workspace.id = "workspace";
    workspace.name = "Workspace";

    regmap::AddressSpace page;
    page.id = "space";
    page.name = "Main";
    page.addressWidth = 12;

    regmap::RegisterBlock first;
    first.id = "block-first";
    first.name = "First";
    first.baseAddress = 0;
    first.size = 0x100;

    regmap::RegisterBlock second;
    second.id = "block-second";
    second.name = "Second";
    second.baseAddress = 0x80;
    second.size = 0x100;

    page.blocks = {first, second};
    workspace.addressSpaces.push_back(page);

    const auto hasDiagnostic =
        [](const regmap::Workspace& candidate,
           std::string_view code, std::string_view objectId,
           std::string_view message) {
            const auto diagnostics = regmap::validateWorkspace(candidate);
            return std::ranges::any_of(
                diagnostics, [&](const regmap::Diagnostic& diagnostic) {
                    return diagnostic.code == code &&
                           diagnostic.objectId == objectId &&
                           diagnostic.message.find(message) !=
                               std::string::npos;
                });
        };

    QVERIFY(hasDiagnostic(
        workspace, "RM3025", "block-second",
        "overlaps register block 'First'"));

    workspace.addressSpaces[0].blocks[1].baseAddress = 0x100;
    QVERIFY(regmap::validateWorkspace(workspace).empty());

    workspace.addressSpaces[0].blocks[1].baseAddress = 0xF80;
    QVERIFY(hasDiagnostic(
        workspace, "RM3011", "block-second",
        "outside the Page address width"));

    auto& addressSpace = workspace.addressSpaces[0];
    addressSpace.addressWidth = 64;
    addressSpace.blocks = {second};
    addressSpace.blocks[0].baseAddress =
        std::numeric_limits<std::uint64_t>::max() - 0x7F;
    QVERIFY(hasDiagnostic(
        workspace, "RM3011", "block-second",
        "overflows the 64-bit address range"));

    addressSpace.blocks[0].baseAddress = 0x200;
    addressSpace.blocks[0].size.reset();
    regmap::Register registerValue;
    registerValue.id = "reg-value";
    registerValue.name = "VALUE";
    registerValue.offset = 0;
    registerValue.width = 32;
    registerValue.array.stride = 4;
    addressSpace.blocks[0].registers.push_back(registerValue);
    QVERIFY(regmap::validateWorkspace(workspace).empty());
}

void CoreTests::generatesReadOnlyArtifacts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const std::filesystem::path manifestPath = std::filesystem::path(
        directory.filePath(QStringLiteral("project.regmap.yaml")).toStdWString());
    auto workspace = stableWorkspace();
    workspace.manifestPath = manifestPath;
    auto manifest = stableManifest(manifestPath);
    manifest.outputDirectory.resolved =
        std::filesystem::path(directory.path().toStdWString()) / "generated";
    for (auto& target : manifest.targets) {
        target.path.resolved = manifest.outputDirectory.resolved / target.path.declared;
    }

    regmap::Register reservedRegister;
    reservedRegister.id = "reg-reserved-test";
    reservedRegister.name = "RESERVED_1C";
    reservedRegister.offset = 0x1C;
    reservedRegister.width = 32;
    reservedRegister.array.count = 1;
    reservedRegister.array.stride = 4;
    reservedRegister.type = regmap::FieldType::reserved;
    reservedRegister.resetValue = regmap::UnsignedValue(0);
    reservedRegister.access = regmap::AccessMode::none;
    reservedRegister.reserved = true;
    reservedRegister.description = "Reserved register style regression.";
    workspace.addressSpaces.front().blocks.front().registers.push_back(
        std::move(reservedRegister));

    regmap::RegisterBlock secondaryBlock;
    secondaryBlock.id = "block-secondary-test";
    secondaryBlock.name = "Secondary";
    secondaryBlock.baseAddress = 0x2000;
    secondaryBlock.size = 0x100;
    secondaryBlock.description = "Second block used to verify block-band colors.";
    workspace.addressSpaces.front().blocks.push_back(
        std::move(secondaryBlock));

    regmap::AddressSpace secondaryPage;
    secondaryPage.id = "space-debug";
    secondaryPage.name = "Debug/Trace";
    secondaryPage.baseAddress = 0x50000000;
    secondaryPage.addressWidth = 32;
    secondaryPage.description = "Empty page used to verify one worksheet per page.";
    workspace.addressSpaces.push_back(std::move(secondaryPage));

    const auto generation = regmap::generateArtifacts(workspace, manifest);
    for (const auto& diagnostic : generation.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(!generation.hasErrors());
    QCOMPARE(generation.artifacts.size(), std::size_t{3});
    const auto* control = regmap::findRegister(workspace, "reg-control");
    QVERIFY(control != nullptr);
    QVERIFY(!control->fields.empty());
    QVERIFY(generation.artifacts[0].isBinary());
    QVERIFY(generation.artifacts[0].binaryContent.size() > std::size_t{1000});
    QVERIFY(generation.artifacts[1].content.find("CONTROL_ADDR") != std::string::npos);
    QVERIFY(generation.artifacts[2].content.find(control->fields.front().name) !=
            std::string::npos);

    const auto diagnostics = regmap::writeGeneratedArtifacts(generation.artifacts);
    QVERIFY(diagnostics.empty());
    for (const auto& artifact : generation.artifacts) {
        QVERIFY(std::filesystem::is_regular_file(artifact.path));
        const auto permissions =
            QFile::permissions(QString::fromStdWString(artifact.path.wstring()));
        QVERIFY((permissions & QFileDevice::WriteOwner) == 0);
        const auto inspection =
            regmap::inspectGeneratedArtifact(
                artifact);
        QVERIFY(inspection.contentCurrent());
        QVERIFY(inspection.readOnly);
        QVERIFY(inspection.synchronized());
    }

    QXlsx::Document workbook(QString::fromStdWString(generation.artifacts.front().path.wstring()));
    QVERIFY(workbook.load());
    QCOMPARE(workbook.documentProperty(QStringLiteral("description")),
             QStringLiteral("Generated by Csrio. Read-only derivative output."));
    const QStringList sheetNames = workbook.sheetNames();
    QCOMPARE(sheetNames.size(), 3);
    QCOMPARE(sheetNames.at(0), QStringLiteral("Overview"));
    QCOMPARE(sheetNames.at(2), QStringLiteral("Debug_Trace"));
    QVERIFY(workbook.selectSheet(QStringLiteral("Overview")));
    QVERIFY(!workbook.currentWorksheet()->isGridLinesVisible());
    QCOMPARE(workbook.currentWorksheet()->frozenRowCount(), 4);
    QCOMPARE(workbook.currentWorksheet()->frozenColumnCount(), 0);
    QCOMPARE(workbook.currentWorksheet()->autoFilter().toString(), QStringLiteral("A4:K7"));
    QVERIFY(workbook.read(1, 1).toString().startsWith(
        QStringLiteral("Register Map Overview - ")));
    QCOMPARE(workbook.read(4, 1).toString(), QStringLiteral("Page"));
    QCOMPARE(workbook.read(4, 6).toString(), QStringLiteral("Block"));
    QCOMPARE(workbook.read(4, 9).toString(), QStringLiteral("Absolute Start"));
    QCOMPARE(workbook.read(5, 6).toString(), QStringLiteral("Control"));
    QCOMPARE(workbook.read(6, 6).toString(), QStringLiteral("Secondary"));
    QCOMPARE(workbook.read(7, 1).toString(), QStringLiteral("Debug/Trace"));
    QCOMPARE(workbook.read(7, 6).toString(), QStringLiteral("Empty"));
    QVERIFY(workbook.selectSheet(QStringLiteral("Debug_Trace")));
    QVERIFY(!workbook.currentWorksheet()->isGridLinesVisible());
    QCOMPARE(workbook.currentWorksheet()->frozenRowCount(), 4);
    QCOMPARE(workbook.currentWorksheet()->frozenColumnCount(), 0);
    QVERIFY(workbook.currentWorksheet()->autoFilter().toString().isEmpty());

    QVERIFY(workbook.selectSheet(sheetNames.at(1)));
    const auto* worksheet = workbook.currentWorksheet();
    QVERIFY(worksheet != nullptr);
    QVERIFY(!worksheet->areSummaryRowsBelow());
    QVERIFY(!worksheet->isGridLinesVisible());
    QCOMPARE(worksheet->frozenRowCount(), 4);
    QCOMPARE(worksheet->frozenColumnCount(), 0);
    QVERIFY(worksheet->autoFilter().toString().isEmpty());
    QCOMPARE(workbook.read(1, 1).toString(),
             QStringLiteral("Page - ") +
                 QString::fromStdString(workspace.addressSpaces.front().name));
    uint expectedDiagramCount = 0;
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            expectedDiagramCount +=
                static_cast<uint>(std::ranges::count_if(block.registers, [](const auto& reg) {
                    return !reg.reserved && reg.type == regmap::FieldType::structure;
                }));
        }
    }
    QCOMPARE(workbook.getImageCount(), expectedDiagramCount);
    QCOMPARE(workbook.read(2, 2).toString(), QStringLiteral("0x43C00000"));
    QCOMPARE(workbook.read(2, 4).toString(), QStringLiteral("32 bits"));
    QVERIFY(workbook.read(2, 6).toString().isEmpty());
    QCOMPARE(workbook.read(4, 1).toString(), QStringLiteral("Address"));
    QCOMPARE(workbook.read(4, 2).toString(), QStringLiteral("Offset"));
    QCOMPARE(workbook.read(4, 3).toString(), QStringLiteral("Name"));
    QCOMPARE(workbook.read(4, 7).toString(), QStringLiteral("Initial Value"));
    QCOMPARE(workbook.read(4, 8).toString(), QStringLiteral("Reset Value"));
    QCOMPARE(workbook.read(4, 9).toString(), QStringLiteral("Tags"));
    QCOMPARE(workbook.read(4, 10).toString(), QStringLiteral("Range"));
    QCOMPARE(workbook.read(4, 11).toString(), QStringLiteral("Description"));
    QVERIFY(workbook.read(5, 1).toString().contains(QStringLiteral("Block - Control")));
    QVERIFY(workbook.read(5, 1).toString().contains(QStringLiteral("Base: 0x0000F000")));
    QVERIFY(workbook.read(5, 1).toString().contains(QStringLiteral("Size: 0x00001000")));
    QVERIFY(std::ranges::any_of(worksheet->mergedCells(), [](const QXlsx::CellRange& range) {
        return range.toString() == QStringLiteral("A5:K5");
    }));
    QVERIFY(std::ranges::any_of(worksheet->mergedCells(), [](const QXlsx::CellRange& range) {
        return range.toString() == QStringLiteral("A26:K26");
    }));
    QVERIFY(workbook.read(26, 1).toString().contains(QStringLiteral("Block - Secondary")));
    const auto firstBlockCell = workbook.cellAt(5, 1);
    const auto secondBlockCell = workbook.cellAt(26, 1);
    QVERIFY(firstBlockCell != nullptr);
    QVERIFY(secondBlockCell != nullptr);
    QVERIFY(firstBlockCell->format().patternForegroundColor() !=
            secondBlockCell->format().patternForegroundColor());
    QCOMPARE(firstBlockCell->format().patternForegroundColor(),
             QColor(QStringLiteral("#4472C4")));
    QCOMPARE(firstBlockCell->format().fontColor(), QColor(QStringLiteral("#FFFFFF")));
    QCOMPARE(workbook.read(6, 1).toString(), QStringLiteral("0x43C0F000"));
    QCOMPARE(workbook.read(6, 2).toString(), QStringLiteral("0x00000000"));
    QVERIFY(workbook.read(6, 3).toString().contains(QStringLiteral("CONTROL")));
    QCOMPARE(workbook.read(6, 6).toString(), QStringLiteral("RW"));
    QCOMPARE(workbook.read(6, 8).toString(), QStringLiteral("0x00000000"));
    const auto firstRegisterCell = workbook.cellAt(6, 1);
    const auto secondRegisterCell = workbook.cellAt(10, 1);
    QVERIFY(firstRegisterCell != nullptr);
    QVERIFY(secondRegisterCell != nullptr);
    QVERIFY(firstRegisterCell->format().patternForegroundColor() !=
            secondRegisterCell->format().patternForegroundColor());
    QCOMPARE(firstRegisterCell->format().horizontalAlignment(),
             QXlsx::Format::AlignHCenter);
    const auto firstDescriptionCell = workbook.cellAt(6, 11);
    QVERIFY(firstDescriptionCell != nullptr);
    QCOMPARE(firstDescriptionCell->format().horizontalAlignment(),
             QXlsx::Format::AlignLeft);

    QImage controlDiagram;
    QVERIFY(workbook.getImage(6, 2, controlDiagram));
    QCOMPARE(controlDiagram.width(), 1240);
    QVERIFY(controlDiagram.height() >= 122);
    QCOMPARE(workbook.read(8, 3).toString(),
             QString::fromStdString(control->fields.front().name));
    QCOMPARE(workbook.read(8, 4).toString(), QStringLiteral("bool"));
    QCOMPARE(workbook.read(8, 6).toString(), QStringLiteral("RW"));
    QCOMPARE(workbook.read(8, 8).toString(), QStringLiteral("0x0"));
    QVERIFY(!workbook.read(8, 3).toString().contains(QChar(0x21B3)));
    const auto firstFieldCell = workbook.cellAt(8, 3);
    const auto alternateFieldCell = workbook.cellAt(12, 3);
    QVERIFY(firstFieldCell != nullptr);
    QVERIFY(alternateFieldCell != nullptr);
    QCOMPARE(firstFieldCell->format().patternForegroundColor(),
             QColor(QStringLiteral("#FFF2CC")));
    QCOMPARE(firstFieldCell->format().patternForegroundColor(),
             alternateFieldCell->format().patternForegroundColor());
    QCOMPARE(firstFieldCell->format().fontColor(), QColor(QStringLiteral("#7F6000")));
    QVERIFY(firstFieldCell->format().fontColor() !=
            firstRegisterCell->format().fontColor());
    QVERIFY(workbook.isRowHidden(7));
    QVERIFY(workbook.isRowHidden(8));
    QStringList expectedTags;
    for (const auto& tag : control->tags) {
        expectedTags.push_back(QString::fromStdString(tag));
    }
    QCOMPARE(workbook.read(6, 9).toString(), expectedTags.join(QStringLiteral(", ")));
    QCOMPARE(workbook.read(25, 1).toString(), QStringLiteral("0x43C0F01C"));
    QCOMPARE(workbook.read(25, 3).toString(), QStringLiteral("[RESERVED] RESERVED_1C"));
    QCOMPARE(workbook.read(25, 4).toString(), QStringLiteral("reserved"));
    QCOMPARE(workbook.read(25, 6).toString(), QStringLiteral("NONE"));
    QCOMPARE(workbook.read(25, 8).toString(), QStringLiteral("0x00000000"));
    const auto reservedAddressCell = workbook.cellAt(25, 1);
    const auto reservedNameCell = workbook.cellAt(25, 3);
    QVERIFY(reservedAddressCell != nullptr);
    QVERIFY(reservedNameCell != nullptr);
    QCOMPARE(reservedAddressCell->format().fontName(), QStringLiteral("Cascadia Mono"));
    QCOMPARE(reservedNameCell->format().patternForegroundColor(),
             QColor(QStringLiteral("#E2E3E5")));
    QCOMPARE(reservedNameCell->format().fontColor(), QColor(QStringLiteral("#9C0006")));
    QVERIFY(reservedNameCell->format().fontBold());

#ifdef Q_OS_WIN
    const auto workbookArtifact =
        std::ranges::find_if(generation.artifacts, [](const regmap::GeneratedArtifact& artifact) {
            return artifact.kind == regmap::GenerationTargetKind::xlsx;
        });
    QVERIFY(workbookArtifact != generation.artifacts.end());
    const HANDLE lockedWorkbook =
        CreateFileW(workbookArtifact->path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(lockedWorkbook != INVALID_HANDLE_VALUE);
    const auto currentLockedDiagnostics = regmap::writeGeneratedArtifacts(
        std::vector<regmap::GeneratedArtifact>{*workbookArtifact});
    QVERIFY(currentLockedDiagnostics.empty());

    auto changedWorkbook = *workbookArtifact;
    QVERIFY(!changedWorkbook.binaryContent.empty());
    changedWorkbook.binaryContent.front() ^= 0xFFU;
    const auto changedLockedDiagnostics = regmap::writeGeneratedArtifacts(
        std::vector<regmap::GeneratedArtifact>{changedWorkbook});
    CloseHandle(lockedWorkbook);
    QVERIFY(std::ranges::any_of(
        changedLockedDiagnostics,
        [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM4000" &&
               diagnostic.message.find("may be open in Excel") != std::string::npos;
    }));
    QVERIFY(
        (QFile::permissions(
             QString::fromStdWString(
                 workbookArtifact
                     ->path.wstring())) &
         QFileDevice::WriteOwner) == 0);

    const HANDLE transientlyLockedWorkbook =
        CreateFileW(
            workbookArtifact->path.wstring().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    QVERIFY(
        transientlyLockedWorkbook !=
        INVALID_HANDLE_VALUE);
    std::thread unlockWorkbook(
        [transientlyLockedWorkbook]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(40));
            CloseHandle(
                transientlyLockedWorkbook);
        });
    const auto transientLockDiagnostics =
        regmap::writeGeneratedArtifacts(
            std::vector<
                regmap::GeneratedArtifact>{
                changedWorkbook});
    unlockWorkbook.join();
    QVERIFY(
        transientLockDiagnostics.empty());
    QVERIFY(
        (QFile::permissions(
             QString::fromStdWString(
                 workbookArtifact
                     ->path.wstring())) &
         QFileDevice::WriteOwner) == 0);
#endif

    const auto header =
        std::ranges::find_if(generation.artifacts, [](const regmap::GeneratedArtifact& artifact) {
            return artifact.kind == regmap::GenerationTargetKind::cHeader;
        });
    QVERIFY(header != generation.artifacts.end());
    const QString headerPath = QString::fromStdWString(header->path.wstring());
    QVERIFY(QFile::setPermissions(headerPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    const auto writableInspection =
        regmap::inspectGeneratedArtifact(
            *header);
    QVERIFY(writableInspection.contentCurrent());
    QVERIFY(!writableInspection.readOnly);
    QVERIFY(!writableInspection.synchronized());
    QVERIFY(
        regmap::writeGeneratedArtifacts(
            std::vector<regmap::GeneratedArtifact>{
                *header})
            .empty());
    QVERIFY(
        regmap::inspectGeneratedArtifact(
            *header)
            .synchronized());

    QVERIFY(QFile::setPermissions(headerPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    writeTextFile(headerPath, QByteArrayLiteral("externally modified\n"));
    QVERIFY(QFile::setPermissions(headerPath, QFileDevice::ReadOwner));
    const auto modifiedInspection =
        regmap::inspectGeneratedArtifact(
            *header);
    QCOMPARE(
        modifiedInspection.state,
        regmap::GeneratedArtifactState::
            modified);
    QVERIFY(!modifiedInspection.synchronized());
    QVERIFY(regmap::writeGeneratedArtifacts(generation.artifacts).empty());
    QFile restoredHeader(headerPath);
    QVERIFY(restoredHeader.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(restoredHeader.readAll(), QByteArray::fromStdString(header->content));
    restoredHeader.close();
    QVERIFY((QFile::permissions(headerPath) & QFileDevice::WriteOwner) == 0);

    for (const auto& artifact : generation.artifacts) {
        QFile::setPermissions(QString::fromStdWString(artifact.path.wstring()),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

void CoreTests::reportsGeneratedIdentifierCollisions()
{
    regmap::Workspace workspace;
    workspace.id = "workspace-c-symbols";
    workspace.name = "C Symbols";

    regmap::AddressSpace page;
    page.id = "page-main";
    page.name = "Main";

    regmap::RegisterBlock block;
    block.id = "block-control";
    block.name = "Control";
    block.size = 8;

    regmap::Register status;
    status.id = "reg-status";
    status.name = "STATUS";
    status.offset = 0;
    status.width = 32;
    status.type = regmap::FieldType::structure;

    regmap::Field dashedField;
    dashedField.id = "field-ready-dashed";
    dashedField.name = "READY-FLAG";
    dashedField.msb = 0;
    dashedField.lsb = 0;

    regmap::Field underscoredField;
    underscoredField.id = "field-ready-underscored";
    underscoredField.name = "READY_FLAG";
    underscoredField.msb = 1;
    underscoredField.lsb = 1;

    status.fields = {dashedField, underscoredField};

    regmap::Register mode;
    mode.id = "reg-mode";
    mode.name = "MODE";
    mode.offset = 4;
    mode.width = 2;
    mode.array.stride = 1;
    mode.type = regmap::FieldType::enumeration;

    regmap::EnumValue widthEnum;
    widthEnum.id = "enum-mode-width";
    widthEnum.name = "WIDTH";
    widthEnum.value = regmap::UnsignedValue(0);

    regmap::EnumValue activeEnum;
    activeEnum.id = "enum-mode-active";
    activeEnum.name = "ACTIVE";
    activeEnum.value = regmap::UnsignedValue(1);
    mode.enumValues = {widthEnum, activeEnum};

    block.registers = {status, mode};
    page.blocks.push_back(block);
    workspace.addressSpaces.push_back(page);
    QVERIFY(regmap::validateWorkspace(workspace).empty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    regmap::ProjectManifest manifest;
    manifest.workspaceId = workspace.id;
    manifest.workspaceName = workspace.name;
    regmap::GenerationTargetConfig target;
    target.kind = regmap::GenerationTargetKind::cHeader;
    target.path.declared = "registers.h";
    target.path.resolved =
        std::filesystem::path(directory.path().toStdWString()) / target.path.declared;
    target.options.emplace("guard", "MAIN_CONTROL_MODE_WIDTH");
    manifest.targets.push_back(std::move(target));

    const auto generation = regmap::generateArtifacts(workspace, manifest);
    QVERIFY(generation.hasErrors());
    QCOMPARE(
        std::ranges::count_if(
            generation.diagnostics, [](const regmap::Diagnostic& diagnostic) {
                return diagnostic.code == "RM4001";
            }),
        std::ptrdiff_t{3});
    QVERIFY(std::ranges::any_of(
        generation.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM4001" &&
                   diagnostic.objectId == "field-ready-underscored";
        }));
    QVERIFY(std::ranges::any_of(
        generation.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM4001" && diagnostic.objectId == "enum-mode-width";
        }));
    QVERIFY(std::ranges::any_of(
        generation.diagnostics, [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM4001" && diagnostic.objectId == "reg-mode";
        }));

    manifest.targets.front().kind = regmap::GenerationTargetKind::markdown;
    manifest.targets.front().path.declared = "registers.md";
    manifest.targets.front().path.resolved =
        std::filesystem::path(directory.path().toStdWString()) /
        manifest.targets.front().path.declared;
    const auto markdownGeneration = regmap::generateArtifacts(workspace, manifest);
    QVERIFY(!markdownGeneration.hasErrors());
    QCOMPARE(markdownGeneration.artifacts.size(), std::size_t{1});
    QVERIFY(markdownGeneration.artifacts.front().content.find("READY-FLAG") !=
            std::string::npos);
    QVERIFY(markdownGeneration.artifacts.front().content.find("READY_FLAG") !=
            std::string::npos);
}

void CoreTests::sanitizesXlsxWorksheetNames()
{
    regmap::Workspace workspace;
    workspace.id = "workspace-sheet-names";
    workspace.name = "Worksheet names";

    const auto appendPage = [&workspace](std::string name) {
        regmap::AddressSpace page;
        page.id = "page-" + std::to_string(workspace.addressSpaces.size());
        page.name = std::move(name);
        workspace.addressSpaces.push_back(std::move(page));
    };

    const std::string longName = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    appendPage("'Status'");
    appendPage("status");
    appendPage(longName);
    appendPage(longName);
    appendPage("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234'TAIL");
    appendPage("[]:*?/\\");
    appendPage("   ");
    appendPage("''");
    appendPage("Overview");

    const auto exported = regmap::exportReadOnlyWorkbook(workspace);
    for (const auto& diagnostic : exported.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " +
                                                     diagnostic.message);
    }
    QVERIFY(exported.diagnostics.empty());
    QVERIFY(!exported.bytes.empty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("worksheet-names.xlsx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray bytes(reinterpret_cast<const char*>(exported.bytes.data()),
                           static_cast<qsizetype>(exported.bytes.size()));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    QXlsx::Document workbook(path);
    QVERIFY(workbook.load());
    const QStringList names = workbook.sheetNames();
    QCOMPARE(names.size(), 10);
    QCOMPARE(names.at(0), QStringLiteral("Overview"));
    QCOMPARE(names.at(1), QStringLiteral("Status"));
    QCOMPARE(names.at(2), QStringLiteral("status (2)"));
    QCOMPARE(names.at(3), QString::fromStdString(longName).left(31));
    QCOMPARE(names.at(4), names.at(3).left(27) + QStringLiteral(" (2)"));
    QCOMPARE(names.at(5), QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234"));
    QCOMPARE(names.at(7), QStringLiteral("Page"));
    QCOMPARE(names.at(8), QStringLiteral("Page (2)"));
    QCOMPARE(names.at(9), QStringLiteral("Overview (2)"));

    for (qsizetype index = 0; index < names.size(); ++index) {
        QVERIFY(names.at(index).size() <= 31);
        QVERIFY(!names.at(index).startsWith(QLatin1Char('\'')));
        QVERIFY(!names.at(index).endsWith(QLatin1Char('\'')));
        for (qsizetype previous = 0; previous < index; ++previous)
            QVERIFY(names.at(previous).compare(names.at(index), Qt::CaseInsensitive) != 0);
    }
}

void CoreTests::diffsByStableId()
{
    regmap::Workspace before;
    before.id = "workspace";
    before.name = "Workspace";
    regmap::AddressSpace address;
    address.id = "space";
    address.name = "Main";
    regmap::RegisterBlock block;
    block.id = "block";
    block.name = "Control";
    regmap::Register reg;
    reg.id = "register";
    reg.name = "CONTROL";
    reg.source.row = 2;
    block.registers.push_back(reg);

    regmap::Register secondRegister;
    secondRegister.id = "register-secondary";
    secondRegister.name = "STATUS";
    secondRegister.offset = 4;
    secondRegister.source.row = 3;
    block.registers.push_back(secondRegister);

    regmap::RegisterBlock secondaryBlock;
    secondaryBlock.id = "block-secondary";
    secondaryBlock.name = "Secondary";
    address.blocks.push_back(block);
    address.blocks.push_back(secondaryBlock);
    before.addressSpaces.push_back(address);

    regmap::Workspace after = before;
    after.addressSpaces.front().blocks.front().registers.front().source.row = 20;
    QVERIFY(regmap::diffWorkspaces(before, after).empty());

    after.addressSpaces.front().blocks.front().registers.front().offset = 4;
    const auto changes = regmap::diffWorkspaces(before, after);
    QCOMPARE(changes.size(), std::size_t{1});
    QVERIFY(changes.front().change == regmap::ChangeKind::modified);
    QVERIFY(changes.front().objectKind == regmap::ObjectKind::reg);
    QCOMPARE(changes.front().id, std::string("register"));
    QCOMPARE(changes.front().summary, std::string("Properties changed"));

    after = before;
    after.addressSpaces.front()
        .blocks.front()
        .registers.front()
        .addressFixed = true;
    const auto fixedChanges =
        regmap::diffWorkspaces(
            before,
            after);
    QCOMPARE(
        fixedChanges.size(),
        std::size_t{1});
    QVERIFY(
        fixedChanges.front().change ==
        regmap::ChangeKind::modified);
    QCOMPARE(
        fixedChanges.front().id,
        std::string("register"));
    QCOMPARE(
        fixedChanges.front().summary,
        std::string(
            "Properties changed"));

    after = before;
    auto& reordered = after.addressSpaces.front().blocks.front().registers;
    std::reverse(reordered.begin(), reordered.end());
    const auto orderChanges = regmap::diffWorkspaces(before, after);
    QCOMPARE(orderChanges.size(), std::size_t{2});
    QVERIFY(std::ranges::all_of(orderChanges, [](const regmap::ModelChange& change) {
        return change.change == regmap::ChangeKind::modified &&
            change.objectKind == regmap::ObjectKind::reg &&
            change.summary == "Moved or reordered";
    }));

    after = before;
    auto& sourceRegisters = after.addressSpaces.front().blocks.front().registers;
    auto& targetRegisters = after.addressSpaces.front().blocks.back().registers;
    targetRegisters.push_back(sourceRegisters.back());
    sourceRegisters.pop_back();
    const auto parentChanges = regmap::diffWorkspaces(before, after);
    QCOMPARE(parentChanges.size(), std::size_t{1});
    QVERIFY(parentChanges.front().change == regmap::ChangeKind::modified);
    QCOMPARE(parentChanges.front().id, std::string("register-secondary"));
    QCOMPARE(parentChanges.front().summary, std::string("Moved or reordered"));
}

void CoreTests::plansExternalChangesWithDependenciesAndValidation()
{
    const regmap::Workspace current = stableWorkspace();
    regmap::Workspace external = current;

    regmap::AddressSpace page;
    page.id = "space-external";
    page.name = "External";
    page.baseAddress = UINT64_C(0x20000000);
    page.addressWidth = 32;
    regmap::RegisterBlock block;
    block.id = "block-external";
    block.name = "External Block";
    block.size = UINT64_C(0x100);
    regmap::Register reg;
    reg.id = "reg-external";
    reg.name = "EXTERNAL";
    reg.type = regmap::FieldType::structure;
    reg.resetValue = regmap::UnsignedValue(0);
    regmap::Field field;
    field.id = "field-external";
    field.name = "MODE";
    field.type = regmap::FieldType::enumeration;
    regmap::EnumValue enumValue;
    enumValue.id = "enum-external-zero";
    enumValue.name = "ZERO";
    enumValue.value = regmap::UnsignedValue(0);
    field.enumValues.push_back(enumValue);
    reg.fields.push_back(field);
    block.registers.push_back(reg);
    page.blocks.push_back(block);
    external.addressSpaces.push_back(page);
    const auto externalDiagnostics = regmap::validateWorkspace(external);
    QVERIFY(std::ranges::none_of(
        externalDiagnostics,
        [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.severity == regmap::DiagnosticSeverity::error;
        }));

    const auto additions = regmap::diffWorkspaces(current, external);
    const auto leaf = std::ranges::find(
        additions, std::string{"enum-external-zero"},
        &regmap::ModelChange::id);
    QVERIFY(leaf != additions.end());
    QVERIFY(!leaf->stableId.empty());
    QCOMPARE(leaf->afterParentId, std::string{"field-external"});
    QVERIFY(!leaf->dependencies.empty());

    regmap::Workspace partiallyPresent = current;
    regmap::AddressSpace partialPage = page;
    partialPage.name = "Locally created placeholder";
    partialPage.blocks.clear();
    partiallyPresent.addressSpaces.push_back(std::move(partialPage));
    const auto reclassified = regmap::diffWorkspaces(
        partiallyPresent, external);
    const auto reclassifiedPage = std::ranges::find(
        reclassified, std::string{"space-external"},
        &regmap::ModelChange::id);
    const auto addedPage = std::ranges::find(
        additions, std::string{"space-external"},
        &regmap::ModelChange::id);
    QVERIFY(reclassifiedPage != reclassified.end());
    QVERIFY(addedPage != additions.end());
    QVERIFY(reclassifiedPage->change == regmap::ChangeKind::modified);
    QVERIFY(addedPage->change == regmap::ChangeKind::added);
    QCOMPARE(reclassifiedPage->stableId, addedPage->stableId);

    const regmap::WorkspaceChangePlan additionPlan =
        regmap::planWorkspaceChanges(
            current, external, {leaf->stableId});
    QVERIFY2(additionPlan.valid(),
             additionPlan.diagnostics.empty()
                 ? "Dependency plan did not produce a workspace"
                 : additionPlan.diagnostics.front().message.c_str());
    QCOMPARE(additionPlan.changes.size(), std::size_t{5});
    QVERIFY(regmap::findAddressSpace(*additionPlan.workspace,
                                     "space-external") != nullptr);
    QVERIFY(regmap::findRegisterBlock(*additionPlan.workspace,
                                      "block-external") != nullptr);
    QVERIFY(regmap::findRegister(*additionPlan.workspace,
                                 "reg-external") != nullptr);
    QVERIFY(regmap::findField(*additionPlan.workspace,
                              "field-external") != nullptr);
    QVERIFY(regmap::findEnumValue(*additionPlan.workspace,
                                  "enum-external-zero") != nullptr);

    regmap::WorkspaceStore store(current);
    const regmap::Workspace accepted = *additionPlan.workspace;
    QVERIFY(store.transact(
        "Accept external dependency tree",
        [accepted](regmap::Workspace& workspace) {
            workspace = accepted;
        }));
    QCOMPARE(store.undoDepth(), std::size_t{1});
    QVERIFY(regmap::findRegister(*store.workspace(), "reg-external") != nullptr);
    QVERIFY(store.undo());
    QVERIFY(regmap::findRegister(*store.workspace(), "reg-external") == nullptr);
    QVERIFY(store.redo());
    QVERIFY(regmap::findRegister(*store.workspace(), "reg-external") != nullptr);

    regmap::Workspace removed = current;
    QVERIFY(regmap::removeObject(removed, "block-control"));
    const auto removals = regmap::diffWorkspaces(current, removed);
    const auto removedBlock = std::ranges::find(
        removals, std::string{"block-control"},
        &regmap::ModelChange::id);
    QVERIFY(removedBlock != removals.end());
    const regmap::WorkspaceChangePlan removalPlan =
        regmap::planWorkspaceChanges(
            current, removed, {removedBlock->stableId});
    QVERIFY(removalPlan.valid());
    QVERIFY(removalPlan.changes.size() > std::size_t{1});
    QVERIFY(regmap::findRegisterBlock(*removalPlan.workspace,
                                      "block-control") == nullptr);

    regmap::Workspace invalid = current;
    regmap::Register* invalidStatus =
        regmap::findRegister(invalid, "reg-status");
    QVERIFY(invalidStatus != nullptr);
    invalidStatus->offset = 0;
    const auto invalidChanges = regmap::diffWorkspaces(current, invalid);
    const auto invalidRegister = std::ranges::find(
        invalidChanges, std::string{"reg-status"},
        &regmap::ModelChange::id);
    QVERIFY(invalidRegister != invalidChanges.end());
    const regmap::WorkspaceChangePlan invalidPlan =
        regmap::planWorkspaceChanges(
            current, invalid, {invalidRegister->stableId});
    QVERIFY(!invalidPlan.valid());
    QVERIFY(std::ranges::any_of(
        invalidPlan.diagnostics,
        [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.code == "RM3024" &&
                diagnostic.severity == regmap::DiagnosticSeverity::error;
        }));
}

QTEST_MAIN(CoreTests)

#include "core_tests.moc"
