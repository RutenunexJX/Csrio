#include "workbench_controls.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMouseEvent>
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
            queuePopup();
        } else if (object == editor_ && event->type() == QEvent::Hide) {
            stopWaitingForRelease();
        } else if (waitingForRelease_ && event->type() == QEvent::MouseButtonRelease
                   && static_cast<QMouseEvent*>(event)->buttons() == Qt::NoButton) {
            stopWaitingForRelease();
            queuePopup();
        }
        return QObject::eventFilter(object, event);
    }
private:
    void stopWaitingForRelease()
    {
        if (!waitingForRelease_) return;
        waitingForRelease_ = false;
        qApp->removeEventFilter(this);
    }

    void queuePopup()
    {
        QTimer::singleShot(0, this, [this] {
            if (!editor_->isVisible()) return;
            // A double-click creates the editor on the second press. Opening
            // before its release lets that release immediately dismiss the popup.
            if (QApplication::mouseButtons() != Qt::NoButton) {
                if (!waitingForRelease_) {
                    waitingForRelease_ = true;
                    qApp->installEventFilter(this);
                }
                return;
            }
            editor_->setProperty("typePopupAutoOpened", true);
            // Existing offscreen edit fixtures route keys to the line edit.
            // Native/editor-specific tests exercise the actual popup.
            if (QApplication::platformName() != QLatin1String("offscreen")
                || editor_->property("regmapTestNativePopup").toBool()) editor_->showPopup();
        });
    }

    QComboBox* editor_;
    bool waitingForRelease_{false};
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
