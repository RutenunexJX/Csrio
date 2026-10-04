#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

class ProjectLoadSnapshot;

class WorkspaceStore {
public:
    using Mutation = std::function<void(Workspace&)>;
    static constexpr std::size_t historyLimit = 256;
    static constexpr std::size_t defaultHistoryByteLimit = 256U * 1024U * 1024U;

    // A candidate is immutable after validation and belongs to one store revision.
    class PreparedEdit {
    public:
        PreparedEdit(PreparedEdit&&) noexcept = default;
        PreparedEdit& operator=(PreparedEdit&&) noexcept = default;
        PreparedEdit(const PreparedEdit&) = delete;
        PreparedEdit& operator=(const PreparedEdit&) = delete;
        [[nodiscard]] const Workspace& workspace() const { return workspace_.value(); }
        [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    private:
        friend class WorkspaceStore;
        PreparedEdit() = default;
        const WorkspaceStore* owner_{};
        std::shared_ptr<const int> identity_;
        std::uint64_t revision_{};
        std::optional<Workspace> workspace_;
        std::string state_;
        std::vector<Diagnostic> diagnostics_;
    };

    WorkspaceStore() = default;
    explicit WorkspaceStore(Workspace workspace);

    void reset(Workspace workspace);
    [[nodiscard]] bool resetLoadedProject(ProjectLoadSnapshot&& loaded);

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
    [[nodiscard]] std::uint64_t validationCount() const noexcept { return validationCount_; }
    [[nodiscard]] std::uint64_t savedRevision() const noexcept { return savedRevision_; }
    [[nodiscard]] std::size_t historyBytes() const noexcept;
    [[nodiscard]] std::uint64_t historyTrimCount() const noexcept { return historyTrimCount_; }
    void setHistoryByteLimit(std::size_t bytes);

    [[nodiscard]] std::optional<PreparedEdit> prepareEdit(const Mutation& mutation);
    [[nodiscard]] std::optional<PreparedEdit> prepareReplacement(
        Workspace candidate, std::uint64_t sourceRevision);
    [[nodiscard]] bool commitPreparedEdit(std::string description, PreparedEdit&& edit);
    [[nodiscard]] std::uint64_t beginUndoGroup(std::string description);
    [[nodiscard]] bool endUndoGroup(std::uint64_t token);

    [[nodiscard]] bool transact(std::string description, const Mutation& mutation,
                                std::uint64_t undoGroup = 0);
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
        std::size_t retainedBytes{};
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
    std::uint64_t validationCount_ {0};
    std::uint64_t savedRevision_ {0};
    std::shared_ptr<const int> identity_ = std::make_shared<const int>(0);
    std::size_t historyByteLimit_{defaultHistoryByteLimit};
    std::uint64_t historyTrimCount_{0};
    struct UndoGroup {
        std::uint64_t token{};
        std::uint64_t lastRevision{};
        std::string description;
        bool hasEntry{false};
    };
    std::optional<UndoGroup> undoGroup_;
    std::uint64_t nextUndoGroup_{0};

    void appendHistory(
        std::vector<HistoryEntry>& history,
        HistoryEntry entry);
    void trimHistory();
    [[nodiscard]] bool commitPreparedEdit(
        std::string description, PreparedEdit&& edit, std::uint64_t undoGroup);
    void resetState(Workspace workspace);
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
