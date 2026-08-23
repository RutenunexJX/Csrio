#include "main_window.hpp"
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

int main(int argumentCount, char* arguments[])
{
    QApplication application(argumentCount, arguments);
    QApplication::setApplicationName(QStringLiteral("Register Map Workbench"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("RegMapWorkbench"));
    QApplication::setWindowIcon(
        QIcon(QStringLiteral(
            ":/icons/regmap_workbench_icon.png")));
    WorkbenchTheme::apply(application);

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
                   applicationName()
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
