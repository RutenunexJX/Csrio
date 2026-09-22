#include "workbench_controls.hpp"

#include <QApplication>
#include <QHeaderView>
#include <QListView>
#include <QListWidget>
#include <QStatusBar>
#include <QTableView>

#ifdef REGMAP_ENABLE_ELA
#include <ElaListView.h>
#include <ElaListWidget.h>
#include <ElaStatusBar.h>
#include <ElaTableView.h>
#endif

namespace WorkbenchControls {
#ifdef REGMAP_ENABLE_ELA
namespace {
void prepareView(QAbstractItemView* view)
{
    view->setProperty("regmapElaItemView", true);
    view->setProperty("ElaUseQtItemSemantics", true);
    view->setFont(qApp->font());
    view->setStyleSheet({});
    view->setTextElideMode(Qt::ElideRight);
    view->setFocusPolicy(Qt::StrongFocus);
    styleScrollArea(view);
}
}
#endif

QStatusBar* statusBar(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* bar = new ElaStatusBar(parent);
        bar->setObjectName(QStringLiteral("workbenchStatusBar"));
        bar->setProperty("regmapElaControl", true);
        bar->setFont(qApp->font());
        bar->setMinimumHeight(qMax(28, bar->fontMetrics().height() + 10));
        bar->setMaximumHeight(QWIDGETSIZE_MAX);
        bar->setContentsMargins(8, 0, 0, 0);
        bar->setStyleSheet({});
        return bar;
    }
#endif
    return new QStatusBar(parent);
}

QListView* listView(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* view = new ElaListView(parent);
        prepareView(view);
        view->setItemHeight(qMax(28, view->fontMetrics().height() + 10));
        return view;
    }
#endif
    return new QListView(parent);
}

QListWidget* listWidget(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* view = new ElaListWidget(parent);
        prepareView(view);
        return view;
    }
#endif
    return new QListWidget(parent);
}

QTableView* resultTable(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* view = new ElaTableView(parent);
        prepareView(view);
        for (auto* header : {view->horizontalHeader(), view->verticalHeader()}) {
            header->setProperty("regmapElaItemView", true);
            header->setFont(qApp->font());
        }
        return view;
    }
#endif
    return new QTableView(parent);
}
}
