#include "regmap/core/three_way_merge.hpp"

#include "regmap/core/model_tokens.hpp"
#include "atomic_file_writer.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace regmap {
namespace {

constexpr std::string_view baselineReadCode = "RM5200";
constexpr std::string_view baselineWriteCode = "RM5201";

struct FlatObject {
    ObjectKind kind{ObjectKind::workspace};
    ObjectId id;
    std::map<std::string, std::string, std::less<>> properties;
    SourceLocation source;
    PropertySources propertySources;
};

using FlatWorkspace = std::map<ObjectId, FlatObject, std::less<>>;

[[nodiscard]] std::string kindToken(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::workspace:
        return "workspace";
    case ObjectKind::addressSpace:
        return "address-space";
    case ObjectKind::registerBlock:
        return "block";
    case ObjectKind::reg:
        return "register";
    case ObjectKind::field:
        return "field";
    case ObjectKind::enumValue:
        return "enum";
    }
    return "workspace";
}

[[nodiscard]] std::optional<ObjectKind> parseKind(std::string_view value)
{
    if (value == "workspace") {
        return ObjectKind::workspace;
    }
    if (value == "address-space") {
        return ObjectKind::addressSpace;
    }
    if (value == "block") {
        return ObjectKind::registerBlock;
    }
    if (value == "register") {
        return ObjectKind::reg;
    }
    if (value == "field") {
        return ObjectKind::field;
    }
    if (value == "enum") {
        return ObjectKind::enumValue;
    }
    return std::nullopt;
}

[[nodiscard]] std::string uint64Text(std::uint64_t value)
{
    return UnsignedValue(value).toHexString();
}

[[nodiscard]] std::string optionalText(const std::optional<UnsignedValue>& value)
{
    return value ? value->toHexString() : std::string{};
}

[[nodiscard]] std::string optionalText(const std::optional<std::uint64_t>& value)
{
    return value ? uint64Text(*value) : std::string{};
}

[[nodiscard]] std::string optionalText(const std::optional<std::string>& value)
{
    return value.value_or(std::string{});
}

[[nodiscard]] std::string stringListText(const std::vector<std::string>& values)
{
    QJsonArray array;
    for (const auto& value : values) {
        array.append(QString::fromStdString(value));
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString();
}

[[nodiscard]] std::vector<std::string> parseStringList(std::string_view value)
{
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(value.data(), static_cast<qsizetype>(value.size())));
    std::vector<std::string> result;
    if (!document.isArray()) {
        return result;
    }
    for (const auto& item : document.array()) {
        if (item.isString()) {
            result.push_back(item.toString().toStdString());
        }
    }
    return result;
}

[[nodiscard]] bool parseBoolean(std::string_view value) noexcept
{
    return value == "true" || value == "1";
}

void insert(FlatWorkspace& workspace, FlatObject object)
{
    workspace.insert_or_assign(object.id, std::move(object));
}

[[nodiscard]] FlatWorkspace flatten(const Workspace& workspace)
{
    FlatWorkspace result;
    FlatObject root;
    root.kind = ObjectKind::workspace;
    root.id = workspace.id;
    root.properties["name"] = workspace.name;
    insert(result, std::move(root));

    std::function<void(const Field&, const ObjectId&, std::size_t)> flattenField;
    flattenField = [&](const Field& field, const ObjectId& parentId, std::size_t fieldIndex) {
        FlatObject fieldObject;
        fieldObject.kind = ObjectKind::field;
        fieldObject.id = field.id;
        fieldObject.source = field.source;
        fieldObject.propertySources = field.propertySources;
        fieldObject.properties = {
            {"parent", parentId},
            {"order", std::to_string(fieldIndex)},
            {"name", field.name},
            {"msb", std::to_string(field.msb)},
            {"lsb", std::to_string(field.lsb)},
            {"type", std::string(toString(field.type))},
            {"sw_access", std::string(toString(field.softwareAccess))},
            {"hw_access", std::string(toString(field.hardwareAccess))},
            {"read_side_effect", std::string(toString(field.readSideEffect))},
            {"write_side_effect", std::string(toString(field.writeSideEffect))},
            {"minimum", optionalText(field.minimumValue)},
            {"maximum", optionalText(field.maximumValue)},
            {"description", field.description}};
        insert(result, std::move(fieldObject));

        for (std::size_t enumIndex = 0; enumIndex < field.enumValues.size(); ++enumIndex) {
            const auto& enumValue = field.enumValues[enumIndex];
            FlatObject enumObject;
            enumObject.kind = ObjectKind::enumValue;
            enumObject.id = enumValue.id;
            enumObject.source = enumValue.source;
            enumObject.propertySources = enumValue.propertySources;
            enumObject.properties = {{"parent", field.id},
                                     {"order", std::to_string(enumIndex)},
                                     {"name", enumValue.name},
                                     {"value", enumValue.value.toHexString()},
                                     {"description", enumValue.description}};
            insert(result, std::move(enumObject));
        }
        for (std::size_t memberIndex = 0; memberIndex < field.members.size(); ++memberIndex) {
            flattenField(field.members[memberIndex], field.id, memberIndex);
        }
    };

    for (std::size_t spaceIndex = 0; spaceIndex < workspace.addressSpaces.size(); ++spaceIndex) {
        const auto& space = workspace.addressSpaces[spaceIndex];
        FlatObject spaceObject;
        spaceObject.kind = ObjectKind::addressSpace;
        spaceObject.id = space.id;
        spaceObject.source = space.source;
        spaceObject.propertySources = space.propertySources;
        spaceObject.properties = {{"parent", workspace.id},
                                  {"order", std::to_string(spaceIndex)},
                                  {"name", space.name},
                                  {"base", uint64Text(space.baseAddress)},
                                  {"address_width", std::to_string(space.addressWidth)},
                                  {"description", space.description}};
        insert(result, std::move(spaceObject));

        for (std::size_t blockIndex = 0; blockIndex < space.blocks.size(); ++blockIndex) {
            const auto& block = space.blocks[blockIndex];
            FlatObject blockObject;
            blockObject.kind = ObjectKind::registerBlock;
            blockObject.id = block.id;
            blockObject.source = block.source;
            blockObject.propertySources = block.propertySources;
            blockObject.properties = {{"parent", space.id},
                                      {"order", std::to_string(blockIndex)},
                                      {"name", block.name},
                                      {"base", uint64Text(block.baseAddress)},
                                      {"size", optionalText(block.size)},
                                      {"description", block.description}};
            insert(result, std::move(blockObject));

            for (std::size_t registerIndex = 0; registerIndex < block.registers.size();
                 ++registerIndex) {
                const auto& reg = block.registers[registerIndex];
                FlatObject registerObject;
                registerObject.kind = ObjectKind::reg;
                registerObject.id = reg.id;
                registerObject.source = reg.source;
                registerObject.propertySources = reg.propertySources;
                registerObject.properties = {{"parent", block.id},
                                             {"order", std::to_string(registerIndex)},
                                             {"name", reg.name},
                                             {"offset", uint64Text(reg.offset)},
                                             {"fixed", reg.addressFixed ? "true" : "false"},
                                             {"width", std::to_string(reg.width)},
                                             {"type", std::string(toString(reg.type))},
                                             {"minimum", optionalText(reg.minimumValue)},
                                             {"maximum", optionalText(reg.maximumValue)},
                                             {"initial", optionalText(reg.initialValue)},
                                             {"reset", optionalText(reg.resetValue)},
                                             {"access", std::string(toString(reg.access))},
                                             {"reserved", reg.reserved ? "true" : "false"},
                                             {"tags", stringListText(reg.tags)},
                                             {"description", reg.description}};
                insert(result, std::move(registerObject));

                for (std::size_t enumIndex = 0; enumIndex < reg.enumValues.size(); ++enumIndex) {
                    const auto& enumValue = reg.enumValues[enumIndex];
                    FlatObject enumObject;
                    enumObject.kind = ObjectKind::enumValue;
                    enumObject.id = enumValue.id;
                    enumObject.source = enumValue.source;
                    enumObject.propertySources = enumValue.propertySources;
                    enumObject.properties = {{"parent", reg.id},
                                             {"order", std::to_string(enumIndex)},
                                             {"name", enumValue.name},
                                             {"value", enumValue.value.toHexString()},
                                             {"description", enumValue.description}};
                    insert(result, std::move(enumObject));
                }
                for (std::size_t fieldIndex = 0; fieldIndex < reg.fields.size(); ++fieldIndex) {
                    flattenField(reg.fields[fieldIndex], reg.id, fieldIndex);
                }
            }
        }
    }
    return result;
}

[[nodiscard]] const std::string& property(const FlatObject& object, std::string_view name)
{
    static const std::string empty;
    const auto iterator = object.properties.find(std::string(name));
    return iterator == object.properties.end() ? empty : iterator->second;
}

[[nodiscard]] std::uint64_t parseUInt64(const FlatObject& object, std::string_view name)
{
    const auto parsed = UnsignedValue::parse(property(object, name));
    return parsed ? parsed->toUInt64().value_or(0) : 0;
}

[[nodiscard]] std::uint32_t parseUInt32(const FlatObject& object, std::string_view name)
{
    return static_cast<std::uint32_t>(parseUInt64(object, name));
}

[[nodiscard]] std::optional<UnsignedValue> parseOptionalUnsigned(const FlatObject& object,
                                                                 std::string_view name)
{
    const std::string& value = property(object, name);
    return value.empty() ? std::nullopt : UnsignedValue::parse(value);
}

[[nodiscard]] std::optional<std::uint64_t> parseOptionalUInt64(const FlatObject& object,
                                                               std::string_view name)
{
    const std::string& value = property(object, name);
    if (value.empty()) {
        return std::nullopt;
    }
    const auto parsed = UnsignedValue::parse(value);
    return parsed ? parsed->toUInt64() : std::nullopt;
}

[[nodiscard]] std::size_t order(const FlatObject& object)
{
    const auto parsed = UnsignedValue::parse(property(object, "order"));
    return parsed ? static_cast<std::size_t>(parsed->toUInt64().value_or(0)) : 0;
}

template <typename Value>
using Grouped = std::map<ObjectId, std::vector<std::pair<std::size_t, Value>>, std::less<>>;

template <typename Value>
[[nodiscard]] std::vector<Value> takeGroup(Grouped<Value>& groups, const ObjectId& parent)
{
    auto iterator = groups.find(parent);
    if (iterator == groups.end()) {
        return {};
    }
    auto values = std::move(iterator->second);
    groups.erase(iterator);
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return std::tie(left.first, left.second.id) < std::tie(right.first, right.second.id);
    });
    std::vector<Value> result;
    result.reserve(values.size());
    for (auto& [index, value] : values) {
        static_cast<void>(index);
        result.push_back(std::move(value));
    }
    return result;
}

void deriveFieldResets(std::vector<Field>& fields,
                       const std::optional<UnsignedValue>& registerReset,
                       std::uint64_t registerWidth,
                       const SourceLocation& resetSource,
                       std::uint64_t parentLsb = 0)
{
    for (auto& field : fields) {
        const std::uint64_t width = field.width();
        const bool offsetValid =
            field.lsb <= std::numeric_limits<std::uint64_t>::max() - parentLsb;
        const std::uint64_t absoluteLsb = offsetValid
            ? parentLsb + field.lsb
            : std::numeric_limits<std::uint64_t>::max();
        const bool rangeValid = registerReset.has_value() && offsetValid && width > 0 &&
            absoluteLsb <= registerWidth && width <= registerWidth - absoluteLsb;
        if (rangeValid) {
            field.resetValue = registerReset->slice(absoluteLsb, width);
            field.propertySources["reset"] = resetSource;
        } else {
            field.resetValue.reset();
            field.propertySources.erase("reset");
        }
        deriveFieldResets(field.members, registerReset, registerWidth, resetSource, absoluteLsb);
    }
}

[[nodiscard]] std::optional<Workspace> rebuild(const FlatWorkspace& values,
                                               const std::filesystem::path& manifestPath)
{
    const auto workspaceIterator = std::ranges::find_if(
        values, [](const auto& entry) { return entry.second.kind == ObjectKind::workspace; });
    if (workspaceIterator == values.end()) {
        return std::nullopt;
    }

    Grouped<EnumValue> enums;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::enumValue) {
            continue;
        }
        EnumValue value;
        value.id = id;
        value.name = property(object, "name");
        value.value = UnsignedValue::parse(property(object, "value")).value_or(UnsignedValue{});
        value.description = property(object, "description");
        value.source = object.source;
        value.propertySources = object.propertySources;
        enums[property(object, "parent")].emplace_back(order(object), std::move(value));
    }

    Grouped<Field> fields;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::field) {
            continue;
        }
        Field value;
        value.id = id;
        value.name = property(object, "name");
        value.msb = parseUInt32(object, "msb");
        value.lsb = parseUInt32(object, "lsb");
        value.type = parseFieldType(property(object, "type")).value_or(FieldType::bits);
        value.softwareAccess =
            parseAccessMode(property(object, "sw_access")).value_or(AccessMode::readWrite);
        value.hardwareAccess =
            parseAccessMode(property(object, "hw_access")).value_or(AccessMode::none);
        value.readSideEffect = parseReadSideEffect(property(object, "read_side_effect"))
                                   .value_or(ReadSideEffect::none);
        value.writeSideEffect = parseWriteSideEffect(property(object, "write_side_effect"))
                                    .value_or(WriteSideEffect::write);
        const std::string minimum = property(object, "minimum");
        const std::string maximum = property(object, "maximum");
        value.minimumValue = minimum.empty() ? std::nullopt : std::optional{minimum};
        value.maximumValue = maximum.empty() ? std::nullopt : std::optional{maximum};
        value.description = property(object, "description");
        value.enumValues = takeGroup(enums, id);
        value.source = object.source;
        value.propertySources = object.propertySources;
        value.propertySources.erase("reset");
        fields[property(object, "parent")].emplace_back(order(object), std::move(value));
    }

    std::function<std::vector<Field>(const ObjectId&)> takeFields;
    takeFields = [&](const ObjectId& parent) {
        std::vector<Field> result = takeGroup(fields, parent);
        for (auto& field : result) {
            field.members = takeFields(field.id);
        }
        return result;
    };

    Grouped<Register> registers;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::reg) {
            continue;
        }
        Register value;
        value.id = id;
        value.name = property(object, "name");
        value.offset = parseUInt64(object, "offset");
        value.addressFixed = parseBoolean(property(object, "fixed"));
        value.width = parseUInt32(object, "width");
        value.array.count = 1;
        value.array.stride = 4;
        const std::string minimum = property(object, "minimum");
        const std::string maximum = property(object, "maximum");
        value.minimumValue = minimum.empty() ? std::nullopt : std::optional{minimum};
        value.maximumValue = maximum.empty() ? std::nullopt : std::optional{maximum};
        value.initialValue = parseOptionalUnsigned(object, "initial");
        value.resetValue = parseOptionalUnsigned(object, "reset");
        value.access = parseAccessMode(property(object, "access")).value_or(AccessMode::readWrite);
        value.reserved = parseBoolean(property(object, "reserved"));
        value.tags = parseStringList(property(object, "tags"));
        value.description = property(object, "description");
        value.enumValues = takeGroup(enums, id);
        value.fields = takeFields(id);
        value.type =
            parseFieldType(property(object, "type"))
                .value_or(value.reserved ? FieldType::reserved
                                         : (value.fields.empty() ? FieldType::unsignedInteger
                                                                 : FieldType::structure));
        value.source = object.source;
        value.propertySources = object.propertySources;
        value.propertySources.erase("array_count");
        value.propertySources.erase("stride");
        const auto resetSource = value.propertySources.find("reset");
        deriveFieldResets(
            value.fields,
            value.resetValue,
            value.width,
            resetSource == value.propertySources.end() ? value.source : resetSource->second);
        registers[property(object, "parent")].emplace_back(order(object), std::move(value));
    }

    Grouped<RegisterBlock> blocks;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::registerBlock) {
            continue;
        }
        RegisterBlock value;
        value.id = id;
        value.name = property(object, "name");
        value.baseAddress = parseUInt64(object, "base");
        value.size = parseOptionalUInt64(object, "size");
        value.description = property(object, "description");
        value.registers = takeGroup(registers, id);
        value.source = object.source;
        value.propertySources = object.propertySources;
        blocks[property(object, "parent")].emplace_back(order(object), std::move(value));
    }

    Grouped<AddressSpace> spaces;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::addressSpace) {
            continue;
        }
        AddressSpace value;
        value.id = id;
        value.name = property(object, "name");
        value.baseAddress = parseUInt64(object, "base");
        value.addressWidth = parseUInt32(object, "address_width");
        value.description = property(object, "description");
        value.blocks = takeGroup(blocks, id);
        value.source = object.source;
        value.propertySources = object.propertySources;
        spaces[property(object, "parent")].emplace_back(order(object), std::move(value));
    }

    Workspace workspace;
    workspace.id = workspaceIterator->second.id;
    workspace.name = property(workspaceIterator->second, "name");
    workspace.manifestPath = manifestPath;
    workspace.addressSpaces = takeGroup(spaces, workspace.id);
    return workspace;
}

[[nodiscard]] bool sameObject(const FlatObject& left, const FlatObject& right)
{
    return left.kind == right.kind && left.properties == right.properties;
}

[[nodiscard]] std::string objectName(const FlatObject* first, const FlatObject* second)
{
    const FlatObject* value = first != nullptr ? first : second;
    return value == nullptr ? std::string{} : property(*value, "name");
}

[[nodiscard]] std::optional<std::string> optionalProperty(const FlatObject* object,
                                                          std::string_view name)
{
    if (object == nullptr) {
        return std::nullopt;
    }
    const auto iterator = object->properties.find(std::string(name));
    return iterator == object->properties.end() ? std::optional<std::string>{}
                                                : std::optional{iterator->second};
}

void appendConflict(std::vector<MergeConflict>& conflicts, const ObjectId& id, ObjectKind kind,
                    const FlatObject* left, const FlatObject* right, std::string propertyName,
                    std::optional<std::string> baseValue, std::optional<std::string> leftValue,
                    std::optional<std::string> rightValue)
{
    conflicts.push_back(MergeConflict{id, kind, objectName(left, right), std::move(propertyName),
                                      std::move(baseValue), std::move(leftValue),
                                      std::move(rightValue)});
}

[[nodiscard]] QJsonObject jsonObject(const FlatObject& object)
{
    QJsonObject properties;
    for (const auto& [name, value] : object.properties) {
        properties.insert(QString::fromStdString(name), QString::fromStdString(value));
    }
    QJsonObject result;
    result.insert(QStringLiteral("kind"), QString::fromStdString(kindToken(object.kind)));
    result.insert(QStringLiteral("id"), QString::fromStdString(object.id));
    result.insert(QStringLiteral("properties"), properties);
    return result;
}

[[nodiscard]] std::optional<FlatObject> flatObject(const QJsonObject& json)
{
    const auto kind = parseKind(json.value(QStringLiteral("kind")).toString().toStdString());
    const std::string id = json.value(QStringLiteral("id")).toString().toStdString();
    if (!kind || id.empty() || !json.value(QStringLiteral("properties")).isObject()) {
        return std::nullopt;
    }
    FlatObject result;
    result.kind = *kind;
    result.id = id;
    const QJsonObject properties = json.value(QStringLiteral("properties")).toObject();
    for (auto iterator = properties.begin(); iterator != properties.end(); ++iterator) {
        if (!iterator.value().isString()) {
            return std::nullopt;
        }
        result.properties.insert_or_assign(iterator.key().toStdString(),
                                           iterator.value().toString().toStdString());
    }
    return result;
}

[[nodiscard]] std::optional<std::string> validateFlatWorkspace(const FlatWorkspace& values)
{
    const FlatObject* root = nullptr;
    for (const auto& [id, object] : values) {
        if (object.kind != ObjectKind::workspace) {
            continue;
        }
        if (root != nullptr) {
            return "The synchronized state contains multiple workspace objects.";
        }
        root = &object;
        static_cast<void>(id);
    }
    if (root == nullptr) {
        return "The synchronized state has no workspace object.";
    }

    const auto require =
        [](const FlatObject& object,
           std::initializer_list<std::string_view> names) -> std::optional<std::string> {
        for (const auto name : names) {
            if (!object.properties.contains(std::string(name))) {
                return "Object '" + object.id + "' is missing property '" + std::string(name) +
                       "'.";
            }
        }
        return std::nullopt;
    };
    const auto unsignedValue = [](std::string_view value, bool optional = false) {
        return (optional && value.empty()) || UnsignedValue::parse(value).has_value();
    };
    const auto uint64Value = [&](std::string_view value, bool optional = false) {
        if (optional && value.empty()) {
            return true;
        }
        const auto parsed = UnsignedValue::parse(value);
        return parsed && parsed->toUInt64().has_value();
    };
    const auto uint32Value = [&](std::string_view value) {
        const auto parsed = UnsignedValue::parse(value);
        const auto converted = parsed ? parsed->toUInt64() : std::nullopt;
        return converted && *converted <= std::numeric_limits<std::uint32_t>::max();
    };
    const auto invalidValue = [](const FlatObject& object, std::string_view name) {
        return "Object '" + object.id + "' has an invalid '" + std::string(name) + "' property.";
    };

    for (const auto& [id, object] : values) {
        static_cast<void>(id);
        std::optional<std::string> missing;
        switch (object.kind) {
        case ObjectKind::workspace:
            missing = require(object, {"name"});
            break;
        case ObjectKind::addressSpace:
            missing = require(object,
                              {"parent", "order", "name", "base", "address_width", "description"});
            break;
        case ObjectKind::registerBlock:
            missing = require(object, {"parent", "order", "name", "base", "size", "description"});
            break;
        case ObjectKind::reg:
            missing = require(object, {"parent", "order", "name", "offset", "width", "reset",
                                       "access", "description"});
            break;
        case ObjectKind::field:
            missing = require(object, {"parent", "order", "name", "msb", "lsb", "type", "sw_access",
                                       "hw_access", "read_side_effect", "write_side_effect",
                                       "description"});
            break;
        case ObjectKind::enumValue:
            missing = require(object, {"parent", "order", "name", "value", "description"});
            break;
        }
        if (missing) {
            return missing;
        }
        if (object.kind == ObjectKind::workspace) {
            continue;
        }

        const ObjectKind expectedParentKind = [&] {
            switch (object.kind) {
            case ObjectKind::addressSpace:
                return ObjectKind::workspace;
            case ObjectKind::registerBlock:
                return ObjectKind::addressSpace;
            case ObjectKind::reg:
                return ObjectKind::registerBlock;
            case ObjectKind::field:
                return ObjectKind::reg;
            case ObjectKind::enumValue:
                return ObjectKind::field;
            case ObjectKind::workspace:
                return ObjectKind::workspace;
            }
            return ObjectKind::workspace;
        }();
        const auto parent = values.find(property(object, "parent"));
        const bool validParent =
            parent != values.end() &&
            (parent->second.kind == expectedParentKind ||
             (object.kind == ObjectKind::field && parent->second.kind == ObjectKind::field) ||
             (object.kind == ObjectKind::enumValue && parent->second.kind == ObjectKind::reg));
        if (!validParent) {
            return "Object '" + object.id + "' has a missing or invalid parent.";
        }
        if (!uint64Value(property(object, "order"))) {
            return invalidValue(object, "order");
        }

        switch (object.kind) {
        case ObjectKind::addressSpace:
            if (!uint64Value(property(object, "base"))) {
                return invalidValue(object, "base");
            }
            if (!uint32Value(property(object, "address_width"))) {
                return invalidValue(object, "address_width");
            }
            break;
        case ObjectKind::registerBlock:
            if (!uint64Value(property(object, "base"))) {
                return invalidValue(object, "base");
            }
            if (!uint64Value(property(object, "size"), true)) {
                return invalidValue(object, "size");
            }
            break;
        case ObjectKind::reg:
            if (!uint64Value(property(object, "offset")) ||
                !uint32Value(property(object, "width")) ||
                (object.properties.contains("array_count") &&
                 !uint32Value(property(object, "array_count"))) ||
                (object.properties.contains("stride") &&
                 !uint64Value(property(object, "stride"))) ||
                (object.properties.contains("type") && !parseFieldType(property(object, "type"))) ||
                !unsignedValue(property(object, "initial"), true) ||
                !unsignedValue(property(object, "reset"), true) ||
                !parseAccessMode(property(object, "access"))) {
                return invalidValue(object, "register value");
            }
            if (object.properties.contains("reserved") && property(object, "reserved") != "true" &&
                property(object, "reserved") != "false") {
                return invalidValue(object, "reserved");
            }
            if (object.properties.contains("fixed") && property(object, "fixed") != "true" &&
                property(object, "fixed") != "false") {
                return invalidValue(object, "fixed");
            }
            break;
        case ObjectKind::field:
            if (!uint32Value(property(object, "msb")) || !uint32Value(property(object, "lsb")) ||
                !parseFieldType(property(object, "type")) ||
                !parseAccessMode(property(object, "sw_access")) ||
                !parseAccessMode(property(object, "hw_access")) ||
                !unsignedValue(property(object, "reset"), true) ||
                !parseReadSideEffect(property(object, "read_side_effect")) ||
                !parseWriteSideEffect(property(object, "write_side_effect"))) {
                return invalidValue(object, "field value");
            }
            break;
        case ObjectKind::enumValue:
            if (!unsignedValue(property(object, "value"))) {
                return invalidValue(object, "value");
            }
            break;
        case ObjectKind::workspace:
            break;
        }
    }
    return std::nullopt;
}

void addFileDiagnostic(std::vector<Diagnostic>& diagnostics, std::string_view code,
                       std::string message, const std::filesystem::path& path)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.source.workbook = path;
    diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

} // namespace

ThreeWayMergeResult mergeWorkspaces(const Workspace& base, const Workspace& workbench,
                                    const Workspace& rtl, MergePreference preference)
{
    const FlatWorkspace baseValues = flatten(base);
    const FlatWorkspace leftValues = flatten(workbench);
    const FlatWorkspace rightValues = flatten(rtl);
    FlatWorkspace mergedValues;
    ThreeWayMergeResult result;

    std::set<ObjectId, std::less<>> ids;
    for (const auto& [id, value] : baseValues) {
        static_cast<void>(value);
        ids.insert(id);
    }
    for (const auto& [id, value] : leftValues) {
        static_cast<void>(value);
        ids.insert(id);
    }
    for (const auto& [id, value] : rightValues) {
        static_cast<void>(value);
        ids.insert(id);
    }

    for (const auto& id : ids) {
        const auto baseIterator = baseValues.find(id);
        const auto leftIterator = leftValues.find(id);
        const auto rightIterator = rightValues.find(id);
        const FlatObject* baseObject =
            baseIterator == baseValues.end() ? nullptr : &baseIterator->second;
        const FlatObject* leftObject =
            leftIterator == leftValues.end() ? nullptr : &leftIterator->second;
        const FlatObject* rightObject =
            rightIterator == rightValues.end() ? nullptr : &rightIterator->second;
        const ObjectKind kind =
            leftObject != nullptr ? leftObject->kind
                                  : (rightObject != nullptr ? rightObject->kind : baseObject->kind);

        if (baseObject == nullptr) {
            if (leftObject != nullptr && rightObject != nullptr) {
                if (sameObject(*leftObject, *rightObject)) {
                    mergedValues.insert_or_assign(id, *leftObject);
                } else {
                    appendConflict(result.conflicts, id, kind, leftObject, rightObject,
                                   "<addition>", std::nullopt, std::string("added"),
                                   std::string("added differently"));
                    mergedValues.insert_or_assign(
                        id, preference == MergePreference::workbench ? *leftObject : *rightObject);
                }
            } else if (leftObject != nullptr) {
                mergedValues.insert_or_assign(id, *leftObject);
            } else if (rightObject != nullptr) {
                mergedValues.insert_or_assign(id, *rightObject);
            }
            continue;
        }

        if (leftObject == nullptr && rightObject == nullptr) {
            continue;
        }
        if (leftObject == nullptr) {
            if (sameObject(*baseObject, *rightObject)) {
                continue;
            }
            appendConflict(result.conflicts, id, kind, leftObject, rightObject, "<presence>",
                           std::string("present"), std::nullopt, std::string("modified"));
            if (preference == MergePreference::rtl) {
                mergedValues.insert_or_assign(id, *rightObject);
            }
            continue;
        }
        if (rightObject == nullptr) {
            if (sameObject(*baseObject, *leftObject)) {
                continue;
            }
            appendConflict(result.conflicts, id, kind, leftObject, rightObject, "<presence>",
                           std::string("present"), std::string("modified"), std::nullopt);
            if (preference == MergePreference::workbench) {
                mergedValues.insert_or_assign(id, *leftObject);
            }
            continue;
        }

        FlatObject merged = preference == MergePreference::workbench ? *leftObject : *rightObject;
        if (leftObject->kind != rightObject->kind || leftObject->kind != baseObject->kind) {
            appendConflict(result.conflicts, id, kind, leftObject, rightObject, "<kind>",
                           kindToken(baseObject->kind), kindToken(leftObject->kind),
                           kindToken(rightObject->kind));
        }

        std::set<std::string, std::less<>> propertyNames;
        for (const auto& [name, value] : baseObject->properties) {
            static_cast<void>(value);
            propertyNames.insert(name);
        }
        for (const auto& [name, value] : leftObject->properties) {
            static_cast<void>(value);
            propertyNames.insert(name);
        }
        for (const auto& [name, value] : rightObject->properties) {
            static_cast<void>(value);
            propertyNames.insert(name);
        }
        for (const auto& name : propertyNames) {
            const auto baseValue = optionalProperty(baseObject, name);
            const auto leftValue = optionalProperty(leftObject, name);
            const auto rightValue = optionalProperty(rightObject, name);
            std::optional<std::string> selected;
            if (leftValue == rightValue) {
                selected = leftValue;
            } else if (leftValue == baseValue) {
                selected = rightValue;
            } else if (rightValue == baseValue) {
                selected = leftValue;
            } else {
                appendConflict(result.conflicts, id, kind, leftObject, rightObject, name, baseValue,
                               leftValue, rightValue);
                selected = preference == MergePreference::workbench ? leftValue : rightValue;
            }
            if (selected) {
                merged.properties.insert_or_assign(name, *selected);
            } else {
                merged.properties.erase(name);
            }
        }
        mergedValues.insert_or_assign(id, std::move(merged));
    }

    result.merged = rebuild(mergedValues, workbench.manifestPath);
    return result;
}

BaselineLoadResult loadSyncBaseline(const std::filesystem::path& path)
{
    QFile file(fromPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        BaselineLoadResult result;
        addFileDiagnostic(result.diagnostics, baselineReadCode,
                          "Cannot open the synchronization baseline.", path);
        return result;
    }
    return parseWorkspaceState(file.readAll().toStdString(), path);
}

BaselineLoadResult parseWorkspaceState(std::string_view text,
                                       const std::filesystem::path& sourcePath)
{
    BaselineLoadResult result;
    QJsonParseError error;
    const QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        addFileDiagnostic(result.diagnostics, baselineReadCode,
                          "Cannot parse the synchronization baseline: " +
                              error.errorString().toStdString(),
                          sourcePath);
        return result;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1 ||
        !root.value(QStringLiteral("objects")).isArray()) {
        addFileDiagnostic(result.diagnostics, baselineReadCode,
                          "The synchronization baseline has an unsupported schema.", sourcePath);
        return result;
    }

    FlatWorkspace values;
    for (const QJsonValue& value : root.value(QStringLiteral("objects")).toArray()) {
        if (!value.isObject()) {
            addFileDiagnostic(result.diagnostics, baselineReadCode,
                              "The synchronization baseline contains a malformed object.",
                              sourcePath);
            return result;
        }
        auto object = flatObject(value.toObject());
        if (!object || values.contains(object->id)) {
            addFileDiagnostic(
                result.diagnostics, baselineReadCode,
                "The synchronization baseline contains an invalid or duplicate object.",
                sourcePath);
            return result;
        }
        values.insert_or_assign(object->id, std::move(*object));
    }
    if (const auto structuralError = validateFlatWorkspace(values)) {
        addFileDiagnostic(result.diagnostics, baselineReadCode, *structuralError, sourcePath);
        return result;
    }
    result.workspace = rebuild(values, sourcePath);
    if (!result.workspace) {
        addFileDiagnostic(result.diagnostics, baselineReadCode,
                          "The synchronization baseline has no workspace object.", sourcePath);
    }
    return result;
}

std::string serializeWorkspaceState(const Workspace& workspace, bool indented)
{
    QJsonArray objects;
    const FlatWorkspace values = flatten(workspace);
    for (const auto& [id, object] : values) {
        static_cast<void>(id);
        objects.append(jsonObject(object));
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("objects"), objects);
    return QJsonDocument(root)
        .toJson(indented ? QJsonDocument::Indented : QJsonDocument::Compact)
        .toStdString();
}

std::vector<Diagnostic> saveSyncBaseline(const std::filesystem::path& path,
                                         const Workspace& workspace)
{
    const detail::AtomicFileWriter writer(fromPath(path));
    const std::string serialized = serializeWorkspaceState(workspace, true);
    const QByteArray bytes(serialized.data(), static_cast<qsizetype>(serialized.size()));

    std::vector<Diagnostic> diagnostics;
    const QFileInfo information(fromPath(path));
    if (!QDir().mkpath(information.absolutePath())) {
        addFileDiagnostic(diagnostics, baselineWriteCode,
                          "Cannot create the synchronization baseline directory.", path);
        return diagnostics;
    }
    const auto written = writer.write(bytes, QIODevice::WriteOnly | QIODevice::Text);
    if (!written.committed) {
        addFileDiagnostic(diagnostics, baselineWriteCode,
                          "Cannot atomically write the synchronization baseline: " +
                              written.errorText.toStdString(), path);
    }
    return diagnostics;
}

} // namespace regmap
