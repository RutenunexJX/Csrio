#include "main_window.hpp"
#include "workbench_theme.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

int main(int argumentCount, char* arguments[])
{
    QApplication application(argumentCount, arguments);
    QApplication::setApplicationName(QStringLiteral("Register Map Workbench"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("RegMapWorkbench"));
    WorkbenchTheme::apply(application);

    MainWindow window;
    window.show();

    const QStringList values = QCoreApplication::arguments();
    if (values.size() > 1) {
        const QString projectPath = values[1];
        QTimer::singleShot(0, &window, [&window, projectPath] {
            window.openStartupProjectPath(projectPath);
        });
    }

    return application.exec();
}
