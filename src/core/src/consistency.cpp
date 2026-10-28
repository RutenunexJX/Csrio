#include "regmap/core/consistency.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace regmap {
std::string inspectedProperty(const InspectedObject& object, std::string_view name)
{
    const auto found = object.properties.find(name);
    return found == object.properties.end() ? std::string{} : found->second;
}
namespace {
bool contains(const std::vector<std::string>& values, const std::string& value)
{ return std::ranges::find(values, value) != values.end(); }
bool descendant(const InspectedModel& model, std::string id, const std::string& parent, bool recursive)
{
    std::set<std::string> seen;
    while (seen.insert(id).second) {
        const auto object = model.find(id);
        if (object == model.end()) return false;
        id = inspectedProperty(object->second, "parent");
        if (id == parent) return true;
        if (!recursive || id.empty()) return false;
    }
    return false;
}
std::string kindName(ObjectKind kind)
{
    if (kind == ObjectKind::addressSpace) return "page";
    if (kind == ObjectKind::registerBlock) return "block";
    if (kind == ObjectKind::reg) return "register";
    if (kind == ObjectKind::enumValue) return "enum";
    return kind == ObjectKind::field ? "field" : "workspace";
}
bool hasTags(const InspectedModel& model, std::string id, const std::vector<std::string>& tags)
{
    if (tags.empty()) return true;
    std::set<std::string> available, seen;
    while (seen.insert(id).second) {
        const auto object = model.find(id);
        if (object == model.end()) break;
        const auto text = inspectedProperty(object->second, "tags");
        const auto values = QJsonDocument::fromJson(QByteArray::fromStdString(text)).array();
        for (const auto& value : values) available.insert(value.toString().toStdString());
        id = inspectedProperty(object->second, "parent");
    }
    return std::ranges::all_of(tags, [&](const std::string& tag) { return available.contains(tag); });
}
bool equalValue(std::string_view a, std::string_view b, bool numeric = false)
{
    if (a == b) return true;
    if (!numeric) return false;
    const auto left = UnsignedValue::parse(a);
    const auto right = UnsignedValue::parse(b);
    return left && right && *left == *right;
}
bool numericProperty(const std::string& property)
{
    static const std::set<std::string> properties{"base","offset","size","width","address_width","msb","lsb","initial","reset","value","minimum","maximum","display_order"};
    return properties.contains(property);
}
std::string layout(const InspectedModel& model, const std::string& id)
{
    std::vector<std::string> fields;
    for (const auto& [childId, child] : model) {
        if (child.kind != ObjectKind::field || inspectedProperty(child, "parent") != id) continue;
        QJsonObject entry;
        for (const auto* property : {"msb", "lsb", "type", "sw_access", "hw_access"})
            entry.insert(QString::fromLatin1(property), QString::fromStdString(inspectedProperty(child, property)));
        entry.insert("members", QString::fromStdString(layout(model, childId)));
        entry.insert("name", QString::fromStdString(inspectedProperty(child, "name")));
        fields.push_back(QJsonDocument(entry).toJson(QJsonDocument::Compact).toStdString());
    }
    std::ranges::sort(fields);
    QJsonArray result;
    for (const auto& field : fields) result.append(QString::fromStdString(field));
    return QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString();
}
}
ObjectSelection selectObjects(const InspectedModel& model, const ObjectSelector& selector)
{
    ObjectSelection result;
    if (selector.recursive && selector.parents.empty()) {
        result.error = "recursive requires at least one parent."; return result;
    }
    for (const auto* ids : {&selector.ids, &selector.parents, &selector.excludeIds})
        for (const auto& id : *ids) if (!model.contains(id)) {
            result.error = "Selector references missing object '" + id + "'."; return result;
        }
    for (const auto& [id, object] : model) {
        if (selector.kind != "all" && selector.kind != kindName(object.kind)) continue;
        if (!selector.ids.empty() && !contains(selector.ids, id)) continue;
        if (!selector.names.empty() && !contains(selector.names, inspectedProperty(object, "name"))) continue;
        if (!selector.parents.empty() && !std::ranges::any_of(selector.parents, [&](const auto& parent) {
            return descendant(model, id, parent, selector.recursive);
        })) continue;
        if (std::ranges::any_of(selector.excludeIds, [&](const auto& excluded) {
            return id == excluded || descendant(model, id, excluded, true);
        })) continue;
        if (!hasTags(model, id, selector.tags)) continue;
        result.ids.push_back(id);
    }
    return result;
}
std::string inspectedPage(const InspectedModel& model, const ObjectId& objectId)
{
    auto id = objectId; std::set<std::string> seen;
    while (seen.insert(id).second) {
        const auto object = model.find(id);
        if (object == model.end()) break;
        if (object->second.kind == ObjectKind::addressSpace) return id;
        id = inspectedProperty(object->second, "parent");
    }
    return {};
}
std::string inspectedPath(const InspectedModel& model, const ObjectId& objectId)
{
    auto id = objectId; std::string path; std::set<std::string> seen;
    while (seen.insert(id).second) {
        const auto object = model.find(id);
        if (object == model.end()) break;
        const auto name = inspectedProperty(object->second, "name");
        path = name + (path.empty() ? "" : "/" + path);
        id = inspectedProperty(object->second, "parent");
    }
    return path;
}
std::vector<ConsistencyFinding> validateConsistencyRules(const Workspace& workspace, const std::vector<ConsistencyRule>& rules)
{
    const auto model = inspectModel(workspace);
    std::vector<ConsistencyFinding> findings;
    for (const auto& rule : rules) {
        auto report = [&](const std::string& id, const std::string& property,
                          const std::string& expected, const std::string& actual) {
            findings.push_back({rule.id, id, inspectedPage(model, id), inspectedPath(model, id),
                                property, expected, actual, rule.severity});
        };
        std::vector<std::string> scopes{workspace.id};
        if (rule.scope) {
            const auto selected = selectObjects(model, *rule.scope);
            if (!selected.error.empty() || selected.ids.empty()) {
                report(workspace.id, "scope", "at least one valid scope", selected.error.empty() ? "0" : selected.error);
                continue;
            }
            scopes = selected.ids;
        }
        const auto selection = selectObjects(model, rule.select);
        if (!selection.error.empty()) { report(workspace.id, "selector", "valid selection", selection.error); continue; }
        std::map<std::string, std::string> shared;
        for (const auto& scope : scopes) {
            std::vector<std::string> ids;
            for (const auto& id : selection.ids)
                if (!rule.scope || descendant(model, id, scope, true)) ids.push_back(id);
            if (rule.count && ids.size() != *rule.count)
                report(scope, "count", std::to_string(*rule.count), std::to_string(ids.size()));
            if (!rule.count && ids.empty()) report(scope, "count", "at least one", "0");
            for (const auto& id : ids) {
                const auto& object = model.at(id);
                for (const auto& [property, expected] : rule.properties) {
                    const auto found = object.properties.find(property);
                    const auto actual = found == object.properties.end() ? "<missing>" : found->second;
                    if (found == object.properties.end() || !equalValue(actual, expected, numericProperty(property))) report(id, property, expected, actual);
                }
                for (const auto& property : rule.same) {
                    const auto actual = property == "layout" ? layout(model, id) : inspectedProperty(object, property);
                    if (property != "layout" && !object.properties.contains(property)) {
                        report(id, property, "existing property", "<missing>"); continue;
                    }
                    const auto key = kindName(object.kind) + "/" + inspectedProperty(object, "name") + "/" + property;
                    const auto [first, inserted] = shared.emplace(key, actual);
                    if (!inserted && !equalValue(first->second, actual, numericProperty(property))) report(id, property, first->second, actual);
                }
                if (!rule.enumValues.empty()) {
                    const auto type = inspectedProperty(object, "type");
                    if (type != "enum") report(id, "type", "enum", type);
                    std::set<std::string> actual;
                    for (const auto& [childId, child] : model) {
                        (void)childId;
                        if (child.kind == ObjectKind::enumValue && inspectedProperty(child, "parent") == id)
                            actual.insert(inspectedProperty(child, "value"));
                    }
                    for (const auto& required : rule.enumValues)
                        if (!std::ranges::any_of(actual, [&](const auto& value) { return equalValue(value, required, true); }))
                            report(id, "enum_values", required, "missing");
                }
            }
        }
    }
    return findings;
}
} // namespace regmap
