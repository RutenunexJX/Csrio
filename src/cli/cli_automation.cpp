#include "cli_automation.hpp"
#include <QFile>
#include <QJsonDocument>
#include <cmath>
#include <set>

namespace regmap::cli {
QJsonArray referenceFindingsJson(const std::vector<ReferenceFinding>& findings)
{
    QJsonArray result;
    for (const auto& item : findings) {
        QJsonArray candidates;
        for (const auto& id : item.candidates) candidates.append(QString::fromStdString(id));
        result.append(QJsonObject{
            {"target_id",QString::fromStdString(item.targetId)}, {"target_path",QString::fromStdString(item.targetPath)},
            {"target_name",QString::fromStdString(item.targetName)}, {"change",QString::fromStdString(item.change)},
            {"object_id",QString::fromStdString(item.objectId)}, {"path",QString::fromStdString(item.path)},
            {"property",QString::fromStdString(item.property)}, {"kind",QString::fromStdString(item.kind)},
            {"status",QString::fromStdString(item.status)}, {"excerpt",QString::fromStdString(item.snippet)},
            {"ambiguous",item.ambiguous}, {"candidate_ids",candidates}});
    }
    return result;
}
namespace {
bool keys(const QJsonObject& object, const std::set<QString>& allowed, QString& error)
{
    for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) {
        error = "Unknown member '" + it.key() + "'."; return false;
    }
    return true;
}
bool strings(const QJsonValue& value, std::vector<std::string>& result, QString& error)
{
    if (!value.isArray() || value.toArray().isEmpty()) { error = "Expected a non-empty string array."; return false; }
    std::set<QString> seen;
    for (const auto& item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty() || !seen.insert(item.toString()).second) {
            error = "Expected unique non-empty strings."; return false;
        }
        result.push_back(item.toString().toStdString());
    }
    return true;
}
QString severity(DiagnosticSeverity value)
{
    if (value == DiagnosticSeverity::warning) return "warning";
    if (value == DiagnosticSeverity::information) return "information";
    return "error";
}
}
bool parseSelector(const QJsonValue& value, ObjectSelector& selector, QString& error)
{
    if (!value.isObject() || value.toObject().isEmpty()) { error = "select must be a non-empty object."; return false; }
    const auto object = value.toObject();
    if (!keys(object, {"kind", "ids", "names", "parents", "recursive", "tags", "exclude_ids"}, error)) return false;
    if (object.contains("kind")) {
        const auto kind = object.value("kind").toString();
        if (!std::set<QString>{"workspace","page","block","register","field","enum","all"}.contains(kind)) {
            error = "Invalid selector kind."; return false;
        }
        selector.kind = kind.toStdString();
    }
    for (const auto& [name, target] : std::vector<std::pair<QString,std::vector<std::string>*>>{
        {"ids", &selector.ids}, {"names", &selector.names}, {"parents", &selector.parents},
        {"tags", &selector.tags}, {"exclude_ids", &selector.excludeIds}}) {
        if (object.contains(name) && !strings(object.value(name), *target, error)) { error = name + ": " + error; return false; }
    }
    if (object.contains("recursive")) {
        if (!object.value("recursive").isBool()) { error = "recursive must be a boolean."; return false; }
        selector.recursive = object.value("recursive").toBool();
    }
    if (selector.recursive && selector.parents.empty()) { error = "recursive requires parents."; return false; }
    return true;
}
bool loadConsistencyRules(const QString& path, std::vector<ConsistencyRule>& rules, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "Cannot read rules file: " + file.errorString(); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject() || parseError.error != QJsonParseError::NoError) { error = "Invalid rules JSON: " + parseError.errorString(); return false; }
    const auto root = document.object();
    if (!keys(root, {"version", "rules"}, error) || root.value("version") != QJsonValue(1) || !root.value("rules").isArray()) {
        if (error.isEmpty()) error = "Rules require version 1 and a rules array.";
        return false;
    }
    std::set<std::string> ids;
    for (const auto& value : root.value("rules").toArray()) {
        if (!value.isObject()) { error = "Every rule must be an object."; return false; }
        const auto object = value.toObject();
        if (!keys(object, {"id","severity","scope","select","assert"}, error)) return false;
        ConsistencyRule rule;
        rule.id = object.value("id").toString().toStdString();
        if (rule.id.empty() || !ids.insert(rule.id).second) { error = "Rule IDs must be unique non-empty strings."; return false; }
        const auto level = object.value("severity").toString("error");
        if (level == "warning") rule.severity = DiagnosticSeverity::warning;
        else if (level == "information") rule.severity = DiagnosticSeverity::information;
        else if (level != "error" || (object.contains("severity") && !object.value("severity").isString())) {
            error = "severity must be error, warning or information."; return false;
        }
        if (object.contains("scope")) {
            rule.scope.emplace();
            if (!parseSelector(object.value("scope"), *rule.scope, error)) return false;
        }
        if (!parseSelector(object.value("select"), rule.select, error)) return false;
        if (!object.value("assert").isObject() || object.value("assert").toObject().isEmpty()) { error = "assert must be a non-empty object."; return false; }
        const auto assertions = object.value("assert").toObject();
        if (!keys(assertions, {"count","properties","same","enum_values"}, error)) return false;
        if (assertions.contains("count")) {
            const auto count = assertions.value("count");
            if (!count.isDouble() || count.toDouble() < 0 || count.toDouble() > 2147483647 || std::floor(count.toDouble()) != count.toDouble()) {
                error = "count must be a non-negative integer."; return false;
            }
            rule.count = static_cast<std::size_t>(count.toInteger());
        }
        if (assertions.contains("properties")) {
            if (!assertions.value("properties").isObject() || assertions.value("properties").toObject().isEmpty()) {
                error = "properties must be a non-empty object."; return false;
            }
            const auto properties = assertions.value("properties").toObject();
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                if (it.value().isString()) rule.properties[it.key().toStdString()] = it.value().toString().toStdString();
                else if (it.value().isBool()) rule.properties[it.key().toStdString()] = it.value().toBool() ? "true" : "false";
                else if (it.value().isDouble() && std::floor(it.value().toDouble()) == it.value().toDouble() &&
                         std::abs(it.value().toDouble()) <= 9007199254740991.0)
                    rule.properties[it.key().toStdString()] = std::to_string(it.value().toInteger());
                else { error = "Rule property values must be strings, booleans or exact integers."; return false; }
            }
        }
        if (assertions.contains("same") && !strings(assertions.value("same"), rule.same, error)) return false;
        if (assertions.contains("enum_values")) {
            if (!strings(assertions.value("enum_values"), rule.enumValues, error)) return false;
            std::set<std::string> codes;
            for (auto& code : rule.enumValues) {
                const auto parsed = UnsignedValue::parse(code);
                if (!parsed || !codes.insert(parsed->toHexString()).second) { error = "enum_values requires distinct unsigned codes."; return false; }
                code = parsed->toHexString();
            }
        }
        rules.push_back(std::move(rule));
    }
    return true;
}
QJsonArray consistencyFindingsJson(const std::vector<ConsistencyFinding>& findings)
{
    QJsonArray result;
    for (const auto& item : findings) result.append(QJsonObject{
        {"rule_id", QString::fromStdString(item.ruleId)}, {"object_id", QString::fromStdString(item.objectId)},
        {"page_id", QString::fromStdString(item.pageId)}, {"path", QString::fromStdString(item.path)},
        {"property", QString::fromStdString(item.property)}, {"expected", QString::fromStdString(item.expected)},
        {"actual", QString::fromStdString(item.actual)}, {"severity", severity(item.severity)}});
    return result;
}
std::vector<Diagnostic> consistencyDiagnostics(const std::vector<ConsistencyFinding>& findings)
{
    std::vector<Diagnostic> result;
    for (const auto& item : findings) {
        Diagnostic diagnostic;
        diagnostic.code = "RMC2100";
        diagnostic.severity = item.severity;
        diagnostic.objectId = item.objectId;
        diagnostic.message = "Rule '" + item.ruleId + "': " + item.property + " expected " + item.expected + ", actual " + item.actual + ".";
        result.push_back(std::move(diagnostic));
    }
    return result;
}
} // namespace regmap::cli
