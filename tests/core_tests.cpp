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
#include "regmap/core/xlsx_export.hpp"
#include "regmap/core/workspace_store.hpp"
#include "regmap/core/three_way_merge.hpp"

#include <xlsxcell.h>
#include <xlsxcellrange.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QColor>
#include <QFile>
#include <QFileDevice>
#include <QDebug>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
    void squashesTransactionsIntoSingleUndoStep();
    void roundTripsManagedRtl();
    void rejectsInvalidManagedRtlStructure();
    void preservesUnmanagedRtlText();
    void refusesUnmanagedRtlOverwrite();
    void mergesDisjointChangesAndReportsConflicts();
    void roundTripsSynchronizationBaseline();
    void rejectsCorruptSynchronizationBaseline();
    void validatesModelConflicts();
    void generatesReadOnlyArtifacts();
    void reportsGeneratedIdentifierCollisions();
    void sanitizesXlsxWorksheetNames();
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

void CoreTests::squashesTransactionsIntoSingleUndoStep()
{
    auto opened = openCheckedInProject();
    QVERIFY(opened.workspace.has_value());
    regmap::WorkspaceStore store(*opened.workspace);

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
    opened.workspace->addressSpaces.front().blocks.front().registers.push_back(
        std::move(reservedRegister));

    regmap::RegisterBlock secondaryBlock;
    secondaryBlock.id = "block-secondary-test";
    secondaryBlock.name = "Secondary";
    secondaryBlock.baseAddress = 0x2000;
    secondaryBlock.size = 0x100;
    secondaryBlock.description = "Second block used to verify block-band colors.";
    opened.workspace->addressSpaces.front().blocks.push_back(
        std::move(secondaryBlock));

    regmap::AddressSpace secondaryPage;
    secondaryPage.id = "space-debug";
    secondaryPage.name = "Debug/Trace";
    secondaryPage.baseAddress = 0x50000000;
    secondaryPage.addressWidth = 32;
    secondaryPage.description = "Empty page used to verify one worksheet per page.";
    opened.workspace->addressSpaces.push_back(std::move(secondaryPage));

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
    const QStringList sheetNames = workbook.sheetNames();
    QCOMPARE(sheetNames.size(), 2);
    QCOMPARE(sheetNames.at(1), QStringLiteral("Debug_Trace"));
    QVERIFY(workbook.selectSheet(QStringLiteral("Debug_Trace")));
    QVERIFY(!workbook.currentWorksheet()->isGridLinesVisible());
    QCOMPARE(workbook.currentWorksheet()->frozenRowCount(), 4);
    QCOMPARE(workbook.currentWorksheet()->frozenColumnCount(), 0);
    QCOMPARE(workbook.currentWorksheet()->autoFilter().toString(), QStringLiteral("A4:K4"));

    QVERIFY(workbook.selectSheet(sheetNames.front()));
    const auto* worksheet = workbook.currentWorksheet();
    QVERIFY(worksheet != nullptr);
    QVERIFY(!worksheet->areSummaryRowsBelow());
    QVERIFY(!worksheet->isGridLinesVisible());
    QCOMPARE(worksheet->frozenRowCount(), 4);
    QCOMPARE(worksheet->frozenColumnCount(), 0);
    QCOMPARE(worksheet->autoFilter().toString(), QStringLiteral("A4:K26"));
    QCOMPARE(workbook.read(1, 1).toString(),
             QStringLiteral("Page - ") +
                 QString::fromStdString(opened.workspace->addressSpaces.front().name));
    uint expectedDiagramCount = 0;
    for (const auto& space : opened.workspace->addressSpaces) {
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
    const auto lockedDiagnostics = regmap::writeGeneratedArtifacts(
        std::vector<regmap::GeneratedArtifact>{*workbookArtifact});
    CloseHandle(lockedWorkbook);
    QVERIFY(std::ranges::any_of(lockedDiagnostics, [](const regmap::Diagnostic& diagnostic) {
        return diagnostic.code == "RM4000" &&
               diagnostic.message.find("may be open in Excel") != std::string::npos;
    }));
#endif

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
    QCOMPARE(names.size(), 8);
    QCOMPARE(names.at(0), QStringLiteral("Status"));
    QCOMPARE(names.at(1), QStringLiteral("status (2)"));
    QCOMPARE(names.at(2), QString::fromStdString(longName).left(31));
    QCOMPARE(names.at(3), names.at(2).left(27) + QStringLiteral(" (2)"));
    QCOMPARE(names.at(4), QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234"));
    QCOMPARE(names.at(6), QStringLiteral("Page"));
    QCOMPARE(names.at(7), QStringLiteral("Page (2)"));

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

QTEST_MAIN(CoreTests)

#include "core_tests.moc"
