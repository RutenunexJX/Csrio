#include "regmap/core/generation.hpp"
#include "regmap/core/manifest.hpp"
#include "regmap/core/model.hpp"
#include "regmap/core/model_tokens.hpp"
#include "regmap/core/project.hpp"
#include "regmap/core/rtl_sync.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/sync_diff.hpp"
#include "regmap/core/unsigned_value.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"
#include "regmap/core/three_way_merge.hpp"

#include <xlsxcell.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QFile>
#include <QFileDevice>
#include <QDebug>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesUnsignedValues();
    void rejectsInvalidUnsignedValues();
    void slicesUnsignedValues();
    void parsesModelTokens();
    void loadsSchemaV2Manifest();
    void reportsInvalidManifest();
    void rejectsUnsafeManifestPaths();
    void loadsCheckedInProject();
    void roundTripsProjectFile();
    void roundTripsExtendedModel();
    void tracksTransactionsAndStableIds();
    void roundTripsManagedRtl();
    void rejectsInvalidManagedRtlStructure();
    void preservesUnmanagedRtlText();
    void refusesUnmanagedRtlOverwrite();
    void mergesDisjointChangesAndReportsConflicts();
    void roundTripsSynchronizationBaseline();
    void rejectsCorruptSynchronizationBaseline();
    void validatesModelConflicts();
    void generatesReadOnlyArtifacts();
    void diffsByStableId();
};

namespace {

[[nodiscard]] std::filesystem::path checkedInProject()
{
    return std::filesystem::path(REGMAP_SOURCE_DIR) / "examples" / "minimal" / ".regmap.yaml";
}

void writeTextFile(const QString& path, const QByteArray& text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write(text), text.size());
    file.close();
}

[[nodiscard]] regmap::ProjectOpenResult openCheckedInProject()
{
    return regmap::openProject(checkedInProject());
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

void CoreTests::loadsCheckedInProject()
{
    const auto result = openCheckedInProject();
    QVERIFY(!result.hasErrors());
    QVERIFY(result.manifest.has_value());
    QVERIFY(result.workspace.has_value());
    QCOMPARE(result.workspace->addressSpaces.size(), std::size_t{1});
    const auto& blocks = result.workspace->addressSpaces.front().blocks;
    QCOMPARE(blocks.size(), std::size_t{1});
    QVERIFY(blocks.front().registers.size() >= std::size_t{3});
    const auto* control = regmap::findRegister(*result.workspace, "reg-control");
    QVERIFY(control != nullptr);
    QVERIFY(!control->fields.empty());
    QVERIFY(control->fields.front().type == regmap::FieldType::boolean);
    QVERIFY(regmap::validateWorkspace(*result.workspace).empty());
    QCOMPARE(blocks.front().registers.front().propertySources.at("offset").cell,
             std::string("workspace.address_spaces[0].blocks[0].registers[0].offset"));

    const auto rtl = regmap::parseManagedRtl(std::filesystem::path(REGMAP_SOURCE_DIR) / "examples" /
                                             "minimal" / "rtl" / "minimal_registers.sv");
    QVERIFY(!rtl.hasErrors());
    QVERIFY(rtl.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*result.workspace, *rtl.workspace).empty());
}

void CoreTests::roundTripsProjectFile()
{
    auto source = openCheckedInProject();
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
    mode.offset = 10;
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
    QVERIFY(regmap::saveProjectFile(manifest, workspace).empty());

    const auto loaded = regmap::loadWorkspaceFromProjectFile(projectPath);
    QVERIFY(!loaded.hasErrors());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(workspace, *loaded.workspace).empty());
    const auto* loadedActive = regmap::findRegister(*loaded.workspace, "reg-active");
    QVERIFY(loadedActive != nullptr);
    QCOMPARE(loadedActive->tags.size(), std::size_t{2});
    QVERIFY(loadedActive->type == regmap::FieldType::structure);
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

    auto invalid = workspace;
    regmap::findField(invalid, "field-count")->maximumValue = "256";
    const auto diagnostics = regmap::validateWorkspace(invalid);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM3052";
    }));
}

void CoreTests::tracksTransactionsAndStableIds()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    regmap::WorkspaceStore store(*opened.workspace);
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
}

void CoreTests::roundTripsManagedRtl()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path rtlPath =
        std::filesystem::path(directory.filePath(QStringLiteral("registers.sv")).toStdWString());

    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", *opened.workspace).empty());
    auto parsed = regmap::parseManagedRtl(rtlPath);
    for (const auto& diagnostic : parsed.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(!parsed.hasErrors());
    QVERIFY(parsed.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*opened.workspace, *parsed.workspace).empty());

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
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("registers.sv"));
    const std::filesystem::path rtlPath(path.toStdWString());
    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", *opened.workspace).empty());

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
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path rtlPath =
        std::filesystem::path(directory.filePath(QStringLiteral("registers.sv")).toStdWString());
    QVERIFY(regmap::writeManagedRtl(rtlPath, "test_registers", *opened.workspace).empty());

    QFile file(QString::fromStdWString(rtlPath.wstring()));
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    text.replace(QStringLiteral("endmodule"),
                 QStringLiteral("  logic user_owned_signal;\nendmodule"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
    file.close();

    regmap::Workspace changed = *opened.workspace;
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
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
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

    const auto diagnostics = regmap::writeManagedRtl(path, "test_registers", *opened.workspace);
    QVERIFY(std::ranges::any_of(diagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM5001";
    }));

    QFile file(qtPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(file.readAll(), original);
}

void CoreTests::mergesDisjointChangesAndReportsConflicts()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    const regmap::Workspace base = *opened.workspace;
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
}

void CoreTests::roundTripsSynchronizationBaseline()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const std::filesystem::path path = std::filesystem::path(
        directory.filePath(QStringLiteral(".regmap.sync.json")).toStdWString());
    QVERIFY(regmap::saveSyncBaseline(path, *opened.workspace).empty());
    const auto loaded = regmap::loadSyncBaseline(path);
    QVERIFY(loaded.diagnostics.empty());
    QVERIFY(loaded.workspace.has_value());
    QVERIFY(regmap::diffWorkspaces(*opened.workspace, *loaded.workspace).empty());
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
    QVERIFY(hasCode("RM3031"));
    QVERIFY(hasCode("RM3035"));
    QVERIFY(hasCode("RM3043"));
}

void CoreTests::generatesReadOnlyArtifacts()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.manifest.has_value());
    QVERIFY(opened.workspace.has_value());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto manifest = *opened.manifest;
    manifest.outputDirectory.resolved =
        std::filesystem::path(directory.path().toStdWString()) / "generated";
    for (auto& target : manifest.targets) {
        target.path.resolved = manifest.outputDirectory.resolved / target.path.declared;
    }

    const auto generation = regmap::generateArtifacts(*opened.workspace, manifest);
    for (const auto& diagnostic : generation.diagnostics) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(!generation.hasErrors());
    QCOMPARE(generation.artifacts.size(), std::size_t{3});
    const auto* control = regmap::findRegister(*opened.workspace, "reg-control");
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
    }

    QXlsx::Document workbook(QString::fromStdWString(generation.artifacts.front().path.wstring()));
    QVERIFY(workbook.load());
    QCOMPARE(workbook.sheetNames(),
             QStringList({QStringLiteral("Registers"), QStringLiteral("Register Map")}));
    QVERIFY(workbook.selectSheet(QStringLiteral("Register Map")));
    QVERIFY(!workbook.currentWorksheet()->areSummaryRowsBelow());
    uint expectedDiagramCount = 0;
    for (const auto& space : opened.workspace->addressSpaces) {
        for (const auto& block : space.blocks) {
            expectedDiagramCount +=
                static_cast<uint>(std::ranges::count_if(block.registers, [](const auto& reg) {
                    return reg.type == regmap::FieldType::structure;
                }));
        }
    }
    QCOMPARE(workbook.getImageCount(), expectedDiagramCount);
    QCOMPARE(workbook.read(3, 2).toString(), QStringLiteral("Page"));
    QCOMPARE(workbook.read(3, 4).toString(), QStringLiteral("Register / Field"));
    QCOMPARE(workbook.read(3, 10).toString(), QStringLiteral("Initial"));
    QCOMPARE(workbook.read(3, 12).toString(), QStringLiteral("Reset Domain"));
    QVERIFY(workbook.read(4, 4).toString().contains(QStringLiteral("CONTROL")));
    const auto firstRegisterCell = workbook.cellAt(4, 1);
    const auto secondRegisterCell = workbook.cellAt(8, 1);
    QVERIFY(firstRegisterCell != nullptr);
    QVERIFY(secondRegisterCell != nullptr);
    QVERIFY(firstRegisterCell->format().patternForegroundColor() !=
            secondRegisterCell->format().patternForegroundColor());

    QImage controlDiagram;
    QVERIFY(workbook.getImage(4, 3, controlDiagram));
    QCOMPARE(controlDiagram.width(), 1240);
    QVERIFY(controlDiagram.height() >= 122);
    QVERIFY(workbook.read(6, 4).toString().contains(
        QString::fromStdString(control->fields.front().name)));
    QCOMPARE(workbook.read(6, 7).toString(), QStringLiteral("bool"));
    QVERIFY(workbook.isRowHidden(5));
    QVERIFY(workbook.isRowHidden(6));
    QStringList expectedTags;
    for (const auto& tag : control->tags) {
        expectedTags.push_back(QString::fromStdString(tag));
    }
    QCOMPARE(workbook.read(4, 13).toString(), expectedTags.join(QStringLiteral(", ")));

    const auto header =
        std::ranges::find_if(generation.artifacts, [](const regmap::GeneratedArtifact& artifact) {
            return artifact.kind == regmap::GenerationTargetKind::cHeader;
        });
    QVERIFY(header != generation.artifacts.end());
    const QString headerPath = QString::fromStdWString(header->path.wstring());
    QVERIFY(QFile::setPermissions(headerPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    writeTextFile(headerPath, QByteArrayLiteral("externally modified\n"));
    QVERIFY(QFile::setPermissions(headerPath, QFileDevice::ReadOwner));
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
    address.blocks.push_back(block);
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
}

QTEST_MAIN(CoreTests)

#include "core_tests.moc"
