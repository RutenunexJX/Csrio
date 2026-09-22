#include "suiteui_adapter.hpp"
#include "workbench_controls.hpp"
#ifdef REGMAP_ENABLE_SUITEUI
#include <SuiteUi/ControlStyle.hpp>
#include <QAbstractItemView>
#include <QEvent>
#include <QPointer>
#include <QStyleFactory>

namespace {
class ViewBoundaries final : public QObject {
public:
    explicit ViewBoundaries(QApplication& application) : QObject(&application) {
        fusion_ = QStyleFactory::create("Fusion");
        fusion_->setParent(this);
        application.installEventFilter(this);
    }
private:
    QStyle* fusion_ = nullptr;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::Polish && event->type() != QEvent::ParentChange) return false;
        auto* widget = qobject_cast<QWidget*>(object);
        if (!widget) return false;
        bool protect = false;
        for (auto* parent = widget; parent; parent = parent->parentWidget()) {
            const auto name = QByteArray(parent->metaObject()->className());
            if (qobject_cast<QAbstractItemView*>(parent) || name == "BitfieldView" || name == "AddressSpaceView") {
                protect = true;
                break;
            }
        }
        if (protect && !widget->property("regmapClassicView").toBool()) {
            widget->setProperty("regmapClassicView", true);
            widget->setStyle(fusion_);
        } else if (!protect && widget->property("regmapClassicView").toBool()) {
            widget->setProperty("regmapClassicView", false);
            widget->setStyle(nullptr);
        }
        return false;
    }
};
class Style final : public SuiteUi::ControlStyle {
    bool isPrimary(const QWidget* widget) const override {
        if (!widget) return false;
        const auto name = widget->objectName();
        return name == "saveSyncButton" || name == "synchronizeButton" || ControlStyle::isPrimary(widget);
    }
};
}
#endif

namespace RegMapSuiteUi {
bool enabled() {
#ifdef REGMAP_ENABLE_SUITEUI
    return WorkbenchControls::backend() == WorkbenchControls::Backend::suiteUi;
#else
    return false;
#endif
}
QStyle* install(QApplication& application) {
#ifdef REGMAP_ENABLE_SUITEUI
    static QPointer<Style> backend;
    if (!backend) {
        backend = new Style;
        backend->setObjectName("RegMapSuiteUi");
        application.setStyle(backend);
        new ViewBoundaries(application);
    }
    return backend;
#else
    Q_UNUSED(application);
    return nullptr;
#endif
}
void update(QStyle* style, WorkbenchTheme::Mode mode) {
#ifdef REGMAP_ENABLE_SUITEUI
    const auto& t = WorkbenchTheme::tokens(mode);
    auto* controls = static_cast<Style*>(style);
    controls->setControlPalette({
        {t.panel, t.selection, t.divider, t.panel},
        {t.text, t.text, t.text, t.mutedText},
        {t.accent, t.accent.lighter(110), t.accent.darker(110), t.panel},
        {t.onAccent, t.onAccent, t.onAccent, t.mutedText},
        {t.border, t.focus, t.accent, t.divider}, t.focus});
    controls->setAnimationsEnabled(!WorkbenchTheme::reducedMotionEnabled());
#else
    Q_UNUSED(style);
    Q_UNUSED(mode);
#endif
}
}
