#include "regmap/core/workspace_store.hpp"

#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/validation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace regmap {
namespace {

template <typename Value>
[[nodiscard]] Value* findById(std::vector<Value>& values, std::string_view id) noexcept
{
    const auto iterator = std::ranges::find(values, id, &Value::id);
    return iterator == values.end() ? nullptr : &*iterator;
}

template <typename Value>
[[nodiscard]] const Value* findById(const std::vector<Value>& values, std::string_view id) noexcept
{
    const auto iterator = std::ranges::find(values, id, &Value::id);
    return iterator == values.end() ? nullptr : &*iterator;
}

template <typename Value>
[[nodiscard]] bool eraseById(std::vector<Value>& values, std::string_view id)
{
    const auto oldSize = values.size();
    std::erase_if(values, [&](const Value& value) { return value.id == id; });
    return values.size() != oldSize;
}

void collectFieldIds(const Field& field, std::set<ObjectId, std::less<>>& result)
{
    result.insert(field.id);
    for (const auto& enumValue : field.enumValues) {
        result.insert(enumValue.id);
    }
    for (const auto& member : field.members) {
        collectFieldIds(member, result);
    }
}

[[nodiscard]] Field* findFieldRecursive(std::vector<Field>& fields, std::string_view id) noexcept
{
    for (auto& field : fields) {
        if (field.id == id) {
            return &field;
        }
        if (auto* result = findFieldRecursive(field.members, id)) {
            return result;
        }
    }
    return nullptr;
}

[[nodiscard]] const Field* findFieldRecursive(const std::vector<Field>& fields,
                                              std::string_view id) noexcept
{
    for (const auto& field : fields) {
        if (field.id == id) {
            return &field;
        }
        if (const auto* result = findFieldRecursive(field.members, id)) {
            return result;
        }
    }
    return nullptr;
}

[[nodiscard]] EnumValue* findEnumRecursive(std::vector<Field>& fields, std::string_view id) noexcept
{
    for (auto& field : fields) {
        if (auto* result = findById(field.enumValues, id)) {
            return result;
        }
        if (auto* result = findEnumRecursive(field.members, id)) {
            return result;
        }
    }
    return nullptr;
}

[[nodiscard]] const EnumValue* findEnumRecursive(const std::vector<Field>& fields,
                                                 std::string_view id) noexcept
{
    for (const auto& field : fields) {
        if (const auto* result = findById(field.enumValues, id)) {
            return result;
        }
        if (const auto* result = findEnumRecursive(field.members, id)) {
            return result;
        }
    }
    return nullptr;
}

[[nodiscard]] bool removeFieldObject(std::vector<Field>& fields, std::string_view id)
{
    if (eraseById(fields, id)) {
        return true;
    }
    for (auto& field : fields) {
        if (eraseById(field.enumValues, id) || removeFieldObject(field.members, id)) {
            return true;
        }
    }
    return false;
}

void collectIds(const Workspace& workspace, std::set<ObjectId, std::less<>>& result)
{
    result.insert(workspace.id);
    for (const auto& space : workspace.addressSpaces) {
        result.insert(space.id);
        for (const auto& block : space.blocks) {
            result.insert(block.id);
            for (const auto& reg : block.registers) {
                result.insert(reg.id);
                for (const auto& enumValue : reg.enumValues) {
                    result.insert(enumValue.id);
                }
                for (const auto& field : reg.fields) {
                    collectFieldIds(field, result);
                }
            }
        }
    }
}

[[nodiscard]] std::string normalizedPrefix(std::string_view prefix)
{
    std::string result;
    result.reserve(prefix.size());
    for (const char rawCharacter : prefix) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(std::tolower(character)));
        } else if (!result.empty() && result.back() != '-') {
            result.push_back('-');
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "object" : result;
}

[[nodiscard]] std::uint64_t entropy()
{
    static std::atomic<std::uint64_t> sequence{0};
    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::random_device random;
    return timestamp ^ (static_cast<std::uint64_t>(random()) << 32U) ^ random() ^
           sequence.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

WorkspaceStore::WorkspaceStore(Workspace workspace) { reset(std::move(workspace)); }

void WorkspaceStore::reset(Workspace workspace)
{
    workspace_ = std::move(workspace);
    savedWorkspace_ = workspace_;
    workspaceState_ = serializeWorkspaceState(*workspace_, false);
    savedState_ = workspaceState_;
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    ++revision_;
    revalidate();
}

const Workspace* WorkspaceStore::workspace() const noexcept
{
    return workspace_ ? &*workspace_ : nullptr;
}

const Workspace* WorkspaceStore::savedWorkspace() const noexcept
{
    return savedWorkspace_ ? &*savedWorkspace_ : nullptr;
}

const std::vector<Diagnostic>& WorkspaceStore::diagnostics() const noexcept { return diagnostics_; }

bool WorkspaceStore::dirty() const
{
    if (!workspace_ || !savedWorkspace_) {
        return workspace_.has_value() != savedWorkspace_.has_value();
    }
    return dirty_;
}

bool WorkspaceStore::canUndo() const noexcept { return !undo_.empty(); }

bool WorkspaceStore::canRedo() const noexcept { return !redo_.empty(); }

std::string_view WorkspaceStore::undoText() const noexcept
{
    return undo_.empty() ? std::string_view{} : undo_.back().description;
}

std::string_view WorkspaceStore::redoText() const noexcept
{
    return redo_.empty() ? std::string_view{} : redo_.back().description;
}

std::size_t WorkspaceStore::undoDepth() const noexcept { return undo_.size(); }

std::uint64_t WorkspaceStore::revision() const noexcept { return revision_; }

void WorkspaceStore::appendHistory(
    std::vector<HistoryEntry>& history,
    HistoryEntry entry)
{
    history.push_back(std::move(entry));
    if (history.size() > historyLimit) {
        history.erase(
            history.begin(),
            history.begin() + static_cast<std::ptrdiff_t>(history.size() - historyLimit));
    }
}

bool WorkspaceStore::transact(std::string description, const Mutation& mutation)
{
    if (!workspace_ || !mutation) {
        return false;
    }
    Workspace candidate = *workspace_;
    mutation(candidate);
    std::string candidateState =
        serializeWorkspaceState(candidate, false);
    if (workspaceState_ == candidateState) {
        return false;
    }

    appendHistory(
        undo_,
        HistoryEntry{*workspace_, workspaceState_, std::move(description)});
    workspace_ = std::move(candidate);
    workspaceState_ = std::move(candidateState);
    dirty_ = workspaceState_ != savedState_;
    redo_.clear();
    ++revision_;
    revalidate();
    return true;
}

bool WorkspaceStore::squashUndoSince(
    std::size_t startingDepth,
    std::string description)
{
    if (undo_.size() >= historyLimit || startingDepth >= undo_.size()) {
        return false;
    }

    undo_[startingDepth].description = std::move(description);
    undo_.resize(startingDepth + 1U);
    return true;
}

bool WorkspaceStore::undo()
{
    if (!workspace_ || undo_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    appendHistory(
        redo_,
        HistoryEntry{*workspace_, workspaceState_, entry.description});
    workspace_ = std::move(entry.workspace);
    workspaceState_ = std::move(entry.state);
    dirty_ = workspaceState_ != savedState_;
    ++revision_;
    revalidate();
    return true;
}

bool WorkspaceStore::redo()
{
    if (!workspace_ || redo_.empty()) {
        return false;
    }
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    appendHistory(
        undo_,
        HistoryEntry{*workspace_, workspaceState_, entry.description});
    workspace_ = std::move(entry.workspace);
    workspaceState_ = std::move(entry.state);
    dirty_ = workspaceState_ != savedState_;
    ++revision_;
    revalidate();
    return true;
}

void WorkspaceStore::markSaved()
{
    savedWorkspace_ = workspace_;
    savedState_ = workspaceState_;
    dirty_ = false;
}

void WorkspaceStore::revalidate()
{
    diagnostics_ = workspace_ ? validateWorkspace(*workspace_) : std::vector<Diagnostic>{};
}

ObjectId makeStableObjectId(const Workspace& workspace, std::string_view prefix)
{
    std::set<ObjectId, std::less<>> ids;
    collectIds(workspace, ids);
    const std::string base = normalizedPrefix(prefix);
    for (;;) {
        std::ostringstream candidate;
        candidate << base << '-' << std::hex << std::setfill('0') << std::setw(16) << entropy();
        if (!ids.contains(candidate.str())) {
            return candidate.str();
        }
    }
}

AddressSpace* findAddressSpace(Workspace& workspace, std::string_view id) noexcept
{
    return findById(workspace.addressSpaces, id);
}

RegisterBlock* findRegisterBlock(Workspace& workspace, std::string_view id) noexcept
{
    for (auto& space : workspace.addressSpaces) {
        if (auto* result = findById(space.blocks, id)) {
            return result;
        }
    }
    return nullptr;
}

Register* findRegister(Workspace& workspace, std::string_view id) noexcept
{
    for (auto& space : workspace.addressSpaces) {
        for (auto& block : space.blocks) {
            if (auto* result = findById(block.registers, id)) {
                return result;
            }
        }
    }
    return nullptr;
}

Field* findField(Workspace& workspace, std::string_view id) noexcept
{
    for (auto& space : workspace.addressSpaces) {
        for (auto& block : space.blocks) {
            for (auto& reg : block.registers) {
                if (auto* result = findFieldRecursive(reg.fields, id)) {
                    return result;
                }
            }
        }
    }
    return nullptr;
}

EnumValue* findEnumValue(Workspace& workspace, std::string_view id) noexcept
{
    for (auto& space : workspace.addressSpaces) {
        for (auto& block : space.blocks) {
            for (auto& reg : block.registers) {
                if (auto* result = findById(reg.enumValues, id)) {
                    return result;
                }
                if (auto* result = findEnumRecursive(reg.fields, id)) {
                    return result;
                }
            }
        }
    }
    return nullptr;
}

const AddressSpace* findAddressSpace(const Workspace& workspace, std::string_view id) noexcept
{
    return findById(workspace.addressSpaces, id);
}

const RegisterBlock* findRegisterBlock(const Workspace& workspace, std::string_view id) noexcept
{
    for (const auto& space : workspace.addressSpaces) {
        if (const auto* result = findById(space.blocks, id)) {
            return result;
        }
    }
    return nullptr;
}

const Register* findRegister(const Workspace& workspace, std::string_view id) noexcept
{
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            if (const auto* result = findById(block.registers, id)) {
                return result;
            }
        }
    }
    return nullptr;
}

const Field* findField(const Workspace& workspace, std::string_view id) noexcept
{
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            for (const auto& reg : block.registers) {
                if (const auto* result = findFieldRecursive(reg.fields, id)) {
                    return result;
                }
            }
        }
    }
    return nullptr;
}

const EnumValue* findEnumValue(const Workspace& workspace, std::string_view id) noexcept
{
    for (const auto& space : workspace.addressSpaces) {
        for (const auto& block : space.blocks) {
            for (const auto& reg : block.registers) {
                if (const auto* result = findById(reg.enumValues, id)) {
                    return result;
                }
                if (const auto* result = findEnumRecursive(reg.fields, id)) {
                    return result;
                }
            }
        }
    }
    return nullptr;
}

bool removeObject(Workspace& workspace, std::string_view id)
{
    if (eraseById(workspace.addressSpaces, id)) {
        return true;
    }
    for (auto& space : workspace.addressSpaces) {
        if (eraseById(space.blocks, id)) {
            return true;
        }
        for (auto& block : space.blocks) {
            if (eraseById(block.registers, id)) {
                return true;
            }
            for (auto& reg : block.registers) {
                if (eraseById(reg.enumValues, id) || removeFieldObject(reg.fields, id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace regmap
