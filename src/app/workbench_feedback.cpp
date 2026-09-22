#include "workbench_controls.hpp"

#include <QApplication>
#include <QAccessible>
#include <QHelpEvent>
#include <QLabel>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#ifdef REGMAP_ENABLE_ELA
#include <ElaMessageBar.h>
#include <ElaToolTip.h>

namespace {
class ControlToolTip final : public QObject {
public:
    explicit ControlToolTip(QWidget* control) : QObject(control), control_(control)
    {
        control->installEventFilter(this);
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] { hide(); });
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == control_ && event->type() == QEvent::ToolTip) {
            if (control_->toolTip().isEmpty() || !control_->isVisible()) return false;
            if (!tip_) {
                // No upstream Enter filter: retain Qt's ToolTip delay and routing.
                tip_ = new ElaToolTip;
                tip_->setParent(control_, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
                tip_->setAttribute(Qt::WA_ShowWithoutActivating);
                tip_->setObjectName(QStringLiteral("workbenchToolTip"));
                label_ = new QLabel(tip_);
                label_->setObjectName(QStringLiteral("workbenchToolTipText"));
                label_->setTextFormat(Qt::PlainText);
                label_->setWordWrap(true);
                tip_->setCustomWidget(label_);
            }
            const auto* help = static_cast<QHelpEvent*>(event);
            auto* screen = QGuiApplication::screenAt(help->globalPos());
            if (!screen) screen = control_->screen();
            const auto bounds = screen->availableGeometry().adjusted(8, 8, -8, -8);
            label_->setFont(qApp->font());
            label_->setText(control_->toolTip());
            label_->setFixedWidth(qMax(1, qMin(360, bounds.width() - 48)));
            tip_->setToolTip(control_->toolTip());
            tip_->setAccessibleName(control_->toolTip());
            tip_->adjustSize();
            tip_->move(qBound(bounds.left(), help->globalPos().x() + 12, qMax(bounds.left(), bounds.right() - tip_->width() + 1)),
                       qBound(bounds.top(), help->globalPos().y() + 18, qMax(bounds.top(), bounds.bottom() - tip_->height() + 1)));
            if (ownerWindow_ != control_->window()) {
                if (ownerWindow_) ownerWindow_->removeEventFilter(this);
                ownerWindow_ = control_->window();
                ownerWindow_->installEventFilter(this);
            }
            tip_->show();
            timer_.start(control_->toolTipDuration() > 0 ? control_->toolTipDuration() : 10000);
            event->accept();
            return true;
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide ||
            event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress ||
            event->type() == QEvent::ToolTipChange || event->type() == QEvent::WindowDeactivate)
            hide();
        return QObject::eventFilter(watched, event);
    }
private:
    void hide() { timer_.stop(); if (tip_) tip_->hide(); }
    QWidget* control_;
    QPointer<ElaToolTip> tip_;
    QLabel* label_{nullptr};
    QPointer<QWidget> ownerWindow_;
    QTimer timer_;
};
}
#endif

namespace WorkbenchControls {
void installToolTip(QWidget* widget)
{
#ifdef REGMAP_ENABLE_ELA
    if (!widget || backend() != Backend::ela || widget->property("regmapElaToolTip").toBool()) return;
    widget->setProperty("regmapElaToolTip", true);
    new ControlToolTip(widget);
#else
    Q_UNUSED(widget);
#endif
}

void dismissMessage(QWidget* parent)
{
    if (!parent) return;
    if (auto* old = parent->findChild<QWidget*>(QStringLiteral("workbenchSuccessMessage"), Qt::FindDirectChildrenOnly))
        delete old;
}

QWidget* successMessage(QWidget* parent, const QString& title, const QString& text, int duration)
{
#ifdef REGMAP_ENABLE_ELA
    if (!parent || backend() != Backend::ela || !parent->isVisible() || parent->width() < 240 || parent->height() < 200) return nullptr;
    dismissMessage(parent);
    ElaMessageBar::success(ElaMessageBarType::BottomRight, title, text, qMax(1, duration), parent);
    auto* bar = parent->findChild<ElaMessageBar*>(QStringLiteral("ElaMessageBar"), Qt::FindDirectChildrenOnly);
    if (!bar) return nullptr;
    bar->setObjectName(QStringLiteral("workbenchSuccessMessage"));
    bar->setAccessibleName(title);
    bar->setAccessibleDescription(text);
    bar->setProperty("ElaMessageAccentLight", WorkbenchTheme::tokens(WorkbenchTheme::Mode::light).success);
    bar->setProperty("ElaMessageAccentDark", WorkbenchTheme::tokens(WorkbenchTheme::Mode::dark).success);
    bar->setFocusPolicy(Qt::NoFocus);
    bar->raise();
    QAccessibleEvent announced(bar, QAccessible::Alert);
    QAccessible::updateAccessibility(&announced);
    return bar;
#else
    Q_UNUSED(parent); Q_UNUSED(title); Q_UNUSED(text); Q_UNUSED(duration);
    return nullptr;
#endif
}
}
