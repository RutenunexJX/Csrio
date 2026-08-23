#include "regmap/core/sync_diff.hpp"

#include <cstddef>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace regmap {
namespace {

struct Snapshot {
    ObjectKind kind{ObjectKind::workspace};
    ObjectId id;
    std::string name;
    std::string fingerprint;
    ObjectId parent;
    std::size_t order{0};
    SourceLocation source;
};

void appendString(std::ostringstream& output, std::string_view value)
{
    output << value.size() << ':' << value << ';';
}

void appendValue(std::ostringstream& output, const std::optional<UnsignedValue>& value)
{
    appendString(output, value ? value->toHexString(false) : std::string_view{});
}

template <typename Enum> void appendEnum(std::ostringstream& output, Enum value)
{
    output << static_cast<int>(value) << ';';
}

[[nodiscard]] std::string fingerprint(const AddressSpace& value)
{
    std::ostringstream output;
    appendString(output, value.name);
    output << value.baseAddress << ';' << value.addressWidth << ';';
    appendString(output, value.description);
    return output.str();
}

[[nodiscard]] std::string fingerprint(const RegisterBlock& value)
{
    std::ostringstream output;
    appendString(output, value.name);
    output << value.baseAddress << ';';
    if (value.size) {
        output << *value.size;
    }
    output << ';';
    appendString(output, value.description);
    return output.str();
}

[[nodiscard]] std::string fingerprint(const Register& value)
{
    std::ostringstream output;
    appendString(output, value.name);
    output << value.offset << ';' << value.addressFixed << ';' << value.width << ';';
    appendEnum(output, value.type);
    appendString(output, value.minimumValue.value_or(std::string{}));
    appendString(output, value.maximumValue.value_or(std::string{}));
    appendValue(output, value.initialValue);
    appendValue(output, value.resetValue);
    appendEnum(output, value.access);
    output << value.reserved << ';';
    for (const auto& tag : value.tags) {
        appendString(output, tag);
    }
    appendString(output, value.description);
    return output.str();
}

[[nodiscard]] std::string fingerprint(const Field& value)
{
    std::ostringstream output;
    appendString(output, value.name);
    output << value.msb << ';' << value.lsb << ';';
    appendEnum(output, value.type);
    appendEnum(output, value.softwareAccess);
    appendEnum(output, value.hardwareAccess);
    appendEnum(output, value.readSideEffect);
    appendEnum(output, value.writeSideEffect);
    appendString(output, value.description);
    appendString(output, value.minimumValue.value_or(std::string{}));
    appendString(output, value.maximumValue.value_or(std::string{}));
    return output.str();
}

[[nodiscard]] std::string fingerprint(const EnumValue& value)
{
    std::ostringstream output;
    appendString(output, value.name);
    appendString(output, value.value.toHexString(false));
    appendString(output, value.description);
    return output.str();
}

void insertSnapshot(std::map<ObjectId, Snapshot, std::less<>>& values, Snapshot snapshot)
{
    values.insert_or_assign(snapshot.id, std::move(snapshot));
}

void insertFieldSnapshots(std::map<ObjectId, Snapshot, std::less<>>& values,
                          const std::vector<Field>& fields, const ObjectId& parent)
{
    for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
        const auto& field = fields[fieldIndex];
        insertSnapshot(
            values,
            Snapshot{ObjectKind::field, field.id, field.name, fingerprint(field), parent,
                     fieldIndex, field.source});
        for (std::size_t enumIndex = 0; enumIndex < field.enumValues.size(); ++enumIndex) {
            const auto& enumValue = field.enumValues[enumIndex];
            insertSnapshot(values, Snapshot{ObjectKind::enumValue, enumValue.id, enumValue.name,
                                            fingerprint(enumValue), field.id, enumIndex,
                                            enumValue.source});
        }
        insertFieldSnapshots(values, field.members, field.id);
    }
}

[[nodiscard]] std::map<ObjectId, Snapshot, std::less<>> snapshots(const Workspace& workspace)
{
    std::map<ObjectId, Snapshot, std::less<>> result;
    std::ostringstream workspaceFingerprint;
    appendString(workspaceFingerprint, workspace.name);
    insertSnapshot(result, Snapshot{ObjectKind::workspace, workspace.id, workspace.name,
                                    workspaceFingerprint.str(), {}, 0, {}});

    for (std::size_t spaceIndex = 0; spaceIndex < workspace.addressSpaces.size(); ++spaceIndex) {
        const auto& addressSpace = workspace.addressSpaces[spaceIndex];
        insertSnapshot(result,
                       Snapshot{ObjectKind::addressSpace, addressSpace.id, addressSpace.name,
                                fingerprint(addressSpace), workspace.id, spaceIndex,
                                addressSpace.source});
        for (std::size_t blockIndex = 0; blockIndex < addressSpace.blocks.size(); ++blockIndex) {
            const auto& block = addressSpace.blocks[blockIndex];
            insertSnapshot(result, Snapshot{ObjectKind::registerBlock, block.id, block.name,
                                            fingerprint(block), addressSpace.id, blockIndex,
                                            block.source});
            for (std::size_t registerIndex = 0; registerIndex < block.registers.size();
                 ++registerIndex) {
                const auto& reg = block.registers[registerIndex];
                insertSnapshot(result, Snapshot{ObjectKind::reg, reg.id, reg.name, fingerprint(reg),
                                                block.id, registerIndex, reg.source});
                for (std::size_t enumIndex = 0; enumIndex < reg.enumValues.size(); ++enumIndex) {
                    const auto& enumValue = reg.enumValues[enumIndex];
                    insertSnapshot(result,
                                   Snapshot{ObjectKind::enumValue, enumValue.id, enumValue.name,
                                            fingerprint(enumValue), reg.id, enumIndex,
                                            enumValue.source});
                }
                insertFieldSnapshots(result, reg.fields, reg.id);
            }
        }
    }
    return result;
}

} // namespace

std::vector<ModelChange> diffWorkspaces(const Workspace& before, const Workspace& after)
{
    const auto oldSnapshots = snapshots(before);
    const auto newSnapshots = snapshots(after);
    std::vector<ModelChange> result;

    auto oldIterator = oldSnapshots.begin();
    auto newIterator = newSnapshots.begin();
    while (oldIterator != oldSnapshots.end() || newIterator != newSnapshots.end()) {
        if (newIterator == newSnapshots.end() ||
            (oldIterator != oldSnapshots.end() && oldIterator->first < newIterator->first)) {
            const Snapshot& value = oldIterator->second;
            result.push_back(ModelChange{ChangeKind::removed,
                                         value.kind,
                                         value.id,
                                         value.name,
                                         "Removed",
                                         value.source,
                                         {}});
            ++oldIterator;
            continue;
        }
        if (oldIterator == oldSnapshots.end() || newIterator->first < oldIterator->first) {
            const Snapshot& value = newIterator->second;
            result.push_back(ModelChange{
                ChangeKind::added, value.kind, value.id, value.name, "Added", {}, value.source});
            ++newIterator;
            continue;
        }

        const Snapshot& oldValue = oldIterator->second;
        const Snapshot& newValue = newIterator->second;
        const bool typeChanged = oldValue.kind != newValue.kind;
        const bool propertiesChanged = oldValue.fingerprint != newValue.fingerprint;
        const bool structureChanged = oldValue.parent != newValue.parent ||
            oldValue.order != newValue.order;
        if (typeChanged || propertiesChanged || structureChanged) {
            std::string summary;
            if (typeChanged) {
                summary = "Object type changed";
            } else if (propertiesChanged && structureChanged) {
                summary = "Properties changed; moved or reordered";
            } else if (propertiesChanged) {
                summary = "Properties changed";
            } else {
                summary = "Moved or reordered";
            }
            result.push_back(ModelChange{
                ChangeKind::modified, newValue.kind, newValue.id, newValue.name,
                std::move(summary), oldValue.source, newValue.source});
        }
        ++oldIterator;
        ++newIterator;
    }
    return result;
}

std::string_view toString(ObjectKind kind) noexcept
{
    switch (kind) {
    case ObjectKind::workspace:
        return "Workspace";
    case ObjectKind::addressSpace:
        return "Page";
    case ObjectKind::registerBlock:
        return "Register Block";
    case ObjectKind::reg:
        return "Register";
    case ObjectKind::field:
        return "Field";
    case ObjectKind::enumValue:
        return "Enum Value";
    }
    return "Unknown";
}

std::string_view toString(ChangeKind kind) noexcept
{
    switch (kind) {
    case ChangeKind::added:
        return "Added";
    case ChangeKind::removed:
        return "Removed";
    case ChangeKind::modified:
        return "Modified";
    }
    return "Unknown";
}

} // namespace regmap
