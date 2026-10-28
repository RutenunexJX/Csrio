#include "regmap/core/xlsx_export.hpp"

#include "regmap/core/model_tokens.hpp"
#include "regmap/core/document_layout.hpp"

#include <xlsxcellrange.h>
#include <xlsxdocument.h>
#include <xlsxformat.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QTextLayout>
#include <QTextOption>
#include <QHashFunctions>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view exportFailureCode = "RM4100";
constexpr int worksheetColumnCount = 11;
constexpr int worksheetDescriptionColumn = worksheetColumnCount;
constexpr std::uint64_t registerAddressSlotBytes = 4;
constexpr std::uint32_t zipLocalHeaderSignature = 0x04034B50U;
constexpr std::uint32_t zipCentralHeaderSignature = 0x02014B50U;
constexpr std::uint32_t zipEndSignature = 0x06054B50U;
constexpr std::uint16_t canonicalZipTime = 0U;
constexpr std::uint16_t canonicalZipDate = 0x0021U;

struct WorkbookFormats {
    QXlsx::Format title;
    QXlsx::Format header;
    QXlsx::Format body;
    QXlsx::Format alternate;
    QXlsx::Format metadata;
    QXlsx::Format field;
    QXlsx::Format fieldAddress;
    QXlsx::Format address;
    QXlsx::Format addressAlternate;
    QXlsx::Format section;
    QXlsx::Format sectionAlternate;
    QXlsx::Format sectionAddress;
    QXlsx::Format sectionAddressAlternate;
    QXlsx::Format pageLabel;
    QXlsx::Format pageValue;
    QXlsx::Format pageAddress;
    QXlsx::Format block;
    QXlsx::Format reserved;
    QXlsx::Format reservedAddress;
};

[[nodiscard]] QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] bool hasBytes(
    const QByteArray& data,
    qsizetype offset,
    qsizetype count)
{
    return offset >= 0 &&
        count >= 0 &&
        offset <= data.size() - count;
}

[[nodiscard]] std::uint16_t readLittleEndian16(
    const QByteArray& data,
    qsizetype offset)
{
    const auto* bytes =
        reinterpret_cast<const unsigned char*>(
            data.constData() + offset);
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1])
         << 8U));
}

[[nodiscard]] std::uint32_t readLittleEndian32(
    const QByteArray& data,
    qsizetype offset)
{
    const auto* bytes =
        reinterpret_cast<const unsigned char*>(
            data.constData() + offset);
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1])
         << 8U) |
        (static_cast<std::uint32_t>(bytes[2])
         << 16U) |
        (static_cast<std::uint32_t>(bytes[3])
         << 24U));
}

void writeLittleEndian16(
    QByteArray& data,
    qsizetype offset,
    std::uint16_t value)
{
    auto* bytes =
        reinterpret_cast<unsigned char*>(
            data.data() + offset);
    bytes[0] =
        static_cast<unsigned char>(
            value & 0xFFU);
    bytes[1] =
        static_cast<unsigned char>(
            (value >> 8U) & 0xFFU);
}

[[nodiscard]] bool canonicalizeZipTimestamps(
    QByteArray& data)
{
    constexpr qsizetype endHeaderSize = 22;
    constexpr qsizetype maximumCommentSize = 65535;
    constexpr qsizetype centralHeaderSize = 46;
    constexpr qsizetype localHeaderSize = 30;
    if (data.size() < endHeaderSize) {
        return false;
    }

    const qsizetype minimumEndOffset =
        std::max<qsizetype>(
            0,
            data.size() -
                endHeaderSize -
                maximumCommentSize);
    qsizetype endOffset = -1;
    for (qsizetype offset =
             data.size() -
                 endHeaderSize;
         ;
         --offset) {
        if (readLittleEndian32(
                data,
                offset) ==
            zipEndSignature) {
            const qsizetype commentSize =
                static_cast<qsizetype>(
                    readLittleEndian16(
                        data,
                        offset + 20));
            if (offset +
                    endHeaderSize +
                    commentSize ==
                data.size()) {
                endOffset = offset;
                break;
            }
        }
        if (offset == minimumEndOffset) {
            break;
        }
    }
    if (endOffset < 0 ||
        readLittleEndian16(
            data,
            endOffset + 4) != 0U ||
        readLittleEndian16(
            data,
            endOffset + 6) != 0U) {
        return false;
    }

    const std::uint16_t entryCount =
        readLittleEndian16(
            data,
            endOffset + 10);
    if (entryCount ==
            std::numeric_limits<
                std::uint16_t>::max() ||
        readLittleEndian16(
            data,
            endOffset + 8) !=
            entryCount) {
        return false;
    }
    const qsizetype centralSize =
        static_cast<qsizetype>(
            readLittleEndian32(
                data,
                endOffset + 12));
    qsizetype centralOffset =
        static_cast<qsizetype>(
            readLittleEndian32(
                data,
                endOffset + 16));
    if (!hasBytes(
            data,
            centralOffset,
            centralSize) ||
        centralOffset +
                centralSize >
            endOffset) {
        return false;
    }

    const qsizetype centralEnd =
        centralOffset +
        centralSize;
    for (std::uint32_t index = 0;
         index <
         static_cast<std::uint32_t>(
             entryCount);
         ++index) {
        if (!hasBytes(
                data,
                centralOffset,
                centralHeaderSize) ||
            readLittleEndian32(
                data,
                centralOffset) !=
                zipCentralHeaderSignature) {
            return false;
        }
        const qsizetype fileNameSize =
            static_cast<qsizetype>(
                readLittleEndian16(
                    data,
                    centralOffset + 28));
        const qsizetype extraSize =
            static_cast<qsizetype>(
                readLittleEndian16(
                    data,
                    centralOffset + 30));
        const qsizetype commentSize =
            static_cast<qsizetype>(
                readLittleEndian16(
                    data,
                    centralOffset + 32));
        const qsizetype recordSize =
            centralHeaderSize +
            fileNameSize +
            extraSize +
            commentSize;
        if (!hasBytes(
                data,
                centralOffset,
                recordSize)) {
            return false;
        }

        const qsizetype localOffset =
            static_cast<qsizetype>(
                readLittleEndian32(
                    data,
                    centralOffset + 42));
        if (!hasBytes(
                data,
                localOffset,
                localHeaderSize) ||
            readLittleEndian32(
                data,
                localOffset) !=
                zipLocalHeaderSignature) {
            return false;
        }
        writeLittleEndian16(
            data,
            centralOffset + 12,
            canonicalZipTime);
        writeLittleEndian16(
            data,
            centralOffset + 14,
            canonicalZipDate);
        writeLittleEndian16(
            data,
            localOffset + 10,
            canonicalZipTime);
        writeLittleEndian16(
            data,
            localOffset + 12,
            canonicalZipDate);
        centralOffset +=
            recordSize;
    }
    return centralOffset == centralEnd;
}

[[nodiscard]] WorkbookFormats formats()
{
    WorkbookFormats result;

    result.title.setFontName(QStringLiteral("Microsoft YaHei"));
    result.title.setFontSize(16);
    result.title.setFontBold(true);
    result.title.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    result.title.setPatternForegroundColor(QColor(QStringLiteral("#17365D")));
    result.title.setPatternBackgroundColor(QColor(QStringLiteral("#17365D")));
    result.title.setFillPattern(QXlsx::Format::PatternSolid);
    result.title.setVerticalAlignment(QXlsx::Format::AlignTop);
    result.title.setTextWrap(true);

    result.header.setFontName(QStringLiteral("Microsoft YaHei"));
    result.header.setFontSize(10);
    result.header.setFontBold(true);
    result.header.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    result.header.setPatternForegroundColor(QColor(QStringLiteral("#385D8A")));
    result.header.setPatternBackgroundColor(QColor(QStringLiteral("#385D8A")));
    result.header.setFillPattern(QXlsx::Format::PatternSolid);
    result.header.setBorderStyle(QXlsx::Format::BorderThin);
    result.header.setBorderColor(QColor(QStringLiteral("#C6D2E1")));
    result.header.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    result.header.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.header.setTextWrap(true);
    result.header.setLocked(true);

    result.body.setFontName(QStringLiteral("Microsoft YaHei"));
    result.body.setFontSize(10);
    result.body.setFontColor(QColor(QStringLiteral("#243447")));
    result.body.setPatternForegroundColor(QColor(QStringLiteral("#FFF9E6")));
    result.body.setPatternBackgroundColor(QColor(QStringLiteral("#FFF9E6")));
    result.body.setFillPattern(QXlsx::Format::PatternSolid);
    result.body.setBorderStyle(QXlsx::Format::BorderThin);
    result.body.setBorderColor(QColor(QStringLiteral("#DCE3EB")));
    result.body.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    result.body.setTextWrap(true);
    result.body.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.body.setLocked(true);

    result.alternate = result.body;
    result.alternate.setPatternForegroundColor(QColor(QStringLiteral("#F4F7FB")));
    result.alternate.setPatternBackgroundColor(QColor(QStringLiteral("#F4F7FB")));

    result.metadata = result.body;
    result.metadata.setPatternForegroundColor(QColor(QStringLiteral("#E7ECF2")));
    result.metadata.setPatternBackgroundColor(QColor(QStringLiteral("#E7ECF2")));
    result.metadata.setFontColor(QColor(QStringLiteral("#5B6573")));

    result.field = result.body;
    result.field.setFontColor(QColor(QStringLiteral("#7F6000")));
    result.field.setPatternForegroundColor(QColor(QStringLiteral("#FFF2CC")));
    result.field.setPatternBackgroundColor(QColor(QStringLiteral("#FFF2CC")));
    result.field.setBorderColor(QColor(QStringLiteral("#D6B656")));
    result.field.setTextWrap(true);

    result.fieldAddress = result.field;
    result.fieldAddress.setFontName(QStringLiteral("Cascadia Mono"));
    result.fieldAddress.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.fieldAddress.setNumberFormat(QStringLiteral("@"));

    result.address = result.body;
    result.address.setFontName(QStringLiteral("Cascadia Mono"));
    result.address.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.address.setNumberFormat(QStringLiteral("@"));

    result.addressAlternate = result.alternate;
    result.addressAlternate.setFontName(QStringLiteral("Cascadia Mono"));
    result.addressAlternate.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.addressAlternate.setNumberFormat(QStringLiteral("@"));

    result.section = result.body;
    result.section.setFontColor(QColor(QStringLiteral("#17365D")));
    result.section.setPatternForegroundColor(QColor(QStringLiteral("#DCE6F1")));
    result.section.setPatternBackgroundColor(QColor(QStringLiteral("#DCE6F1")));
    result.section.setTextWrap(true);
    result.sectionAlternate = result.section;
    result.sectionAlternate.setPatternForegroundColor(QColor(QStringLiteral("#EDF2F7")));
    result.sectionAlternate.setPatternBackgroundColor(QColor(QStringLiteral("#EDF2F7")));

    result.sectionAddress = result.section;
    result.sectionAddress.setFontName(QStringLiteral("Cascadia Mono"));
    result.sectionAddress.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.sectionAddress.setNumberFormat(QStringLiteral("@"));

    result.sectionAddressAlternate = result.sectionAlternate;
    result.sectionAddressAlternate.setFontName(QStringLiteral("Cascadia Mono"));
    result.sectionAddressAlternate.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.sectionAddressAlternate.setNumberFormat(QStringLiteral("@"));

    result.pageLabel = result.metadata;
    result.pageLabel.setFontBold(true);
    result.pageLabel.setFontColor(QColor(QStringLiteral("#17365D")));

    result.pageValue = result.body;
    result.pageValue.setPatternForegroundColor(QColor(QStringLiteral("#FFFFFF")));
    result.pageValue.setPatternBackgroundColor(QColor(QStringLiteral("#FFFFFF")));

    result.pageAddress = result.pageValue;
    result.pageAddress.setFontName(QStringLiteral("Cascadia Mono"));
    result.pageAddress.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.pageAddress.setNumberFormat(QStringLiteral("@"));

    result.block = result.body;
    result.block.setFontColor(QColor(QStringLiteral("#FFFFFF")));
    result.block.setPatternForegroundColor(QColor(QStringLiteral("#4472C4")));
    result.block.setPatternBackgroundColor(QColor(QStringLiteral("#4472C4")));
    result.block.setBorderColor(QColor(QStringLiteral("#2F5597")));
    result.block.setHorizontalAlignment(QXlsx::Format::AlignLeft);
    result.block.setVerticalAlignment(QXlsx::Format::AlignTop);

    result.reserved = result.body;
    result.reserved.setFontColor(QColor(QStringLiteral("#9C0006")));
    result.reserved.setPatternForegroundColor(QColor(QStringLiteral("#E2E3E5")));
    result.reserved.setPatternBackgroundColor(QColor(QStringLiteral("#E2E3E5")));

    result.reservedAddress = result.reserved;
    result.reservedAddress.setFontName(QStringLiteral("Cascadia Mono"));
    result.reservedAddress.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    result.reservedAddress.setNumberFormat(QStringLiteral("@"));

    return result;
}

struct ExportCell {
    QVariant value;
    QXlsx::Format format;
    int columnSpan = 1;
};

[[nodiscard]] double textHeight(const QString& value, const QXlsx::Format& format,
                                double widthPixels)
{
    // Measure at Excel's 100% scale, independently of the desktop DPI setting.
    QImage device(1, 1, QImage::Format_ARGB32_Premultiplied);
    device.setDotsPerMeterX(3780);
    device.setDotsPerMeterY(3780);
    const QFont font = format.font();
    const QFontMetricsF metrics(font, &device);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QString normalized = value;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    double height = 0.0;
    for (const auto& paragraph : normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        if (paragraph.isEmpty()) {
            height += std::ceil(metrics.lineSpacing()) + 2.0;
            continue;
        }
        QTextLayout layout(paragraph, font, &device);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            // Cell margins and a small wrap tolerance cover Excel/Qt rounding differences.
            line.setLineWidth(std::max(1.0, widthPixels - 10.0));
            height += std::ceil(std::max(line.height(), metrics.lineSpacing())) + 2.0;
        }
        layout.endLayout();
    }
    return std::ceil((height + 8.0) * 72.0 / 96.0);
}

void writeMeasuredRow(QXlsx::Document& document, int& row,
                      const std::vector<ExportCell>& cells, double minimumHeight = 24.0)
{
    double height = minimumHeight;
    int column = 1;
    for (const auto& cell : cells) {
        double widthPixels = 0.0;
        for (int offset = 0; offset < cell.columnSpan; ++offset) {
            // QXlsx uses Calibri 11 for the workbook Normal style (7 px digit width).
            // Keep the five-pixel column padding as additional wrap tolerance.
            widthPixels += std::floor(document.columnWidth(column + offset) * 7.0);
        }
        if (!cell.value.toString().isEmpty())
            height = std::max(height, textHeight(cell.value.toString(), cell.format, widthPixels));
        column += cell.columnSpan;
    }
    // Excel limits a physical row to 409.5 pt. Preserve oversized cells as a
    // vertically merged area instead of clamping the height or splitting text.
    const int rowSpan = std::max(1, static_cast<int>(std::ceil(height / 400.0)));
    const int lastRow = row + rowSpan - 1;
    document.setRowHeight(row, lastRow, std::ceil(height / rowSpan));
    column = 1;
    for (const auto& cell : cells) {
        if (rowSpan > 1 || cell.columnSpan > 1)
            document.mergeCells(QXlsx::CellRange(row, column, lastRow,
                                column + cell.columnSpan - 1), cell.format);
        const bool emptyText = cell.value.metaType() == QMetaType::fromType<QString>() &&
            cell.value.toString().isEmpty();
        document.write(row, column, emptyText ? QVariant{} : cell.value, cell.format);
        column += cell.columnSpan;
    }
    row = lastRow;
}

void writeTitle(QXlsx::Document& document, int& row, const QString& title, int lastColumn,
                const WorkbookFormats& style)
{
    writeMeasuredRow(document, row, {{title, style.title, lastColumn}}, 27.0);
}

void writeHeaders(QXlsx::Document& document, int& row, const QStringList& headers,
                  const WorkbookFormats& style)
{
    std::vector<ExportCell> cells;
    for (const auto& header : headers) cells.push_back({header, style.header});
    writeMeasuredRow(document, row, cells, 30.0);
}

void writeRow(QXlsx::Document& document, int& row, const std::vector<QVariant>& values,
              const WorkbookFormats& style, const std::vector<int>& addressColumns = {},
              const QXlsx::Format* overrideFormat = nullptr, bool alternateRow = false,
              const QXlsx::Format* overrideAddressFormat = nullptr,
              const std::vector<int>& longTextColumns = {})
{
    std::vector<ExportCell> cells;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const int column = static_cast<int>(index) + 1;
        const bool address = std::ranges::find(addressColumns, column) != addressColumns.end();
        QXlsx::Format format =
            overrideFormat != nullptr
                ? (address && overrideAddressFormat != nullptr ? *overrideAddressFormat
                                                              : *overrideFormat)
                : (address ? (alternateRow ? style.addressAlternate : style.address)
                           : (alternateRow ? style.alternate : style.body));
        if (std::ranges::find(longTextColumns, column) != longTextColumns.end()) {
            format.setHorizontalAlignment(QXlsx::Format::AlignLeft);
            format.setVerticalAlignment(QXlsx::Format::AlignTop);
        }
        cells.push_back({values[index], format});
    }
    writeMeasuredRow(document, row, cells);
}

void configureColumns(QXlsx::Document& document, const std::vector<double>& widths)
{
    for (std::size_t index = 0; index < widths.size(); ++index) {
        document.setColumnWidth(static_cast<int>(index) + 1, widths[index]);
    }
}

[[nodiscard]] QString fixedHex(const UnsignedValue& value, std::uint32_t bitWidth)
{
    const int minimumDigits =
        std::max(1, static_cast<int>((static_cast<std::uint64_t>(bitWidth) + 3) / 4));
    const QString digits = text(value.toHexString(false)).toUpper();
    return QStringLiteral("0x") + digits.rightJustified(minimumDigits, QLatin1Char('0'));
}

[[nodiscard]] QString fixedHex(std::uint64_t value, std::uint32_t bitWidth)
{
    return fixedHex(UnsignedValue(value), bitWidth);
}

[[nodiscard]] QString accessText(AccessMode access)
{
    return text(toString(access)).toUpper();
}

[[nodiscard]] QString fieldAccessText(const Field& field)
{
    return accessText(field.softwareAccess);
}

[[nodiscard]] QString withoutBoundaryApostrophes(QString value)
{
    value = value.trimmed();
    while (value.startsWith(QLatin1Char('\''))) {
        value.remove(0, 1);
        value = value.trimmed();
    }
    while (value.endsWith(QLatin1Char('\''))) {
        value.chop(1);
        value = value.trimmed();
    }
    return value;
}

[[nodiscard]] QString sanitizedSheetName(const QString& requested, const QStringList& usedNames)
{
    QString base = withoutBoundaryApostrophes(requested);
    for (const QChar invalid : QStringLiteral("[]:*?/\\"))
        base.replace(invalid, QLatin1Char('_'));
    base = withoutBoundaryApostrophes(base.left(31));
    if (base.isEmpty())
        base = QStringLiteral("Page");

    auto alreadyUsed = [&usedNames](const QString& candidate) {
        return std::ranges::any_of(usedNames, [&candidate](const QString& used) {
            return used.compare(candidate, Qt::CaseInsensitive) == 0;
        });
    };
    QString candidate = base;
    for (int suffixNumber = 2; alreadyUsed(candidate); ++suffixNumber) {
        const QString suffix = QStringLiteral(" (%1)").arg(suffixNumber);
        candidate = base.left(31 - suffix.size()) + suffix;
    }
    return candidate;
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

[[nodiscard]] QXlsx::Format blockBandFormat(const WorkbookFormats& style, std::size_t blockIndex)
{
    static const std::vector<QColor> palette{
        QColor(QStringLiteral("#4472C4")), QColor(QStringLiteral("#548235")),
        QColor(QStringLiteral("#C55A11")), QColor(QStringLiteral("#7030A0")),
        QColor(QStringLiteral("#A64D79")), QColor(QStringLiteral("#5B6573")),
    };
    QXlsx::Format format = style.block;
    const QColor color = palette[blockIndex % palette.size()];
    format.setPatternForegroundColor(color);
    format.setPatternBackgroundColor(color);
    return format;
}

[[nodiscard]] QString blockBandText(const RegisterBlock& block, const AddressSpace& space)
{
    const QString size =
        block.size ? fixedHex(*block.size, space.addressWidth) : QString(QChar(0x2014));
    QString result =
        QStringLiteral("Block - %1    Base: %2    Size: %3")
            .arg(text(block.name), fixedHex(block.baseAddress, space.addressWidth), size);
    if (!block.description.empty())
        result += QStringLiteral("    %1").arg(text(block.description));
    return result;
}

[[nodiscard]] std::uint64_t saturatedAdd(
    std::uint64_t left,
    std::uint64_t right)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

[[nodiscard]] std::uint64_t inferredBlockExtent(
    const RegisterBlock& block)
{
    if (block.size && *block.size > 0) {
        return *block.size;
    }
    std::uint64_t extent = 0;
    for (const auto& reg : block.registers) {
        extent = std::max(
            extent,
            saturatedAdd(
                reg.offset,
                registerAddressSlotBytes));
    }
    return extent;
}

[[nodiscard]] std::uint64_t mappedPageSpan(
    const AddressSpace& space)
{
    std::uint64_t span = 0;
    for (const auto& block : space.blocks) {
        span = std::max(
            span,
            saturatedAdd(
                block.baseAddress,
                inferredBlockExtent(block)));
    }
    return span;
}

[[nodiscard]] QVariant overviewAddress(
    std::uint64_t value,
    std::uint32_t bitWidth)
{
    return fixedHex(value, bitWidth);
}

[[nodiscard]] bool writeOverviewSheet(
    QXlsx::Document& document,
    const Workspace& workspace,
    const WorkbookFormats& style)
{
    auto* worksheet = document.currentWorksheet();
    if (worksheet == nullptr) {
        return false;
    }
    worksheet->setGridLinesVisible(false);
    configureColumns(document, {24, 16, 14, 16, 14, 24, 16, 18, 18, 18, 12});
    int row = 1;
    writeTitle(
        document, row,
        QStringLiteral("Register Map Overview - ") +
            text(workspace.name),
        worksheetColumnCount,
        style);

    std::size_t blockCount = 0;
    std::size_t registerCount = 0;
    for (const auto& space : workspace.addressSpaces) {
        blockCount += space.blocks.size();
        for (const auto& block : space.blocks) {
            registerCount += block.registers.size();
        }
    }
    QXlsx::Format summary = style.pageValue;
    summary.setHorizontalAlignment(QXlsx::Format::AlignLeft);
    ++row;
    writeMeasuredRow(document, row,
        {{QStringLiteral("%1 Page(s) · %2 Block(s) · %3 Register(s)")
            .arg(workspace.addressSpaces.size()).arg(blockCount).arg(registerCount),
          summary, worksheetColumnCount}});
    document.setRowHeight(++row, 9.0);
    const int headerRow = ++row;
    writeHeaders(
        document,
        row,
        {QStringLiteral("Page"),
         QStringLiteral("Page Base"),
         QStringLiteral("Address Width"),
         QStringLiteral("Mapped Span"),
         QStringLiteral("Block Count"),
         QStringLiteral("Block"),
         QStringLiteral("Block Base"),
         QStringLiteral("Block Size"),
         QStringLiteral("Absolute Start"),
         QStringLiteral("Absolute End"),
         QStringLiteral("Registers")},
        style);

    worksheet->freezePanes(row, 0);
    ++row;
    for (std::size_t pageIndex = 0;
         pageIndex < workspace.addressSpaces.size();
         ++pageIndex) {
        const auto& space =
            workspace.addressSpaces[pageIndex];
        const bool alternate =
            (pageIndex % 2U) != 0U;
        const QXlsx::Format& rowFormat =
            alternate
                ? style.sectionAlternate
                : style.section;
        const QXlsx::Format& addressFormat =
            alternate
                ? style.sectionAddressAlternate
                : style.sectionAddress;
        const std::uint64_t pageSpan =
            mappedPageSpan(space);
        const auto writeOverviewRow =
            [&](const RegisterBlock* block) {
                const std::uint64_t blockExtent =
                    block == nullptr
                        ? 0
                        : inferredBlockExtent(*block);
                const std::uint64_t absoluteStart =
                    block == nullptr
                        ? space.baseAddress
                        : saturatedAdd(
                              space.baseAddress,
                              block->baseAddress);
                const std::uint64_t absoluteEnd =
                    block == nullptr ||
                            blockExtent == 0
                        ? absoluteStart
                        : saturatedAdd(
                              absoluteStart,
                              blockExtent - 1);
                writeRow(
                    document,
                    row,
                    {text(space.name),
                     overviewAddress(
                         space.baseAddress,
                         space.addressWidth),
                     QStringLiteral("%1 bits")
                         .arg(space.addressWidth),
                     overviewAddress(
                         pageSpan,
                         space.addressWidth),
                     static_cast<qulonglong>(
                         space.blocks.size()),
                     block == nullptr
                         ? QVariant{
                               QStringLiteral("Empty")}
                         : QVariant{text(block->name)},
                     block == nullptr
                         ? QVariant{}
                         : overviewAddress(
                               block->baseAddress,
                               space.addressWidth),
                     block == nullptr
                         ? QVariant{}
                         : (block->size
                                ? overviewAddress(
                                      *block->size,
                                      space.addressWidth)
                                : QVariant{
                                      QStringLiteral("Auto (%1)")
                                          .arg(fixedHex(
                                              blockExtent,
                                              space.addressWidth))}),
                     block == nullptr
                         ? QVariant{}
                         : overviewAddress(
                               absoluteStart,
                               space.addressWidth),
                     block == nullptr
                         ? QVariant{}
                         : overviewAddress(
                               absoluteEnd,
                               space.addressWidth),
                     static_cast<qulonglong>(
                         block == nullptr
                             ? 0
                             : block->registers.size())},
                    style,
                    {2, 4, 7, 8, 9, 10},
                    &rowFormat,
                    alternate,
                    &addressFormat);
                ++row;
            };
        if (space.blocks.empty()) {
            writeOverviewRow(nullptr);
        } else {
            for (const auto& block : space.blocks) {
                writeOverviewRow(&block);
            }
        }
    }

    const int lastRow = std::max(headerRow, row - 1);
    if (!worksheet->setAutoFilter(
            QXlsx::CellRange(
                headerRow, 1, lastRow,
                worksheetColumnCount))) {
        return false;
    }
    return true;
}

[[nodiscard]] QImage renderBitfieldDiagram(const Register& reg)
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
    image.fill(QColor(QStringLiteral("#FFF2CC")));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.setPen(QPen(QColor(QStringLiteral("#D6B656")), 1));
    painter.drawRect(QRect(0, 0, image.width() - 1, image.height() - 1));

    QFont titleFont(QStringLiteral("Microsoft YaHei"), 10);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(QStringLiteral("#7F6000")));
    painter.drawText(QRect(horizontalMargin, 6, imageWidth - horizontalMargin * 2, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 - %2-bit field layout").arg(text(reg.name)).arg(reg.width));

    QFont markerFont(QStringLiteral("Microsoft YaHei"), 8);
    const QRect barRect(horizontalMargin, barTop, imageWidth - horizontalMargin * 2, barHeight);
    painter.fillRect(barRect, QColor(QStringLiteral("#FFF9E6")));
    painter.fillRect(barRect, QBrush(QColor(QStringLiteral("#D6B656")), Qt::BDiagPattern));
    painter.setPen(QPen(QColor(QStringLiteral("#BF9000")), 1));
    painter.drawRect(barRect.adjusted(0, 0, -1, -1));

    const std::uint64_t registerWidth = std::max<std::uint64_t>(1, reg.width);
    QFont fieldFont(QStringLiteral("Microsoft YaHei"), 8);
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

        painter.setFont(markerFont);
        painter.setPen(QColor(QStringLiteral("#9C6500")));
        const QRect markerRect(fieldRect.left(), barRect.top() - 15, fieldRect.width(), 14);
        if (boundedMsb == boundedLsb) {
            painter.drawText(markerRect, Qt::AlignHCenter | Qt::AlignBottom,
                             QString::number(static_cast<qulonglong>(boundedMsb)));
        } else {
            const QRect paddedMarkerRect =
                fieldRect.width() > 8 ? markerRect.adjusted(2, 0, -2, 0) : markerRect;
            painter.drawText(paddedMarkerRect, Qt::AlignLeft | Qt::AlignBottom,
                             QString::number(static_cast<qulonglong>(boundedMsb)));
            painter.drawText(paddedMarkerRect, Qt::AlignRight | Qt::AlignBottom,
                             QString::number(static_cast<qulonglong>(boundedLsb)));
        }

        painter.fillRect(fieldRect, color);
        if (field.type == FieldType::reserved) {
            painter.fillRect(fieldRect,
                             QBrush(QColor(QStringLiteral("#737373")), Qt::BDiagPattern));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#FFFFFF")), 1));
        painter.drawRect(fieldRect.adjusted(0, 0, -1, -1));

        const QString name = text(field.name);
        painter.setFont(fieldFont);
        if (fieldRect.width() >= fieldMetrics.horizontalAdvance(name) + 10) {
            painter.setPen(field.type == FieldType::reserved ? QColor(QStringLiteral("#243447"))
                                                             : QColor(QStringLiteral("#FFFFFF")));
            painter.drawText(fieldRect.adjusted(4, 0, -4, 0), Qt::AlignCenter, name);
        }
    }

    QFont legendFont(QStringLiteral("Microsoft YaHei"), 8);
    painter.setFont(legendFont);
    const QFontMetrics legendMetrics(legendFont);
    painter.setPen(QColor(QStringLiteral("#594300")));
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
            painter.setPen(QColor(QStringLiteral("#594300")));
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

void writePageMetadata(QXlsx::Document& document, int& row, const AddressSpace& space,
                       const WorkbookFormats& style)
{
    QXlsx::Format descriptionFormat = style.pageValue;
    descriptionFormat.setHorizontalAlignment(QXlsx::Format::AlignLeft);
    descriptionFormat.setVerticalAlignment(QXlsx::Format::AlignTop);
    writeMeasuredRow(document, row,
        {{QStringLiteral("Page Base"), style.pageLabel},
         {fixedHex(space.baseAddress, space.addressWidth), style.pageAddress},
         {QStringLiteral("Address Width"), style.pageLabel},
         {QStringLiteral("%1 bits").arg(space.addressWidth), style.pageValue},
         {QStringLiteral("Description"), style.pageLabel},
         {text(space.description), descriptionFormat, 6}});
}

using FieldNameCounts = std::map<std::string, std::size_t, std::less<>>;

void countFieldNames(const std::vector<Field>& fields, FieldNameCounts& counts)
{
    for (const auto& field : fields) {
        ++counts[field.name];
        countFieldNames(field.members, counts);
    }
}

[[nodiscard]] bool writeFieldRows(QXlsx::Document& document, int& row, const std::vector<Field>& fields,
                    std::uint64_t parentLsb,
                    const std::optional<UnsignedValue>& registerReset,
                    std::uint64_t registerWidth,
                    const WorkbookFormats& style, bool consecutiveEnums,
                    std::string_view registerName, const FieldNameCounts& fieldNames)
{
    std::vector<const Field*> orderedFields;
    for (const auto& field : fields) orderedFields.push_back(&field);
    const auto references = consecutiveEnums ? consecutiveEnumReferences(orderedFields) : std::map<ObjectId, ObjectId>{};
    std::map<ObjectId, std::pair<int, const Field*>> firstRows;
    for (const Field* fieldPointer : orderedFields) {
        const auto& field = *fieldPointer;
        const int fieldRow = row;
        firstRows.emplace(field.id, std::pair{fieldRow, &field});
        QString enumText = fieldRangeText(field);
        int referenceRow = 0;
        if (const auto reference = references.find(field.id); reference != references.end()) {
            const auto& [firstRow, firstField] = firstRows.at(reference->second);
            referenceRow = firstRow;
            const QString name = fieldNames.at(firstField->name) > 1
                ? text(registerName) + QLatin1Char('.') + text(firstField->name)
                : text(firstField->name);
            enumText = QStringLiteral("枚举定义同 %1").arg(name);
        }
        const std::uint64_t absoluteLsb = parentLsb + field.lsb;
        const std::uint64_t absoluteMsb = parentLsb + field.msb;
        const QString bits = absoluteMsb == absoluteLsb
                                 ? QString::number(absoluteLsb)
                                 : QStringLiteral("%1:%2").arg(absoluteMsb).arg(absoluteLsb);
        const std::uint64_t fieldWidth = field.width();
        const bool resetRangeValid =
            registerReset.has_value() &&
            fieldWidth > 0 &&
            absoluteLsb <= registerWidth &&
            fieldWidth <= registerWidth - absoluteLsb;
        writeRow(document, row,
                 {QVariant{}, QVariant{}, text(field.name), fieldTypeText(field), bits,
                  fieldAccessText(field), QVariant{},
                  resetRangeValid
                      ? QVariant{fixedHex(
                            registerReset->slice(
                                absoluteLsb,
                                fieldWidth),
                            static_cast<std::uint32_t>(fieldWidth))}
                      : QVariant{},
                  QVariant{}, enumText, text(field.description)},
                 style, {8}, &style.field, false, &style.fieldAddress,
                 {10, worksheetDescriptionColumn});
        if (referenceRow != 0) {
            auto* worksheet = document.currentWorksheet();
            if (worksheet == nullptr) return false;
            QString sheetName = worksheet->sheetName();
            sheetName.replace(QLatin1Char('\''), QStringLiteral("''"));
            QUrl target;
            target.setFragment(QStringLiteral("'%1'!$J$%2").arg(sheetName).arg(referenceRow));
            QXlsx::Format linkFormat = style.field;
            linkFormat.setHorizontalAlignment(QXlsx::Format::AlignLeft);
            linkFormat.setVerticalAlignment(QXlsx::Format::AlignTop);
            linkFormat.setFontColor(QColor(QStringLiteral("#0563C1")));
            linkFormat.setFontUnderline(QXlsx::Format::FontUnderlineSingle);
            if (!worksheet->writeHyperlink(fieldRow, 10, target, linkFormat, enumText)) return false;
        }
        ++row;
        if (!writeFieldRows(
                document,
                row,
                field.members,
                absoluteLsb,
                registerReset,
                registerWidth,
                style, consecutiveEnums, registerName, fieldNames)) return false;
    }
    return true;
}

[[nodiscard]] bool writePageSheet(QXlsx::Document& document, const AddressSpace& space,
                                  const WorkbookFormats& style, bool consecutiveEnums)
{
    auto* worksheet = document.currentWorksheet();
    if (worksheet == nullptr) {
        return false;
    }
    worksheet->setSummaryRowsBelow(false);
    worksheet->setGridLinesVisible(false);
    configureColumns(document, {14, 14, 24, 11, 10, 10, 14, 14, 18, 38, 70});
    int row = 1;
    writeTitle(document, row, QStringLiteral("Page - ") + text(space.name), worksheetColumnCount, style);
    ++row;
    writePageMetadata(document, row, space, style);
    document.setRowHeight(++row, 9.0);
    ++row;
    writeHeaders(document, row,
                 {QStringLiteral("Address"), QStringLiteral("Offset"), QStringLiteral("Name"),
                  QStringLiteral("Type"), QStringLiteral("Width / Bits"), QStringLiteral("Access"),
                  QStringLiteral("Initial Value"), QStringLiteral("Reset Value"),
                  QStringLiteral("Tags"), QStringLiteral("Range"), QStringLiteral("Description")},
                 style);
    worksheet->freezePanes(row, 0);
    FieldNameCounts fieldNames;
    if (consecutiveEnums) {
        for (const auto& block : space.blocks)
            for (const auto& reg : block.registers)
                if (!reg.reserved && reg.type == FieldType::structure)
                    countFieldNames(reg.fields, fieldNames);
    }
    ++row;
    std::size_t registerIndex = 0;
    std::size_t blockIndex = 0;
    for (const auto& block : space.blocks) {
        const QXlsx::Format blockFormat = blockBandFormat(style, blockIndex++);
        writeMeasuredRow(document, row,
            {{blockBandText(block, space), blockFormat, worksheetColumnCount}}, 28.0);
        ++row;
        for (const Register* registerPointer : documentRegisters(block, true)) {
            const auto& reg = *registerPointer;
            const bool alternateGroup = (registerIndex++ % 2) != 0;
            const QXlsx::Format& sectionFormat =
                alternateGroup ? style.sectionAlternate : style.section;
            const QXlsx::Format& sectionAddressFormat =
                alternateGroup ? style.sectionAddressAlternate : style.sectionAddress;
            const QXlsx::Format* rowFormat = reg.reserved ? &style.reserved : &sectionFormat;
            const QXlsx::Format* addressFormat =
                reg.reserved ? &style.reservedAddress : &sectionAddressFormat;
            const QString registerName =
                reg.reserved ? QStringLiteral("[RESERVED] ") + text(reg.name) : text(reg.name);
            const QString registerType =
                reg.reserved ? QStringLiteral("reserved") : valueTypeText(reg.type, reg.width);
            writeRow(document, row,
                     {fixedHex(absoluteAddress(space, block, reg), space.addressWidth),
                      fixedHex(reg.offset, space.addressWidth), registerName, registerType,
                      reg.width, accessText(reg.access),
                      reg.initialValue ? fixedHex(*reg.initialValue, reg.width) : QVariant{},
                      reg.resetValue ? fixedHex(*reg.resetValue, reg.width) : QVariant{},
                      joinedTags(reg.tags), registerRangeText(reg), text(reg.description)},
                     style, {1, 2, 7, 8}, rowFormat, alternateGroup, addressFormat,
                     {10, worksheetDescriptionColumn});
            ++row;
            if (reg.reserved || reg.type != FieldType::structure)
                continue;
            const int firstDetailRow = row;
            const QImage diagram = renderBitfieldDiagram(reg);
            writeRow(document, row, std::vector<QVariant>(worksheetColumnCount), style, {},
                     &style.field, false, &style.fieldAddress);
            document.setRowHeight(row, static_cast<double>(diagram.height()) * 0.75);
            if (worksheet->insertImage(row - 1, 2, row, worksheetColumnCount, diagram) == 0)
                return false;
            ++row;
            if (!writeFieldRows(
                    document,
                    row,
                    reg.fields,
                    0,
                    reg.resetValue,
                    reg.width,
                    style, consecutiveEnums, reg.name, fieldNames)) return false;
            document.groupRows(firstDetailRow, row - 1, true);
        }
    }

    return true;
}

void writeEmptyWorkbook(QXlsx::Document& document, const WorkbookFormats& style)
{
    auto* worksheet = document.currentWorksheet();
    if (worksheet != nullptr)
        worksheet->setGridLinesVisible(false);
    configureColumns(document, {14, 14, 24, 11, 10, 10, 14, 14, 18, 38, 70});
    int row = 1;
    writeTitle(document, row, QStringLiteral("Page - Register Map"), worksheetColumnCount, style);
    ++row;
    writeMeasuredRow(document, row,
        {{QStringLiteral("No pages defined."), style.pageValue, worksheetColumnCount}});
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string message)
{
    Diagnostic diagnostic;
    diagnostic.code = exportFailureCode;
    diagnostic.message = std::move(message);
    diagnostics.push_back(std::move(diagnostic));
}

} // namespace

XlsxExportResult exportReadOnlyWorkbook(const Workspace& workspace, bool consecutiveEnums)
{
    XlsxExportResult result;
    QHashSeed::setDeterministicGlobalSeed();
    QXlsx::Document document;
    const auto style = formats();
    const QString firstSheet = document.sheetNames().value(0);
    const QString overviewSheetName =
        QStringLiteral("Overview");
    const bool overviewReady =
        firstSheet.isEmpty()
            ? document.addSheet(
                  overviewSheetName)
            : document.renameSheet(
                  firstSheet,
                  overviewSheetName);
    if (!overviewReady ||
        !document.selectSheet(
            overviewSheetName) ||
        !writeOverviewSheet(
            document,
            workspace,
            style)) {
        addDiagnostic(
            result.diagnostics,
            "Cannot initialize the generated workbook overview.");
        return result;
    }

    QStringList generatedSheetNames{
        overviewSheetName};
    if (workspace.addressSpaces.empty()) {
        const QString emptySheetName =
            QStringLiteral("Register Map");
        if (!document.addSheet(
                emptySheetName) ||
            !document.selectSheet(
                emptySheetName)) {
            addDiagnostic(
                result.diagnostics,
                "Cannot initialize the empty register-map worksheet.");
            return result;
        }
        writeEmptyWorkbook(document, style);
    } else {
        for (std::size_t index = 0; index < workspace.addressSpaces.size(); ++index) {
            const auto& space = workspace.addressSpaces[index];
            const QString sheetName = sanitizedSheetName(text(space.name), generatedSheetNames);
            const bool ready = document.addSheet(sheetName);
            if (!ready || !document.selectSheet(sheetName)) {
                addDiagnostic(result.diagnostics, "Cannot initialize a Page worksheet.");
                return result;
            }
            generatedSheetNames.push_back(sheetName);
            if (!writePageSheet(document, space, style, consecutiveEnums)) {
                addDiagnostic(result.diagnostics,
                              "Cannot draw or configure a generated Page worksheet.");
                return result;
            }
        }
    }

    document.selectSheet(overviewSheetName);
    document.setDocumentProperty(QStringLiteral("title"), text(workspace.name + " Register Map"));
    document.setDocumentProperty(
        QStringLiteral("description"),
        QStringLiteral("Generated by Csrio. Read-only derivative output."));
    const QString canonicalTimestamp =
        QStringLiteral(
            "2000-01-01T00:00:00Z");
    document.setDocumentProperty(
        QStringLiteral("created"),
        canonicalTimestamp);
    document.setDocumentProperty(
        QStringLiteral("modified"),
        canonicalTimestamp);

    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !document.saveAs(&buffer)) {
        addDiagnostic(result.diagnostics, "Cannot serialize the generated workbook.");
        return result;
    }
    buffer.close();
    if (!canonicalizeZipTimestamps(data)) {
        addDiagnostic(
            result.diagnostics,
            "Cannot normalize the generated workbook container.");
        return result;
    }
    result.bytes.assign(data.begin(), data.end());
    return result;
}

} // namespace regmap
