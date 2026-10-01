#pragma once

#include <QCoreApplication>
#include <QGuiApplication>

namespace regmap::workbench {

inline void configureApplicationIdentity()
{
    // These are storage identities: QSettings must continue to use the pre-Csrio
    // organization/application keys for preferences, recent projects and layout.
    QCoreApplication::setOrganizationName(QStringLiteral("RegMapWorkbench"));
    QCoreApplication::setApplicationName(QStringLiteral("Register Map Workbench"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Csrio"));
}

} // namespace regmap::workbench
