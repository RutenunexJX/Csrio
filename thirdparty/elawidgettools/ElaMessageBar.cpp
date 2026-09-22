#include "ElaMessageBar.h"

#include "ElaApplication.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>

#include "ElaIconButton.h"
#include "ElaTheme.h"
#include "private/ElaMessageBarPrivate.h"

ElaMessageBar::ElaMessageBar(ElaMessageBarType::PositionPolicy policy, ElaMessageBarType::MessageMode messageMode, QString& title, QString& text, int displayMsec, QWidget* parent)
    : QWidget{parent}, d_ptr(new ElaMessageBarPrivate())
{
    Q_D(ElaMessageBar);
    d->q_ptr = this;
    d->_borderRadius = 6;
    d->_title = title;
    d->_text = text;
    d->_policy = policy;
    d->_messageMode = messageMode;
    d->_themeMode = eTheme->getThemeMode();
    setMouseTracking(true);
    d->_pOpacity = 1;
    setFont(qApp->font());
    setAccessibleName(title);
    setAccessibleDescription(text);
    parent->installEventFilter(this);
    d->_closeButton = new ElaIconButton(ElaIconType::Xmark, 17, d->_closeButtonWidth, 30, this);
    switch (d->_messageMode)
    {
    case ElaMessageBarType::Success:
    {
        d->_closeButton->setLightHoverColor(QColor(0xCA, 0xDE, 0xC8));
        d->_closeButton->setDarkHoverColor(QColor(0xCA, 0xDE, 0xC8));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    case ElaMessageBarType::Warning:
    {
        d->_closeButton->setLightHoverColor(QColor(0x5E, 0x4C, 0x22));
        d->_closeButton->setDarkHoverColor(QColor(0x5E, 0x4C, 0x22));
        d->_closeButton->setLightIconColor(Qt::white);
        d->_closeButton->setDarkIconColor(Qt::white);
        d->_closeButton->setLightHoverIconColor(Qt::white);
        break;
    }
    case ElaMessageBarType::Information:
    {
        d->_closeButton->setLightHoverColor(QColor(0xDE, 0xDE, 0xDE));
        d->_closeButton->setDarkHoverColor(QColor(0xDE, 0xDE, 0xDE));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    case ElaMessageBarType::Error:
    {
        d->_closeButton->setLightHoverColor(QColor(0xF2, 0xDD, 0xE0));
        d->_closeButton->setDarkHoverColor(QColor(0xF2, 0xDD, 0xE0));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    }
    d->_closeButton->setBorderRadius(5);
    d->_closeButton->setAccessibleName(tr("Dismiss notification"));
    d->_closeButton->setObjectName("messageDismissButton");
    d->_closeButton->setAutoDefault(false);
    d->_closeButton->setLightIconColor(ElaThemeColor(ElaThemeType::Light, BasicText));
    d->_closeButton->setDarkIconColor(ElaThemeColor(ElaThemeType::Dark, BasicText));
    d->_closeButton->setLightHoverIconColor(ElaThemeColor(ElaThemeType::Light, BasicText));
    d->_closeButton->setDarkHoverIconColor(ElaThemeColor(ElaThemeType::Dark, BasicText));
    d->_closeButton->setLightHoverColor(ElaThemeColor(ElaThemeType::Light, BasicHover));
    d->_closeButton->setDarkHoverColor(ElaThemeColor(ElaThemeType::Dark, BasicHover));
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this, d](ElaThemeType::ThemeMode mode) {
        d->_themeMode = mode;
        update();
    });
    connect(d->_closeButton, &ElaIconButton::clicked, d, &ElaMessageBarPrivate::messageBarEnd);
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 10, 0);
    mainLayout->addStretch();
    mainLayout->addWidget(d->_closeButton);
    setObjectName("ElaMessageBar");
    setStyleSheet("#ElaMessageBar{background-color:transparent;}");
    d->_messageBarCreate(displayMsec);
}

ElaMessageBar::~ElaMessageBar()
{
}

void ElaMessageBar::success(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    // qDebug() << QApplication::topLevelWidgets();
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }

    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Success, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::warning(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Warning, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::information(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Information, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::error(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Error, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    Q_D(ElaMessageBar);
    QPainter painter(this);
    painter.setOpacity(d->_pOpacity);
    painter.setRenderHint(QPainter::Antialiasing);
    eTheme->drawEffectShadow(&painter, rect(), d->_shadowBorderWidth, d->_borderRadius);
    const QRect frame = rect().adjusted(6, 6, -6, -6);
    painter.setPen(ElaThemeColor(d->_themeMode, PopupBorder));
    painter.setBrush(ElaThemeColor(d->_themeMode, PopupBase));
    painter.drawRoundedRect(frame, d->_borderRadius, d->_borderRadius);
    const auto accentProperty = property(d->_themeMode == ElaThemeType::Light
                                            ? "ElaMessageAccentLight" : "ElaMessageAccentDark");
    const QColor accent = accentProperty.isValid() ? accentProperty.value<QColor>()
        : d->_messageMode == ElaMessageBarType::Error ? ElaThemeColor(d->_themeMode, StatusDanger)
                                                    : ElaThemeColor(d->_themeMode, PrimaryNormal);
    painter.setPen(QPen(accent, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(27, 27), 8, 8);
    if (d->_messageMode == ElaMessageBarType::Success) {
        painter.drawLine(QPointF(23, 27), QPointF(26, 30));
        painter.drawLine(QPointF(26, 30), QPointF(31, 24));
    } else if (d->_messageMode == ElaMessageBarType::Error) {
        painter.drawLine(QPointF(24, 24), QPointF(30, 30));
        painter.drawLine(QPointF(24, 30), QPointF(30, 24));
    } else {
        painter.drawText(QRect(19, 19, 16, 16), Qt::AlignCenter,
                         d->_messageMode == ElaMessageBarType::Information ? QStringLiteral("i") : QStringLiteral("!"));
    }
    QFont titleFont = font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(ElaThemeColor(d->_themeMode, BasicText));
    const int lineHeight = fontMetrics().height();
    const QRect titleRect(50, 16, qMax(1, width() - 100), lineHeight);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(d->_title, Qt::ElideRight, titleRect.width()));
    painter.setFont(font());
    painter.drawText(QRect(50, 20 + lineHeight, qMax(1, width() - 100), height() - lineHeight - 36),
                     Qt::AlignLeft | Qt::TextWordWrap | Qt::TextWrapAnywhere, d->_text);
}

bool ElaMessageBar::eventFilter(QObject* watched, QEvent* event)
{
    Q_D(ElaMessageBar);
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        for (auto* animation : findChildren<QPropertyAnimation*>()) {
            if (animation->targetObject() == this && animation->propertyName() == "pos")
                animation->stop();
        }
        d->_updateSize();
        int startX = 0, startY = 0, endX = 0, endY = 0;
        d->_calculateInitialPos(startX, startY, endX, endY);
        move(endX, endY);
        d->_isNormalDisplay = true;
    }
    return QWidget::eventFilter(watched, event);
}
