#include "regmap/core/generation.hpp"
#include "regmap/core/validation.hpp"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct RangeCase {
    const char* name;
    const char* input;
    const char* expected;
    regmap::FieldType type{regmap::FieldType::unsignedInteger};
};

regmap::Workspace makeWorkspace()
{
    regmap::Workspace workspace;
    workspace.id = "workspace";
    workspace.name = "Ranges";
    regmap::AddressSpace space;
    space.id = "space";
    space.name = "Main";
    regmap::RegisterBlock block;
    block.id = "block";
    block.name = "Control";
    space.blocks.push_back(block);
    workspace.addressSpaces.push_back(space);
    return workspace;
}

std::array<std::string, 3> addRange(regmap::Workspace& workspace, const RangeCase& value,
                                    std::uint32_t width = 32)
{
    auto& registers = workspace.addressSpaces.front().blocks.front().registers;
    regmap::Register scalar;
    scalar.id = value.name;
    scalar.name = value.name;
    scalar.width = width;
    scalar.offset = registers.size() * 4;
    scalar.type = value.type;
    scalar.minimumValue = value.input;
    scalar.maximumValue = value.input;
    registers.push_back(scalar);

    regmap::Field field;
    field.id = scalar.id + "-field";
    field.name = "VALUE";
    field.msb = width - 1;
    field.type = value.type;
    field.minimumValue = value.input;
    field.maximumValue = value.input;

    regmap::Field group;
    group.id = scalar.id + "-group";
    group.name = "GROUP";
    group.type = regmap::FieldType::structure;
    group.msb = width - 1;
    group.members.push_back(field);
    group.members.front().id += "-member";

    regmap::Register compound;
    compound.id = scalar.id + "-compound";
    compound.name = "FIELDS_" + scalar.name;
    compound.width = width;
    compound.offset = registers.size() * 4;
    compound.type = regmap::FieldType::structure;
    compound.fields = {field};
    registers.push_back(compound);
    regmap::Register nested = compound;
    nested.id = scalar.id + "-nested";
    nested.name = "NESTED_" + scalar.name;
    nested.offset = registers.size() * 4;
    nested.fields = {group};
    registers.push_back(nested);
    return {"MAIN_CONTROL_" + scalar.name,
            "MAIN_CONTROL_" + compound.name + "_VALUE",
            "MAIN_CONTROL_" + nested.name + "_GROUP_VALUE"};
}

regmap::GenerationResult generateHeader(const regmap::Workspace& workspace)
{
    regmap::ProjectManifest manifest;
    regmap::GenerationTargetConfig target;
    target.kind = regmap::GenerationTargetKind::cHeader;
    target.path.resolved = std::filesystem::path("ranges.h");
    manifest.targets.push_back(target);
    return regmap::generateArtifacts(workspace, manifest);
}

void writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(content), content.size());
}

} // namespace

class CHeaderRangeTests final : public QObject {
    Q_OBJECT

private slots:
    void compilesRangesWithModelValues_data();
    void compilesRangesWithModelValues();
    void rejectsUnrepresentableRanges_data();
    void rejectsUnrepresentableRanges();
};

void CHeaderRangeTests::compilesRangesWithModelValues_data()
{
    QTest::addColumn<int>("standard");
    QTest::newRow("C11") << 11;
    QTest::newRow("C17") << 17;
}

void CHeaderRangeTests::compilesRangesWithModelValues()
{
    QFETCH(int, standard);
    using regmap::FieldType;
    const RangeCase cases[] = {
        {"LEADING_ZERO", "010", "UINT64_C(10)"},
        {"ZERO", "000", "UINT64_C(0)"},
        {"OCTAL", "0o10", "UINT64_C(8)"},
        {"UPPER_OCTAL", "0O17", "UINT64_C(15)"},
        {"SEPARATORS", "1_000", "UINT64_C(1000)"},
        {"HEX", "+0X00_FF", "UINT64_C(255)"},
        {"BINARY", "0b10_10", "UINT64_C(10)"},
        {"WHITESPACE", "\t+010 \r\n", "UINT64_C(10)"},
        {"NEGATIVE", "-010", "(-INT64_C(10))", FieldType::signedInteger},
        {"NEGATIVE_OCTAL", "-0o10", "(-INT64_C(8))", FieldType::signedInteger},
        {"NEGATIVE_SEPARATORS", "-1_000", "(-INT64_C(1000))", FieldType::signedInteger},
        {"NEGATIVE_ZERO", "-000", "INT64_C(0)", FieldType::signedInteger},
        {"UNSIGNED_NEGATIVE_ZERO", "-0o0", "UINT64_C(0)"},
        {"SIGNED_POSITIVE", "+1_000", "INT64_C(1000)", FieldType::signedInteger},
        {"SIGNED_MIN", "-0x8000_0000", "INT32_MIN", FieldType::signedInteger},
        {"SIGNED_DECIMAL_MIN", "-2147483648", "INT32_MIN", FieldType::signedInteger},
        {"SIGNED_MAX", "02147483647", "INT32_MAX", FieldType::signedInteger},
        {"UNSIGNED_MAX", "0xFFFF_FFFF", "UINT32_MAX"},
        {"UNSIGNED_DECIMAL_MAX", "4294967295", "UINT32_MAX"},
    };
    auto workspace = makeWorkspace();
    std::string source = "#include \"ranges.h\"\n";
    const auto addAssertions = [&](const RangeCase& value, std::uint32_t width) {
        for (const auto& symbol : addRange(workspace, value, width)) {
            for (const auto* suffix : {"_MIN", "_MAX"}) {
                const auto macro = symbol + suffix;
                source += "_Static_assert(" + macro + " == " + value.expected +
                          ", \"" + macro + " value\");\n";
                if (value.type == FieldType::signedInteger) {
                    source += "_Static_assert(_Generic((" + macro +
                              "), int: 1, long: 1, long long: 1, default: 0), \"" +
                              macro + " signed type\");\n";
                }
            }
        }
    };
    for (const auto& value : cases) {
        addAssertions(value, 32);
    }
    const auto validation = regmap::validateWorkspace(workspace);
    for (const auto& diagnostic : validation) {
        qWarning().noquote() << QString::fromStdString(diagnostic.code + ": " + diagnostic.message);
    }
    QVERIFY(validation.empty());

    // Exercise the generator's integer limits directly; persisted registers are limited to 32 bits.
    const RangeCase generatorLimits[] = {
        {"INT64_MIN_HEX", "-0x8000_0000_0000_0000", "INT64_MIN", FieldType::signedInteger},
        {"INT64_MIN_DECIMAL", "-9223372036854775808", "INT64_MIN", FieldType::signedInteger},
        {"INT64_MAX_DECIMAL", "09223372036854775807", "INT64_MAX", FieldType::signedInteger},
        {"UINT64_MAX_HEX", "0xFFFF_FFFF_FFFF_FFFF", "UINT64_MAX"},
        {"UINT64_MAX_DECIMAL", "18446744073709551615", "UINT64_MAX"},
    };
    for (const auto& value : generatorLimits) {
        addAssertions(value, 64);
    }
    const auto generated = generateHeader(workspace);
    QVERIFY(!generated.hasErrors());
    QCOMPARE(generated.artifacts.size(), std::size_t{1});

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeFile(directory.filePath("ranges.h"), QByteArray::fromStdString(generated.artifacts.front().content));
    writeFile(directory.filePath("values.c"), QByteArray::fromStdString(source));
    writeFile(directory.filePath("CMakeLists.txt"), R"(cmake_minimum_required(VERSION 3.24)
project(CsrioRangeValues LANGUAGES C)
add_library(range_values OBJECT values.c)
set_target_properties(range_values PROPERTIES C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(range_values PRIVATE /W4 /WX)
else()
    target_compile_options(range_values PRIVATE -Wall -Wextra -Werror -pedantic-errors)
endif()
)");

    QByteArray processOutput;
    const auto runCMake = [&](const QStringList& arguments) {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QString::fromUtf8(REGMAP_TEST_CMAKE), arguments);
        const bool finished = process.waitForStarted() && process.waitForFinished(60000);
        processOutput = process.readAll() + process.errorString().toUtf8();
        return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QStringList configure{"-S", directory.path(), "-B", directory.filePath("build"),
                          "-G", REGMAP_TEST_GENERATOR,
                          "-DCMAKE_C_COMPILER=" REGMAP_TEST_C_COMPILER,
                          "-DCMAKE_C_STANDARD=" + QString::number(standard)};
    if (QString::fromUtf8(REGMAP_TEST_GENERATOR_PLATFORM).size() > 0) {
        configure << "-A" << REGMAP_TEST_GENERATOR_PLATFORM;
    }
    if (QString::fromUtf8(REGMAP_TEST_GENERATOR_TOOLSET).size() > 0) {
        configure << "-T" << REGMAP_TEST_GENERATOR_TOOLSET;
    }
    QVERIFY2(runCMake(configure), processOutput.constData());
    QVERIFY2(runCMake({"--build", directory.filePath("build"), "--config", "Debug"}),
             processOutput.constData());
}

void CHeaderRangeTests::rejectsUnrepresentableRanges_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("isSigned");
    QTest::newRow("unsigned-overflow") << QString("18446744073709551616") << false;
    QTest::newRow("signed-overflow") << QString("9223372036854775808") << true;
    QTest::newRow("signed-underflow") << QString("-9223372036854775809") << true;
    QTest::newRow("invalid-syntax") << QString("0x") << false;
    QTest::newRow("negative-unsigned") << QString("-1") << false;
}

void CHeaderRangeTests::rejectsUnrepresentableRanges()
{
    QFETCH(QString, input);
    QFETCH(bool, isSigned);
    auto workspace = makeWorkspace();
    const auto text = input.toStdString();
    const RangeCase value{"VALUE", text.c_str(), "", isSigned ? regmap::FieldType::signedInteger
                                                           : regmap::FieldType::unsignedInteger};
    const auto symbols = addRange(workspace, value, 128);
    const auto generated = generateHeader(workspace);
    QVERIFY(generated.hasErrors());
    QCOMPARE(generated.diagnostics.size(), std::size_t{6});
    for (const auto& diagnostic : generated.diagnostics) {
        QCOMPARE(diagnostic.code, std::string("RM4004"));
        QVERIFY(!diagnostic.objectId.empty());
    }
    for (const auto& symbol : symbols) {
        for (const auto* suffix : {"_MIN ", "_MAX "}) {
            QVERIFY(generated.artifacts.front().content.find("#define " + symbol + suffix) == std::string::npos);
        }
    }
    QCOMPARE(workspace.addressSpaces.front().blocks.front().registers.front().minimumValue.value(), text);
}

QTEST_GUILESS_MAIN(CHeaderRangeTests)
#include "c_header_range_tests.moc"
