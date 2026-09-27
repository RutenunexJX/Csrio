#pragma once

#include "regmap/core/model.hpp"
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <vector>

namespace regmap::ui {
struct ObjectInspection {
    QString id;
    QString kind;
    QString path;
    QString blockId;
    QJsonObject properties;
};
using WorkspaceInspection = QMap<QString, ObjectInspection>;
struct PropertyDifference {
    QString objectId;
    QString path;
    QString property;
    QString before;
    QString after;
};
WorkspaceInspection inspectWorkspace(const Workspace& workspace);
std::vector<PropertyDifference> inspectDifferences(
    const Workspace& before, const Workspace& after, const QString& objectId = {});
}
