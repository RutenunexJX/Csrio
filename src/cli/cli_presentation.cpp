#include "cli_presentation.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>

namespace regmap::cli {
const QStringList& projectionProperties()
{
    static const QStringList properties{
        "id","kind","name","parent_id","path","page_id","block_id","register_id",
        "field_id","base","absolute_base","address","offset","width","size","address_width","fixed","display_order",
        "description","tags","type","access","software_access","hardware_access","sw_access","hw_access","msb","lsb","minimum","maximum",
        "initial","reset","reserved","read_side_effect","write_side_effect","value","enum_values","fields",
        "members","registers","blocks","pages","address_spaces","source","workbench_navigation","match_rank","match_field"};
    return properties;
}
bool parsePresentationOptions(QStringList& arguments, qsizetype& terminator, PresentationOptions& options, QString& error)
{
    const auto command = arguments.value(0).toLower();
    QSet<QString> seen;
    for (qsizetype i = 1; i < arguments.size() && (terminator < 0 || i < terminator);) {
        const auto token = arguments.at(i);
        if (token != "--properties" && token != "--changes" && token != "--rules" && token != "--stats") { ++i; continue; }
        if (seen.contains(token)) { error = token + " may be specified only once."; return false; }
        seen.insert(token);
        if ((token == "--properties" && !QStringList{"list","find","get","get-many"}.contains(command)) ||
            (token == "--changes" && command != "apply" && command != "diff") ||
            (token == "--rules" && command != "apply" && command != "validate")) {
            error = token + " is not supported by " + command + "."; return false;
        }
        qsizetype consumed = token == "--stats" ? 1 : 2;
        if (consumed == 2 && (i + 1 >= arguments.size() || (terminator >= 0 && i + 1 >= terminator) ||
                             arguments.at(i + 1).startsWith("--") || arguments.at(i + 1).isEmpty())) {
            error = token + " requires a value."; return false;
        }
        const auto value = consumed == 2 ? arguments.at(i + 1) : QString{};
        if (token == "--stats") {
            if (!QStringList{"init","summary","list","find","get","get-many","validate","diff","status","generate","apply"}.contains(command)) {
                error = "--stats is not supported by " + command + "."; return false;
            }
            options.stats = true;
        }
        else if (token == "--rules") options.rulesPath = value;
        else if (token == "--changes") {
            if (value != "full" && value != "summary") { error = "--changes accepts full or summary."; return false; }
            options.summary = value == "summary";
        } else {
            const auto& allowed = projectionProperties();
            for (const auto& property : value.split(',')) {
                if (!allowed.contains(property) || options.properties.contains(property)) {
                    error = "Invalid or repeated projection property '" + property + "'."; return false;
                }
                options.properties.push_back(property);
            }
        }
        for (qsizetype n = 0; n < consumed; ++n) arguments.removeAt(i);
        if (terminator >= 0) terminator -= consumed;
    }
    return true;
}
namespace {
QJsonObject project(const QJsonObject& object, const QStringList& properties)
{
    QJsonObject result{{"id", object.value("id")}};
    for (const auto& property : properties) result.insert(property, object.contains(property) ? object.value(property) : QJsonValue(QJsonValue::Null));
    return result;
}
}
QJsonValue presentResult(const QString& command, const QJsonValue& value, const PresentationOptions& options)
{
    if (!options.properties.isEmpty()) {
        if ((command == "list" || command == "find") && value.isArray()) {
            QJsonArray result;
            for (const auto& item : value.toArray()) result.append(project(item.toObject(), options.properties));
            return result;
        }
        if (command == "get" && value.isObject() && value.toObject().contains("id")) return project(value.toObject(), options.properties);
        if (command == "get-many" && value.isObject()) {
            auto result = value.toObject(); QJsonArray items;
            for (const auto& entry : result.value("items").toArray()) {
                auto item = entry.toObject();
                if (item.value("found").toBool()) item.insert("object", project(item.value("object").toObject(), options.properties));
                items.append(item);
            }
            result.insert("items", items); return result;
        }
    }
    if (options.summary && value.isObject()) {
        auto result = value.toObject();
        if (!result.value("changes").isArray()) return result;
        QJsonArray compact, ids; QSet<QString> unique;
        auto addId = [&](const QJsonValue& id) { if (id.isString() && !unique.contains(id.toString())) { unique.insert(id.toString()); ids.append(id); } };
        const auto changes = result.value("changes").toArray();
        qsizetype changedCount = 0;
        for (const auto& entry : changes) {
            auto item = entry.toObject();
            for (const auto* property : {"before","after","property_changes","details","before_navigation","after_navigation"}) item.remove(property);
            if (command == "diff" || item.value("changed").toBool()) {
                ++changedCount; addId(item.value("id"));
                for (const auto& target : item.value("targets").toArray()) addId(target.toObject().value("id"));
                for (const auto& affected : item.value("affected_ids").toArray()) addId(affected);
            }
            compact.append(item);
        }
        result.insert("changes", compact); result.insert("changes_mode", "summary");
        result.insert("reported_change_count", static_cast<qint64>(changedCount));
        result.insert("reported_object_ids", ids);
        return result;
    }
    return value;
}
} // namespace regmap::cli
