#include "cli_gui.hpp"
#include "../common/gui_session_registry.hpp"
#ifdef REGMAP_HAS_SUITEAPP
#include <suiteapp/transport.h>
#endif

namespace regmap::cli {
QJsonObject requestGui(const QString& session, const QString& method,
    const QJsonObject& parameters, int timeoutMs)
{
    QString message;
#ifdef REGMAP_HAS_SUITEAPP
    const auto result = SuiteApp::sendRequest("csrio.session." + session,
        SuiteApp::makeRequest(method,parameters), timeoutMs);
    if (result.hasResponse() && result.response.value("ok").toBool()) return result.response.value("result").toObject();
    message = result.hasResponse() ? result.response.value("error").toObject().value("message").toString() : result.errorMessage;
#else
    Q_UNUSED(method); Q_UNUSED(timeoutMs);
    message = "This build has no GUI session support.";
#endif
    return {{"ok",false},{"status","unavailable"},{"session",session},
        {"error_code","session_unavailable"},{"error",message},{"gui_adopted",false},
        {"writes_performed",false},{"suggestion","Discover live sessions for this project and retry synchronization against the saved revision."},
        {"reconnect",QJsonObject{{"command","gui"},{"arguments",QJsonArray{"gui",parameters.value("project"),"--discover"}}}}};
}

QJsonArray discoverGuiSessions(const QString& project)
{
    QJsonArray result;
    for (const auto& candidate : regmap::session::candidates()) {
        const auto state = requestGui(candidate,"regmap.gui",{{"project",project}},1000);
        if (state.value("ok").toBool()) result.append(state);
    }
    return result;
}

void GuiCommitClient::setTarget(const QString& session, const QString& project)
{
    session_ = session; project_ = project;
    state_ = {{"status","not_started"},{"session",session_},{"project",project_},
        {"gui_adopted",false},{"writes_performed",false},
        {"suggestion","Resolve the reported patch or project error before preparing a GUI commit."}};
}

QJsonObject GuiCommitClient::begin(const QString& session, const QString& project, const QString& revision, bool preview)
{
    session_ = session; project_ = project;
    state_ = requestGui(session_,"regmap.commit",{{"phase","begin"},{"project",project_},{"expect",revision},{"preview",preview}});
    if (state_.value("ok").toBool()) {
        token_ = state_.value("token").toString(); epoch_ = state_.value("session_epoch").toString();
    }
    return state_;
}

QJsonObject GuiCommitClient::call(const QString& phase, const QString& revision)
{
    state_ = requestGui(session_,"regmap.commit",{{"phase",phase},{"project",project_},
        {"expect",revision},{"token",token_},{"session_epoch",epoch_}});
    return state_;
}
QJsonObject GuiCommitClient::confirm() { return call("confirm"); }
QJsonObject GuiCommitClient::finish(const QString& savedRevision) { return call("finish",savedRevision); }
void GuiCommitClient::cancel()
{
    if (prepared()) {
        // Preserve the original blocking state in the response.
        const auto previous = state_;
        const auto cancelled = call("cancel"); state_ = previous;
        if (cancelled.value("ok").toBool()) state_.insert("commit_pending",false);
    }
}
} // namespace regmap::cli
