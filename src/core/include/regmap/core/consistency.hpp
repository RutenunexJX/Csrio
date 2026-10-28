#pragma once
#include "regmap/core/model_inspection.hpp"
#include "regmap/core/diagnostic.hpp"
#include <optional>
namespace regmap {
struct ObjectSelector {
    std::string kind{"all"};
    std::vector<std::string> ids, names, parents, tags, excludeIds;
    bool recursive{false};
};
struct ObjectSelection {
    std::vector<ObjectId> ids;
    std::string error;
};
[[nodiscard]] ObjectSelection selectObjects(const InspectedModel&, const ObjectSelector&);
[[nodiscard]] std::string inspectedProperty(const InspectedObject&, std::string_view);
[[nodiscard]] std::string inspectedPage(const InspectedModel&, const ObjectId&);
[[nodiscard]] std::string inspectedPath(const InspectedModel&, const ObjectId&);
struct ConsistencyRule {
    std::string id;
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::optional<ObjectSelector> scope;
    ObjectSelector select;
    std::optional<std::size_t> count;
    std::map<std::string, std::string, std::less<>> properties;
    std::vector<std::string> same, enumValues;
};
struct ConsistencyFinding {
    std::string ruleId, objectId, pageId, path, property, expected, actual;
    DiagnosticSeverity severity{DiagnosticSeverity::error};
};
[[nodiscard]] std::vector<ConsistencyFinding> validateConsistencyRules(
    const Workspace&, const std::vector<ConsistencyRule>&);
} // namespace regmap
