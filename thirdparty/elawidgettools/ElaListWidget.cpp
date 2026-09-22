#include "ElaListWidget.h"

#include "ElaListViewStyle.h"
#include "ElaTheme.h"

ElaListWidget::ElaListWidget(QWidget* parent)
    : QListWidget(parent), _listStyle(new ElaListViewStyle)
{
    setObjectName("ElaListWidget");
    setProperty("ElaUseQtItemSemantics", true);
    _listStyle->setParent(this);
    _listStyle->setItemHeight(qMax(28, fontMetrics().height() + 10));
    setStyle(_listStyle);
    setMouseTracking(true);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { viewport()->update(); });
}

ElaListWidget::~ElaListWidget()
{
    viewport()->setStyle(nullptr);
    setStyle(nullptr);
    delete _listStyle;
}
