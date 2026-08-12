#include "cli_app.hpp"

#include <QGuiApplication>
#include <QStringConverter>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv)
{
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
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (qEnvironmentVariableIsEmpty(
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
    QGuiApplication application(argc, argv);
    application.setApplicationName(
        QStringLiteral("regmapc"));
    application.setApplicationVersion(
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
        application.arguments().mid(1),
        standardInput,
        standardOutput,
        standardError);
}
