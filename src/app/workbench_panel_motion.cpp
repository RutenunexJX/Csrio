#include "workbench_controls.hpp"

#include <QApplication>
#include <QPointer>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>

#ifdef REGMAP_ENABLE_ELA
#include <ElaDrawerArea.h>

namespace {
class PanelMotion final : public QObject {
    Q_OBJECT
public:
    explicit PanelMotion(QWidget* panel, Qt::Edge edge) : QObject(panel), panel_(panel)
    {
        setObjectName(QStringLiteral("workbenchPanelMotion"));
        auto* body = new QWidget(panel);
        body->setLayout(panel->layout());
        drawer_ = new ElaDrawerArea(panel);
        drawer_->setObjectName(QStringLiteral("workbenchPanelDrawer"));
        drawer_->setDrawerHeaderVisible(false);
        drawer_->setDrawerEdge(edge);
        drawer_->addDrawer(body);
        drawer_->setExpanded(true, false);
        auto* shell = new QVBoxLayout(panel);
        shell->setContentsMargins(0, 0, 0, 0);
        shell->setSpacing(0);
        shell->addWidget(drawer_);
        requested_ = !panel->isHidden();
        connect(drawer_, &ElaDrawerArea::drawerAnimationFinished, this, [this](bool expanded) {
            panel_->setProperty("regmapPanelSnapshotBytes", drawer_->drawerSnapshotBytes());
            if (!expanded && !requested_) panel_->hide();
            if (expanded) restoreFocus();
        });
    }

    bool requested() const { return requested_; }

    void finish()
    {
        if (!pendingOpen_ && !drawer_->isDrawerAnimating() && drawer_->getIsExpand() == requested_
            && panel_->isVisible() == requested_) return;
        ++generation_;
        pendingOpen_ = false;
        drawer_->setExpanded(requested_, false);
        drawer_->finishDrawerAnimation();
        panel_->setVisible(requested_);
        panel_->setProperty("regmapPanelSnapshotBytes", qint64(0));
        restoreFocus();
    }

    void setVisible(bool visible)
    {
        if (requested_ == visible && panel_->isVisible() == visible) return;
        const auto generation = ++generation_;
        requested_ = visible;
        if (WorkbenchTheme::reducedMotionEnabled() || !panel_->window()->isVisible()) {
            finish();
            return;
        }
        if (visible) {
            focusCancelled_ = false;
            qApp->installEventFilter(this);
            if (drawer_->isDrawerAnimating()) {
                drawer_->setExpanded(true, true);
                qApp->installEventFilter(this);
                return;
            }
            drawer_->setExpanded(false, false);
            panel_->show();
            pendingOpen_ = true;
            // Let the existing QSplitter restore its endpoint once, before capture.
            QTimer::singleShot(0, this, [this, generation] {
                if (generation != generation_ || !requested_) return;
                pendingOpen_ = false;
                if (!focusTarget_ && !focusCancelled_) focusTarget_ = panel_->focusWidget();
                {
                    QScopedValueRollback<bool> capturing(capturing_, true);
                    drawer_->setExpanded(true, true);
                }
                // Clear stale focus intent before the drawer's input filter finishes it.
                qApp->installEventFilter(this);
                record();
                if (!drawer_->isDrawerAnimating()) restoreFocus();
            });
        } else {
            pendingOpen_ = false;
            focusTarget_.clear();
            qApp->removeEventFilter(this);
            drawer_->setExpanded(false, true);
            record();
            if (!drawer_->isDrawerAnimating()) panel_->hide();
        }
    }

    void requestFocus(QWidget* widget, Qt::FocusReason reason)
    {
        focusCancelled_ = false;
        focusTarget_ = widget;
        widget->setFocus(reason);
        if (!pendingOpen_ && !drawer_->isDrawerAnimating() && widget->isVisible())
            focusTarget_.clear();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!capturing_ && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress)) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget != panel_ && !panel_->isAncestorOf(widget)) {
                focusCancelled_ = true;
                focusTarget_.clear();
            }
        }
        return false;
    }

private:
    void restoreFocus()
    {
        qApp->removeEventFilter(this);
        if (requested_ && focusTarget_ && focusTarget_->isVisible()
            && focusTarget_->isEnabled()) focusTarget_->setFocus(Qt::OtherFocusReason);
        focusTarget_.clear();
    }
    void record()
    {
        panel_->setProperty("regmapPanelSnapshotBytes", drawer_->drawerSnapshotBytes());
        panel_->setProperty("regmapPanelPeakSnapshotBytes",
            qMax(panel_->property("regmapPanelPeakSnapshotBytes").toLongLong(), drawer_->drawerSnapshotBytes()));
        panel_->setProperty("regmapPanelPreparationMs", drawer_->drawerPreparationMs());
    }
    QWidget* panel_;
    ElaDrawerArea* drawer_;
    bool requested_{};
    bool capturing_{};
    bool pendingOpen_{};
    bool focusCancelled_{};
    QPointer<QWidget> focusTarget_;
    quint64 generation_{};
};

PanelMotion* motion(QWidget* panel)
{
    return panel ? panel->findChild<PanelMotion*>(QStringLiteral("workbenchPanelMotion"),
                                                Qt::FindDirectChildrenOnly) : nullptr;
}
}
#endif

namespace WorkbenchControls {
void installPanelMotion(QWidget* panel, Qt::Edge edge)
{
#ifdef REGMAP_ENABLE_ELA
    if (panel && panel->layout() && backend() == Backend::ela && !motion(panel))
        new PanelMotion(panel, edge);
#else
    Q_UNUSED(panel); Q_UNUSED(edge);
#endif
}

void setPanelVisible(QWidget* panel, bool visible)
{
    if (!panel) return;
#ifdef REGMAP_ENABLE_ELA
    if (auto* controller = motion(panel)) { controller->setVisible(visible); return; }
#endif
    panel->setVisible(visible);
}

bool panelRequestedVisible(QWidget* panel)
{
#ifdef REGMAP_ENABLE_ELA
    if (auto* controller = motion(panel)) return controller->requested();
#endif
    return panel && panel->isVisible();
}

void finishPanelAnimations(QWidget* window)
{
#ifdef REGMAP_ENABLE_ELA
    for (auto* controller : window->findChildren<PanelMotion*>()) controller->finish();
#else
    Q_UNUSED(window);
#endif
}

void focusWhenVisible(QWidget* widget, Qt::FocusReason reason)
{
#ifdef REGMAP_ENABLE_ELA
    for (auto* parent = widget; parent; parent = parent->parentWidget()) {
        if (auto* controller = motion(parent)) { controller->requestFocus(widget, reason); return; }
    }
#endif
    widget->setFocus(reason);
}
}

#ifdef REGMAP_ENABLE_ELA
#include "workbench_panel_motion.moc"
#endif
