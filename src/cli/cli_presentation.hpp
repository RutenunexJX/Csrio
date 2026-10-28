#pragma once
#include <QJsonValue>
#include <QStringList>
namespace regmap::cli {
struct PresentationOptions {
    QStringList properties;
    QString rulesPath;
    bool summary{false};
    bool stats{false};
};
[[nodiscard]] const QStringList& projectionProperties();
[[nodiscard]] bool parsePresentationOptions(QStringList&, qsizetype& terminator, PresentationOptions&, QString& error);
[[nodiscard]] QJsonValue presentResult(const QString& command, const QJsonValue&, const PresentationOptions&);
} // namespace regmap::cli
