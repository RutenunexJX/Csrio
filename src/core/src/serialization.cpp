#include "regmap/core/serialization.hpp"

#include "regmap/core/model_tokens.hpp"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QString>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view invalidYamlCode = "RM1100";
constexpr std::string_view missingValueCode = "RM1101";
constexpr std::string_view invalidValueCode = "RM1102";
constexpr std::string_view writeFailureCode = "RM1103";

[[nodiscard]] SourceLocation yamlLocation(const std::filesystem::path& path, const YAML::Mark& mark,
                                          std::string yamlPath = {})
{
    SourceLocation location;
    location.workbook = path;
    location.sheet = "Project Model";
    if (!mark.is_null()) {
        location.row = static_cast<std::uint32_t>(mark.line + 1);
        location.column = static_cast<std::uint32_t>(mark.column + 1);
    }
    location.cell = std::move(yamlPath);
    return location;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, std::string_view code, std::string message,
                   const std::filesystem::path& filePath, const YAML::Node& node = {},
                   std::string yamlPath = {}, ObjectId objectId = {})
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.objectId = std::move(objectId);
    diagnostic.source = yamlLocation(filePath, node.Mark(), std::move(yamlPath));
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] std::string childPath(std::string_view parent, std::string_view child)
{
    if (parent.empty()) {
        return std::string(child);
    }
    return std::string(parent) + '.' + std::string(child);
}

[[nodiscard]] std::string indexedPath(std::string_view parent, std::size_t index)
{
    return std::string(parent) + '[' + std::to_string(index) + ']';
}

[[nodiscard]] std::optional<std::string> scalar(const YAML::Node& parent, std::string_view key,
                                                bool required,
                                                const std::filesystem::path& filePath,
                                                std::string_view parentPath,
                                                std::vector<Diagnostic>& diagnostics)
{
    const std::string path = childPath(parentPath, key);
    const YAML::Node node = parent[std::string(key)];
    if (!node) {
        if (required) {
            addDiagnostic(diagnostics, missingValueCode, "Missing required value '" + path + "'.",
                          filePath, parent, path);
        }
        return std::nullopt;
    }
    if (!node.IsScalar()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a scalar.",
                      filePath, node, path);
        return std::nullopt;
    }
    const std::string value = node.Scalar();
    if (value.empty() && required) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must not be empty.",
                      filePath, node, path);
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<bool> boolValue(const YAML::Node& parent, std::string_view key,
                                            bool required, const std::filesystem::path& filePath,
                                            std::string_view parentPath,
                                            std::vector<Diagnostic>& diagnostics)
{
    const auto value = scalar(parent, key, required, filePath, parentPath, diagnostics);
    if (!value) {
        return std::nullopt;
    }
    std::string token;
    token.reserve(value->size());
    std::ranges::transform(*value, std::back_inserter(token), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    if (token == "true" || token == "yes" || token == "1") {
        return true;
    }
    if (token == "false" || token == "no" || token == "0") {
        return false;
    }
    const std::string path = childPath(parentPath, key);
    addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be true or false.",
                  filePath, parent[std::string(key)], path);
    return std::nullopt;
}

[[nodiscard]] std::optional<UnsignedValue> unsignedValue(const YAML::Node& parent,
                                                         std::string_view key, bool required,
                                                         const std::filesystem::path& filePath,
                                                         std::string_view parentPath,
                                                         std::vector<Diagnostic>& diagnostics)
{
    const auto value = scalar(parent, key, required, filePath, parentPath, diagnostics);
    if (!value) {
        return std::nullopt;
    }
    const auto parsed = UnsignedValue::parse(*value);
    if (!parsed) {
        const std::string path = childPath(parentPath, key);
        addDiagnostic(diagnostics, invalidValueCode,
                      "Value '" + path + "' must be an unsigned integer.", filePath,
                      parent[std::string(key)], path);
    }
    return parsed;
}

[[nodiscard]] std::optional<std::uint64_t> uint64Value(const YAML::Node& parent,
                                                       std::string_view key, bool required,
                                                       const std::filesystem::path& filePath,
                                                       std::string_view parentPath,
                                                       std::vector<Diagnostic>& diagnostics)
{
    const auto value = unsignedValue(parent, key, required, filePath, parentPath, diagnostics);
    if (!value) {
        return std::nullopt;
    }
    const auto result = value->toUInt64();
    if (!result) {
        const std::string path = childPath(parentPath, key);
        addDiagnostic(diagnostics, invalidValueCode,
                      "Value '" + path + "' exceeds the supported 64-bit range.", filePath,
                      parent[std::string(key)], path);
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> uint32Value(const YAML::Node& parent,
                                                       std::string_view key, bool required,
                                                       const std::filesystem::path& filePath,
                                                       std::string_view parentPath,
                                                       std::vector<Diagnostic>& diagnostics)
{
    const auto value = uint64Value(parent, key, required, filePath, parentPath, diagnostics);
    if (!value) {
        return std::nullopt;
    }
    if (*value > std::numeric_limits<std::uint32_t>::max()) {
        const std::string path = childPath(parentPath, key);
        addDiagnostic(diagnostics, invalidValueCode,
                      "Value '" + path + "' exceeds the supported 32-bit range.", filePath,
                      parent[std::string(key)], path);
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

[[nodiscard]] YAML::Node sequence(const YAML::Node& parent, std::string_view key, bool required,
                                  const std::filesystem::path& filePath,
                                  std::string_view parentPath, std::vector<Diagnostic>& diagnostics)
{
    const std::string path = childPath(parentPath, key);
    const YAML::Node node = parent[std::string(key)];
    if (!node) {
        if (required) {
            addDiagnostic(diagnostics, missingValueCode,
                          "Missing required sequence '" + path + "'.", filePath, parent, path);
        }
        return {};
    }
    if (!node.IsSequence()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a sequence.",
                      filePath, node, path);
        return {};
    }
    return node;
}

template <typename Object>
void setObjectSource(Object& object, const std::filesystem::path& filePath, const YAML::Node& node,
                     const std::string& path)
{
    object.source = yamlLocation(filePath, node.Mark(), path);
}

template <typename Object>
void setPropertySource(Object& object, std::string property, const std::filesystem::path& filePath,
                       const YAML::Node& parent, std::string_view key, std::string path)
{
    const YAML::Node node = parent[std::string(key)];
    object.propertySources.insert_or_assign(
        std::move(property),
        yamlLocation(filePath, node ? node.Mark() : parent.Mark(), std::move(path)));
}

template <typename Enum, typename Parser>
[[nodiscard]] Enum enumValue(const YAML::Node& parent, std::string_view key, Enum fallback,
                             Parser parser, const std::filesystem::path& filePath,
                             std::string_view parentPath, std::vector<Diagnostic>& diagnostics)
{
    const auto value = scalar(parent, key, false, filePath, parentPath, diagnostics);
    if (!value) {
        return fallback;
    }
    const auto parsed = parser(*value);
    if (!parsed) {
        const std::string path = childPath(parentPath, key);
        addDiagnostic(diagnostics, invalidValueCode,
                      "Value '" + path + "' has an unsupported token '" + *value + "'.", filePath,
                      parent[std::string(key)], path);
        return fallback;
    }
    return *parsed;
}

[[nodiscard]] EnumValue parseEnumValue(const YAML::Node& node,
                                       const std::filesystem::path& filePath,
                                       const std::string& path,
                                       std::vector<Diagnostic>& diagnostics)
{
    EnumValue result;
    setObjectSource(result, filePath, node, path);
    if (!node.IsMap()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a mapping.",
                      filePath, node, path);
        return result;
    }

    result.id = scalar(node, "id", true, filePath, path, diagnostics).value_or("");
    result.name = scalar(node, "name", true, filePath, path, diagnostics).value_or("");
    result.value =
        unsignedValue(node, "value", true, filePath, path, diagnostics).value_or(UnsignedValue{});
    result.description =
        scalar(node, "description", false, filePath, path, diagnostics).value_or("");
    setPropertySource(result, "name", filePath, node, "name", childPath(path, "name"));
    setPropertySource(result, "value", filePath, node, "value", childPath(path, "value"));
    return result;
}

[[nodiscard]] Field parseField(const YAML::Node& node, const std::filesystem::path& filePath,
                               const std::string& path, std::vector<Diagnostic>& diagnostics)
{
    Field result;
    setObjectSource(result, filePath, node, path);
    if (!node.IsMap()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a mapping.",
                      filePath, node, path);
        return result;
    }

    result.id = scalar(node, "id", true, filePath, path, diagnostics).value_or("");
    result.name = scalar(node, "name", true, filePath, path, diagnostics).value_or("");
    result.msb = uint32Value(node, "msb", true, filePath, path, diagnostics).value_or(0);
    result.lsb = uint32Value(node, "lsb", true, filePath, path, diagnostics).value_or(0);
    result.type = enumValue<FieldType>(node, "type", FieldType::bits, parseFieldType, filePath,
                                       path, diagnostics);
    result.softwareAccess = enumValue<AccessMode>(node, "sw_access", AccessMode::readWrite,
                                                  parseAccessMode, filePath, path, diagnostics);
    result.hardwareAccess = enumValue<AccessMode>(node, "hw_access", AccessMode::none,
                                                  parseAccessMode, filePath, path, diagnostics);
    result.resetValue = unsignedValue(node, "reset", false, filePath, path, diagnostics);
    result.readSideEffect =
        enumValue<ReadSideEffect>(node, "read_side_effect", ReadSideEffect::none,
                                  parseReadSideEffect, filePath, path, diagnostics);
    result.writeSideEffect =
        enumValue<WriteSideEffect>(node, "write_side_effect", WriteSideEffect::write,
                                   parseWriteSideEffect, filePath, path, diagnostics);
    result.description =
        scalar(node, "description", false, filePath, path, diagnostics).value_or("");
    result.minimumValue = scalar(node, "minimum", false, filePath, path, diagnostics);
    result.maximumValue = scalar(node, "maximum", false, filePath, path, diagnostics);

    setPropertySource(result, "name", filePath, node, "name", childPath(path, "name"));
    setPropertySource(result, "msb", filePath, node, "msb", childPath(path, "msb"));
    setPropertySource(result, "lsb", filePath, node, "lsb", childPath(path, "lsb"));
    setPropertySource(result, "type", filePath, node, "type", childPath(path, "type"));
    setPropertySource(result, "sw_access", filePath, node, "sw_access",
                      childPath(path, "sw_access"));
    setPropertySource(result, "hw_access", filePath, node, "hw_access",
                      childPath(path, "hw_access"));
    setPropertySource(result, "reset", filePath, node, "reset", childPath(path, "reset"));
    setPropertySource(result, "read_side_effect", filePath, node, "read_side_effect",
                      childPath(path, "read_side_effect"));
    setPropertySource(result, "write_side_effect", filePath, node, "write_side_effect",
                      childPath(path, "write_side_effect"));
    setPropertySource(result, "minimum", filePath, node, "minimum", childPath(path, "minimum"));
    setPropertySource(result, "maximum", filePath, node, "maximum", childPath(path, "maximum"));

    const std::string valuesPath = childPath(path, "enum_values");
    const YAML::Node values = sequence(node, "enum_values", false, filePath, path, diagnostics);
    for (std::size_t index = 0; index < values.size(); ++index) {
        result.enumValues.push_back(
            parseEnumValue(values[index], filePath, indexedPath(valuesPath, index), diagnostics));
    }

    const std::string membersPath = childPath(path, "members");
    const YAML::Node members = sequence(node, "members", false, filePath, path, diagnostics);
    for (std::size_t index = 0; index < members.size(); ++index) {
        result.members.push_back(
            parseField(members[index], filePath, indexedPath(membersPath, index), diagnostics));
    }
    return result;
}

[[nodiscard]] Register parseRegister(const YAML::Node& node, const std::filesystem::path& filePath,
                                     const std::string& path, std::vector<Diagnostic>& diagnostics)
{
    Register result;
    setObjectSource(result, filePath, node, path);
    if (!node.IsMap()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a mapping.",
                      filePath, node, path);
        return result;
    }

    result.id = scalar(node, "id", true, filePath, path, diagnostics).value_or("");
    result.name = scalar(node, "name", true, filePath, path, diagnostics).value_or("");
    result.offset = uint64Value(node, "offset", true, filePath, path, diagnostics).value_or(0);
    result.width = uint32Value(node, "width", true, filePath, path, diagnostics).value_or(32);
    const bool hasExplicitType = static_cast<bool>(node["type"]);
    result.type = enumValue<FieldType>(node, "type", FieldType::unsignedInteger, parseFieldType,
                                       filePath, path, diagnostics);
    result.minimumValue = scalar(node, "minimum", false, filePath, path, diagnostics);
    result.maximumValue = scalar(node, "maximum", false, filePath, path, diagnostics);
    result.initialValue = unsignedValue(node, "initial", false, filePath, path, diagnostics);
    result.resetValue = unsignedValue(node, "reset", false, filePath, path, diagnostics);
    result.access = enumValue<AccessMode>(node, "access", AccessMode::readWrite, parseAccessMode,
                                          filePath, path, diagnostics);
    result.reserved =
        boolValue(node, "reserved", false, filePath, path, diagnostics).value_or(false);
    result.description =
        scalar(node, "description", false, filePath, path, diagnostics).value_or("");

    const std::string tagsPath = childPath(path, "tags");
    const YAML::Node tags = sequence(node, "tags", false, filePath, path, diagnostics);
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (!tags[index].IsScalar()) {
            addDiagnostic(diagnostics, invalidValueCode,
                          "Value '" + indexedPath(tagsPath, index) + "' must be a scalar.",
                          filePath, tags[index], indexedPath(tagsPath, index), result.id);
            continue;
        }
        result.tags.push_back(tags[index].Scalar());
    }

    const YAML::Node array = node["array"];
    const std::string arrayPath = childPath(path, "array");
    if (array) {
        if (!array.IsMap()) {
            addDiagnostic(diagnostics, invalidValueCode,
                          "Value '" + arrayPath + "' must be a mapping.", filePath, array,
                          arrayPath);
        } else {
            result.array.count =
                uint32Value(array, "count", false, filePath, arrayPath, diagnostics).value_or(1);
            result.array.stride =
                uint64Value(array, "stride", false, filePath, arrayPath, diagnostics).value_or(0);
        }
    }

    setPropertySource(result, "name", filePath, node, "name", childPath(path, "name"));
    setPropertySource(result, "offset", filePath, node, "offset", childPath(path, "offset"));
    setPropertySource(result, "width", filePath, node, "width", childPath(path, "width"));
    setPropertySource(result, "type", filePath, node, "type", childPath(path, "type"));
    setPropertySource(result, "minimum", filePath, node, "minimum", childPath(path, "minimum"));
    setPropertySource(result, "maximum", filePath, node, "maximum", childPath(path, "maximum"));
    setPropertySource(result, "initial", filePath, node, "initial", childPath(path, "initial"));
    setPropertySource(result, "array_count", filePath, array ? array : node, "count",
                      childPath(arrayPath, "count"));
    setPropertySource(result, "stride", filePath, array ? array : node, "stride",
                      childPath(arrayPath, "stride"));
    setPropertySource(result, "reset", filePath, node, "reset", childPath(path, "reset"));
    setPropertySource(result, "access", filePath, node, "access", childPath(path, "access"));
    setPropertySource(result, "reserved", filePath, node, "reserved", childPath(path, "reserved"));
    setPropertySource(result, "tags", filePath, node, "tags", tagsPath);

    const std::string valuesPath = childPath(path, "enum_values");
    const YAML::Node values = sequence(node, "enum_values", false, filePath, path, diagnostics);
    for (std::size_t index = 0; index < values.size(); ++index) {
        result.enumValues.push_back(
            parseEnumValue(values[index], filePath, indexedPath(valuesPath, index), diagnostics));
    }

    const std::string fieldsPath = childPath(path, "fields");
    const YAML::Node fields = sequence(node, "fields", true, filePath, path, diagnostics);
    for (std::size_t index = 0; index < fields.size(); ++index) {
        result.fields.push_back(
            parseField(fields[index], filePath, indexedPath(fieldsPath, index), diagnostics));
    }
    if (!hasExplicitType) {
        result.type = result.reserved ? FieldType::reserved
                                      : (result.fields.empty() ? FieldType::unsignedInteger
                                                               : FieldType::structure);
    }
    return result;
}

[[nodiscard]] RegisterBlock parseBlock(const YAML::Node& node,
                                       const std::filesystem::path& filePath,
                                       const std::string& path,
                                       std::vector<Diagnostic>& diagnostics)
{
    RegisterBlock result;
    setObjectSource(result, filePath, node, path);
    if (!node.IsMap()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a mapping.",
                      filePath, node, path);
        return result;
    }

    result.id = scalar(node, "id", true, filePath, path, diagnostics).value_or("");
    result.name = scalar(node, "name", true, filePath, path, diagnostics).value_or("");
    result.baseAddress = uint64Value(node, "base", true, filePath, path, diagnostics).value_or(0);
    result.size = uint64Value(node, "size", false, filePath, path, diagnostics);
    result.description =
        scalar(node, "description", false, filePath, path, diagnostics).value_or("");
    setPropertySource(result, "block_name", filePath, node, "name", childPath(path, "name"));
    setPropertySource(result, "block_base", filePath, node, "base", childPath(path, "base"));
    setPropertySource(result, "block_size", filePath, node, "size", childPath(path, "size"));

    const std::string registersPath = childPath(path, "registers");
    const YAML::Node registers = sequence(node, "registers", true, filePath, path, diagnostics);
    for (std::size_t index = 0; index < registers.size(); ++index) {
        result.registers.push_back(parseRegister(registers[index], filePath,
                                                 indexedPath(registersPath, index), diagnostics));
    }
    return result;
}

[[nodiscard]] AddressSpace parseAddressSpace(const YAML::Node& node,
                                             const std::filesystem::path& filePath,
                                             const std::string& path,
                                             std::vector<Diagnostic>& diagnostics)
{
    AddressSpace result;
    setObjectSource(result, filePath, node, path);
    if (!node.IsMap()) {
        addDiagnostic(diagnostics, invalidValueCode, "Value '" + path + "' must be a mapping.",
                      filePath, node, path);
        return result;
    }

    result.id = scalar(node, "id", true, filePath, path, diagnostics).value_or("");
    result.name = scalar(node, "name", true, filePath, path, diagnostics).value_or("");
    result.baseAddress = uint64Value(node, "base", true, filePath, path, diagnostics).value_or(0);
    result.addressWidth =
        uint32Value(node, "address_width", true, filePath, path, diagnostics).value_or(32);
    result.description =
        scalar(node, "description", false, filePath, path, diagnostics).value_or("");
    setPropertySource(result, "address_space_name", filePath, node, "name",
                      childPath(path, "name"));
    setPropertySource(result, "address_space_base", filePath, node, "base",
                      childPath(path, "base"));
    setPropertySource(result, "address_width", filePath, node, "address_width",
                      childPath(path, "address_width"));

    const std::string blocksPath = childPath(path, "blocks");
    const YAML::Node blocks = sequence(node, "blocks", true, filePath, path, diagnostics);
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        result.blocks.push_back(
            parseBlock(blocks[index], filePath, indexedPath(blocksPath, index), diagnostics));
    }
    return result;
}

void emitDescription(YAML::Emitter& output, const std::string& description)
{
    if (!description.empty()) {
        output << YAML::Key << "description" << YAML::Value << description;
    }
}

[[nodiscard]] std::string hex(std::uint64_t value) { return UnsignedValue(value).toHexString(); }

void emitEnumValue(YAML::Emitter& output, const EnumValue& value)
{
    output << YAML::BeginMap << YAML::Key << "id" << YAML::Value << value.id << YAML::Key << "name"
           << YAML::Value << value.name << YAML::Key << "value" << YAML::Value
           << value.value.toHexString();
    emitDescription(output, value.description);
    output << YAML::EndMap;
}

void emitField(YAML::Emitter& output, const Field& field)
{
    output << YAML::BeginMap << YAML::Key << "id" << YAML::Value << field.id << YAML::Key << "name"
           << YAML::Value << field.name << YAML::Key << "msb" << YAML::Value << field.msb
           << YAML::Key << "lsb" << YAML::Value << field.lsb << YAML::Key << "type" << YAML::Value
           << std::string(toString(field.type)) << YAML::Key << "sw_access" << YAML::Value
           << std::string(toString(field.softwareAccess)) << YAML::Key << "hw_access" << YAML::Value
           << std::string(toString(field.hardwareAccess));
    if (field.resetValue) {
        output << YAML::Key << "reset" << YAML::Value << field.resetValue->toHexString();
    }
    output << YAML::Key << "read_side_effect" << YAML::Value
           << std::string(toString(field.readSideEffect)) << YAML::Key << "write_side_effect"
           << YAML::Value << std::string(toString(field.writeSideEffect));
    if (field.minimumValue) {
        output << YAML::Key << "minimum" << YAML::Value << *field.minimumValue;
    }
    if (field.maximumValue) {
        output << YAML::Key << "maximum" << YAML::Value << *field.maximumValue;
    }
    emitDescription(output, field.description);
    output << YAML::Key << "enum_values" << YAML::Value << YAML::BeginSeq;
    for (const auto& value : field.enumValues) {
        emitEnumValue(output, value);
    }
    output << YAML::EndSeq;
    if (!field.members.empty() || field.type == FieldType::structure) {
        output << YAML::Key << "members" << YAML::Value << YAML::BeginSeq;
        for (const auto& member : field.members) {
            emitField(output, member);
        }
        output << YAML::EndSeq;
    }
    output << YAML::EndMap;
}

void emitRegister(YAML::Emitter& output, const Register& reg)
{
    output << YAML::BeginMap << YAML::Key << "id" << YAML::Value << reg.id << YAML::Key << "name"
           << YAML::Value << reg.name << YAML::Key << "offset" << YAML::Value << hex(reg.offset)
           << YAML::Key << "width" << YAML::Value << reg.width << YAML::Key << "array"
           << YAML::Value << YAML::BeginMap << YAML::Key << "count" << YAML::Value
           << reg.array.count << YAML::Key << "stride" << YAML::Value << hex(reg.array.stride)
           << YAML::EndMap << YAML::Key << "type" << YAML::Value << std::string(toString(reg.type));
    if (reg.minimumValue) {
        output << YAML::Key << "minimum" << YAML::Value << *reg.minimumValue;
    }
    if (reg.maximumValue) {
        output << YAML::Key << "maximum" << YAML::Value << *reg.maximumValue;
    }
    if (reg.initialValue) {
        output << YAML::Key << "initial" << YAML::Value << reg.initialValue->toHexString();
    }
    if (reg.resetValue) {
        output << YAML::Key << "reset" << YAML::Value << reg.resetValue->toHexString();
    }
    output << YAML::Key << "access" << YAML::Value << std::string(toString(reg.access));
    if (reg.reserved) {
        output << YAML::Key << "reserved" << YAML::Value << true;
    }
    if (!reg.tags.empty()) {
        output << YAML::Key << "tags" << YAML::Value << YAML::BeginSeq;
        for (const auto& tag : reg.tags) {
            output << tag;
        }
        output << YAML::EndSeq;
    }
    emitDescription(output, reg.description);
    output << YAML::Key << "enum_values" << YAML::Value << YAML::BeginSeq;
    for (const auto& value : reg.enumValues) {
        emitEnumValue(output, value);
    }
    output << YAML::EndSeq << YAML::Key << "fields" << YAML::Value << YAML::BeginSeq;
    for (const auto& field : reg.fields) {
        emitField(output, field);
    }
    output << YAML::EndSeq << YAML::EndMap;
}

void emitBlock(YAML::Emitter& output, const RegisterBlock& block)
{
    output << YAML::BeginMap << YAML::Key << "id" << YAML::Value << block.id << YAML::Key << "name"
           << YAML::Value << block.name << YAML::Key << "base" << YAML::Value
           << hex(block.baseAddress);
    if (block.size) {
        output << YAML::Key << "size" << YAML::Value << hex(*block.size);
    }
    emitDescription(output, block.description);
    output << YAML::Key << "registers" << YAML::Value << YAML::BeginSeq;
    for (const auto& reg : block.registers) {
        emitRegister(output, reg);
    }
    output << YAML::EndSeq << YAML::EndMap;
}

void emitAddressSpace(YAML::Emitter& output, const AddressSpace& addressSpace)
{
    output << YAML::BeginMap << YAML::Key << "id" << YAML::Value << addressSpace.id << YAML::Key
           << "name" << YAML::Value << addressSpace.name << YAML::Key << "base" << YAML::Value
           << hex(addressSpace.baseAddress) << YAML::Key << "address_width" << YAML::Value
           << addressSpace.addressWidth;
    emitDescription(output, addressSpace.description);
    output << YAML::Key << "blocks" << YAML::Value << YAML::BeginSeq;
    for (const auto& block : addressSpace.blocks) {
        emitBlock(output, block);
    }
    output << YAML::EndSeq << YAML::EndMap;
}

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

} // namespace

bool WorkspaceFileLoadResult::hasErrors() const noexcept
{
    return std::ranges::any_of(diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

WorkspaceFileLoadResult loadWorkspaceFromProjectFile(const std::filesystem::path& path)
{
    WorkspaceFileLoadResult result;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
    YAML::Node root;
    try {
        root = YAML::LoadFile(absolutePath.string());
    } catch (const YAML::Exception& error) {
        addDiagnostic(result.diagnostics, invalidYamlCode,
                      "Cannot parse project model: " + std::string(error.what()), absolutePath, {},
                      "workspace");
        return result;
    }
    if (!root.IsMap()) {
        addDiagnostic(result.diagnostics, invalidYamlCode, "Project root must be a mapping.",
                      absolutePath, root);
        return result;
    }

    const YAML::Node workspaceNode = root["workspace"];
    if (!workspaceNode || !workspaceNode.IsMap()) {
        addDiagnostic(result.diagnostics, missingValueCode,
                      "Required mapping 'workspace' is missing.", absolutePath,
                      workspaceNode ? workspaceNode : root, "workspace");
        return result;
    }

    Workspace workspace;
    workspace.manifestPath = absolutePath;
    workspace.id = scalar(workspaceNode, "id", true, absolutePath, "workspace", result.diagnostics)
                       .value_or("");
    workspace.name =
        scalar(workspaceNode, "name", true, absolutePath, "workspace", result.diagnostics)
            .value_or("");
    const std::string spacesPath = "workspace.address_spaces";
    const YAML::Node spaces = sequence(workspaceNode, "address_spaces", true, absolutePath,
                                       "workspace", result.diagnostics);
    for (std::size_t index = 0; index < spaces.size(); ++index) {
        workspace.addressSpaces.push_back(parseAddressSpace(
            spaces[index], absolutePath, indexedPath(spacesPath, index), result.diagnostics));
    }

    if (!result.hasErrors()) {
        result.workspace = std::move(workspace);
    }
    return result;
}

std::vector<Diagnostic> saveProjectFile(const ProjectManifest& manifest, const Workspace& workspace)
{
    YAML::Emitter output;
    output.SetIndent(2);
    output << YAML::BeginMap << YAML::Key << "schema_version" << YAML::Value
           << ProjectManifest::currentSchemaVersion << YAML::Key << "workspace" << YAML::Value
           << YAML::BeginMap << YAML::Key << "id" << YAML::Value << workspace.id << YAML::Key
           << "name" << YAML::Value << workspace.name << YAML::Key << "address_spaces"
           << YAML::Value << YAML::BeginSeq;
    for (const auto& addressSpace : workspace.addressSpaces) {
        emitAddressSpace(output, addressSpace);
    }
    output << YAML::EndSeq << YAML::EndMap << YAML::Key << "rtl" << YAML::Value << YAML::BeginMap
           << YAML::Key << "path" << YAML::Value << manifest.rtl.path.declared.generic_string()
           << YAML::Key << "module" << YAML::Value << manifest.rtl.moduleName << YAML::EndMap
           << YAML::Key << "generation" << YAML::Value << YAML::BeginMap << YAML::Key
           << "output_directory" << YAML::Value
           << manifest.outputDirectory.declared.generic_string() << YAML::Key << "targets"
           << YAML::Value << YAML::BeginSeq;
    for (const auto& target : manifest.targets) {
        output << YAML::BeginMap << YAML::Key << "kind" << YAML::Value
               << std::string(toString(target.kind)) << YAML::Key << "path" << YAML::Value
               << target.path.declared.generic_string();
        if (!target.options.empty()) {
            output << YAML::Key << "options" << YAML::Value << YAML::BeginMap;
            for (const auto& [name, value] : target.options) {
                output << YAML::Key << name << YAML::Value << value;
            }
            output << YAML::EndMap;
        }
        output << YAML::EndMap;
    }
    output << YAML::EndSeq << YAML::EndMap << YAML::EndMap;

    std::vector<Diagnostic> diagnostics;
    if (!output.good()) {
        Diagnostic diagnostic;
        diagnostic.code = writeFailureCode;
        diagnostic.message = "Cannot serialize project file: " + output.GetLastError();
        diagnostic.source.workbook = manifest.manifestPath;
        diagnostics.push_back(std::move(diagnostic));
        return diagnostics;
    }

    const QString filePath = fromPath(manifest.manifestPath);
    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        Diagnostic diagnostic;
        diagnostic.code = writeFailureCode;
        diagnostic.message = "Cannot create the project file directory.";
        diagnostic.source.workbook = manifest.manifestPath;
        diagnostics.push_back(std::move(diagnostic));
        return diagnostics;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Diagnostic diagnostic;
        diagnostic.code = writeFailureCode;
        diagnostic.message =
            "Cannot open the project file for writing: " + file.errorString().toStdString();
        diagnostic.source.workbook = manifest.manifestPath;
        diagnostics.push_back(std::move(diagnostic));
        return diagnostics;
    }
    const std::string text = std::string(output.c_str()) + '\n';
    const QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        Diagnostic diagnostic;
        diagnostic.code = writeFailureCode;
        diagnostic.message =
            "Cannot atomically save the project file: " + file.errorString().toStdString();
        diagnostic.source.workbook = manifest.manifestPath;
        diagnostics.push_back(std::move(diagnostic));
    }
    return diagnostics;
}

} // namespace regmap
