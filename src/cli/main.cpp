#include "cli_app.hpp"
#include "cli_runtime.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QStringConverter>
#include <QTextStream>

#include <memory>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv)
{
    // QGuiApplication consumes --session as a Qt option. Preserve the CLI's
    // original arguments, including Unicode Windows paths, before constructing it.
    QStringList arguments;
#ifdef Q_OS_WIN
    int nativeCount = 0;
    auto** nativeArguments = CommandLineToArgvW(GetCommandLineW(), &nativeCount);
    if (!nativeArguments) return 7;
    for (int index = 1; index < nativeCount; ++index)
        arguments.append(QString::fromWCharArray(nativeArguments[index]));
    LocalFree(nativeArguments);
#else
    for (int index = 1; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));
#endif
#ifdef Q_OS_WIN
    static_cast<void>(
        _setmode(
            _fileno(stdin),
            _O_BINARY));
    static_cast<void>(
        _setmode(
            _fileno(stdout),
            _O_BINARY));
    static_cast<void>(
        _setmode(
            _fileno(stderr),
            _O_BINARY));
#endif
    const bool requiresGui =
        regmap::cli::commandRequiresGuiApplication(
            argc,
            argv);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
    if (requiresGui &&
        qEnvironmentVariableIsEmpty(
            "QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty(
            "DISPLAY") &&
        qEnvironmentVariableIsEmpty(
            "WAYLAND_DISPLAY")) {
        qputenv(
            "QT_QPA_PLATFORM",
            QByteArrayLiteral("offscreen"));
    }
#endif
    std::unique_ptr<QCoreApplication> application;
    if (requiresGui) {
        application =
            std::make_unique<QGuiApplication>(
                argc,
                argv);
    } else {
        application =
            std::make_unique<QCoreApplication>(
                argc,
                argv);
    }
    application->setApplicationName(
        QStringLiteral("regmapc"));
    application->setApplicationVersion(
        QString::fromLatin1(
            regmap::cli::cliVersion.data(),
            static_cast<qsizetype>(
                regmap::cli::cliVersion
                    .size())));

    QTextStream standardInput(stdin);
    QTextStream standardOutput(stdout);
    QTextStream standardError(stderr);
    standardInput.setEncoding(
        QStringConverter::Utf8);
    standardOutput.setEncoding(
        QStringConverter::Utf8);
    standardError.setEncoding(
        QStringConverter::Utf8);
    return regmap::cli::run(
        arguments,
        standardInput,
        standardOutput,
        standardError);
}
