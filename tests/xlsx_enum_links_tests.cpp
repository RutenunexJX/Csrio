#include "project_controller.hpp"
#include "regmap/core/project.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/rtl_sync.hpp"

#include <xlsxcell.h>
#include <xlsxdocument.h>
#include <xlsxworksheet.h>

#include <QBuffer>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtTest>

namespace {
using namespace regmap;
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
struct Reply { int code; QJsonObject root; QByteArray bytes; };
Reply cli(QStringList args, const QJsonObject& input = {})
{
    args.prepend("--json");
    QProcess process;
    process.start(QString::fromUtf8(REGMAP_CLI_EXECUTABLE), args);
    if (!process.waitForStarted(10000)) return {-1, {}, process.errorString().toUtf8()};
    if (!input.isEmpty()) process.write(QJsonDocument(input).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    if (!process.waitForFinished(30000)) return {-1, {}, process.errorString().toUtf8()};
    const auto bytes = process.readAllStandardOutput();
    return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            QJsonDocument::fromJson(bytes).object(), bytes};
}
struct Link { QString location; bool external; };
QMap<QString, Link> hyperlinks(QXlsx::Worksheet& sheet)
{
    QMap<QString, Link> result;
    QXmlStreamReader xml(sheet.saveToXmlData());
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("hyperlink")) {
            const auto attrs = xml.attributes();
            result.insert(attrs.value("ref").toString(),
                {attrs.value("location").toString(),
                 attrs.hasAttribute("http://schemas.openxmlformats.org/officeDocument/2006/relationships", "id")});
        }
    }
    return result;
}
QStringList layoutDescriptions()
{
    return {QStringLiteral("短说明。"),
        QStringLiteral("长中文说明：采样通道在转换完成后更新寄存器，软件读取时应保留状态位，配置变更须等待下一采样周期生效。").repeated(8),
        QStringLiteral("Mixed 中英文：DMA buffer receives 1024 samples; sample_rate = 48 kHz，设置 ENABLE 后等待 READY。 ").repeated(8),
        QStringLiteral("第一行：初始化。\n第二行：启动采样。\n\n第四行：读取结果。\n第五行：结束。"),
        QStringLiteral("CRLF 第一行\r\n第二行\r第三行"),
        QStringLiteral("UnbrokenToken_") + QString(240, QLatin1Char('W')),
        QStringLiteral("短说明，同行由长枚举决定高度。"),
        QStringLiteral("超长说明，保留内容且不超过 Excel 单行高度限制。\n").repeated(50) + QStringLiteral("说明结束")};
}
struct Fixture {
    QTemporaryDir directory;
    QString path, xlsx, header;
    explicit Fixture(bool ambiguous, bool layout = false)
    {
        path = directory.filePath("project.regmap.yaml");
        xlsx = directory.filePath("out/map.xlsx");
        header = directory.filePath("out/map.h");
        Workspace workspace;
        workspace.id = "workspace";
        workspace.name = "Enum link acceptance";
        workspace.manifestPath = std::filesystem::path(path.toStdWString());
        AddressSpace page;
        page.id = "page-a";
        page.name = "AI's [inputs] 中文";
        RegisterBlock block;
        block.id = "block-a";
        block.name = "AI";
        block.size = 256;
        if (layout) {
            page.description = layoutDescriptions().at(1).toStdString();
            block.description = layoutDescriptions().at(3).toStdString();
        }
        for (int registerIndex = 0; registerIndex < (ambiguous ? 2 : 1); ++registerIndex) {
            Register reg;
            reg.id = "reg-" + std::to_string(registerIndex);
            reg.name = registerIndex == 0 ? "REQUESTED_RANGES" : "OTHER_RANGES";
            reg.type = FieldType::structure;
            reg.offset = static_cast<std::uint64_t>(registerIndex) * 4;
            reg.addressFixed = true;
            reg.resetValue = UnsignedValue(0);
            if (layout) reg.description = layoutDescriptions().at(2).toStdString();
            for (unsigned i = 0; i < 8; ++i) {
                Field field;
                field.id = reg.id + "-internal-stable-field-" + std::to_string(i);
                field.name = QStringLiteral("CH%1_RANGE").arg(i + 8, 2, 10, QLatin1Char('0')).toStdString();
                field.type = FieldType::enumeration;
                field.lsb = i * (layout ? 4 : 2);
                field.msb = field.lsb + (layout ? 3 : 1);
                if (layout) field.description = layoutDescriptions().at(i).toStdString();
                for (unsigned code = 0; code < (layout ? 16U : 2U); ++code) {
                    EnumValue value;
                    value.id = field.id + "-enum-" + std::to_string(code);
                    value.name = code == 0 ? "OFF" : "ON";
                    value.value = UnsignedValue(code);
                    value.description = layout
                        ? QStringLiteral("枚举长说明：设置此档位后硬件执行范围检查，Status register 返回处理状态。保留字节不得修改。枚举条目结束。").toStdString()
                        : "Shared definition";
                    if (layout) value.name = "MODE_" + std::to_string(code) + "_WITH_FULL_RANGE_CALIBRATION_AND_STATUS_CHECK";
                    field.enumValues.push_back(value);
                }
                reg.fields.push_back(field);
            }
            block.registers.push_back(reg);
        }
        page.blocks.push_back(block);
        workspace.addressSpaces.push_back(page);
        ProjectManifest manifest;
        manifest.manifestPath = workspace.manifestPath;
        manifest.workspaceId = workspace.id;
        manifest.workspaceName = workspace.name;
        manifest.rtl.path.declared = "rtl/map.sv";
        manifest.rtl.path.resolved = std::filesystem::path(directory.filePath("rtl/map.sv").toStdWString());
        manifest.rtl.moduleName = "enum_links";
        manifest.outputDirectory.declared = "out";
        manifest.outputDirectory.resolved = std::filesystem::path(directory.filePath("out").toStdWString());
        for (const auto& [kind, name] : std::vector<std::pair<GenerationTargetKind, std::string>>{
                 {GenerationTargetKind::xlsx,"map.xlsx"}, {GenerationTargetKind::markdown,"map.md"},
                 {GenerationTargetKind::cHeader,"map.h"}}) {
            GenerationTargetConfig target;
            target.kind = kind;
            target.path.declared = name;
            target.path.resolved = manifest.outputDirectory.resolved / name;
            manifest.targets.push_back(target);
        }
        if (!saveProjectFile(manifest, workspace).empty() ||
            !writeManagedRtl(manifest.rtl.path.resolved, manifest.rtl.moduleName, workspace).empty())
            qFatal("Cannot create isolated enum-link fixture");
    }
    ~Fixture()
    {
        QDirIterator files(directory.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) QFile::setPermissions(files.next(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    Reply setMode(const QString& mode)
    {
        const auto revision = cli({"summary",path}).root.value("revision").toString();
        return cli({"apply",path,"-","--expect",revision,"--enum-tables",mode},
                   {{"api_version",1},{"operations",QJsonArray{}}});
    }
};
}

class XlsxEnumLinksTests : public QObject {
    Q_OBJECT
private slots:
    void laysOutLongContentThroughCliAndGui_data();
    void laysOutLongContentThroughCliAndGui();
    void distinguishesInternalAndExternalLinks();
    void exportsNamedLinksThroughCliAndGui_data();
    void exportsNamedLinksThroughCliAndGui();
};

void XlsxEnumLinksTests::distinguishesInternalAndExternalLinks()
{
    QXlsx::Document document;
    auto* sheet = document.currentWorksheet();
    QVERIFY(sheet);
    QUrl internal;
    internal.setFragment("'Sheet1'!$J$8");
    QVERIFY(sheet->writeHyperlink(1, 1, internal, {}, QStringLiteral("枚举定义同 CH08_RANGE")));
    QVERIFY(sheet->writeHyperlink(2, 1, QUrl("https://example.com/spec#enum"), {}, "External reference"));
    QByteArray bytes;
    QBuffer output(&bytes);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(document.saveAs(&output));
    output.close();
    QBuffer input(&bytes);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QXlsx::Document reopened(&input);
    const auto links = hyperlinks(*reopened.currentWorksheet());
    QCOMPARE(links.size(), 2);
    QVERIFY(!links.value("A1").external);
    QCOMPARE(links.value("A1").location, "'Sheet1'!$J$8");
    QVERIFY(links.value("A2").external);
    QCOMPARE(links.value("A2").location, "enum");
    QCOMPARE(reopened.read(1, 1).toString(), QStringLiteral("枚举定义同 CH08_RANGE"));
}

void XlsxEnumLinksTests::exportsNamedLinksThroughCliAndGui_data()
{
    QTest::addColumn<bool>("ambiguous");
    QTest::newRow("unique-field-names") << false;
    QTest::newRow("same-field-name-in-two-registers") << true;
}

void XlsxEnumLinksTests::exportsNamedLinksThroughCliAndGui()
{
    QFETCH(bool, ambiguous);
    Fixture fixture(ambiguous);
    const auto generated = cli({"generate",fixture.path});
    QVERIFY2(generated.code == 0, generated.bytes.constData());
    const auto fullWorkbook = read(fixture.xlsx), header = read(fixture.header);
    const auto originalFields = cli({"get",fixture.path,"reg-0"}).root.value("result").toObject().value("fields");
    const auto compact = fixture.setMode("consecutive");
    QVERIFY2(compact.code == 0, compact.bytes.constData());
    const auto cliWorkbook = read(fixture.xlsx);
    QVERIFY(cliWorkbook != fullWorkbook);
    QCOMPARE(read(fixture.header), header);
    QCOMPARE(cli({"get",fixture.path,"reg-0"}).root.value("result").toObject().value("fields"), originalFields);

    QXlsx::Document book(fixture.xlsx);
    const auto sheetName = book.sheetNames().at(1);
    QVERIFY(book.selectSheet(sheetName));
    QMap<QString, QMap<QString, int>> rows;
    QString currentRegister;
    for (int row = 1; row <= book.dimension().lastRow(); ++row) {
        const auto name = book.read(row, 3).toString();
        if (name == "REQUESTED_RANGES" || name == "OTHER_RANGES") currentRegister = name;
        if (name.startsWith("CH")) rows[currentRegister].insert(name, row);
    }
    QCOMPARE(rows.size(), ambiguous ? 2 : 1);
    const auto links = hyperlinks(*book.currentWorksheet());
    QCOMPARE(links.size(), (ambiguous ? 2 : 1) * 7);
    QString quotedSheet = sheetName;
    quotedSheet.replace(QLatin1Char('\''), QStringLiteral("''"));
    for (auto reg = rows.cbegin(); reg != rows.cend(); ++reg) {
        QCOMPARE(reg.value().size(), 8);
        const int firstRow = reg.value().value("CH08_RANGE");
        QVERIFY(book.read(firstRow, 10).toString().contains("OFF=0x0"));
        QVERIFY(!links.contains(QStringLiteral("J%1").arg(firstRow)));
        const auto expectedLabel = QStringLiteral("枚举定义同 ") +
            (ambiguous ? reg.key() + ".CH08_RANGE" : QStringLiteral("CH08_RANGE"));
        for (int channel = 9; channel <= 15; ++channel) {
            const int row = reg.value().value(QStringLiteral("CH%1_RANGE").arg(channel, 2, 10, QLatin1Char('0')));
            QCOMPARE(book.read(row, 10).toString(), expectedLabel);
            QVERIFY(!book.cellAt(row, 10)->hasFormula());
            const auto address = QStringLiteral("J%1").arg(row);
            QVERIFY(links.contains(address));
            QVERIFY(!links.value(address).external);
            QCOMPARE(links.value(address).location, QStringLiteral("'%1'!$J$%2").arg(quotedSheet).arg(firstRow));
        }
    }

    {
        ProjectController controller;
        QVERIFY(controller.openProject(fixture.path));
        QVERIFY(!controller.hasProjectErrors());
        QVERIFY(!controller.hasConflicts());
        controller.generateNow();
        QVERIFY(!controller.hasProjectErrors());
        QCOMPARE(read(fixture.xlsx), cliWorkbook);
        QCOMPARE(read(fixture.header), header);
    }
    const auto restored = fixture.setMode("full");
    QVERIFY2(restored.code == 0, restored.bytes.constData());
    QCOMPARE(read(fixture.xlsx), fullWorkbook);
    QCOMPARE(read(fixture.header), header);
    QCOMPARE(cli({"get",fixture.path,"reg-0"}).root.value("result").toObject().value("fields"), originalFields);
}

void XlsxEnumLinksTests::laysOutLongContentThroughCliAndGui_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("full-enums") << QStringLiteral("full");
    QTest::newRow("consecutive-enum-links") << QStringLiteral("consecutive");
}

void XlsxEnumLinksTests::laysOutLongContentThroughCliAndGui()
{
    QFETCH(QString, mode);
    Fixture fixture(false, true);
    const auto generated = cli({"generate", fixture.path});
    QVERIFY2(generated.code == 0, generated.bytes.constData());
    const auto header = read(fixture.header);
    const auto fields = cli({"get",fixture.path,"reg-0"}).root.value("result").toObject().value("fields");
    const auto result = fixture.setMode(mode);
    QVERIFY2(result.code == 0, result.bytes.constData());
    const auto cliWorkbook = read(fixture.xlsx);
    QCOMPARE(read(fixture.header), header);
    QCOMPARE(cli({"get",fixture.path,"reg-0"}).root.value("result").toObject().value("fields"), fields);
    QXlsx::Document book(fixture.xlsx);
    QVERIFY(book.selectSheet(book.sheetNames().at(1)));
    auto* sheet = book.currentWorksheet();
    QVERIFY(sheet);
    QMap<QString, int> rows;
    for (int row = 1; row <= book.dimension().lastRow(); ++row) {
        const QString name = book.read(row, 3).toString();
        if (name.startsWith("CH")) rows.insert(name, row);
        QVERIFY2(book.rowHeight(row) <= 409.5, "Excel physical-row limit exceeded");
    }
    QCOMPARE(rows.size(), 8);
    const auto area = [&](int row, int column) {
        for (const auto& merged : sheet->mergedCells())
            if (merged.firstRow() == row && merged.firstColumn() == column) return merged;
        return QXlsx::CellRange(row, column, row, column);
    };
    const auto height = [&](int row, int column) {
        const auto range = area(row, column);
        double total = 0;
        for (int r = range.firstRow(); r <= range.lastRow(); ++r) total += book.rowHeight(r);
        return total;
    };
    for (int i = 0; i < 8; ++i) {
        const int row = rows.value(QStringLiteral("CH%1_RANGE").arg(i+8,2,10,QLatin1Char('0')));
        QString expected = layoutDescriptions().at(i);
        expected.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        expected.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        QCOMPARE(book.read(row, 11).toString(), expected);
        const auto format = book.cellAt(row, 11)->format();
        QCOMPARE(format.fontName(), QStringLiteral("Microsoft YaHei"));
        QCOMPARE(format.fontSize(), 10);
        QVERIFY(!format.fontBold());
        QVERIFY(format.textWrap());
        QVERIFY(!format.shrinkToFit());
        QCOMPARE(format.horizontalAlignment(), QXlsx::Format::AlignLeft);
        QCOMPARE(format.verticalAlignment(), QXlsx::Format::AlignTop);
        for (int r = area(row,11).firstRow(); r <= area(row,11).lastRow(); ++r)
            QVERIFY(sheet->isRowHidden(r));
    }
    const int first = rows.value("CH08_RANGE");
    QVERIFY(book.read(first,10).toString().contains("MODE_15_WITH_FULL_RANGE_CALIBRATION_AND_STATUS_CHECK=0xF"));
    QVERIFY(height(first,10) > 409.5);
    QVERIFY(area(first,10).lastRow() > first);
    const int last = rows.value("CH15_RANGE");
    QVERIFY(height(last,11) >= 51 * 10);
    QVERIFY(area(last,11).lastRow() > last);
    QVERIFY(book.columnWidth(11) > book.columnWidth(10));
    QVERIFY(book.columnWidth(11) > 2 * book.columnWidth(6));
    if (mode == "consecutive") {
        const auto links = hyperlinks(*sheet);
        QCOMPARE(links.size(), 7);
        QVERIFY(height(rows.value("CH09_RANGE"),11) > height(rows.value("CH14_RANGE"),11));
        QVERIFY(height(rows.value("CH11_RANGE"),11) >= 5 * 10);
        for (int channel = 9; channel <= 15; ++channel) {
            const int row = rows.value(QStringLiteral("CH%1_RANGE").arg(channel,2,10,QLatin1Char('0')));
            QCOMPARE(book.read(row,10).toString(), QStringLiteral("枚举定义同 CH08_RANGE"));
            QVERIFY(links.value(QStringLiteral("J%1").arg(row)).location.endsWith(QStringLiteral("!$J$%1").arg(first)));
        }
    }
    ProjectController controller;
    QVERIFY(controller.openProject(fixture.path));
    controller.generateNow();
    QVERIFY(!controller.hasProjectErrors());
    QCOMPARE(read(fixture.xlsx), cliWorkbook);
    QCOMPARE(read(fixture.header), header);
}

QTEST_MAIN(XlsxEnumLinksTests)
#include "xlsx_enum_links_tests.moc"
