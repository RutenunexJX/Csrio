#define main regmap_snapshot_main
#include "ui_snapshot.cpp"
#undef main

#include <ElaComboBox.h>
#include <ElaDrawerArea.h>
#include <ElaMenu.h>
#include <ElaScrollBar.h>
#include <ElaText.h>
#include <ElaTreeView.h>
#include <ElaListView.h>
#include <ElaTableView.h>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

class NativeCapabilities final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        qputenv("QT_REDUCE_MOTION", "0");
        WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::light);
    }

    void comboAnimationIsRealAndInterruptible()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        QComboBox* combo = WorkbenchControls::comboBox(&host);
        auto* ela = qobject_cast<ElaComboBox*>(combo);
        QVERIFY(ela);
        combo->addItems({"RW", "RO", "WO"}); layout->addWidget(combo);
        host.show(); QTest::qWait(20);
        QSignalSpy activated(combo, &QComboBox::activated);
        for (auto theme : {WorkbenchTheme::Mode::light, WorkbenchTheme::Mode::dark}) {
            WorkbenchTheme::apply(*qApp, theme);
            combo->showPopup(); QVERIFY(ela->isPopupAnimating());
            QTest::keyClick(combo->view(), Qt::Key_Escape);
            QVERIFY(!ela->isPopupAnimating()); QVERIFY(!combo->view()->isVisible());
            QCOMPARE(activated.count(), 0);
            combo->showPopup(); QVERIFY(ela->isPopupAnimating());
            host.resize(host.width() + 10, host.height());
            QCoreApplication::processEvents();
            QVERIFY(!ela->isPopupAnimating());
            QVERIFY(combo->view()->window()->layout()->indexOf(combo->view()) >= 0);
            combo->hidePopup();
            combo->showPopup(); QTRY_VERIFY(!ela->isPopupAnimating());
            QVERIFY(combo->view()->isVisible());
            for (int row = 0; row < combo->count(); ++row)
                QVERIFY(combo->view()->viewport()->rect().contains(
                    combo->view()->visualRect(combo->model()->index(row, 0))));
            const auto settledSize = combo->view()->window()->size();
            combo->hidePopup(); combo->showPopup(); ela->finishPopupAnimation();
            QCOMPARE(combo->view()->window()->size(), settledSize);
            combo->hidePopup();
        }
        combo->showPopup();
        QTest::keyClick(combo->view(), Qt::Key_Down);
        QTest::keyClick(combo->view(), Qt::Key_Return);
        QCOMPARE(combo->currentIndex(), 1); QCOMPARE(activated.count(), 1);
        qputenv("QT_REDUCE_MOTION", "1");
        WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::light);
        combo->showPopup(); QVERIFY(!ela->isPopupAnimating()); combo->hidePopup();
        qputenv("QT_REDUCE_MOTION", "0");
        WorkbenchTheme::apply(*qApp, WorkbenchTheme::Mode::light);
        combo->showPopup(); QPointer<QComboBox> guard(combo);
        delete combo; QVERIFY(guard.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QApplication::activePopupWidget());
    }

    void comboPopupRowsAndHeightRemainStable()
    {
        for (auto theme : {WorkbenchTheme::Mode::light, WorkbenchTheme::Mode::dark}) {
            WorkbenchTheme::apply(*qApp, theme);
            for (const bool editable : {false, true}) {
                for (const int count : {1, 3, 5}) {
                    QWidget host;
                    auto* layout = new QVBoxLayout(&host);
                    auto* combo = WorkbenchControls::comboBox(&host);
                    auto* ela = qobject_cast<ElaComboBox*>(combo);
                    QVERIFY(ela);
                    combo->setEditable(editable);
                    for (int row = 0; row < count; ++row) combo->addItem(QString("Item %1").arg(row));
                    layout->addWidget(combo); host.resize(360,80); host.show();
                    QTest::qWait(20);
                    for (const int selected : {0, count - 1}) {
                        combo->setCurrentIndex(selected);
                        QSize endpoint;
                        for (int repeat = 0; repeat < 3; ++repeat) {
                            combo->showPopup();
                            if (!repeat) QTRY_VERIFY(!ela->isPopupAnimating());
                            else ela->finishPopupAnimation();
                            QCoreApplication::processEvents();
                            if (!repeat) endpoint = combo->view()->window()->size();
                            QCOMPARE(combo->view()->window()->size(), endpoint);
                            combo->showPopup(); // Repeated Show on the visible popup is idempotent.
                            QCOMPARE(combo->view()->window()->size(), endpoint);
                            for (int row = 0; row < count; ++row) {
                                const auto index = combo->model()->index(row, 0);
                                QVERIFY(!combo->view()->visualRect(index).isEmpty());
                                QTRY_VERIFY(combo->view()->viewport()->rect().contains(combo->view()->visualRect(index)));
                            }
                            QCOMPARE(combo->view()->font().pointSizeF(), 10.0);
                            combo->hidePopup();
                        }
                    }
                }
            }
        }
    }

    void menuAnimationKeepsImmediateInputAndLiveEditors()
    {
        QWidget host; host.resize(500, 350); host.show();
        auto* menu = qobject_cast<ElaMenu*>(WorkbenchControls::menu(&host));
        QVERIFY(menu);
        auto* action = menu->addAction("&Apply\tCtrl+Enter"); action->setCheckable(true);
        auto* disabled = menu->addAction("Unavailable"); disabled->setEnabled(false);
        auto* submenu = WorkbenchControls::addMenu(menu, "More"); submenu->addAction("Details");
        QSignalSpy activated(action, &QAction::triggered);
        for (int i = 0; i < 8; ++i) {
            menu->popup(host.mapToGlobal(QPoint(10, 10)));
            QVERIFY(menu->isPopupAnimating());
            QTest::keyClick(menu, Qt::Key_Escape);
            QVERIFY(!menu->isVisible()); QVERIFY(!menu->isPopupAnimating());
        }
        menu->popup(host.mapToGlobal(QPoint(10, 10))); menu->setActiveAction(action);
        QTest::keyClick(menu, Qt::Key_Return);
        QCOMPARE(activated.count(), 1); QVERIFY(action->isChecked());
        menu->popup(host.mapToGlobal(QPoint(10, 10))); action->setText("Changed");
        QVERIFY(!menu->isPopupAnimating()); menu->hide();
        menu->popup(host.mapToGlobal(QPoint(10, 10)));
        QTRY_VERIFY(!menu->isPopupAnimating()); QVERIFY(menu->isVisible()); menu->hide();
        auto* widgetAction = new QWidgetAction(menu);
        auto* edit = WorkbenchControls::lineEdit(menu); widgetAction->setDefaultWidget(edit);
        menu->addAction(widgetAction);
        menu->popup(host.mapToGlobal(QPoint(10, 10)));
        QVERIFY(!menu->isPopupAnimating());
        QTest::keyClicks(edit, "live"); QCOMPARE(edit->text(), QString("live"));
        delete menu;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void smoothScrollPreservesPrecisionAndNavigation()
    {
        std::unique_ptr<QListView> view(WorkbenchControls::listView(nullptr));
        QStandardItemModel model;
        for (int i = 0; i < 200; ++i) model.appendRow(new QStandardItem(QString::number(i)));
        view->setModel(&model); view->resize(420, 240); view->show(); QTest::qWait(20);
        auto* bar = qobject_cast<ElaScrollBar*>(view->verticalScrollBar());
        QVERIFY(bar && bar->smoothWheelEnabled()); bar->setValue(200);
        const int target = 200 + qMin(bar->pageStep(), bar->singleStep() * QApplication::wheelScrollLines());
        QWheelEvent wheel(QPointF(8,8), QPointF(8,8), {}, QPoint(0,-120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &wheel);
        QVERIFY(wheel.isAccepted()); QCOMPARE(bar->value(), 200);
        QTRY_COMPARE(bar->value(), target);
        QApplication::sendEvent(view->viewport(), &wheel);
        QTest::keyClick(view.get(), Qt::Key_End, Qt::ControlModifier);
        const auto end = bar->value(); QTest::qWait(220); QCOMPARE(bar->value(), end);
        bar->setValue(200);
        QWheelEvent pixel(QPointF(8,8), QPointF(8,8), QPoint(0,-13), {},
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(view->viewport(), &pixel); QCOMPARE(bar->value(), 213);
        QApplication::sendEvent(view->viewport(), &wheel); bar->setRange(0, 50);
        QTest::qWait(220); QCOMPARE(bar->value(), 50);
        QScrollArea precise;
        WorkbenchControls::styleScrollArea(&precise);
        QVERIFY(!qobject_cast<ElaScrollBar*>(precise.verticalScrollBar())->smoothWheelEnabled());
    }

    void overlaySurvivesOriginReplacement()
    {
        QScrollArea area;
        auto* content = new QWidget;
        content->setMinimumSize(320, 2000);
        area.setWidget(content);
        QPointer<QScrollBar> origin = area.verticalScrollBar();
        auto* overlay = new ElaScrollBar(origin, &area);
        overlay->setSmoothWheelEnabled(true);
        area.resize(400,300); area.show(); QTest::qWait(20);
        QWheelEvent wheel(QPointF(8,8), QPointF(8,8), {}, QPoint(0,-120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(overlay, &wheel);
        area.setVerticalScrollBar(new QScrollBar(Qt::Vertical, &area));
        QVERIFY(origin.isNull());
        QVERIFY(!overlay->isVisible());
        area.resize(420,320);
        QVERIFY(!area.grab().isNull());
        overlay->setValue(10);
        QTest::qWait(220);
        QVERIFY(!overlay->isVisible());
    }

    void treeAndSemanticLabelsRetainQtContracts()
    {
        QTreeView tree; QStandardItemModel model;
        auto* parent = new QStandardItem("Workspace");
        auto* child = new QStandardItem("Page"); child->setCheckable(true);
        parent->appendRow(child); model.appendRow(parent); tree.setModel(&model);
        WorkbenchControls::styleHierarchy(&tree); tree.resize(480,300); tree.show();
        QVERIFY(tree.isAnimated());
        tree.expand(model.index(0,0));
        ElaTreeView::finishExpansion(&tree);
        tree.setCurrentIndex(model.index(0,0,model.index(0,0)));
        QTest::keyClick(&tree, Qt::Key_Space); QCOMPARE(child->checkState(), Qt::Checked);
        std::unique_ptr<QLabel> text(WorkbenchControls::label("Error", nullptr));
        QVERIFY(text->inherits("ElaText")); QCOMPARE(text->font().pointSizeF(), 10.0);
        QPalette palette = text->palette(); palette.setColor(QPalette::WindowText, QColor("#c63131"));
        text->setPalette(palette); text->show();
        for (auto theme : {WorkbenchTheme::Mode::dark, WorkbenchTheme::Mode::light}) {
            WorkbenchTheme::apply(*qApp, theme); text->grab();
            QCOMPARE(text->palette().color(QPalette::WindowText), QColor("#c63131"));
        }
    }

    void contentDialogRolesMaskAndDestruction()
    {
        QWidget host; host.resize(640,480); host.show();
        std::unique_ptr<QDialog> dialog(WorkbenchControls::contentDialog(&host));
        QVERIFY(dialog->inherits("ElaContentDialog")); dialog->setWindowTitle("Batch Edit");
        auto* layout = new QVBoxLayout(WorkbenchControls::dialogContent(dialog.get()));
        auto* edit = WorkbenchControls::lineEdit(dialog.get());
        auto* buttons = WorkbenchControls::dialogButtons(dialog.get());
        layout->addWidget(edit); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, dialog.get(), &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, dialog.get(), &QDialog::reject);
        QSignalSpy accepted(dialog.get(), &QDialog::accepted);
        dialog->show(); edit->setFocus(); QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1); QVERIFY(!dialog->isVisible());
        for (auto* mask : host.findChildren<QWidget*>("ElaMaskWidget")) QVERIFY(!mask->isVisible());
        dialog->show(); QTest::keyClick(dialog.get(), Qt::Key_Escape); QVERIFY(!dialog->isVisible());
        dialog->show(); dialog.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (auto* mask : host.findChildren<QWidget*>("ElaMaskWidget")) QVERIFY(!mask->isVisible());
    }

    void drawerReversalInputResizeAndDestruction()
    {
        QWidget host; auto* outer = new QVBoxLayout(&host);
        auto* panel = new QWidget(&host); auto* content = new QVBoxLayout(panel);
        auto* edit = WorkbenchControls::lineEdit(panel); edit->setText("Preserved");
        content->addWidget(edit); outer->addWidget(panel);
        WorkbenchControls::installPanelMotion(panel, Qt::RightEdge);
        auto* drawer = panel->findChild<ElaDrawerArea*>(); QVERIFY(drawer);
        host.resize(640,480); host.show(); QTest::qWait(20);
        WorkbenchControls::setPanelVisible(panel, false);
        QVERIFY(drawer->isDrawerAnimating());
        QVERIFY(drawer->drawerSnapshotBytes() > 0);
        QVERIFY(drawer->drawerSnapshotBytes() <= 32 * 1024 * 1024);
        WorkbenchControls::setPanelVisible(panel, true);
        QTest::keyClick(&host, Qt::Key_Tab);
        QVERIFY(!drawer->isDrawerAnimating()); QCOMPARE(drawer->drawerSnapshotBytes(), qint64(0));
        QVERIFY(panel->isVisible()); QCOMPARE(edit->text(), QString("Preserved"));
        WorkbenchControls::setPanelVisible(panel, false);
        host.resize(620,460); QCoreApplication::processEvents();
        QTRY_VERIFY(!panel->isVisible()); QCOMPARE(drawer->drawerSnapshotBytes(), qint64(0));
        WorkbenchControls::setPanelVisible(panel, true);
        QTRY_VERIFY(panel->isVisible()); WorkbenchControls::finishPanelAnimations(&host);
        QVERIFY(edit->isVisible()); edit->setFocus(); QTRY_VERIFY(edit->hasFocus());
        WorkbenchControls::setPanelVisible(panel, false);
        QPointer<QWidget> guard(edit); delete panel; QVERIFY(guard.isNull());
        QTest::qWait(350);
    }

    void focusedItemViewsDestroySafely()
    {
        QWidget host; auto* layout = new QVBoxLayout(&host);
        auto* model = new QStandardItemModel(&host);
        model->appendRow(new QStandardItem("Focused row"));
        host.resize(640,480); host.show();
        for (int iteration = 0; iteration < 8; ++iteration) {
            const QList<QAbstractItemView*> views{
                new ElaListView(&host), new ElaTreeView(&host), new ElaTableView(&host)};
            for (auto* view : views) {
                layout->addWidget(view); view->setModel(model);
                view->show(); host.activateWindow(); view->setFocus();
                QTRY_VERIFY(view->hasFocus());
                QPointer<QWidget> guard(view); delete view; QVERIFY(guard.isNull());
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        }
    }

    void actualWorkbenchDrawersRetainFocusAndSizes()
    {
        QTemporaryDir project;
        const auto path = project.filePath("native.regmap.yaml");
        QVERIFY(createFixture(path));
        QSettings().clear();
        MainWindow window; QVERIFY(window.openProjectPath(path));
        window.resize(1440,900); window.show(); QTest::qWait(30); window.resize(1440,900);
        auto* toggle = window.findChild<QAction*>("toggleResultsAction");
        auto* results = window.findChild<QWidget*>("resultsPanel");
        auto* resultsDrawer = results ? results->findChild<ElaDrawerArea*>() : nullptr;
        auto* table = window.findChild<QTableView*>("registerView");
        auto* fields = window.findChild<QTableView*>("fieldView");
        QVERIFY(toggle && results && resultsDrawer && table && fields);
        WorkbenchControls::finishPanelAnimations(&window);
        if (WorkbenchControls::panelRequestedVisible(results)) toggle->trigger();
        WorkbenchControls::finishPanelAnimations(&window);
        toggle->trigger(); QTRY_VERIFY(resultsDrawer->isDrawerAnimating());
        QTRY_VERIFY(!resultsDrawer->isDrawerAnimating());
        QVERIFY(results->isVisible());
        toggle->trigger(); QVERIFY(resultsDrawer->isDrawerAnimating());
        QTRY_VERIFY(!results->isVisible());
        Q_EMIT table->clicked(table->model()->index(0,5));
        QTRY_VERIFY(fields->isVisible());
        QTRY_VERIFY(fields->hasFocus());
        QCOMPARE(window.size(), QSize(1440,900));
        QCOMPARE(window.font().pointSizeF(), 10.0);
    }
    void openingDrawerCannotStealFocusFromNewInput()
    {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* external = WorkbenchControls::lineEdit(&host);
        auto* panel = new QWidget(&host);
        auto* body = new QVBoxLayout(panel);
        auto* inside = WorkbenchControls::lineEdit(panel);
        body->addWidget(inside);
        layout->addWidget(external);
        layout->addWidget(panel);
        WorkbenchControls::installPanelMotion(panel, Qt::RightEdge);
        host.resize(640,480); host.show(); host.activateWindow();
        WorkbenchControls::setPanelVisible(panel, false);
        WorkbenchControls::finishPanelAnimations(&host);
        WorkbenchControls::setPanelVisible(panel, true);
        WorkbenchControls::focusWhenVisible(inside, Qt::OtherFocusReason);
        auto* drawer = panel->findChild<ElaDrawerArea*>();
        QVERIFY(drawer);
        QTRY_VERIFY(drawer->isDrawerAnimating());
        external->setFocus();
        QVERIFY(external->hasFocus());
        QTest::keyClicks(external, "valid input");
        QVERIFY(external->hasFocus());
        QCOMPARE(external->text(), QString("valid input"));
        QTRY_VERIFY(!drawer->isDrawerAnimating());
        QVERIFY(external->hasFocus());
        WorkbenchControls::setPanelVisible(panel, false);
        WorkbenchControls::finishPanelAnimations(&host);
        WorkbenchControls::setPanelVisible(panel, true);
        WorkbenchControls::setPanelVisible(panel, false);
        QTest::qWait(350);
        QVERIFY(!panel->isVisible());
        QVERIFY(!drawer->isDrawerAnimating());
        QCOMPARE(drawer->drawerSnapshotBytes(), qint64(0));
    }
};

QTEST_MAIN(NativeCapabilities)
#include "ela_native_capabilities_test.moc"
