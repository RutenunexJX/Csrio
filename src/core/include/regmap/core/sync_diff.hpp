#pragma once

#include "regmap/core/model.hpp"

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
};

[[nodiscard]] std::vector<ModelChange> diffWorkspaces(
    const Workspace& before,
    const Workspace& after);
[[nodiscard]] std::string_view toString(ObjectKind kind) noexcept;
[[nodiscard]] std::string_view toString(ChangeKind kind) noexcept;

} // namespace regmap
