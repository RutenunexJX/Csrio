#pragma once

#include <QString>
#include <QStringList>

namespace regmap::workbench {

enum class StartupAction {
    launch,
    showHelp,
    showVersion,
    error,
};

struct StartupOptions {
    StartupAction action{
        StartupAction::launch};
    QString projectPath;
    QString selectedObjectId;
    QString error;
};

[[nodiscard]] StartupOptions
parseStartupOptions(
    const QStringList& arguments);

[[nodiscard]] QString startupUsage();

} // namespace regmap::workbench
