#include "regmap/core/xlsx_export.hpp"

#include "regmap/core/model_tokens.hpp"

#include <xlsxcellrange.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view exportFailureCode = "RM4100";

struct WorkbookFormats {
    QXlsx::Format title;
    QXlsx::Format header;
    QXlsx::Format body;
    QXlsx::Format alternate;
    QXlsx::Format metadata;
    QXlsx::Format address;
    QXlsx::Format addressAlternate;
    QXlsx::Format section;
    QXlsx::Format sectionAlternate;
};

[[nodiscard]] QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] WorkbookFormats formats()
{
    WorkbookFormats result;

    result.title.setFontName(QStringLiteral("Aptos Display"));
    result.title.setFontSize(16);
    result.title.setFontBold(true);
    result.title.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    result.title.setPatternForegroundColor(QColor(QStringLiteral("#17365D")));
    result.title.setPatternBackgroundColor(QColor(QStringLiteral("#17365D")));
    result.title.setFillPattern(QXlsx::Format::PatternSolid);
    result.title.setVerticalAlignment(QXlsx::Format::AlignVCenter);

    result.header.setFontName(QStringLiteral("Aptos"));
    result.header.setFontSize(10);
    result.header.setFontBold(true);
    result.header.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    result.header.setPatternForegroundColor(QColor(QStringLiteral("#385D8A")));
    result.header.setPatternBackgroundColor(QColor(QStringLiteral("#385D8A")));
    result.header.setFillPattern(QXlsx::Format::PatternSolid);
    result.header.setBorderStyle(QXlsx::Format::BorderThin);
    result.header.setBorderColor(QColor(QStringLiteral("#C6D2E1")));
    result.header.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    result.header.setTextWrap(true);
    result.header.setLocked(true);

    result.body.setFontName(QStringLiteral("Aptos"));
    result.body.setFontSize(10);
    result.body.setFontColor(QColor(QStringLiteral("#243447")));
    result.body.setPatternForegroundColor(QColor(QStringLiteral("#FFF9E6")));
    result.body.setPatternBackgroundColor(QColor(QStringLiteral("#FFF9E6")));
    result.body.setFillPattern(QXlsx::Format::PatternSolid);
    result.body.setBorderStyle(QXlsx::Format::BorderThin);
    result.body.setBorderColor(QColor(QStringLiteral("#DCE3EB")));
    result.body.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    result.body.setLocked(true);

    result.alternate = result.body;
    result.alternate.setPatternForegroundColor(QColor(QStringLiteral("#F4F7FB")));
    result.alternate.setPatternBackgroundColor(QColor(QStringLiteral("#F4F7FB")));

    result.metadata = result.body;
    result.metadata.setPatternForegroundColor(QColor(QStringLiteral("#E7ECF2")));
    result.metadata.setPatternBackgroundColor(QColor(QStringLiteral("#E7ECF2")));
    result.metadata.setFontColor(QColor(QStringLiteral("#5B6573")));

    result.address = result.body;
    result.address.setFontName(QStringLiteral("Cascadia Mono"));
    result.address.setHorizontalAlignment(QXlsx::Format::AlignRight);

    result.addressAlternate = result.alternate;
    result.addressAlternate.setFontName(QStringLiteral("Cascadia Mono"));
    result.addressAlternate.setHorizontalAlignment(QXlsx::Format::AlignRight);

    result.section = result.body;
    result.section.setFontBold(true);
    result.section.setFontColor(QColor(QStringLiteral("#17365D")));
    result.section.setPatternForegroundColor(QColor(QStringLiteral("#DCE6F1")));
    result.section.setPatternBackgroundColor(QColor(QStringLiteral("#DCE6F1")));
    result.section.setTextWrap(true);
    result.sectionAlternate = result.section;
    result.sectionAlternate.setPatternForegroundColor(QColor(QStringLiteral("#EDF2F7")));
    result.sectionAlternate.setPatternBackgroundColor(QColor(QStringLiteral("#EDF2F7")));

    return result;
}

void writeTitle(QXlsx::Document& document, const QString& title, int lastColumn,
                const WorkbookFormats& style)
{
    document.mergeCells(QXlsx::CellRange(1, 1, 1, lastColumn), style.title);
    document.write(1, 1, title, style.title);
    document.setRowHeight(1, 27.0);
}

void writeHeaders(QXlsx::Document& document, const QStringList& headers,
                  const WorkbookFormats& style)
{
    for (int column = 0; column < headers.size(); ++column) {
        document.write(3, column + 1, headers[column], style.header);
    }
    document.setRowHeight(3, 30.0);
}

void writeRow(QXlsx::Document& document, int row, const std::vector<QVariant>& values,
              const WorkbookFormats& style, const std::vector<int>& addressColumns = {},
              const QXlsx::Format* overrideFormat = nullptr, bool alternateRow = false)
{
    for (std::size_t index = 0; index < values.size(); ++index) {
        const int column = static_cast<int>(index) + 1;
        const bool address = std::ranges::find(addressColumns, column) != addressColumns.end();
        const QXlsx::Format& format =
            overrideFormat != nullptr
                ? *overrideFormat
                : (address ? (alternateRow ? style.addressAlternate : style.address)
                           : (alternateRow ? style.alternate : style.body));
        const QVariant& value = values[index];
        const bool emptyText =
            value.metaType() == QMetaType::fromType<QString>() && value.toString().isEmpty();
        document.write(row, column, emptyText ? QVariant{} : value, format);
    }
}

void configureColumns(QXlsx::Document& document, const std::vector<double>& widths)
{
    for (std::size_t index = 0; index < widths.size(); ++index) {
        document.setColumnWidth(static_cast<int>(index) + 1, widths[index]);
    }
}

[[nodiscard]] std::uint64_t absoluteAddress(const AddressSpace& space, const RegisterBlock& block,
                                            const Register& reg)
{
    return space.baseAddress + block.baseAddress + reg.offset;
}

[[nodiscard]] QString joinedTags(const std::vector<std::string>& tags)
{
    QStringList result;
    for (const auto& tag : tags) {
        result.push_back(text(tag));
    }
    return result.join(QStringLiteral(", "));
}

[[nodiscard]] QString valueTypeText(FieldType type, std::uint64_t width)
{
    switch (type) {
    case FieldType::bits:
        return QStringLiteral("bits");
    case FieldType::boolean:
        return QStringLiteral("bool");
    case FieldType::unsignedInteger:
        return QStringLiteral("uint%1").arg(width);
    case FieldType::signedInteger:
        return QStringLiteral("int%1").arg(width);
    case FieldType::enumeration:
        return QStringLiteral("enum");
    case FieldType::structure:
        return QStringLiteral("field");
    case FieldType::reserved:
        return QStringLiteral("reserved");
    }
    return QStringLiteral("bits");
}

[[nodiscard]] QString fieldTypeText(const Field& field)
{
    return valueTypeText(field.type, field.width());
}

[[nodiscard]] QString rangeText(const std::optional<std::string>& minimum,
                                const std::optional<std::string>& maximum)
{
    if (!minimum && !maximum) {
        return {};
    }
    return QStringLiteral("%1 .. %2")
        .arg(minimum ? text(*minimum) : QStringLiteral("—"),
             maximum ? text(*maximum) : QStringLiteral("—"));
}

[[nodiscard]] QString enumSummary(FieldType type, const std::vector<EnumValue>& enumValues)
{
    QStringList values;
    if (type == FieldType::boolean && enumValues.empty()) {
        values << QStringLiteral("FALSE=0") << QStringLiteral("TRUE=1");
    } else {
        for (const auto& value : enumValues) {
            values << QStringLiteral("%1=%2").arg(text(value.name),
                                                  text(value.value.toHexString()));
        }
    }
    return values.join(QStringLiteral("; "));
}

[[nodiscard]] QString registerRangeText(const Register& reg)
{
    return reg.minimumValue || reg.maximumValue ? rangeText(reg.minimumValue, reg.maximumValue)
                                                : enumSummary(reg.type, reg.enumValues);
}

[[nodiscard]] QString fieldRangeText(const Field& field)
{
    return field.minimumValue || field.maximumValue
               ? rangeText(field.minimumValue, field.maximumValue)
               : enumSummary(field.type, field.enumValues);
}

[[nodiscard]] QString bitRangeText(const Field& field)
{
    if (field.msb == field.lsb) {
        return QString::number(field.lsb);
    }
    return QStringLiteral("%1:%2").arg(field.msb).arg(field.lsb);
}

[[nodiscard]] QColor bitfieldColor(const Field& field, std::size_t index)
{
    if (field.type == FieldType::reserved) {
        return QColor(QStringLiteral("#A6A6A6"));
    }
    static const std::vector<QColor> palette{
        QColor(QStringLiteral("#4472C4")), QColor(QStringLiteral("#548235")),
        QColor(QStringLiteral("#C55A11")), QColor(QStringLiteral("#7030A0")),
        QColor(QStringLiteral("#BF9000")), QColor(QStringLiteral("#008C95")),
        QColor(QStringLiteral("#A64D79")), QColor(QStringLiteral("#5B6573")),
    };
    return palette[index % palette.size()];
}

[[nodiscard]] QImage renderBitfieldDiagram(const Register& reg, bool alternateGroup)
{
    constexpr int imageWidth = 1240;
    constexpr int horizontalMargin = 18;
    constexpr int barTop = 42;
    constexpr int barHeight = 38;
    constexpr int legendTop = 94;
    constexpr int legendColumns = 3;
    constexpr int legendRowHeight = 22;
    constexpr std::size_t maximumLegendFields = 30;

    std::vector<const Field*> fields;
    fields.reserve(reg.fields.size());
    for (const auto& field : reg.fields) {
        fields.push_back(&field);
    }
    std::sort(fields.begin(), fields.end(), [](const Field* left, const Field* right) {
        if (left->msb != right->msb) {
            return left->msb > right->msb;
        }
        if (left->lsb != right->lsb) {
            return left->lsb > right->lsb;
        }
        return left->name < right->name;
    });

    const std::size_t legendFieldCount = std::min(fields.size(), maximumLegendFields);
    const int legendRows = static_cast<int>((legendFieldCount + legendColumns - 1) / legendColumns);
    const bool hasOverflow = fields.size() > maximumLegendFields;
    const int imageHeight = std::max(122, legendTop + std::max(1, legendRows) * legendRowHeight +
                                              (hasOverflow ? legendRowHeight : 0) + 8);

    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.setDotsPerMeterX(3780);
    image.setDotsPerMeterY(3780);
    image.fill(QColor(alternateGroup ? QStringLiteral("#F4F7FB")
                                     : QStringLiteral("#FFF9E6")));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.setPen(QPen(QColor(QStringLiteral("#DCE3EB")), 1));
    painter.drawRect(QRect(0, 0, image.width() - 1, image.height() - 1));

    QFont titleFont(QStringLiteral("Aptos"), 10);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#17365D")));
    painter.drawText(QRect(horizontalMargin, 6, imageWidth - horizontalMargin * 2, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 · %2-bit field layout").arg(text(reg.name)).arg(reg.width));

    QFont markerFont(QStringLiteral("Aptos"), 8);
    painter.setFont(markerFont);
    painter.setPen(QColor(QStringLiteral("#5B6573")));
    painter.drawText(QRect(horizontalMargin, 25, 180, 15), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("MSB %1").arg(reg.width > 0 ? reg.width - 1 : 0));
    painter.drawText(QRect(imageWidth - horizontalMargin - 180, 25, 180, 15),
                     Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("LSB 0"));

    const QRect barRect(horizontalMargin, barTop, imageWidth - horizontalMargin * 2, barHeight);
    painter.fillRect(barRect, QColor(QStringLiteral("#E7ECF2")));
    painter.fillRect(barRect, QBrush(QColor(QStringLiteral("#C6D2E1")), Qt::BDiagPattern));
    painter.setPen(QPen(QColor(QStringLiteral("#17365D")), 1));
    painter.drawRect(barRect.adjusted(0, 0, -1, -1));

    const std::uint64_t registerWidth = std::max<std::uint64_t>(1, reg.width);
    QFont fieldFont(QStringLiteral("Aptos"), 8);
    fieldFont.setBold(true);
    painter.setFont(fieldFont);
    const QFontMetrics fieldMetrics(fieldFont);

    for (std::size_t index = 0; index < fields.size(); ++index) {
        const Field& field = *fields[index];
        const std::uint64_t boundedMsb = std::min<std::uint64_t>(field.msb, registerWidth - 1);
        const std::uint64_t boundedLsb = std::min<std::uint64_t>(field.lsb, boundedMsb);
        const long double leftRatio = static_cast<long double>(registerWidth - 1 - boundedMsb) /
                                      static_cast<long double>(registerWidth);
        const long double rightRatio = static_cast<long double>(registerWidth - boundedLsb) /
                                       static_cast<long double>(registerWidth);
        const int left = barRect.left() + static_cast<int>(std::floor(leftRatio * barRect.width()));
        const int right =
            barRect.left() + static_cast<int>(std::ceil(rightRatio * barRect.width()));
        const QRect fieldRect(left, barRect.top(), std::max(1, right - left), barRect.height());
        const QColor color = bitfieldColor(field, index);
        painter.fillRect(fieldRect, color);
        if (field.type == FieldType::reserved) {
            painter.fillRect(fieldRect,
                             QBrush(QColor(QStringLiteral("#737373")), Qt::BDiagPattern));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#FFFFFF")), 1));
        painter.drawRect(fieldRect.adjusted(0, 0, -1, -1));

        const QString name = text(field.name);
        if (fieldRect.width() >= fieldMetrics.horizontalAdvance(name) + 10) {
            painter.setPen(field.type == FieldType::reserved ? QColor(QStringLiteral("#243447"))
                                                             : QColor(QStringLiteral("#FFFFFF")));
            painter.drawText(fieldRect.adjusted(4, 0, -4, 0), Qt::AlignCenter, name);
        }
    }

    QFont legendFont(QStringLiteral("Aptos"), 8);
    painter.setFont(legendFont);
    const QFontMetrics legendMetrics(legendFont);
    painter.setPen(QColor(QStringLiteral("#243447")));
    const int legendColumnWidth = (imageWidth - horizontalMargin * 2) / legendColumns;
    if (fields.empty()) {
        painter.drawText(QRect(horizontalMargin, legendTop, imageWidth - horizontalMargin * 2, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("No fields defined; all bits are unassigned."));
    } else {
        for (std::size_t index = 0; index < legendFieldCount; ++index) {
            const int column = static_cast<int>(index % legendColumns);
            const int legendRow = static_cast<int>(index / legendColumns);
            const int x = horizontalMargin + column * legendColumnWidth;
            const int y = legendTop + legendRow * legendRowHeight;
            const QColor color = bitfieldColor(*fields[index], index);
            painter.fillRect(QRect(x, y + 3, 13, 13), color);
            painter.setPen(QPen(QColor(QStringLiteral("#FFFFFF")), 1));
            painter.drawRect(QRect(x, y + 3, 13, 13).adjusted(0, 0, -1, -1));
            painter.setPen(QColor(QStringLiteral("#243447")));
            const QString label = QStringLiteral("%1 [%2]").arg(text(fields[index]->name),
                                                                bitRangeText(*fields[index]));
            painter.drawText(
                QRect(x + 19, y, legendColumnWidth - 24, 19), Qt::AlignLeft | Qt::AlignVCenter,
                legendMetrics.elidedText(label, Qt::ElideRight, legendColumnWidth - 24));
        }
        if (hasOverflow) {
            painter.drawText(QRect(horizontalMargin, legendTop + legendRows * legendRowHeight,
                                   imageWidth - horizontalMargin * 2, 19),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("+ %1 additional fields; see the rows below.")
                                 .arg(fields.size() - maximumLegendFields));
        }
    }

    painter.end();
    return image;
}

void writeRegistersSheet(QXlsx::Document& document, const Workspace& workspace,
                         const WorkbookFormats& style)
{
    writeTitle(document, text(workspace.name + " — Registers"), 18, style);
    writeHeaders(document,
                 {QStringLiteral("Page"), QStringLiteral("Page Base"), QStringLiteral("Block"),
                  QStringLiteral("Block Base"), QStringLiteral("Register"),
                  QStringLiteral("Absolute Address"), QStringLiteral("Offset"),
                  QStringLiteral("Width"), QStringLiteral("Type"), QStringLiteral("Range"),
                  QStringLiteral("Initial"), QStringLiteral("Reset"), QStringLiteral("Access"),
                  QStringLiteral("State"), QStringLiteral("Tags"), QStringLiteral("Description"),
                  QStringLiteral("Register ID"), QStringLiteral("Block ID")},
                 style);
    int row = 4;
    std::size_t registerIndex = 0;
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            for (const auto& reg : block.registers) {
                const bool alternateRow = (registerIndex++ % 2) != 0;
                writeRow(document, row++,
                         {text(space.name), text(UnsignedValue(space.baseAddress).toHexString()),
                          text(block.name), text(UnsignedValue(block.baseAddress).toHexString()),
                          text(reg.name),
                          text(UnsignedValue(absoluteAddress(space, block, reg)).toHexString()),
                          text(UnsignedValue(reg.offset).toHexString()), reg.width,
                          valueTypeText(reg.type, reg.width), registerRangeText(reg),
                          reg.initialValue ? text(reg.initialValue->toHexString()) : QVariant{},
                          reg.resetValue ? text(reg.resetValue->toHexString()) : QVariant{},
                          text(toString(reg.access)),
                          reg.reserved ? QStringLiteral("reserved") : QStringLiteral("active"),
                          joinedTags(reg.tags), text(reg.description), text(reg.id),
                          text(block.id)},
                         style, {2, 4, 6, 7, 11, 12}, nullptr, alternateRow);
            }
        }
    }
    configureColumns(document,
                     {18, 15, 18, 15, 24, 18, 14, 10, 13, 18, 14, 14, 10, 12, 24, 38, 28, 28});
    document.setColumnHidden(17, 18, true);
}

void writeMapFieldRows(QXlsx::Document& document, int& row, const std::vector<Field>& fields,
                       std::uint64_t parentLsb, int depth, const WorkbookFormats& style,
                       bool alternateGroup)
{
    for (const auto& field : fields) {
        const std::uint64_t absoluteLsb = parentLsb + field.lsb;
        const std::uint64_t absoluteMsb = parentLsb + field.msb;
        const QString bits = absoluteMsb == absoluteLsb
                                 ? QString::number(absoluteLsb)
                                 : QStringLiteral("%1:%2").arg(absoluteMsb).arg(absoluteLsb);
        const QString prefix = QString(depth * 2, QLatin1Char(' ')) + QStringLiteral("↳ ");
        writeRow(document, row,
                 {QVariant{}, QVariant{}, QVariant{}, prefix + text(field.name),
                  field.type == FieldType::structure ? QStringLiteral("Compound Field")
                                                     : QStringLiteral("Field"),
                  bits, fieldTypeText(field), text(toString(field.softwareAccess)),
                  text(toString(field.hardwareAccess)), QVariant{},
                  field.resetValue ? text(field.resetValue->toHexString()) : QVariant{},
                  text(field.resetDomain), fieldRangeText(field), text(field.description)},
                 style, {11}, nullptr, alternateGroup);
        document.setRowHeight(row, 22.0);
        ++row;
        writeMapFieldRows(document, row, field.members, absoluteLsb, depth + 1, style,
                          alternateGroup);
    }
}

[[nodiscard]] bool writeMapSheet(QXlsx::Document& document, const Workspace& workspace,
                                 const WorkbookFormats& style)
{
    auto* worksheet = document.currentWorksheet();
    if (worksheet == nullptr) {
        return false;
    }
    worksheet->setSummaryRowsBelow(false);
    writeTitle(document, text(workspace.name + " — Register Map"), 14, style);
    writeHeaders(document,
                 {QStringLiteral("Address"), QStringLiteral("Page"), QStringLiteral("Block"),
                  QStringLiteral("Register / Field"), QStringLiteral("Kind"),
                  QStringLiteral("Bits"), QStringLiteral("Type"), QStringLiteral("SW Access"),
                  QStringLiteral("HW Access"), QStringLiteral("Initial"), QStringLiteral("Reset"),
                  QStringLiteral("Reset Domain"), QStringLiteral("Tags / Range / Enum"),
                  QStringLiteral("Description")},
                 style);
    int row = 4;
    std::size_t registerIndex = 0;
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            for (const auto& reg : block.registers) {
                const bool alternateGroup = (registerIndex++ % 2) != 0;
                QString metadata = joinedTags(reg.tags);
                const QString range = registerRangeText(reg);
                if (!range.isEmpty()) {
                    if (!metadata.isEmpty()) {
                        metadata += QStringLiteral(" | ");
                    }
                    metadata += range;
                }
                const QXlsx::Format& sectionFormat =
                    alternateGroup ? style.sectionAlternate : style.section;
                writeRow(document, row,
                         {text(UnsignedValue(absoluteAddress(space, block, reg)).toHexString()),
                          text(space.name), text(block.name), text(reg.name),
                          reg.reserved ? QStringLiteral("Reserved") : QStringLiteral("Register"),
                          reg.width, valueTypeText(reg.type, reg.width), text(toString(reg.access)),
                          QVariant{},
                          reg.initialValue ? text(reg.initialValue->toHexString()) : QVariant{},
                          reg.resetValue ? text(reg.resetValue->toHexString()) : QVariant{},
                          QVariant{}, metadata, text(reg.description)},
                         style, {}, &sectionFormat);
                document.setRowHeight(row, 24.0);
                ++row;
                if (reg.type != FieldType::structure) {
                    continue;
                }
                const int firstDetailRow = row;
                const QImage diagram = renderBitfieldDiagram(reg, alternateGroup);
                writeRow(document, row, std::vector<QVariant>(14), style, {}, nullptr,
                         alternateGroup);
                document.setRowHeight(row, static_cast<double>(diagram.height()) * 0.75);
                if (worksheet->insertImage(row - 1, 3, row, 14, diagram) == 0) {
                    return false;
                }
                ++row;
                writeMapFieldRows(document, row, reg.fields, 0, 1, style, alternateGroup);
                document.groupRows(firstDetailRow, row - 1, true);
            }
        }
    }
    configureColumns(document, {16, 16, 18, 28, 15, 12, 14, 11, 11, 14, 14, 18, 30, 44});
    return true;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string message)
{
    Diagnostic diagnostic;
    diagnostic.code = exportFailureCode;
    diagnostic.message = std::move(message);
    diagnostics.push_back(std::move(diagnostic));
}

} // namespace

XlsxExportResult exportReadOnlyWorkbook(const Workspace& workspace)
{
    XlsxExportResult result;
    QXlsx::Document document;
    const auto style = formats();
    const QString firstSheet = document.sheetNames().value(0);
    const bool registersReady = firstSheet.isEmpty()
                                    ? document.addSheet(QStringLiteral("Registers"))
                                    : document.renameSheet(firstSheet, QStringLiteral("Registers"));
    if (!registersReady) {
        addDiagnostic(result.diagnostics, "Cannot initialize the Registers worksheet.");
        return result;
    }
    if (!document.addSheet(QStringLiteral("Register Map"))) {
        addDiagnostic(result.diagnostics, "Cannot initialize the generated workbook sheets.");
        return result;
    }

    document.selectSheet(QStringLiteral("Registers"));
    writeRegistersSheet(document, workspace, style);
    document.selectSheet(QStringLiteral("Register Map"));
    if (!writeMapSheet(document, workspace, style)) {
        addDiagnostic(result.diagnostics, "Cannot draw the generated register bitfield diagrams.");
        return result;
    }
    document.selectSheet(QStringLiteral("Register Map"));
    document.setDocumentProperty(QStringLiteral("title"), text(workspace.name + " Register Map"));
    document.setDocumentProperty(
        QStringLiteral("comments"),
        QStringLiteral("Generated by Register Map Workbench. Read-only derivative output."));

    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !document.saveAs(&buffer)) {
        addDiagnostic(result.diagnostics, "Cannot serialize the generated workbook.");
        return result;
    }
    result.bytes.assign(data.begin(), data.end());
    return result;
}

} // namespace regmap
