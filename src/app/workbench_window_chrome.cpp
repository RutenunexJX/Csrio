#include "workbench_controls.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QWindow>

#ifdef REGMAP_ENABLE_ELA
#include <ElaAppBar.h>
#endif

namespace WorkbenchControls {
QWidget* installWindowChrome(QMainWindow* window)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() != Backend::ela) return nullptr;
    auto* bar = new ElaAppBar(window);
    bar->setObjectName(QStringLiteral("workbenchAppBar"));
    bar->setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint
        | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
    bar->setAppBarHeight(qMax(36, window->fontMetrics().height() + 16));
    bar->setAccessibleName(QStringLiteral("Window title bar"));
    bar->setIsDefaultClosed(true);
    for (auto* button : bar->findChildren<QAbstractButton*>()) {
        button->setFocusPolicy(Qt::TabFocus);
        installToolTip(button);
    }
    bar->resize(window->width(), bar->height());
    bar->raise();
    return bar;
#else
    Q_UNUSED(window);
    return nullptr;
#endif
}

bool windowChromeNativeEvent(QWidget* chrome, const QByteArray& type, void* message, qintptr* result)
{
#if defined(REGMAP_ENABLE_ELA) && defined(Q_OS_WIN)
    if (auto* bar = qobject_cast<ElaAppBar*>(chrome))
        return bar->takeOverNativeEvent(type, message, result) == 1;
#else
    Q_UNUSED(chrome); Q_UNUSED(type); Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}
}
