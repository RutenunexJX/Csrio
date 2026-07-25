#include "regmap/core/rtl_sync.hpp"

#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/unsigned_value.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view openFailureCode = "RM5000";
constexpr std::string_view markerCode = "RM5001";
constexpr std::string_view objectCode = "RM5002";
constexpr std::string_view valueCode = "RM5003";
constexpr std::string_view writeCode = "RM5004";
constexpr std::string_view beginMarker = "// RMW:BEGIN schema=1";
constexpr std::string_view endMarker = "// RMW:END";

struct SourceLines {
    std::map<ObjectId, std::uint32_t, std::less<>> objects;
    std::map<std::pair<ObjectId, std::string>, std::uint32_t> properties;
};

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string_view code, std::string message,
                   const std::filesystem::path& path,
                   std::optional<std::uint32_t> line = std::nullopt, ObjectId objectId = {})
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.objectId = std::move(objectId);
    diagnostic.source.workbook = path;
    diagnostic.source.sheet = "Managed RTL";
    diagnostic.source.row = line;
    if (line) {
        diagnostic.source.cell = "line " + std::to_string(*line);
    }
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::string trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

[[nodiscard]] std::optional<UnsignedValue> parseSystemVerilogUnsigned(std::string_view text)
{
    std::string value = trim(text);
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    const auto apostrophe = value.find('\'');
    if (apostrophe == std::string::npos) {
        return UnsignedValue::parse(value);
    }
    if (apostrophe + 2 > value.size()) {
        return std::nullopt;
    }
    std::size_t baseIndex = apostrophe + 1;
    if (value[baseIndex] == 's' || value[baseIndex] == 'S') {
        ++baseIndex;
    }
    if (baseIndex >= value.size()) {
        return std::nullopt;
    }
    const char base = static_cast<char>(std::tolower(static_cast<unsigned char>(value[baseIndex])));
    const std::string digits = value.substr(baseIndex + 1);
    if (digits.empty() || std::ranges::any_of(digits, [](char character) {
            return character == 'x' || character == 'X' || character == 'z' || character == 'Z' ||
                   character == '?';
        })) {
        return std::nullopt;
    }
    switch (base) {
    case 'h':
        return UnsignedValue::parse("0x" + digits);
    case 'd':
        return UnsignedValue::parse(digits);
    case 'b':
        return UnsignedValue::parse("0b" + digits);
    case 'o':
        return UnsignedValue::parse("0o" + digits);
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::string compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}

[[nodiscard]] std::uint64_t fnv1a(std::string_view value)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string identifier(std::string_view id, std::string_view property)
{
    std::string result = "RMW_";
    for (const char rawCharacter : id) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        result.push_back(std::isalnum(character) != 0 ? static_cast<char>(std::toupper(character))
                                                      : '_');
    }
    std::ostringstream suffix;
    suffix << '_' << std::hex << std::uppercase << std::setfill('0') << std::setw(8)
           << static_cast<std::uint32_t>(fnv1a(id)) << '_';
    result += suffix.str();
    for (const char rawCharacter : property) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        result.push_back(std::isalnum(character) != 0 ? static_cast<char>(std::toupper(character))
                                                      : '_');
    }
    return result;
}

[[nodiscard]] bool isIntegerProperty(std::string_view property)
{
    return property == "address_width" || property == "width" || property == "array_count" ||
           property == "msb" || property == "lsb" || property == "order";
}

[[nodiscard]] bool isNumericProperty(std::string_view kind, std::string_view property)
{
    if (property == "order") {
        return false;
    }
    if (kind == "address-space") {
        return property == "base" || property == "address_width";
    }
    if (kind == "block") {
        return property == "base" || property == "size";
    }
    if (kind == "register") {
        return property == "offset" || property == "width" || property == "array_count" ||
               property == "stride" || property == "initial" || property == "reset";
    }
    if (kind == "field") {
        return property == "msb" || property == "lsb" || property == "reset";
    }
    return kind == "enum" && property == "value";
}

[[nodiscard]] std::string valueDeclaration(const std::string& id, const std::string& propertyName,
                                           const std::string& value, std::size_t semanticWidth)
{
    QJsonObject marker;
    marker.insert(QStringLiteral("id"), QString::fromStdString(id));
    marker.insert(QStringLiteral("property"), QString::fromStdString(propertyName));
    std::ostringstream output;
    if (isIntegerProperty(propertyName)) {
        output << "  localparam int unsigned " << identifier(id, propertyName) << " = " << value;
    } else {
        const auto parsed = UnsignedValue::parse(value).value_or(UnsignedValue{});
        const std::size_t width = std::max({std::size_t{1}, semanticWidth, parsed.bitWidth()});
        output << "  localparam logic [" << (width - 1) << ":0] " << identifier(id, propertyName)
               << " = " << width << "'h" << parsed.toHexString(false);
    }
    output << "; // RMW:VALUE " << compactJson(marker) << '\n';
    return output.str();
}

[[nodiscard]] std::size_t numericProperty(const QJsonObject& properties, std::string_view name,
                                          std::size_t fallback = 1)
{
    const auto parsed = UnsignedValue::parse(
        properties.value(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())))
            .toString()
            .toStdString());
    const auto value = parsed ? parsed->toUInt64() : std::nullopt;
    return value ? static_cast<std::size_t>(*value) : fallback;
}

[[nodiscard]] std::size_t
semanticValueWidth(std::string_view kind, std::string_view propertyName,
                   const QJsonObject& properties,
                   const std::map<ObjectId, QJsonObject, std::less<>>& objects)
{
    if (propertyName == "base" || propertyName == "size" || propertyName == "offset" ||
        propertyName == "stride") {
        return 64;
    }
    if ((propertyName == "initial" || propertyName == "reset") && kind == "register") {
        return numericProperty(properties, "width");
    }
    if (propertyName == "reset" && kind == "field") {
        const std::size_t msb = numericProperty(properties, "msb", 0);
        const std::size_t lsb = numericProperty(properties, "lsb", 0);
        return msb >= lsb ? msb - lsb + 1 : 1;
    }
    if (propertyName == "value" && kind == "enum") {
        const ObjectId parent = properties.value(QStringLiteral("parent")).toString().toStdString();
        const auto owner = objects.find(parent);
        if (owner != objects.end()) {
            const QJsonObject ownerObject = owner->second;
            const QJsonObject ownerProperties =
                ownerObject.value(QStringLiteral("properties")).toObject();
            if (ownerObject.value(QStringLiteral("kind")).toString() ==
                QStringLiteral("register")) {
                return numericProperty(ownerProperties, "width");
            }
            const std::size_t msb = numericProperty(ownerProperties, "msb", 0);
            const std::size_t lsb = numericProperty(ownerProperties, "lsb", 0);
            return msb >= lsb ? msb - lsb + 1 : 1;
        }
    }
    return 1;
}

[[nodiscard]] SourceLocation rtlLocation(const std::filesystem::path& path,
                                         std::optional<std::uint32_t> line)
{
    SourceLocation source;
    source.workbook = path;
    source.sheet = "Managed RTL";
    source.row = line;
    if (line) {
        source.cell = "line " + std::to_string(*line);
    }
    return source;
}

template <typename Object>
void applySources(Object& object, const std::filesystem::path& path, const SourceLines& lines,
                  const std::map<std::string, std::string, std::less<>>& aliases = {})
{
    const auto objectLine = lines.objects.find(object.id);
    object.source = rtlLocation(
        path, objectLine == lines.objects.end() ? std::nullopt : std::optional{objectLine->second});
    for (const auto& [key, line] : lines.properties) {
        if (key.first != object.id) {
            continue;
        }
        object.propertySources.insert_or_assign(key.second, rtlLocation(path, line));
        const auto alias = aliases.find(key.second);
        if (alias != aliases.end()) {
            object.propertySources.insert_or_assign(alias->second, rtlLocation(path, line));
        }
    }
}

void applyFieldSources(Field& field, const std::filesystem::path& path, const SourceLines& lines)
{
    applySources(field, path, lines);
    for (auto& enumValue : field.enumValues) {
        applySources(enumValue, path, lines);
    }
    for (auto& member : field.members) {
        applyFieldSources(member, path, lines);
    }
}

void applySourceLocations(Workspace& workspace, const std::filesystem::path& path,
                          const SourceLines& lines)
{
    for (auto& space : workspace.addressSpaces) {
        applySources(space, path, lines,
                     {{"name", "address_space_name"}, {"base", "address_space_base"}});
        for (auto& block : space.blocks) {
            applySources(block, path, lines,
                         {{"name", "block_name"}, {"base", "block_base"}, {"size", "block_size"}});
            for (auto& reg : block.registers) {
                applySources(reg, path, lines);
                for (auto& enumValue : reg.enumValues) {
                    applySources(enumValue, path, lines);
                }
                for (auto& field : reg.fields) {
                    applyFieldSources(field, path, lines);
                }
            }
        }
    }
}

[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> managedRange(std::string_view text)
{
    std::optional<std::size_t> rangeBegin;
    std::optional<std::size_t> rangeEnd;
    std::size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const std::size_t newline = text.find('\n', lineStart);
        const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
        const std::string line = trim(text.substr(lineStart, lineEnd - lineStart));
        if (line == beginMarker) {
            if (rangeBegin || rangeEnd) {
                return std::nullopt;
            }
            rangeBegin = lineStart;
        } else if (line == endMarker) {
            if (!rangeBegin || rangeEnd) {
                return std::nullopt;
            }
            rangeEnd = newline == std::string_view::npos ? text.size() : newline + 1;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        lineStart = newline + 1;
    }
    return rangeBegin && rangeEnd ? std::optional{std::pair{*rangeBegin, *rangeEnd}} : std::nullopt;
}

} // namespace

bool RtlParseResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

std::string renderManagedRtlRegion(const Workspace& workspace)
{
    const std::string state = serializeWorkspaceState(workspace, false);
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(state));
    const QJsonArray serializedObjects =
        document.object().value(QStringLiteral("objects")).toArray();
    std::map<ObjectId, QJsonObject, std::less<>> objects;
    for (const QJsonValue& value : serializedObjects) {
        const QJsonObject object = value.toObject();
        objects.insert_or_assign(object.value(QStringLiteral("id")).toString().toStdString(),
                                 object);
    }
    std::ostringstream output;
    output << "  " << beginMarker << '\n'
           << "  // This region is synchronized by Register Map Workbench.\n"
           << "  // Edit RMW:OBJECT JSON for text/enumerated properties and RMW:VALUE literals"
              " for numeric properties.\n";
    for (const QJsonValue& value : serializedObjects) {
        const QJsonObject object = value.toObject();
        output << "  // RMW:OBJECT " << compactJson(object) << '\n';
        const std::string kind = object.value(QStringLiteral("kind")).toString().toStdString();
        const std::string id = object.value(QStringLiteral("id")).toString().toStdString();
        const QJsonObject properties = object.value(QStringLiteral("properties")).toObject();
        for (auto iterator = properties.begin(); iterator != properties.end(); ++iterator) {
            const std::string propertyName = iterator.key().toStdString();
            const std::string propertyValue = iterator.value().toString().toStdString();
            if (propertyValue.empty() || !isNumericProperty(kind, propertyName)) {
                continue;
            }
            output << valueDeclaration(id, propertyName, propertyValue,
                                       semanticValueWidth(kind, propertyName, properties, objects));
        }
        output << '\n';
    }
    output << "  " << endMarker << '\n';
    return output.str();
}

RtlParseResult parseManagedRtl(const std::filesystem::path& path)
{
    RtlParseResult result;
    QFile file(fromPath(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        addDiagnostic(result.diagnostics, openFailureCode, "Cannot open managed RTL.", path);
        return result;
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split('\n');
    bool inside = false;
    bool sawBegin = false;
    bool sawEnd = false;
    std::map<ObjectId, QJsonObject, std::less<>> objects;
    SourceLines sources;
    const QRegularExpression valuePattern(QStringLiteral(
        R"(^\s*localparam\s+.+?\s+[A-Za-z_][A-Za-z0-9_$]*\s*=\s*([^;]+);\s*//\s*RMW:VALUE\s+(\{.*\})\s*$)"));

    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QString line = lines[index];
        const QString trimmedLine = line.trimmed();
        const std::uint32_t lineNumber = static_cast<std::uint32_t>(index + 1);
        if (trimmedLine ==
            QString::fromUtf8(beginMarker.data(), static_cast<qsizetype>(beginMarker.size()))) {
            if (inside || sawBegin) {
                addDiagnostic(result.diagnostics, markerCode,
                              "Managed RTL contains multiple or nested begin markers.", path,
                              lineNumber);
            }
            inside = true;
            sawBegin = true;
            continue;
        }
        if (trimmedLine ==
            QString::fromUtf8(endMarker.data(), static_cast<qsizetype>(endMarker.size()))) {
            if (!inside) {
                addDiagnostic(result.diagnostics, markerCode,
                              "Managed RTL end marker has no matching begin marker.", path,
                              lineNumber);
            }
            inside = false;
            sawEnd = true;
            continue;
        }
        if (!inside) {
            continue;
        }

        const qsizetype objectMarker = line.indexOf(QStringLiteral("// RMW:OBJECT "));
        if (objectMarker >= 0) {
            const QByteArray json = line.mid(objectMarker + 14).trimmed().toUtf8();
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(json, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                addDiagnostic(result.diagnostics, objectCode,
                              "Cannot parse RMW:OBJECT JSON: " + error.errorString().toStdString(),
                              path, lineNumber);
                continue;
            }
            const QJsonObject object = document.object();
            const ObjectId id = object.value(QStringLiteral("id")).toString().toStdString();
            if (id.empty() || objects.contains(id)) {
                addDiagnostic(result.diagnostics, objectCode,
                              "RMW:OBJECT has an empty or duplicate stable ID.", path, lineNumber,
                              id);
                continue;
            }
            objects.insert_or_assign(id, object);
            sources.objects.insert_or_assign(id, lineNumber);
            continue;
        }

        const QRegularExpressionMatch match = valuePattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument marker = QJsonDocument::fromJson(match.captured(2).toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !marker.isObject()) {
            addDiagnostic(result.diagnostics, valueCode, "Cannot parse RMW:VALUE metadata.", path,
                          lineNumber);
            continue;
        }
        const ObjectId id = marker.object().value(QStringLiteral("id")).toString().toStdString();
        const std::string propertyName =
            marker.object().value(QStringLiteral("property")).toString().toStdString();
        const auto parsedValue = parseSystemVerilogUnsigned(match.captured(1).toStdString());
        const auto object = objects.find(id);
        if (object == objects.end() || propertyName.empty() || !parsedValue) {
            addDiagnostic(
                result.diagnostics, valueCode,
                "RMW:VALUE references an unknown object/property or has an unsupported literal.",
                path, lineNumber, id);
            continue;
        }
        QJsonObject updated = object->second;
        QJsonObject properties = updated.value(QStringLiteral("properties")).toObject();
        properties.insert(QString::fromStdString(propertyName),
                          QString::fromStdString(isIntegerProperty(propertyName)
                                                     ? parsedValue->toDecimalString()
                                                     : parsedValue->toHexString()));
        updated.insert(QStringLiteral("properties"), properties);
        object->second = updated;
        sources.properties.insert_or_assign({id, propertyName}, lineNumber);
    }

    if (!sawBegin || !sawEnd || inside) {
        addDiagnostic(result.diagnostics, markerCode,
                      "Managed RTL must contain one complete RMW:BEGIN/RMW:END region.", path);
    }
    if (result.hasErrors()) {
        return result;
    }

    QJsonArray objectArray;
    for (const auto& [id, object] : objects) {
        static_cast<void>(id);
        objectArray.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("objects"), objectArray);
    const std::string state = QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
    auto parsed = parseWorkspaceState(state, path);
    result.diagnostics = std::move(parsed.diagnostics);
    for (auto& diagnostic : result.diagnostics) {
        diagnostic.code = objectCode;
        diagnostic.source.sheet = "Managed RTL";
    }
    if (!parsed.workspace) {
        return result;
    }
    applySourceLocations(*parsed.workspace, path, sources);
    auto validation = validateWorkspace(*parsed.workspace);
    result.diagnostics.insert(result.diagnostics.end(), std::make_move_iterator(validation.begin()),
                              std::make_move_iterator(validation.end()));
    result.workspace = std::move(parsed.workspace);
    return result;
}

std::vector<Diagnostic> writeManagedRtl(const std::filesystem::path& path,
                                        std::string_view moduleName, const Workspace& workspace)
{
    std::vector<Diagnostic> diagnostics;
    std::string existing;
    QFile input(fromPath(path));
    const bool exists = input.exists();
    if (exists) {
        if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
            addDiagnostic(diagnostics, openFailureCode, "Cannot read existing managed RTL.", path);
            return diagnostics;
        }
        existing = input.readAll().toStdString();
        input.close();
    }

    const std::string region = renderManagedRtlRegion(workspace);
    std::string output;
    if (!exists || existing.empty()) {
        output = "`default_nettype none\nmodule " + std::string(moduleName) + " ();\n\n" + region +
                 "\nendmodule\n`default_nettype wire\n";
    } else if (const auto range = managedRange(existing)) {
        output = existing.substr(0, range->first) + region + existing.substr(range->second);
    } else {
        addDiagnostic(
            diagnostics, markerCode,
            "Existing RTL has no managed region; refusing to overwrite uncontrolled code.", path);
        return diagnostics;
    }

    const QFileInfo information(fromPath(path));
    if (!QDir().mkpath(information.absolutePath())) {
        addDiagnostic(diagnostics, writeCode, "Cannot create the managed RTL directory.", path);
        return diagnostics;
    }
    QSaveFile file(fromPath(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        addDiagnostic(diagnostics, writeCode, "Cannot open managed RTL for writing.", path);
        return diagnostics;
    }
    const QByteArray bytes(output.data(), static_cast<qsizetype>(output.size()));
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        addDiagnostic(diagnostics, writeCode, "Cannot atomically write managed RTL.", path);
    }
    return diagnostics;
}

} // namespace regmap
