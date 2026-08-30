#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"
#include "regmap/core/sync_diff.hpp"

#include <optional>
#include <string>
#include <vector>

namespace regmap {

struct WorkspaceChangePlan {
    std::optional<Workspace> workspace;
    std::vector<ModelChange> changes;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] WorkspaceChangePlan planWorkspaceChanges(
    const Workspace& current,
    const Workspace& external,
    const std::vector<std::string>& requestedChangeIds);

} // namespace regmap
