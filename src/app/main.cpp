#include "main_window.hpp"
#include "application_identity.hpp"
#ifdef REGMAP_HAS_SUITEAPP
#include "suite_integration.hpp"
#endif
#include "startup_options.hpp"
#include "workbench_theme.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>

#ifndef REGMAP_APP_VERSION
#define REGMAP_APP_VERSION "0.0.0"
#endif

int main(int argumentCount, char* arguments[])
{
    QApplication application(argumentCount, arguments);
    regmap::workbench::configureApplicationIdentity();
    QApplication::setApplicationVersion(
        QStringLiteral(REGMAP_APP_VERSION));
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(
            ":/icons/regmap_workbench_icon.png")));

    const regmap::workbench::StartupOptions
        startup =
            regmap::workbench::
                parseStartupOptions(
                    QCoreApplication::
                        arguments()
                        .mid(1));
    if (startup.action ==
        regmap::workbench::
            StartupAction::showHelp) {
        QTextStream output(stdout);
        output.setEncoding(
            QStringConverter::Utf8);
        output
            << regmap::workbench::
                   startupUsage();
        output.flush();
        return 0;
    }
    if (startup.action ==
        regmap::workbench::
            StartupAction::showVersion) {
        QTextStream output(stdout);
        output.setEncoding(
            QStringConverter::Utf8);
        output
            << QApplication::
                   applicationDisplayName()
            << ' '
            << QApplication::
                   applicationVersion()
            << '\n';
        output.flush();
        return 0;
    }
    if (startup.action ==
        regmap::workbench::
            StartupAction::error) {
        QTextStream error(stderr);
        error.setEncoding(
            QStringConverter::Utf8);
        error
            << startup.error
            << "\n\n"
            << regmap::workbench::
                   startupUsage();
        error.flush();
        return 2;
    }

    WorkbenchTheme::apply(application);
    MainWindow window;
    window.show();

#ifdef REGMAP_HAS_SUITEAPP
    regmap::workbench::RegMapSuiteIntegration suiteIntegration(
        &window, &application);
    QTimer::singleShot(0, &application, [&suiteIntegration] {
        suiteIntegration.start();
    });
#endif

    if (!startup.projectPath
             .isEmpty()) {
        const QString projectPath =
            startup.projectPath;
        const QString selectedObjectId =
            startup.selectedObjectId;
        QTimer::singleShot(0, &window, [&window, projectPath, selectedObjectId] {
            window.openStartupProjectPath(
                projectPath,
                selectedObjectId);
        });
    }

    return application.exec();
}
