#include "startup_options.hpp"

#include <utility>

namespace regmap::workbench {
namespace {

[[nodiscard]] StartupOptions
startupError(QString message)
{
    StartupOptions result;
    result.action =
        StartupAction::error;
    result.error =
        std::move(message);
    return result;
}

} // namespace

StartupOptions parseStartupOptions(
    const QStringList& arguments)
{
    for (const QString& token :
         arguments) {
        if (token ==
            QStringLiteral("--")) {
            break;
        }
        if (token ==
                QStringLiteral(
                    "--help") ||
            token ==
                QStringLiteral("-h")) {
            StartupOptions result;
            result.action =
                StartupAction::showHelp;
            return result;
        }
        if (token ==
            QStringLiteral("--version")) {
            StartupOptions result;
            result.action =
                StartupAction::showVersion;
            return result;
        }
    }

    StartupOptions result;
    bool optionsTerminated = false;
    for (qsizetype index = 0;
         index < arguments.size();
         ++index) {
        const QString token =
            arguments.at(index);
        if (!optionsTerminated &&
            token ==
                QStringLiteral("--")) {
            optionsTerminated = true;
            continue;
        }
        if (!optionsTerminated &&
            (token ==
                 QStringLiteral(
                     "--project") ||
             token ==
                 QStringLiteral("-p"))) {
            if (index + 1 >=
                arguments.size()) {
                return startupError(
                    QStringLiteral(
                        "%1 requires a project path.")
                        .arg(token));
            }
            const QString path =
                arguments.at(++index);
            if (path.isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "%1 requires a non-empty project path.")
                        .arg(token));
            }
            if (!result.projectPath
                     .isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "Only one startup project may be specified."));
            }
            result.projectPath = path;
            continue;
        }
        if (!optionsTerminated &&
            token ==
                QStringLiteral(
                    "--select")) {
            if (index + 1 >=
                arguments.size()) {
                return startupError(
                    QStringLiteral(
                        "--select requires a stable object ID."));
            }
            const QString objectId =
                arguments.at(++index)
                    .trimmed();
            if (objectId.isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "--select requires a non-empty stable object ID."));
            }
            if (!result.selectedObjectId
                     .isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "Only one startup object may be selected."));
            }
            result.selectedObjectId =
                objectId;
            continue;
        }
        if (!optionsTerminated &&
            token.startsWith(
                QStringLiteral(
                    "--project="))) {
            const QString path =
                token.sliced(
                    QStringLiteral(
                        "--project=")
                        .size());
            if (path.isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "--project requires a non-empty project path."));
            }
            if (!result.projectPath
                     .isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "Only one startup project may be specified."));
            }
            result.projectPath = path;
            continue;
        }
        if (!optionsTerminated &&
            token.startsWith(
                QStringLiteral(
                    "--select="))) {
            const QString objectId =
                token.sliced(
                    QStringLiteral(
                        "--select=")
                        .size())
                    .trimmed();
            if (objectId.isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "--select requires a non-empty stable object ID."));
            }
            if (!result.selectedObjectId
                     .isEmpty()) {
                return startupError(
                    QStringLiteral(
                        "Only one startup object may be selected."));
            }
            result.selectedObjectId =
                objectId;
            continue;
        }
        if (!optionsTerminated &&
            token.startsWith(
                QLatin1Char('-'))) {
            return startupError(
                QStringLiteral(
                    "Unknown option '%1'.")
                    .arg(token));
        }
        if (token.isEmpty()) {
            return startupError(
                QStringLiteral(
                    "The startup project path must not be empty."));
        }
        if (!result.projectPath
                 .isEmpty()) {
            return startupError(
                QStringLiteral(
                    "Only one startup project may be specified."));
        }
        result.projectPath = token;
    }
    if (!result.selectedObjectId
             .isEmpty() &&
        result.projectPath.isEmpty()) {
        return startupError(
            QStringLiteral(
                "--select requires a startup project."));
    }
    return result;
}

QString startupUsage()
{
    return QStringLiteral(
        "Csrio\n"
        "\n"
        "Usage:\n"
        "  Csrio [project.regmap.yaml]\n"
        "  Csrio --project <project.regmap.yaml> [--select <stable-id>]\n"
        "\n"
        "Options:\n"
        "  -p, --project <path>  Open one register-map project.\n"
        "      --select <id>      Locate one stable object ID after opening.\n"
        "  -h, --help            Show this help without opening a window.\n"
        "      --version         Show the application version.\n"
        "      --                Treat the remaining token as a project path.\n");
}

} // namespace regmap::workbench
