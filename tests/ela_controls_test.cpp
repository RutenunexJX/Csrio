#include "workbench_controls.hpp"
#include "workbench_theme.hpp"

#include <QAbstractItemView>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QDir>
#include <memory>
#include <array>

class ElaContracts final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::light);
        QCOMPARE(WorkbenchControls::backend(), WorkbenchControls::Backend::ela);
    }

    void realControlsAndTheme()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* button = WorkbenchControls::pushButton("&Apply", &host);
        auto* tool = WorkbenchControls::toolButton(&host);
        tool->setText("Toggle"); tool->setCheckable(true);
        tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* edit = WorkbenchControls::lineEdit(&host);
        auto* check = WorkbenchControls::checkBox("&Enabled", &host);
        const std::array<QWidget*, 4> controls{button, tool, edit, check};
        for (auto* widget : controls) layout->addWidget(widget);
        QVERIFY(button->inherits("ElaPushButton"));
        QVERIFY(tool->inherits("ElaToolButton"));
        QCOMPARE(tool->focusPolicy(), Qt::TabFocus);
        QVERIFY(edit->inherits("ElaLineEdit"));
        QVERIFY(check->inherits("ElaCheckBox"));
        QSignalSpy clicks(button, &QPushButton::clicked);
        host.show(); host.activateWindow();
        for (auto mode : {WorkbenchTheme::Mode::dark, WorkbenchTheme::Mode::light}) {
            WorkbenchTheme::apply(*qApp, mode);
            QTest::qWait(10);
            for (auto* widget : controls) {
                QCOMPARE(widget->font(), qApp->font());
                QVERIFY(widget->height() >= widget->minimumSizeHint().height());
                widget->setFocus(Qt::TabFocusReason);
                QTRY_VERIFY(widget->hasFocus());
                QVERIFY(QAccessible::queryAccessibleInterface(widget));
            }
            button->setFocus();
            QTest::keyClick(button, Qt::Key_Space);
            QCOMPARE(clicks.count(), mode == WorkbenchTheme::Mode::dark ? 1 : 2);
            QTest::keyClick(tool, Qt::Key_Space);
            QCOMPARE(tool->isChecked(), mode == WorkbenchTheme::Mode::dark);
            QTest::keyClicks(edit, "alpha");
            QCOMPARE(edit->text(), QString("alpha"));
            edit->selectAll(); QTest::keyClick(edit, Qt::Key_X, Qt::ControlModifier);
            QVERIFY(edit->text().isEmpty()); edit->undo();
            QCOMPARE(edit->text(), QString("alpha")); edit->clear();
        }
        button->setEnabled(false);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(clicks.count(), 2);
    }

    void popupCancelSelectionAndDestruction()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* combo = WorkbenchControls::comboBox(&host);
        layout->addWidget(combo);
        QVERIFY(combo->inherits("ElaComboBox"));
        combo->addItems({"RW", "RO", "WO"});
        host.show(); host.activateWindow();
        QSignalSpy activated(combo, &QComboBox::activated);
        for (int i = 0; i < 8; ++i) {
            combo->showPopup();
            QTRY_VERIFY(combo->view()->isVisible());
            QTest::keyClick(combo->view(), Qt::Key_Escape);
            QTRY_VERIFY(!combo->view()->isVisible());
            QCOMPARE(combo->currentIndex(), 0);
        }
        QCOMPARE(activated.count(), 0);
        combo->showPopup();
        QTest::keyClick(combo->view(), Qt::Key_Down);
        QTest::keyClick(combo->view(), Qt::Key_Return);
        QTRY_VERIFY(!combo->view()->isVisible());
        QCOMPARE(combo->currentIndex(), 1);
        QCOMPARE(activated.count(), 1);
        combo->showPopup();
        delete combo;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QApplication::activePopupWidget());
    }

    void menusAndNonDetachableTabs()
    {
        for (bool reduced : {false, true}) {
            qputenv("QT_REDUCE_MOTION", reduced ? "1" : "0");
            WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::dark);
            QWidget host;
            auto* layout = new QVBoxLayout(&host);
            auto* bar = WorkbenchControls::menuBar(&host);
            auto* menu = WorkbenchControls::addMenu(bar, "&File");
            auto* action = menu->addAction("&Open");
            action->setCheckable(true);
            auto* sub = WorkbenchControls::addMenu(menu, "Recent"); sub->addAction("Test");
            auto* toolbar = WorkbenchControls::toolBar("Commands", &host);
            WorkbenchControls::addAction(toolbar, action);
            QCOMPARE(toolbar->actions().size(), 1);
            QCOMPARE(toolbar->actions().front()->text(), action->text());
            QSignalSpy triggered(action, &QAction::triggered);
            toolbar->actions().front()->trigger();
            QCOMPARE(triggered.count(), 1);
            QVERIFY(action->isChecked());
            action->setChecked(false);
            QVERIFY(!toolbar->actions().front()->isChecked());
            action->setEnabled(false);
            QVERIFY(!toolbar->actions().front()->isEnabled());
            action->setEnabled(true);
            auto* tabs = WorkbenchControls::tabWidget(&host);
            tabs->addTab(new QWidget(tabs), "Problems");
            tabs->addTab(new QWidget(tabs), "Generated");
            layout->addWidget(bar); layout->addWidget(toolbar); layout->addWidget(tabs);
            QVERIFY(bar->inherits("ElaMenuBar")); QVERIFY(menu->inherits("ElaMenu"));
            QVERIFY(sub->inherits("ElaMenu")); QVERIFY(toolbar->inherits("ElaToolBar"));
            QVERIFY(tabs->inherits("ElaTabWidget"));
            QVERIFY(!tabs->isMovable()); QVERIFY(!tabs->tabsClosable());
            QVERIFY(!tabs->acceptDrops()); QVERIFY(!tabs->tabBar()->acceptDrops());
            QVERIFY(tabs->tabBar()->sizeHint().width() < 1000);
            host.resize(480, 320); host.show();
            QCOMPARE(host.size(), QSize(480, 320));
            menu->popup(host.mapToGlobal(QPoint(20, 20)));
            QTRY_VERIFY(menu->isVisible());
            menu->setActiveAction(action);
            QTest::keyClick(menu, Qt::Key_Return);
            QTRY_VERIFY(!menu->isVisible()); QVERIFY(action->isChecked());
            menu->popup(host.mapToGlobal(QPoint(20, 20)));
            QTRY_VERIFY(menu->isVisible());
            QTest::keyClick(menu, Qt::Key_Escape);
            QTRY_VERIFY(!menu->isVisible());
            menu->popup(host.mapToGlobal(QPoint(20, 20)));
            QPointer<QMenu> guard(menu);
            delete menu; QVERIFY(guard.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        qunsetenv("QT_REDUCE_MOTION");
    }

    void scrollBarQtContracts()
    {
        QScrollArea area;
        auto* content = new QWidget;
        content->setFixedSize(1400, 900);
        area.setWidget(content);
        area.resize(360, 240); area.show(); QTest::qWait(20);
        auto* original = area.verticalScrollBar();
        original->setValue(41); original->setSingleStep(7); original->setPageStep(83);
        const auto maximum = original->maximum();
        WorkbenchControls::styleScrollArea(&area);
        auto* vertical = area.verticalScrollBar();
        QVERIFY(vertical->inherits("ElaScrollBar"));
        QVERIFY(area.horizontalScrollBar()->inherits("ElaScrollBar"));
        QCOMPARE(vertical->value(), 41);
        QCOMPARE(vertical->maximum(), maximum);
        QCOMPARE(vertical->singleStep(), 7);
        QCOMPARE(vertical->pageStep(), 83);
        QCOMPARE(area.widget(), content);
        WorkbenchControls::styleScrollArea(&area);
        QCOMPARE(area.verticalScrollBar(), vertical);
        // QAbstractScrollArea recalculates range/page size during layout.
        QTest::qWait(30);
        for (auto* bar : {vertical, area.horizontalScrollBar()}) {
            QScrollBar reference(bar->orientation());
            reference.setRange(bar->minimum(), bar->maximum());
            reference.setSingleStep(bar->singleStep());
            reference.setPageStep(bar->pageStep());
            for (auto modifiers : {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier}) {
                reference.setValue(200); bar->setValue(200);
                for (auto* target : {&reference, bar}) {
                    QWheelEvent event(QPointF(4, 4), QPointF(4, 4), QPoint(),
                                      QPoint(0, -120), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
                    QCoreApplication::sendEvent(target, &event);
                }
                QCOMPARE(bar->value(), reference.value());
                QTest::qWait(20); QCOMPARE(bar->value(), reference.value());
            }
            for (auto key : {Qt::Key_PageDown, Qt::Key_End, Qt::Key_Home}) {
                QTest::keyClick(&reference, key); QTest::keyClick(bar, key);
                QCOMPARE(bar->value(), reference.value());
            }
            QCOMPARE(bar->style()->styleHint(QStyle::SH_ScrollBar_LeftClickAbsolutePosition, nullptr, bar),
                     reference.style()->styleHint(QStyle::SH_ScrollBar_LeftClickAbsolutePosition, nullptr, &reference));
            QVERIFY(QAccessible::queryAccessibleInterface(bar));
        }
    }

    void dialogRolesAndKeyboard()
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* edit = WorkbenchControls::lineEdit(&dialog);
        auto* box = WorkbenchControls::dialogButtons(&dialog);
        layout->addWidget(edit); layout->addWidget(box);
        auto* ok = WorkbenchControls::standardButton(box, QDialogButtonBox::Ok);
        auto* cancel = WorkbenchControls::standardButton(box, QDialogButtonBox::Cancel);
        QVERIFY(ok && cancel);
        QVERIFY(ok->inherits("ElaPushButton")); QVERIFY(cancel->inherits("ElaPushButton"));
        QCOMPARE(box->buttonRole(ok), QDialogButtonBox::AcceptRole);
        QCOMPARE(box->buttonRole(cancel), QDialogButtonBox::RejectRole);
        QVERIFY(ok->isDefault()); QVERIFY(!cancel->autoDefault());
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        QSignalSpy accepted(&dialog, &QDialog::accepted), rejected(&dialog, &QDialog::rejected);
        dialog.show(); dialog.activateWindow(); edit->setFocus();
        ok->setEnabled(false);
        QTest::keyClick(edit, Qt::Key_Return); QCOMPARE(accepted.count(), 0);
        QVERIFY(dialog.isVisible());
        ok->setEnabled(true); QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1); QVERIFY(!dialog.isVisible());
        dialog.show(); QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(rejected.count(), 1); QVERIFY(!dialog.isVisible());
        dialog.show(); QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(rejected.count(), 2);
    }

    void feedbackLifecycleAndBounds()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* edit = WorkbenchControls::lineEdit(&host);
        auto* button = WorkbenchControls::pushButton("Save", &host);
        button->setToolTip("Save the project and synchronize managed RTL. Existing errors remain available in diagnostics.");
        layout->addWidget(edit); layout->addWidget(button);
        host.resize(520, 320); host.show(); host.activateWindow(); edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        for (auto mode : {WorkbenchTheme::Mode::light, WorkbenchTheme::Mode::dark}) {
            WorkbenchTheme::apply(*qApp, mode);
            const auto bounds = host.screen()->availableGeometry();
            QHelpEvent help(QEvent::ToolTip, QPoint(3, 3), bounds.bottomRight() - QPoint(2, 2));
            QCoreApplication::sendEvent(button, &help);
            auto* tip = button->findChild<QWidget*>("workbenchToolTip");
            QVERIFY(tip && tip->inherits("ElaToolTip")); QVERIFY(tip->isVisible());
            QVERIFY(bounds.contains(tip->frameGeometry()));
            QCOMPARE(tip->accessibleName(), button->toolTip()); QVERIFY(edit->hasFocus());
            auto* label = tip->findChild<QLabel*>("workbenchToolTipText");
            QVERIFY(label); QCOMPARE(label->font().pointSizeF(), qApp->font().pointSizeF());
            for (bool reduced : {false, true}) {
                qputenv("QT_REDUCE_MOTION", reduced ? "1" : "0");
                WorkbenchTheme::apply(*qApp, mode);
                auto* bar = WorkbenchControls::successMessage(&host, "Saved", "Project, managed RTL, and read-only outputs saved", 2000);
                QVERIFY(bar && bar->inherits("ElaMessageBar"));
                QTest::qWait(reduced ? 5 : 400);
                QVERIFY(host.rect().contains(bar->geometry())); QVERIFY(edit->hasFocus());
                QCOMPARE(bar->font().pointSizeF(), qApp->font().pointSizeF());
                QCOMPARE(bar->accessibleName(), QString("Saved"));
                host.resize(300, 260); QTest::qWait(20);
                QVERIFY(host.rect().contains(bar->geometry()));
                const auto output = qEnvironmentVariable("REGMAP_UI_ARTIFACT_DIR");
                if (!output.isEmpty() && reduced) {
                    QDir().mkpath(output);
                    const auto theme = WorkbenchTheme::modeName(mode);
                    QVERIFY(host.grab().save(output + "/feedback-" + theme + ".png"));
                    QVERIFY(tip->grab().save(output + "/tooltip-" + theme + ".png"));
                }
                QPointer<QWidget> guard(bar);
                auto* close = bar->findChild<QPushButton*>("messageDismissButton");
                QVERIFY(close); QVERIFY(!close->accessibleName().isEmpty());
                QTest::mouseClick(close, Qt::LeftButton);
                QTRY_VERIFY(guard.isNull());
                host.resize(520, 320);
            }
            QEvent leave(QEvent::Leave); QCoreApplication::sendEvent(button, &leave);
            QVERIFY(!tip->isVisible());
            QPointer<QWidget> expiring = WorkbenchControls::successMessage(&host, "Saved", "Saved", 15);
            QVERIFY(expiring); QTRY_VERIFY(expiring.isNull());
        }
        for (int i = 0; i < 8; ++i) {
            QPointer<QWidget> previous = WorkbenchControls::successMessage(&host, "Saved", "Saved", 2000);
            QVERIFY(previous);
            QVERIFY(WorkbenchControls::successMessage(&host, "Generated", "Generated", 2000));
            QVERIFY(previous.isNull());
        }
        WorkbenchControls::dismissMessage(&host);
        auto* transient = new QWidget;
        transient->resize(400, 300); transient->show();
        QPointer<QWidget> child = WorkbenchControls::successMessage(transient, "Saved", "Saved", 20);
        delete transient; QVERIFY(child.isNull()); QTest::qWait(30);
        qunsetenv("QT_REDUCE_MOTION");
    }
};
QTEST_MAIN(ElaContracts)
#include "ela_controls_test.moc"
