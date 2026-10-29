#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace regmap::session {

// Discovery hints only: clients must query the live endpoint for project identity
// and session epoch. Stale records never authorize a commit or adoption.
inline QDir directory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Csrio/sessions");
}

inline bool publish(const QString& epoch)
{
    const auto folder = directory();
    if (!QDir().mkpath(folder.path())) return false;
    QSaveFile file(folder.filePath(QString::number(QCoreApplication::applicationPid()) + ".json"));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{{"epoch",epoch}}).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}

inline void remove(const QString& epoch)
{
    QFile file(directory().filePath(QString::number(QCoreApplication::applicationPid()) + ".json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto stored = QJsonDocument::fromJson(file.readAll()).object().value("epoch").toString();
    file.close();
    if (stored == epoch) file.remove();
}

inline QStringList candidates()
{
    QStringList result;
    for (const auto& name : directory().entryList({"*.json"},QDir::Files)) {
        const auto pid = name.chopped(5);
        bool ok = false;
        const auto number = pid.toLongLong(&ok);
        if (ok && number > 0 && QString::number(number) == pid) result.append(pid);
    }
    return result;
}
} // namespace regmap::session
