#pragma once

#include "regmap/core/model.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

enum class ObjectKind {
    workspace,
    addressSpace,
    registerBlock,
    reg,
    field,
    enumValue,
};

enum class ChangeKind {
    added,
    removed,
    modified,
};

struct ModelChange {
    ChangeKind change {ChangeKind::modified};
    ObjectKind objectKind {ObjectKind::workspace};
    ObjectId id;
    std::string name;
    std::string summary;
    SourceLocation beforeSource;
    SourceLocation afterSource;
    std::string stableId;
    ObjectId beforeParentId;
    ObjectId afterParentId;
    std::size_t beforeOrder{0};
    std::size_t afterOrder{0};
    std::size_t beforeDepth{0};
    std::size_t afterDepth{0};
    std::vector<ObjectId> dependencies;
    bool operator==(const ModelChange&) const = default;
};

// Owns comparison data; never retains pointers into a mutable Workspace.
class WorkspaceDiffSnapshot {
public:
    explicit WorkspaceDiffSnapshot(const Workspace& workspace);
private:
    struct Data;
    std::shared_ptr<const Data> data_;
    friend std::vector<ModelChange> diffWorkspaces(
        const WorkspaceDiffSnapshot&, const WorkspaceDiffSnapshot&);
};

[[nodiscard]] std::vector<ModelChange> diffWorkspaces(
    const WorkspaceDiffSnapshot& before, const WorkspaceDiffSnapshot& after);

[[nodiscard]] std::vector<ModelChange> diffWorkspaces(
    const Workspace& before,
    const Workspace& after);
[[nodiscard]] std::string_view toString(ObjectKind kind) noexcept;
[[nodiscard]] std::string_view toString(ChangeKind kind) noexcept;

} // namespace regmap
