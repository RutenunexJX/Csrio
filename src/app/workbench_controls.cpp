#include "workbench_controls.hpp"

#include <QAction>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QHeaderView>
#include <QTableView>
#include <QTreeView>
#include <QWheelEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <memory>

#ifdef REGMAP_ENABLE_ELA
#include <ElaApplication.h>
#include <ElaCheckBox.h>
#include <ElaComboBox.h>
#include <ElaContentDialog.h>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaPushButton.h>
#include <ElaScrollBar.h>
#include <ElaTabWidget.h>
#include <ElaTheme.h>
#include <ElaToolBar.h>
#include <ElaToolButton.h>
#include <ElaTreeView.h>
#include <ElaText.h>
#endif

namespace WorkbenchControls {
Backend backend()
{
    static const Backend selected = [] {
        if (qEnvironmentVariable("REGMAP_UI_STYLE") == QStringLiteral("classic"))
            return Backend::classic;
#ifdef REGMAP_ENABLE_ELA
        return Backend::ela;
#elif defined(REGMAP_ENABLE_SUITEUI)
        return Backend::suiteUi;
#else
        return Backend::classic;
#endif
    }();
    return selected;
}

#ifdef REGMAP_ENABLE_ELA
namespace {
template<class Control>
class FocusControl : public Control {
public:
    explicit FocusControl(QWidget* parent) : Control(parent) {}
protected:
    void paintEvent(QPaintEvent* event) override
    {
        Control::paintEvent(event);
        const bool primary = this->objectName() == QStringLiteral("saveSyncButton")
            || this->objectName() == QStringLiteral("synchronizeButton");
        if (!this->isEnabled() || !this->hasFocus()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(primary ? WorkbenchTheme::currentTokens().onAccent
                                   : WorkbenchTheme::currentTokens().focus, 2.0));
        painter.drawRoundedRect(this->rect().adjusted(2, 2, -2, -2), 5, 5);
    }
};

class ActionButton final : public FocusControl<ElaPushButton> {
public:
    using FocusControl::FocusControl;
    QSize sizeHint() const override
    {
        const auto textSize = fontMetrics().size(Qt::TextShowMnemonic, text());
        return QSize(textSize.width() + 24 + (icon().isNull() ? 0 : iconSize().width() + 8),
                     qMax(WorkbenchTheme::metrics().standardControlHeight, textSize.height() + 12));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

class ChoiceBox final : public FocusControl<ElaComboBox> {
public:
    using FocusControl::FocusControl;
};

class StableScrollBar final : public ElaScrollBar {
public:
    explicit StableScrollBar(Qt::Orientation orientation) : ElaScrollBar(orientation)
    {
        setProperty("ElaUseQtScrollSemantics", true);
        setIsAnimation(false);
    }
protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (smoothWheelEnabled() && !WorkbenchTheme::reducedMotionEnabled())
            ElaScrollBar::wheelEvent(event);
        else {
            stopSmoothWheel();
            QScrollBar::wheelEvent(event);
        }
    }
    void contextMenuEvent(QContextMenuEvent* event) override { QScrollBar::contextMenuEvent(event); }
};

class PrecisionWheelRouter final : public QObject {
public:
    explicit PrecisionWheelRouter(QAbstractScrollArea* area) : QObject(area), area_(area)
    {
        area->installEventFilter(this);
        area->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress) {
            if (auto* tree = qobject_cast<QTreeView*>(area_)) ElaTreeView::finishExpansion(tree);
            for (auto* bar : {area_->horizontalScrollBar(), area_->verticalScrollBar()})
                if (auto* ela = qobject_cast<ElaScrollBar*>(bar)) ela->stopSmoothWheel();
        }
        if (event->type() != QEvent::Wheel) return false;
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->pixelDelta().isNull()) return false;
        const bool horizontal = qAbs(wheel->pixelDelta().x()) > qAbs(wheel->pixelDelta().y())
            || wheel->modifiers().testFlag(Qt::ShiftModifier);
        auto* bar = horizontal ? area_->horizontalScrollBar() : area_->verticalScrollBar();
        QApplication::sendEvent(bar, wheel);
        return wheel->isAccepted();
    }
private:
    QAbstractScrollArea* area_;
};

class ContentDialog final : public ElaContentDialog {
public:
    using ElaContentDialog::ElaContentDialog;
protected:
    // Preserve QDialog's default button and Escape contracts; stock Ela buttons
    // are deliberately replaced with the application's existing button roles.
    void keyPressEvent(QKeyEvent* event) override { QDialog::keyPressEvent(event); }
};

class TextInput final : public FocusControl<ElaLineEdit> {
public:
    using FocusControl::FocusControl;
protected:
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        // Keep Qt's undo, read-only, clipboard and localization contracts.
        const std::unique_ptr<QMenu> actions(createStandardContextMenu());
        const std::unique_ptr<QMenu> popup(WorkbenchControls::menu(this));
        popup->addActions(actions->actions());
        popup->exec(event->globalPos());
    }
};

template<class Control>
Control* prepare(Control* control, int minimumHeight = WorkbenchTheme::metrics().compactControlHeight)
{
    control->setMinimumSize(0, minimumHeight);
    control->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    control->setFont(qApp->font());
    control->setProperty("regmapElaControl", true);
    control->setFocusPolicy(Qt::StrongFocus);
    installToolTip(control);
    return control;
}

void mapTheme(WorkbenchTheme::Mode mode)
{
    using namespace ElaThemeType;
    const auto elaMode = mode == WorkbenchTheme::Mode::light ? Light : Dark;
    const auto& t = WorkbenchTheme::tokens(mode);
    const auto set = [elaMode](ThemeColor role, const QColor& color) {
        eTheme->setThemeColor(elaMode, role, color);
    };
    set(WindowBase, t.application); set(WindowCentralStackBase, t.canvas);
    set(PrimaryNormal, t.accent); set(PrimaryHover, t.accent.lighter(110));
    set(PrimaryPress, t.accent.darker(110));
    set(PopupBorder, t.border); set(PopupBorderHover, t.focus);
    set(PopupBase, t.raisedSurface); set(PopupHover, t.selection);
    set(DialogBase, t.input); set(DialogLayoutArea, t.panel);
    set(BasicText, t.text); set(BasicTextInvert, t.onAccent);
    set(BasicDetailsText, t.mutedText); set(BasicTextNoFocus, t.mutedText);
    set(BasicTextDisable, t.mutedText); set(BasicTextPress, t.text);
    set(BasicTextCategory, t.text);
    set(BasicBorder, t.border); set(BasicBorderDeep, t.border); set(BasicBorderHover, t.focus);
    set(BasicBase, t.input); set(BasicBaseDeep, t.panel); set(BasicDisable, t.panel);
    set(BasicHover, t.selection); set(BasicPress, t.divider);
    set(BasicSelectedHover, t.selection); set(BasicBaseLine, t.border);
    set(BasicHemline, t.border); set(BasicIndicator, t.accent);
    set(BasicChute, t.divider); set(BasicAlternating, t.panel);
    set(BasicBaseAlpha, t.input); set(BasicBaseDeepAlpha, t.panel);
    set(BasicHoverAlpha, t.selection); set(BasicPressAlpha, t.divider);
    set(BasicSelectedAlpha, t.selection); set(BasicSelectedHoverAlpha, t.selection);
    set(StatusDanger, t.error); set(ScrollBarHandle, t.mutedText);
    set(ToggleSwitchNoToggledCenter, t.mutedText);
}
} // namespace
#endif

void initialize(QApplication& application)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() != Backend::ela || application.property("regmapElaInitialized").toBool()) return;
    const auto font = application.font();
    const bool nativeSiblings = QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    eApp->init();
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, nativeSiblings);
    application.setFont(font);
    application.setProperty("regmapElaInitialized", true);
    mapTheme(WorkbenchTheme::Mode::light);
    mapTheme(WorkbenchTheme::Mode::dark);
#else
    Q_UNUSED(application);
#endif
}

void updateTheme(WorkbenchTheme::Mode mode)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        qApp->setProperty("ElaReducedMotion", WorkbenchTheme::reducedMotionEnabled());
        eTheme->setThemeMode(mode == WorkbenchTheme::Mode::light
                                ? ElaThemeType::Light : ElaThemeType::Dark);
    }
#else
    Q_UNUSED(mode);
#endif
}

QPushButton* pushButton(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* button = prepare(new ActionButton(parent), WorkbenchTheme::metrics().standardControlHeight);
        button->setBorderRadius(WorkbenchTheme::metrics().smallRadius);
        return button;
    }
#endif
    return new QPushButton(parent);
}
QPushButton* pushButton(const QString& text, QWidget* parent)
{
    auto* button = pushButton(parent);
    button->setText(text);
    return button;
}
QToolButton* toolButton(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* button = prepare(new FocusControl<ElaToolButton>(parent));
        // Match QToolButton: mouse clicks must not steal an active cell editor's
        // focus before MainWindow's transactional navigation guard runs.
        button->setFocusPolicy(Qt::TabFocus);
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setIconSize(QSize(16, 16));
        button->setBorderRadius(WorkbenchTheme::metrics().smallRadius);
        button->setIsTransparent(false);
        QObject::connect(button, &QObject::objectNameChanged, button, [button](const QString& name) {
            button->setProperty("ElaPrimary", name == QStringLiteral("saveSyncButton")
                                                || name == QStringLiteral("synchronizeButton"));
            button->update();
        });
        QObject::connect(button, &QToolButton::toggled, button, &ElaToolButton::setIsSelected);
        return button;
    }
#endif
    return new QToolButton(parent);
}
QLineEdit* lineEdit(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* edit = prepare(new TextInput(parent));
        edit->setIsClearButtonEnable(false);
        edit->setStyleSheet({});
        edit->setTextMargins(8, 0, 8, 0);
        return edit;
    }
#endif
    return new QLineEdit(parent);
}
QComboBox* comboBox(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) return prepare(new ChoiceBox(parent));
#endif
    return new QComboBox(parent);
}

QLabel* label(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* text = new ElaText(parent);
        text->setThemeColorEnabled(false);
        text->setStyleSheet({});
        text->setPalette(QPalette());
        text->setFont(qApp->font());
        text->setWordWrap(false);
        return text;
    }
#endif
    return new QLabel(parent);
}

QLabel* label(const QString& text, QWidget* parent)
{
    auto* result = label(parent);
    result->setText(text);
    return result;
}

QDialog* contentDialog(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* dialog = new ContentDialog(parent);
        dialog->setStandardButtonsVisible(false);
        dialog->setFont(qApp->font());
        auto* central = new QWidget(dialog);
        auto* shell = new QVBoxLayout(central);
        shell->setContentsMargins(0, 0, 0, 0);
        auto* title = label(dialog->windowTitle(), central);
        title->setObjectName(QStringLiteral("contentDialogTitle"));
        title->setContentsMargins(16, 16, 16, 0);
        auto font = title->font(); font.setBold(true); title->setFont(font);
        QObject::connect(dialog, &QWidget::windowTitleChanged, title, &QLabel::setText);
        auto* body = new QWidget(central);
        body->setObjectName(QStringLiteral("contentDialogBody"));
        shell->addWidget(title); shell->addWidget(body);
        dialog->setCentralWidget(central);
        return dialog;
    }
#endif
    return new QDialog(parent);
}

QWidget* dialogContent(QDialog* dialog)
{
    if (auto* body = dialog->findChild<QWidget*>(QStringLiteral("contentDialogBody"))) return body;
    return dialog;
}
QCheckBox* checkBox(const QString& text, QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* check = prepare(new FocusControl<ElaCheckBox>(parent));
        check->setText(text);
        return check;
    }
#endif
    return new QCheckBox(text, parent);
}
QMenu* menu(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* menu = new ElaMenu(parent);
        menu->setNativeMenuBehavior(true);
        menu->setFont(qApp->font());
        menu->setMenuItemHeight(WorkbenchTheme::metrics().standardControlHeight);
        return menu;
    }
#endif
    return new QMenu(parent);
}
QMenuBar* menuBar(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* bar = new ElaMenuBar(parent);
        bar->setMinimumHeight(WorkbenchTheme::metrics().compactControlHeight);
        return bar;
    }
#endif
    return new QMenuBar(parent);
}
QMenu* addMenu(QMenuBar* parent, const QString& title)
{
    auto* child = menu(parent);
    child->setTitle(title);
    parent->addMenu(child);
    return child;
}
QMenu* addMenu(QMenu* parent, const QString& title)
{
    auto* child = menu(parent);
    child->setTitle(title);
    parent->addMenu(child);
    return child;
}
QToolBar* toolBar(const QString& title, QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* toolbar = new ElaToolBar(title, parent);
        toolbar->setToolBarSpacing(WorkbenchTheme::metrics().baseSpacing);
        return toolbar;
    }
#endif
    return new QToolBar(title, parent);
}
void addAction(QToolBar* toolbar, QAction* action)
{
    if (backend() != Backend::ela) {
        toolbar->addAction(action);
        return;
    }
    auto* button = toolButton(toolbar);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* proxy = toolbar->addWidget(button);
    const auto update = [action, proxy] {
        proxy->setText(action->text());
        proxy->setIcon(action->icon());
        proxy->setToolTip(action->toolTip());
        proxy->setEnabled(action->isEnabled());
        proxy->setVisible(action->isVisible());
        proxy->setCheckable(action->isCheckable());
        proxy->setChecked(action->isChecked());
    };
    QObject::connect(action, &QAction::changed, proxy, update);
    QObject::connect(proxy, &QAction::triggered, action, &QAction::trigger);
    update();
}
QTabWidget* tabWidget(QWidget* parent)
{
#ifdef REGMAP_ENABLE_ELA
    if (backend() == Backend::ela) {
        auto* tabs = new ElaTabWidget(parent);
        tabs->setTabsClosable(false);
        tabs->setMovable(false);
        tabs->setAcceptDrops(false);
        tabs->tabBar()->setAcceptDrops(false);
        tabs->tabBar()->setUsesScrollButtons(true);
        tabs->setIsContainerAcceptDrops(false);
        tabs->setTabSize(QSize(140, WorkbenchTheme::metrics().standardControlHeight));
        return tabs;
    }
#endif
    return new QTabWidget(parent);
}

void styleScrollArea(QAbstractScrollArea* area)
{
#ifdef REGMAP_ENABLE_ELA
    if (!area || backend() != Backend::ela || area->property("regmapElaScrollBars").toBool()) return;
    if (!area->property("regmapElaItemView").isValid()) area->setProperty("regmapElaItemView", false);
    if (auto* table = qobject_cast<QTableView*>(area)) {
        for (auto* header : {table->horizontalHeader(), table->verticalHeader()})
            header->setProperty("regmapElaItemView", area->property("regmapElaItemView"));
    } else if (auto* tree = qobject_cast<QTreeView*>(area)) {
        tree->header()->setProperty("regmapElaItemView", false);
    }
    // Install before client connections: Qt preserves range, value and steps.
    area->setHorizontalScrollBar(new StableScrollBar(Qt::Horizontal));
    area->setVerticalScrollBar(new StableScrollBar(Qt::Vertical));
    area->horizontalScrollBar()->setAccessibleName(QStringLiteral("Horizontal scroll"));
    area->verticalScrollBar()->setAccessibleName(QStringLiteral("Vertical scroll"));
    area->setProperty("regmapElaScrollBars", true);
#else
    Q_UNUSED(area);
#endif
}

void enableSmoothScrolling(QAbstractScrollArea* area)
{
#ifdef REGMAP_ENABLE_ELA
    if (!area || backend() != Backend::ela || area->property("regmapSmoothScrolling").toBool()) return;
    styleScrollArea(area);
    area->setProperty("regmapSmoothScrolling", true);
    new PrecisionWheelRouter(area);
    if (auto* view = qobject_cast<QAbstractItemView*>(area)) {
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    for (auto* bar : {area->horizontalScrollBar(), area->verticalScrollBar()}) {
        if (auto* ela = qobject_cast<ElaScrollBar*>(bar)) {
            ela->setSmoothWheelEnabled(true);
            ela->setWheelAnimationDuration(160);
        }
    }
#else
    Q_UNUSED(area);
#endif
}

void styleHierarchy(QTreeView* view)
{
#ifdef REGMAP_ENABLE_ELA
    if (!view || backend() != Backend::ela) return;
    auto* style = ElaTreeView::createStyle(qApp, qMax(28, view->fontMetrics().height() + 10));
    view->setStyle(style);
    QObject::connect(view, &QObject::destroyed, style, &QObject::deleteLater);
    view->setProperty("regmapElaItemView", true);
    view->setProperty("ElaUseQtItemSemantics", true);
    view->setAnimated(!WorkbenchTheme::reducedMotionEnabled());
    enableSmoothScrolling(view);
#else
    Q_UNUSED(view);
#endif
}

QDialogButtonBox* dialogButtons(QWidget* parent)
{
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, parent);
    if (backend() != Backend::ela) return box;
    for (auto which : {QDialogButtonBox::Ok, QDialogButtonBox::Cancel}) {
        auto* original = box->button(which);
        auto* button = pushButton(original->text(), box);
        button->setProperty("regmapStandardButton", static_cast<int>(which));
        button->setObjectName(which == QDialogButtonBox::Ok
                                  ? QStringLiteral("dialogAcceptButton") : QStringLiteral("dialogCancelButton"));
        button->setMinimumWidth(88);
        button->setAutoDefault(false);
        const auto role = box->buttonRole(original);
        box->removeButton(original);
        delete original;
        box->addButton(button, role);
        button->setDefault(which == QDialogButtonBox::Ok);
    }
    return box;
}

QPushButton* standardButton(QDialogButtonBox* box, QDialogButtonBox::StandardButton which)
{
    if (!box) return nullptr;
    if (auto* button = box->button(which)) return button;
    // Qt does not assign StandardButton IDs to custom QPushButton subclasses.
    for (auto* button : box->buttons()) {
        if (button->property("regmapStandardButton").toInt() == static_cast<int>(which))
            return qobject_cast<QPushButton*>(button);
    }
    return nullptr;
}
} // namespace WorkbenchControls
