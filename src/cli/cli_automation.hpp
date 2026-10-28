#pragma once
#include "regmap/core/consistency.hpp"
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
namespace regmap::cli {
[[nodiscard]] bool parseSelector(const QJsonValue&, ObjectSelector&, QString& error);
[[nodiscard]] bool loadConsistencyRules(const QString&, std::vector<ConsistencyRule>&, QString& error);
[[nodiscard]] QJsonArray consistencyFindingsJson(const std::vector<ConsistencyFinding>&);
[[nodiscard]] std::vector<Diagnostic> consistencyDiagnostics(const std::vector<ConsistencyFinding>&);
} // namespace regmap::cli
