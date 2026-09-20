#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#ifdef REGMAP_TEST_UI_INITIALIZATION
#include "workbench_theme.hpp"
#include <QApplication>
#include <QTimer>
#endif

static void isolateProfile()
{
    static QTemporaryDir directory;
    if (!directory.isValid()) qFatal("Cannot create isolated test profile");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName("RegMapTests");
    QCoreApplication::setApplicationName("RegMapIsolatedTests");
#ifdef REGMAP_TEST_UI_INITIALIZATION
    QTimer::singleShot(0, [] {
        WorkbenchTheme::apply(*qApp, qEnvironmentVariable("REGMAP_TEST_THEME") == "dark"
            ? WorkbenchTheme::Mode::dark : WorkbenchTheme::Mode::light);
    });
#endif
}
Q_COREAPP_STARTUP_FUNCTION(isolateProfile)
