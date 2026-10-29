#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace regmap::cli {
[[nodiscard]] QJsonObject requestGui(const QString& session, const QString& method,
    const QJsonObject& parameters, int timeoutMs = 10000);
[[nodiscard]] QJsonArray discoverGuiSessions(const QString& project);

class GuiCommitClient {
public:
    void setTarget(const QString& session, const QString& project);
    QJsonObject begin(const QString& session, const QString& project, const QString& revision, bool preview);
    QJsonObject confirm();
    QJsonObject finish(const QString& savedRevision);
    void cancel();
    [[nodiscard]] bool enabled() const { return !session_.isEmpty(); }
    [[nodiscard]] bool prepared() const { return !token_.isEmpty(); }
    [[nodiscard]] const QJsonObject& state() const { return state_; }
    [[nodiscard]] const QString& session() const { return session_; }
    [[nodiscard]] const QString& token() const { return token_; }
private:
    QJsonObject call(const QString& phase, const QString& revision = {});
    QString session_, project_, token_, epoch_;
    QJsonObject state_;
};
} // namespace regmap::cli
