#include "ElaStatusBar.h"

#include <QPainter>
#include <QTimer>

#include "ElaStatusBarStyle.h"
#include "ElaTheme.h"
ElaStatusBar::ElaStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    setObjectName("ElaStatusBar");
    setStyleSheet("#ElaStatusBar{background-color:transparent;}");
    setFixedHeight(28);
    setContentsMargins(20, 0, 0, 0);
    _statusStyle = new ElaStatusBarStyle(style());
    _statusStyle->setParent(this);
    setStyle(_statusStyle);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
}

ElaStatusBar::~ElaStatusBar()
{
    setStyle(nullptr);
    delete _statusStyle;
}
