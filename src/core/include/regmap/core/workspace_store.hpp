#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

class WorkspaceStore {
public:
    using Mutation = std::function<void(Workspace&)>;
    static constexpr std::size_t historyLimit = 256;

    WorkspaceStore() = default;
    explicit WorkspaceStore(Workspace workspace);

    void reset(Workspace workspace);

    [[nodiscard]] const Workspace* workspace() const noexcept;
    [[nodiscard]] const Workspace* savedWorkspace() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] std::string_view undoText() const noexcept;
    [[nodiscard]] std::string_view redoText() const noexcept;
    [[nodiscard]] std::size_t undoDepth() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

    [[nodiscard]] bool transact(std::string description, const Mutation& mutation);
    [[nodiscard]] bool squashUndoSince(
        std::size_t startingDepth,
        std::string description);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void markSaved();

private:
    struct HistoryEntry {
        Workspace workspace;
        std::string compressedState;
        std::string description;
    };

    std::optional<Workspace> workspace_;
    std::optional<Workspace> savedWorkspace_;
    std::string workspaceState_;
    std::string savedState_;
    bool dirty_{false};
    std::vector<Diagnostic> diagnostics_;
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::uint64_t revision_ {0};

    static void appendHistory(
        std::vector<HistoryEntry>& history,
        HistoryEntry entry);
    void revalidate();
};

[[nodiscard]] ObjectId makeStableObjectId(
    const Workspace& workspace,
    std::string_view prefix);

[[nodiscard]] AddressSpace* findAddressSpace(Workspace& workspace, std::string_view id) noexcept;
[[nodiscard]] RegisterBlock* findRegisterBlock(Workspace& workspace, std::string_view id) noexcept;
[[nodiscard]] Register* findRegister(Workspace& workspace, std::string_view id) noexcept;
[[nodiscard]] Field* findField(Workspace& workspace, std::string_view id) noexcept;
[[nodiscard]] EnumValue* findEnumValue(Workspace& workspace, std::string_view id) noexcept;

[[nodiscard]] const AddressSpace* findAddressSpace(
    const Workspace& workspace,
    std::string_view id) noexcept;
[[nodiscard]] const RegisterBlock* findRegisterBlock(
    const Workspace& workspace,
    std::string_view id) noexcept;
[[nodiscard]] const Register* findRegister(
    const Workspace& workspace,
    std::string_view id) noexcept;
[[nodiscard]] const Field* findField(const Workspace& workspace, std::string_view id) noexcept;
[[nodiscard]] const EnumValue* findEnumValue(
    const Workspace& workspace,
    std::string_view id) noexcept;

[[nodiscard]] bool removeObject(Workspace& workspace, std::string_view id);

} // namespace regmap
