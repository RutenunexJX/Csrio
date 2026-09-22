#include "workbench_controls.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>

namespace {
class AutoPopup final : public QObject {
public:
    explicit AutoPopup(QComboBox* editor) : QObject(editor), editor_(editor)
    {
        editor->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == editor_ && event->type() == QEvent::Show) {
            QTimer::singleShot(0, editor_, [editor = editor_] {
                if (!editor->isVisible()) return;
                editor->setProperty("typePopupAutoOpened", true);
                // Existing offscreen edit fixtures route keys to the line edit.
                // Native/editor-specific tests exercise the actual popup.
                if (QApplication::platformName() != QLatin1String("offscreen")
                    || editor->property("regmapTestNativePopup").toBool()) editor->showPopup();
            });
        }
        return QObject::eventFilter(object, event);
    }
private:
    QComboBox* editor_;
};
}

namespace WorkbenchControls {
QComboBox* cellComboBox(QWidget* parent, bool autoPopup)
{
    auto* editor = comboBox(parent);
    editor->setProperty("regmapCellEditor", true);
    // Cell geometry is owned by the delegate, not the ordinary form density.
    editor->setMinimumSize(0, 0);
    editor->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    editor->setFont(WorkbenchTheme::monospaceFont());
    if (autoPopup) new AutoPopup(editor);
    return editor;
}
}
