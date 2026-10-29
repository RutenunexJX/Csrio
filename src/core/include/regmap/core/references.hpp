#pragma once
#include "regmap/core/model.hpp"

namespace regmap {
struct ReferenceFinding {
    ObjectId targetId;
    std::string targetPath, targetName, change;
    ObjectId objectId;
    std::string path, property, kind, status, snippet;
    std::vector<ObjectId> candidates;
    bool ambiguous{false};
};

[[nodiscard]] std::vector<ReferenceFinding> findReferences(
    const Workspace& workspace, const std::vector<ObjectId>& targets);
[[nodiscard]] std::vector<ReferenceFinding> checkReferenceChanges(
    const Workspace& before, const Workspace& after);
} // namespace regmap
