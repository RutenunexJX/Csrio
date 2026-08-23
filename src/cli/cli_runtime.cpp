#include "cli_runtime.hpp"

#include <QString>
#include <QStringList>

namespace regmap::cli {
namespace {

[[nodiscard]] QStringList rawArguments(
    int argc,
    char** argv)
{
    QStringList result;
    for (int index = 1; index < argc; ++index) {
        result.push_back(
            QString::fromLocal8Bit(argv[index]));
    }
    return result;
}

[[nodiscard]] QString commandToken(
    const QStringList& arguments)
{
    bool terminated = false;
    for (const QString& token : arguments) {
        if (!terminated && token == QStringLiteral("--")) {
            terminated = true;
            continue;
        }
        if (!terminated &&
            (token == QStringLiteral("--json") ||
             token == QStringLiteral("--help") ||
             token == QStringLiteral("-h"))) {
            continue;
        }
        return token.toLower();
    }
    return {};
}

[[nodiscard]] bool helpRequested(
    const QStringList& arguments)
{
    bool terminated = false;
    for (const QString& token : arguments) {
        if (!terminated && token == QStringLiteral("--")) {
            terminated = true;
            continue;
        }
        if (!terminated &&
            (token == QStringLiteral("--help") ||
             token == QStringLiteral("-h"))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool explicitlyExcludesXlsx(
    const QStringList& arguments)
{
    bool sawTarget = false;
    bool selectedXlsx = false;
    for (qsizetype index = 0;
         index < arguments.size();
         ++index) {
        if (arguments.at(index) !=
                QStringLiteral("--target") ||
            index + 1 >= arguments.size()) {
            continue;
        }
        sawTarget = true;
        selectedXlsx = selectedXlsx ||
            arguments.at(++index)
                .compare(
                    QStringLiteral("xlsx"),
                    Qt::CaseInsensitive) == 0;
    }
    return sawTarget && !selectedXlsx;
}

} // namespace

bool commandRequiresGuiApplication(
    int argc,
    char** argv)
{
    const QStringList arguments =
        rawArguments(argc, argv);
    if (helpRequested(arguments)) {
        return false;
    }
    const QString command =
        commandToken(arguments);
    if (command == QStringLiteral("apply")) {
        return true;
    }
    if (command == QStringLiteral("generate") ||
        command == QStringLiteral("status")) {
        return !explicitlyExcludesXlsx(arguments);
    }
    if (command == QStringLiteral("init")) {
        if (arguments.contains(
                QStringLiteral("--no-generate"))) {
            return false;
        }
        return !explicitlyExcludesXlsx(arguments);
    }
    return false;
}

} // namespace regmap::cli
