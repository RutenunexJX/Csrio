#pragma once

#include "regmap/core/model.hpp"

#include <cstddef>
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
};

[[nodiscard]] std::vector<ModelChange> diffWorkspaces(
    const Workspace& before,
    const Workspace& after);
[[nodiscard]] std::string_view toString(ObjectKind kind) noexcept;
[[nodiscard]] std::string_view toString(ChangeKind kind) noexcept;

} // namespace regmap
