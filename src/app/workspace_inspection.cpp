#include "workspace_inspection.hpp"
#include "regmap/core/three_way_merge.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace regmap::ui {
WorkspaceInspection inspectWorkspace(const Workspace& workspace)
{
    const auto state = serializeWorkspaceState(workspace, false);
    const auto objects = QJsonDocument::fromJson(QByteArray::fromStdString(state))
                             .object().value("objects").toArray();
    WorkspaceInspection result;
    for (const auto& value : objects) {
        const auto object = value.toObject();
        ObjectInspection entry;
        entry.id = object.value("id").toString();
        entry.kind = object.value("kind").toString();
        entry.properties = object.value("properties").toObject();
        result.insert(entry.id, entry);
    }
    for (auto entry = result.begin(); entry != result.end(); ++entry) {
        QStringList names;
        QSet<QString> visited;
        QString id = entry->id;
        while (result.contains(id) && !visited.contains(id)) {
            visited.insert(id);
            const auto& parent = result[id];
            names.prepend(parent.properties.value("name").toString());
            if (parent.kind == "block") entry->blockId = id;
            id = parent.properties.value("parent").toString();
        }
        entry->path = names.join(" / ");
    }
    return result;
}

std::vector<PropertyDifference> inspectDifferences(
    const Workspace& before, const Workspace& after, const QString& objectId)
{
    const auto oldObjects = inspectWorkspace(before);
    const auto newObjects = inspectWorkspace(after);
    QSet<QString> ids;
    if (!objectId.isEmpty()) ids.insert(objectId);
    else {
        for (auto it = oldObjects.begin(); it != oldObjects.end(); ++it) ids.insert(it.key());
        for (auto it = newObjects.begin(); it != newObjects.end(); ++it) ids.insert(it.key());
    }
    auto orderedIds = ids.values();
    orderedIds.sort();
    std::vector<PropertyDifference> result;
    for (const auto& id : orderedIds) {
        const auto oldObject = oldObjects.value(id);
        const auto newObject = newObjects.value(id);
        const QString path = newObjects.contains(id) ? newObject.path : oldObject.path;
        QSet<QString> keys;
        for (const auto& key : oldObject.properties.keys()) keys.insert(key);
        for (const auto& key : newObject.properties.keys()) keys.insert(key);
        auto orderedKeys = keys.values();
        orderedKeys.sort();
        const auto display = [](const ObjectInspection& object, const QString& key,
                                const WorkspaceInspection& objects) {
            if (!object.properties.contains(key)) return QStringLiteral("<absent>");
            const QString value = object.properties.value(key).toString();
            if (key == "parent" && objects.contains(value)) return objects[value].path;
            return value.isEmpty() ? QStringLiteral("<empty>") : value;
        };
        for (const auto& key : orderedKeys) {
            if (oldObject.properties.value(key) == newObject.properties.value(key)) continue;
            result.push_back({id, path, key, display(oldObject, key, oldObjects),
                              display(newObject, key, newObjects)});
        }
    }
    return result;
}
}
