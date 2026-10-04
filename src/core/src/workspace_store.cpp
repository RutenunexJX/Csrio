#include "regmap/core/workspace_store.hpp"
#include "regmap/core/project.hpp"

#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/validation.hpp"

#include <QByteArray>

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

// Retained allocation estimate, deliberately including string capacity and tree
// nodes. It is a history budget, not a process working-set measurement.
std::size_t sourceBytes(const SourceLocation& source)
{
    return source.workbook.native().capacity() * sizeof(std::filesystem::path::value_type)
        + source.sheet.capacity() + source.cell.capacity();
}

template <typename Object> std::size_t objectBytes(const Object& value)
{
    std::size_t bytes = value.id.capacity() + value.name.capacity()
        + value.description.capacity() + sourceBytes(value.source);
    for (const auto& [key, source] : value.propertySources) {
        bytes += sizeof(PropertySources::value_type) + 4U * sizeof(void*)
            + key.capacity() + sourceBytes(source);
    }
    return bytes;
}

std::size_t valueBytes(const std::optional<UnsignedValue>& value)
{
    return value ? 2U * ((value->bitWidth() + 31U) / 32U) * sizeof(std::uint32_t) : 0U;
}

template <typename Object> std::size_t rangeBytes(const Object& value)
{
    return (value.minimumValue ? value.minimumValue->capacity() : 0U)
        + (value.maximumValue ? value.maximumValue->capacity() : 0U);
}

std::size_t enumBytes(const std::vector<EnumValue>& values)
{
    std::size_t bytes = values.capacity() * sizeof(EnumValue);
    for (const auto& value : values) {
        bytes += objectBytes(value) + 2U * ((value.value.bitWidth() + 31U) / 32U)
            * sizeof(std::uint32_t);
    }
    return bytes;
}

std::size_t fieldBytes(const std::vector<Field>& fields)
{
    std::size_t bytes = fields.capacity() * sizeof(Field);
    for (const auto& field : fields) {
        bytes += objectBytes(field) + rangeBytes(field) + valueBytes(field.resetValue)
            + enumBytes(field.enumValues) + fieldBytes(field.members);
    }
    return bytes;
}

std::size_t workspaceBytes(const Workspace& workspace)
{
    std::size_t bytes = sizeof(Workspace) + workspace.id.capacity() + workspace.name.capacity()
        + workspace.manifestPath.native().capacity() * sizeof(std::filesystem::path::value_type)
        + workspace.addressSpaces.capacity() * sizeof(AddressSpace);
    for (const auto& page : workspace.addressSpaces) {
        bytes += objectBytes(page) + page.blocks.capacity() * sizeof(RegisterBlock);
        for (const auto& block : page.blocks) {
            bytes += objectBytes(block) + block.registers.capacity() * sizeof(Register);
            for (const auto& reg : block.registers) {
                bytes += objectBytes(reg) + rangeBytes(reg) + valueBytes(reg.initialValue)
                    + valueBytes(reg.resetValue) + enumBytes(reg.enumValues) + fieldBytes(reg.fields)
                    + reg.tags.capacity() * sizeof(std::string);
                for (const auto& tag : reg.tags) bytes += tag.capacity();
            }
        }
    }
    return bytes;
}

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
    resetState(std::move(workspace));
    revalidate();
}

bool WorkspaceStore::resetLoadedProject(ProjectLoadSnapshot&& loaded)
{
    if (!loaded.workspace_) return false;
    resetState(std::move(*loaded.workspace_));
    loaded.workspace_.reset();
    diagnostics_ = std::move(loaded.modelDiagnostics_);
    return true;
}

void WorkspaceStore::resetState(Workspace workspace)
{
    workspace_ = std::move(workspace);
    savedWorkspace_ = workspace_;
    workspaceState_ = serializeWorkspaceState(*workspace_, false);
    savedState_ = workspaceState_;
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    undoGroup_.reset();
    historyTrimCount_ = 0;
    ++savedRevision_;
    ++revision_;
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
    entry.retainedBytes = workspaceBytes(entry.workspace) + entry.compressedState.capacity()
        + entry.description.capacity() + sizeof(HistoryEntry);
    history.push_back(std::move(entry));
    if (history.size() > historyLimit) {
        historyTrimCount_ += history.size() - historyLimit;
        history.erase(
            history.begin(),
            history.begin() + static_cast<std::ptrdiff_t>(history.size() - historyLimit));
    }
    trimHistory();
}

std::size_t WorkspaceStore::historyBytes() const noexcept
{
    std::size_t bytes = 0;
    for (const auto& entry : undo_) bytes += entry.retainedBytes;
    for (const auto& entry : redo_) bytes += entry.retainedBytes;
    return bytes;
}

void WorkspaceStore::setHistoryByteLimit(std::size_t bytes)
{
    historyByteLimit_ = bytes;
    trimHistory();
}

void WorkspaceStore::trimHistory()
{
    auto bytes = historyBytes();
    // Retain the nearest undo/redo step even when one snapshot exceeds the budget.
    while (bytes > historyByteLimit_ && undo_.size() + redo_.size() > 1U) {
        auto& history = !undo_.empty() && (undo_.size() > 1U || redo_.empty()) ? undo_ : redo_;
        bytes -= history.front().retainedBytes;
        history.erase(history.begin());
        ++historyTrimCount_;
    }
}

std::optional<WorkspaceStore::PreparedEdit> WorkspaceStore::prepareEdit(const Mutation& mutation)
{
    if (!workspace_ || !mutation) return std::nullopt;
    const auto sourceRevision = revision_;
    Workspace candidate = *workspace_;
    mutation(candidate);
    return prepareReplacement(std::move(candidate), sourceRevision);
}

std::optional<WorkspaceStore::PreparedEdit> WorkspaceStore::prepareReplacement(
    Workspace candidate, std::uint64_t sourceRevision)
{
    if (!workspace_ || sourceRevision != revision_) return std::nullopt;
    PreparedEdit edit;
    edit.owner_ = this;
    edit.identity_ = identity_;
    edit.revision_ = revision_;
    edit.state_ = serializeWorkspaceState(candidate, false);
    if (edit.state_ == workspaceState_) {
        edit.diagnostics_ = diagnostics_;
    } else {
        ++validationCount_;
        edit.diagnostics_ = validateWorkspace(candidate);
    }
    edit.workspace_ = std::move(candidate);
    return edit;
}

bool WorkspaceStore::transact(std::string description, const Mutation& mutation,
                              std::uint64_t undoGroup)
{
    auto edit = prepareEdit(mutation);
    return edit && commitPreparedEdit(std::move(description), std::move(*edit), undoGroup);
}

bool WorkspaceStore::commitPreparedEdit(std::string description, PreparedEdit&& edit)
{
    return commitPreparedEdit(std::move(description), std::move(edit), 0);
}

bool WorkspaceStore::commitPreparedEdit(
    std::string description, PreparedEdit&& edit, std::uint64_t undoGroup)
{
    if (edit.owner_ != this || edit.identity_ != identity_ || edit.revision_ != revision_
        || !edit.workspace_ || !workspace_) return false;
    edit.identity_.reset(); // Consumed even if this is a no-op.
    if (workspaceState_ == edit.state_) return false;

    const bool grouped = undoGroup_ && undoGroup != 0 && undoGroup_->token == undoGroup
        && undoGroup_->lastRevision == revision_;
    if (!grouped) undoGroup_.reset();
    redo_.clear();
    if (!grouped || !undoGroup_->hasEntry || undo_.empty()) {
        const QByteArray compressed = qCompress(
            reinterpret_cast<const uchar*>(workspaceState_.data()),
            static_cast<qsizetype>(workspaceState_.size()), 1);
        appendHistory(undo_, HistoryEntry{std::move(*workspace_), compressed.toStdString(),
            grouped ? undoGroup_->description : std::move(description)});
    }

    workspace_ = std::move(edit.workspace_);
    edit.workspace_.reset();
    workspaceState_ = std::move(edit.state_);
    diagnostics_ = std::move(edit.diagnostics_);
    dirty_ = workspaceState_ != savedState_;
    ++revision_;
    if (grouped) {
        undoGroup_->hasEntry = true;
        undoGroup_->lastRevision = revision_;
    }
    return true;
}

std::uint64_t WorkspaceStore::beginUndoGroup(std::string description)
{
    const auto token = ++nextUndoGroup_;
    undoGroup_ = UndoGroup{token, revision_, std::move(description), false};
    return token;
}

bool WorkspaceStore::endUndoGroup(std::uint64_t token)
{
    if (!undoGroup_ || undoGroup_->token != token) return false;
    const bool changed = undoGroup_->hasEntry;
    undoGroup_.reset();
    return changed;
}

bool WorkspaceStore::squashUndoSince(
    std::size_t startingDepth,
    std::string description)
{
    if (historyTrimCount_ != 0 || undo_.size() >= historyLimit || startingDepth >= undo_.size()) {
        return false;
    }

    auto& entry = undo_[startingDepth];
    entry.retainedBytes -= entry.description.capacity();
    entry.description = std::move(description);
    entry.retainedBytes += entry.description.capacity();
    undo_.resize(startingDepth + 1U);
    trimHistory();
    return true;
}

bool WorkspaceStore::undo()
{
    if (!workspace_ || undo_.empty()) {
        return false;
    }
    undoGroup_.reset();
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    const QByteArray compressed = qCompress(
        reinterpret_cast<const uchar*>(workspaceState_.data()),
        static_cast<qsizetype>(workspaceState_.size()), 1);
    appendHistory(
        redo_,
        HistoryEntry{std::move(*workspace_), compressed.toStdString(), std::move(entry.description)});
    workspace_ = std::move(entry.workspace);
    workspaceState_ = qUncompress(
        reinterpret_cast<const uchar*>(entry.compressedState.data()),
        static_cast<qsizetype>(entry.compressedState.size())).toStdString();
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
    undoGroup_.reset();
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    const QByteArray compressed = qCompress(
        reinterpret_cast<const uchar*>(workspaceState_.data()),
        static_cast<qsizetype>(workspaceState_.size()), 1);
    appendHistory(
        undo_,
        HistoryEntry{std::move(*workspace_), compressed.toStdString(), std::move(entry.description)});
    workspace_ = std::move(entry.workspace);
    workspaceState_ = qUncompress(
        reinterpret_cast<const uchar*>(entry.compressedState.data()),
        static_cast<qsizetype>(entry.compressedState.size())).toStdString();
    dirty_ = workspaceState_ != savedState_;
    ++revision_;
    revalidate();
    return true;
}

void WorkspaceStore::markSaved()
{
    undoGroup_.reset();
    ++savedRevision_;
    savedWorkspace_ = workspace_;
    savedState_ = workspaceState_;
    dirty_ = false;
}

void WorkspaceStore::revalidate()
{
    ++validationCount_;
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
