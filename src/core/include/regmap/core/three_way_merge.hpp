#pragma once

#include "regmap/core/diagnostic.hpp"
#include "regmap/core/model.hpp"
#include "regmap/core/sync_diff.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace regmap {

enum class MergePreference {
    workbench,
    rtl,
};

struct MergeConflict {
    ObjectId objectId;
    ObjectKind objectKind {ObjectKind::workspace};
    std::string objectName;
    std::string property;
    std::optional<std::string> baseValue;
    std::optional<std::string> workbenchValue;
    std::optional<std::string> rtlValue;
};

struct ThreeWayMergeResult {
    std::optional<Workspace> merged;
    std::vector<MergeConflict> conflicts;
};

[[nodiscard]] ThreeWayMergeResult mergeWorkspaces(
    const Workspace& base,
    const Workspace& workbench,
    const Workspace& rtl,
    MergePreference preference = MergePreference::workbench);

struct BaselineLoadResult {
    std::optional<Workspace> workspace;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] BaselineLoadResult loadSyncBaseline(const std::filesystem::path& path);
[[nodiscard]] BaselineLoadResult parseWorkspaceState(
    std::string_view text,
    const std::filesystem::path& sourcePath = {});
[[nodiscard]] std::string serializeWorkspaceState(
    const Workspace& workspace,
    bool indented = true);
[[nodiscard]] std::vector<Diagnostic> saveSyncBaseline(
    const std::filesystem::path& path,
    const Workspace& workspace);

} // namespace regmap
