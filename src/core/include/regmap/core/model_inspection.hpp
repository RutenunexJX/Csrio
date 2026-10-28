#pragma once
#include "regmap/core/sync_diff.hpp"
#include <map>
namespace regmap {
struct InspectedObject {
    ObjectKind kind{ObjectKind::workspace};
    ObjectId id;
    std::map<std::string, std::string, std::less<>> properties;
    SourceLocation source;
    PropertySources propertySources;
};
using InspectedModel = std::map<ObjectId, InspectedObject, std::less<>>;
[[nodiscard]] InspectedModel inspectModel(const Workspace& workspace);
} // namespace regmap
