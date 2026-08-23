#pragma once

#include <QStringList>

#include <string_view>

class QTextStream;

#ifndef REGMAP_CLI_VERSION
#define REGMAP_CLI_VERSION "0.0.0"
#endif

namespace regmap::cli {

inline constexpr int apiVersion = 1;
inline constexpr std::string_view cliVersion =
    REGMAP_CLI_VERSION;

enum class ExitCode : int {
    success = 0,
    projectError = 1,
    usageError = 2,
    revisionConflict = 3,
    writeError = 4,
    outputsOutOfDate = 5,
    differencesFound = 6,
    inputError = 7,
    generationError = 8,
};

[[nodiscard]] int run(
    const QStringList& arguments,
    QTextStream& standardInput,
    QTextStream& standardOutput,
    QTextStream& standardError);

[[nodiscard]] int run(
    const QStringList& arguments,
    QTextStream& standardOutput,
    QTextStream& standardError);

} // namespace regmap::cli
