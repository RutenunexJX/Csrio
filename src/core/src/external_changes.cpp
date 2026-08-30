#include "regmap/core/external_changes.hpp"

#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace regmap {
namespace {

struct ObjectPosition {
    ObjectKind kind{ObjectKind::workspace};
    ObjectId parentId;
    std::size_t order{0};
    std::size_t depth{0};
};

using PositionIndex = std::map<ObjectId, ObjectPosition, std::less<>>;

void indexFields(PositionIndex& result,
                 const std::vector<Field>& fields,
                 const ObjectId& parentId,
                 const std::size_t depth)
{
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const Field& field = fields[index];
        result.insert_or_assign(
            field.id,
            ObjectPosition{ObjectKind::field, parentId, index, depth});
        for (std::size_t enumIndex = 0;
             enumIndex < field.enumValues.size(); ++enumIndex) {
            result.insert_or_assign(
                field.enumValues[enumIndex].id,
                ObjectPosition{ObjectKind::enumValue, field.id,
                               enumIndex, depth + 1});
        }
        indexFields(result, field.members, field.id, depth + 1);
    }
}

[[nodiscard]] PositionIndex positions(const Workspace& workspace)
{
    PositionIndex result;
    result.insert_or_assign(
        workspace.id,
        ObjectPosition{ObjectKind::workspace, {}, 0, 0});
    for (std::size_t pageIndex = 0;
         pageIndex < workspace.addressSpaces.size(); ++pageIndex) {
        const AddressSpace& page = workspace.addressSpaces[pageIndex];
        result.insert_or_assign(
            page.id,
            ObjectPosition{ObjectKind::addressSpace, workspace.id,
                           pageIndex, 1});
        for (std::size_t blockIndex = 0;
             blockIndex < page.blocks.size(); ++blockIndex) {
            const RegisterBlock& block = page.blocks[blockIndex];
            result.insert_or_assign(
                block.id,
                ObjectPosition{ObjectKind::registerBlock, page.id,
                               blockIndex, 2});
            for (std::size_t registerIndex = 0;
                 registerIndex < block.registers.size(); ++registerIndex) {
                const Register& reg = block.registers[registerIndex];
                result.insert_or_assign(
                    reg.id,
                    ObjectPosition{ObjectKind::reg, block.id,
                                   registerIndex, 3});
                for (std::size_t enumIndex = 0;
                     enumIndex < reg.enumValues.size(); ++enumIndex) {
                    result.insert_or_assign(
                        reg.enumValues[enumIndex].id,
                        ObjectPosition{ObjectKind::enumValue, reg.id,
                                       enumIndex, 4});
                }
                indexFields(result, reg.fields, reg.id, 4);
            }
        }
    }
    return result;
}

[[nodiscard]] bool isDescendant(const PositionIndex& index,
                                const ObjectId& objectId,
                                const ObjectId& ancestorId)
{
    auto found = index.find(objectId);
    while (found != index.end() && !found->second.parentId.empty()) {
        if (found->second.parentId == ancestorId) {
            return true;
        }
        found = index.find(found->second.parentId);
    }
    return false;
}

void appendError(std::vector<Diagnostic>& diagnostics,
                 std::string code,
                 std::string message,
                 ObjectId objectId = {})
{
    diagnostics.push_back(
        Diagnostic{std::move(code), DiagnosticSeverity::error,
                   std::move(message), std::move(objectId), {}});
}

template <typename Value>
void insertAt(std::vector<Value>& values, Value value, std::size_t order)
{
    const auto position = values.begin() +
        static_cast<std::ptrdiff_t>(std::min(order, values.size()));
    values.insert(position, std::move(value));
}

[[nodiscard]] std::vector<Field>* fieldContainer(
    Workspace& workspace, std::string_view parentId)
{
    if (Register* reg = findRegister(workspace, parentId)) {
        return &reg->fields;
    }
    if (Field* field = findField(workspace, parentId)) {
        return &field->members;
    }
    return nullptr;
}

[[nodiscard]] std::vector<EnumValue>* enumContainer(
    Workspace& workspace, std::string_view parentId)
{
    if (Register* reg = findRegister(workspace, parentId)) {
        return &reg->enumValues;
    }
    if (Field* field = findField(workspace, parentId)) {
        return &field->enumValues;
    }
    return nullptr;
}

[[nodiscard]] bool addExternalObject(Workspace& candidate,
                                     const Workspace& external,
                                     const ModelChange& change)
{
    switch (change.objectKind) {
    case ObjectKind::workspace:
        return false;
    case ObjectKind::addressSpace: {
        const AddressSpace* source = findAddressSpace(external, change.id);
        if (source == nullptr || change.afterParentId != candidate.id) {
            return false;
        }
        AddressSpace value = *source;
        value.blocks.clear();
        insertAt(candidate.addressSpaces, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::registerBlock: {
        const RegisterBlock* source = findRegisterBlock(external, change.id);
        AddressSpace* parent = findAddressSpace(candidate, change.afterParentId);
        if (source == nullptr || parent == nullptr) {
            return false;
        }
        RegisterBlock value = *source;
        value.registers.clear();
        insertAt(parent->blocks, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::reg: {
        const Register* source = findRegister(external, change.id);
        RegisterBlock* parent = findRegisterBlock(candidate, change.afterParentId);
        if (source == nullptr || parent == nullptr) {
            return false;
        }
        Register value = *source;
        value.fields.clear();
        value.enumValues.clear();
        insertAt(parent->registers, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::field: {
        const Field* source = findField(external, change.id);
        std::vector<Field>* parent = fieldContainer(candidate, change.afterParentId);
        if (source == nullptr || parent == nullptr) {
            return false;
        }
        Field value = *source;
        value.members.clear();
        value.enumValues.clear();
        insertAt(*parent, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::enumValue: {
        const EnumValue* source = findEnumValue(external, change.id);
        std::vector<EnumValue>* parent =
            enumContainer(candidate, change.afterParentId);
        if (source == nullptr || parent == nullptr) {
            return false;
        }
        insertAt(*parent, *source, change.afterOrder);
        return true;
    }
    }
    return false;
}

template <typename Value>
void copyIntrinsic(Value& target, const Value& source)
{
    target = source;
}

void copyIntrinsic(AddressSpace& target, const AddressSpace& source)
{
    auto children = std::move(target.blocks);
    target = source;
    target.blocks = std::move(children);
}

void copyIntrinsic(RegisterBlock& target, const RegisterBlock& source)
{
    auto children = std::move(target.registers);
    target = source;
    target.registers = std::move(children);
}

void copyIntrinsic(Register& target, const Register& source)
{
    auto fields = std::move(target.fields);
    auto values = std::move(target.enumValues);
    target = source;
    target.fields = std::move(fields);
    target.enumValues = std::move(values);
}

void copyIntrinsic(Field& target, const Field& source)
{
    auto members = std::move(target.members);
    auto values = std::move(target.enumValues);
    target = source;
    target.members = std::move(members);
    target.enumValues = std::move(values);
}

[[nodiscard]] bool moveExternalObject(Workspace& candidate,
                                      const Workspace& external,
                                      const ModelChange& change)
{
    const bool structural =
        change.beforeParentId != change.afterParentId ||
        change.beforeOrder != change.afterOrder;
    if (!structural) {
        return true;
    }
    switch (change.objectKind) {
    case ObjectKind::workspace:
        return true;
    case ObjectKind::addressSpace: {
        AddressSpace* existing = findAddressSpace(candidate, change.id);
        if (existing == nullptr) return false;
        AddressSpace value = *existing;
        if (!removeObject(candidate, change.id) ||
            change.afterParentId != candidate.id) return false;
        insertAt(candidate.addressSpaces, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::registerBlock: {
        RegisterBlock* existing = findRegisterBlock(candidate, change.id);
        if (existing == nullptr) return false;
        RegisterBlock value = *existing;
        if (!removeObject(candidate, change.id)) return false;
        AddressSpace* parent = findAddressSpace(candidate, change.afterParentId);
        if (parent == nullptr) return false;
        insertAt(parent->blocks, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::reg: {
        Register* existing = findRegister(candidate, change.id);
        if (existing == nullptr) return false;
        Register value = *existing;
        if (!removeObject(candidate, change.id)) return false;
        RegisterBlock* parent = findRegisterBlock(candidate, change.afterParentId);
        if (parent == nullptr) return false;
        insertAt(parent->registers, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::field: {
        Field* existing = findField(candidate, change.id);
        if (existing == nullptr) return false;
        Field value = *existing;
        if (!removeObject(candidate, change.id)) return false;
        std::vector<Field>* parent = fieldContainer(candidate, change.afterParentId);
        if (parent == nullptr) return false;
        insertAt(*parent, std::move(value), change.afterOrder);
        return true;
    }
    case ObjectKind::enumValue: {
        EnumValue* existing = findEnumValue(candidate, change.id);
        if (existing == nullptr) return false;
        EnumValue value = *existing;
        if (!removeObject(candidate, change.id)) return false;
        std::vector<EnumValue>* parent =
            enumContainer(candidate, change.afterParentId);
        if (parent == nullptr) return false;
        insertAt(*parent, std::move(value), change.afterOrder);
        return true;
    }
    }
    static_cast<void>(external);
    return false;
}

[[nodiscard]] bool updateExternalObject(Workspace& candidate,
                                        const Workspace& external,
                                        const ModelChange& change)
{
    if (!moveExternalObject(candidate, external, change)) {
        return false;
    }
    switch (change.objectKind) {
    case ObjectKind::workspace:
        if (change.id != candidate.id || change.id != external.id) return false;
        candidate.name = external.name;
        return true;
    case ObjectKind::addressSpace: {
        AddressSpace* target = findAddressSpace(candidate, change.id);
        const AddressSpace* source = findAddressSpace(external, change.id);
        if (target == nullptr || source == nullptr) return false;
        copyIntrinsic(*target, *source);
        return true;
    }
    case ObjectKind::registerBlock: {
        RegisterBlock* target = findRegisterBlock(candidate, change.id);
        const RegisterBlock* source = findRegisterBlock(external, change.id);
        if (target == nullptr || source == nullptr) return false;
        copyIntrinsic(*target, *source);
        return true;
    }
    case ObjectKind::reg: {
        Register* target = findRegister(candidate, change.id);
        const Register* source = findRegister(external, change.id);
        if (target == nullptr || source == nullptr) return false;
        copyIntrinsic(*target, *source);
        return true;
    }
    case ObjectKind::field: {
        Field* target = findField(candidate, change.id);
        const Field* source = findField(external, change.id);
        if (target == nullptr || source == nullptr) return false;
        copyIntrinsic(*target, *source);
        return true;
    }
    case ObjectKind::enumValue: {
        EnumValue* target = findEnumValue(candidate, change.id);
        const EnumValue* source = findEnumValue(external, change.id);
        if (target == nullptr || source == nullptr) return false;
        copyIntrinsic(*target, *source);
        return true;
    }
    }
    return false;
}

[[nodiscard]] bool hasErrors(const std::vector<Diagnostic>& diagnostics)
{
    return std::ranges::any_of(
        diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
}

} // namespace

bool WorkspaceChangePlan::valid() const noexcept
{
    return workspace.has_value() && !hasErrors(diagnostics);
}

WorkspaceChangePlan planWorkspaceChanges(
    const Workspace& current,
    const Workspace& external,
    const std::vector<std::string>& requestedChangeIds)
{
    WorkspaceChangePlan plan;
    const std::vector<ModelChange> allChanges =
        diffWorkspaces(current, external);
    const PositionIndex before = positions(current);
    const PositionIndex after = positions(external);
    std::map<std::string, const ModelChange*, std::less<>> byStableId;
    std::map<ObjectId, const ModelChange*, std::less<>> byObjectId;
    for (const ModelChange& change : allChanges) {
        byStableId.insert_or_assign(change.stableId, &change);
        byObjectId.insert_or_assign(change.id, &change);
    }

    std::set<std::string, std::less<>> selected;
    for (const std::string& requested : requestedChangeIds) {
        if (!byStableId.contains(requested)) {
            appendError(plan.diagnostics, "RM5400",
                        "The requested external change is stale or no longer exists.");
            continue;
        }
        selected.insert(requested);
    }
    if (selected.empty()) {
        if (plan.diagnostics.empty()) {
            appendError(plan.diagnostics, "RM5400",
                        "Select at least one external change.");
        }
        return plan;
    }

    bool expanded = true;
    while (expanded) {
        expanded = false;
        const std::vector<std::string> snapshot(selected.begin(), selected.end());
        for (const std::string& stableId : snapshot) {
            const ModelChange& change = *byStableId.at(stableId);
            if (change.change != ChangeKind::removed) {
                auto parent = after.find(change.id);
                ObjectId parentId = parent == after.end()
                    ? ObjectId{}
                    : parent->second.parentId;
                while (!parentId.empty()) {
                    const auto dependency = byObjectId.find(parentId);
                    if (dependency != byObjectId.end() &&
                        dependency->second->change == ChangeKind::added) {
                        expanded = selected.insert(
                            dependency->second->stableId).second || expanded;
                    }
                    const auto next = after.find(parentId);
                    parentId = next == after.end()
                        ? ObjectId{}
                        : next->second.parentId;
                }
            }
            const bool subtreeChange =
                change.change == ChangeKind::added ||
                change.change == ChangeKind::removed;
            if (!subtreeChange) {
                continue;
            }
            for (const ModelChange& candidate : allChanges) {
                if (candidate.id == change.id) {
                    continue;
                }
                if (isDescendant(before, candidate.id, change.id) ||
                    isDescendant(after, candidate.id, change.id)) {
                    expanded = selected.insert(candidate.stableId).second || expanded;
                }
            }
        }
    }

    for (const ModelChange& change : allChanges) {
        if (selected.contains(change.stableId)) {
            plan.changes.push_back(change);
        }
    }
    Workspace candidate = current;
    std::vector<ModelChange> additions;
    std::vector<ModelChange> modifications;
    std::vector<ModelChange> removals;
    for (const ModelChange& change : plan.changes) {
        if (change.change == ChangeKind::added) additions.push_back(change);
        else if (change.change == ChangeKind::removed) removals.push_back(change);
        else modifications.push_back(change);
    }
    const auto afterOrder = [](const ModelChange& left,
                               const ModelChange& right) {
        return std::pair{left.afterDepth, left.afterOrder} <
            std::pair{right.afterDepth, right.afterOrder};
    };
    std::ranges::sort(additions, afterOrder);
    std::ranges::sort(modifications, afterOrder);
    std::ranges::sort(
        removals,
        [](const ModelChange& left, const ModelChange& right) {
            return std::pair{left.beforeDepth, left.beforeOrder} >
                std::pair{right.beforeDepth, right.beforeOrder};
        });

    for (const ModelChange& change : additions) {
        if (!addExternalObject(candidate, external, change)) {
            appendError(plan.diagnostics, "RM5401",
                        "A required parent for the external addition is unavailable.",
                        change.id);
        }
    }
    for (const ModelChange& change : modifications) {
        if (!updateExternalObject(candidate, external, change)) {
            appendError(plan.diagnostics, "RM5401",
                        "The external modification could not be applied without losing dependencies.",
                        change.id);
        }
    }
    for (const ModelChange& change : removals) {
        if (change.objectKind == ObjectKind::workspace ||
            !removeObject(candidate, change.id)) {
            appendError(plan.diagnostics, "RM5401",
                        "The external removal could not be applied safely.",
                        change.id);
        }
    }
    if (!hasErrors(plan.diagnostics)) {
        std::vector<Diagnostic> validation = validateWorkspace(candidate);
        plan.diagnostics.insert(
            plan.diagnostics.end(),
            std::make_move_iterator(validation.begin()),
            std::make_move_iterator(validation.end()));
    }
    plan.workspace = std::move(candidate);
    return plan;
}

} // namespace regmap
