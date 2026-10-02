#define main unusedSnapshotMain
#include "ui_snapshot.cpp"
#undef main
#include "workbench_controls.hpp"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QProxyStyle>
#include <QSignalSpy>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QTest>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
class NativeCellPopup final : public QObject {
public:
    NativeCellPopup() { qApp->installEventFilter(this); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Show && object->property("regmapCellEditor").toBool())
            object->setProperty("regmapTestNativePopup", true);
        return false;
    }
};

class GuardedWindow final : public QMainWindow {
public:
    QWidget* chrome{nullptr};
    GuardedWindow() { chrome = WorkbenchControls::installWindowChrome(this); }
    bool rejectClose{true};
    int closeRequests{0};
protected:
    void closeEvent(QCloseEvent* event) override {
        ++closeRequests;
        if (rejectClose) event->ignore(); else event->accept();
    }
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override {
        if (WorkbenchControls::windowChromeNativeEvent(chrome, type, message, result)) return true;
        return QMainWindow::nativeEvent(type, message, result);
    }
};

bool hasRedText(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            count += color.red() > 190 && color.green() < 90 && color.blue() < 130;
        }
    return count > 10;
}
}

class RemainingContracts final : public QObject {
    Q_OBJECT
    QString output_;
private slots:
    void initTestCase() {
        WorkbenchTheme::apply(*qApp, qEnvironmentVariable("REGMAP_TEST_THEME") == "dark"
            ? WorkbenchTheme::Mode::dark : WorkbenchTheme::Mode::light);
        output_ = qEnvironmentVariable("REGMAP_UI_ARTIFACT_DIR");
        if (!output_.isEmpty()) QVERIFY(QDir().mkpath(output_));
        QCOMPARE(WorkbenchControls::backend(), WorkbenchControls::Backend::ela);
        if (qEnvironmentVariableIsSet("REGMAP_EXPECTED_SCALE"))
            QCOMPARE(qApp->primaryScreen()->devicePixelRatio(), qEnvironmentVariable("REGMAP_EXPECTED_SCALE").toDouble());
    }

    void init() {
        WorkbenchTheme::apply(*qApp, qEnvironmentVariable("REGMAP_TEST_THEME") == "dark"
            ? WorkbenchTheme::Mode::dark : WorkbenchTheme::Mode::light);
    }

    void itemRolesSelectionAndLifetime() {
        for (auto mode : {WorkbenchTheme::Mode::light, WorkbenchTheme::Mode::dark}) {
            WorkbenchTheme::apply(*qApp, mode);
            QWidget host;
            auto* layout = new QVBoxLayout(&host);
            auto* list = WorkbenchControls::listWidget(&host);
            QVERIFY(list->inherits("ElaListWidget"));
            list->setSelectionMode(QAbstractItemView::MultiSelection);
            list->addItems({"Critical register", "Status", "A very long candidate label that must be elided inside the list"});
            auto* originalModel = list->model();
            auto* item = list->item(0);
            item->setForeground(QColor("#FF0033"));
            item->setCheckState(Qt::Checked);
            item->setData(Qt::UserRole, "stable-id");
            auto* result = WorkbenchControls::resultTable(&host);
            QVERIFY(result->inherits("ElaTableView"));
            for (auto* view : {static_cast<QAbstractItemView*>(list), static_cast<QAbstractItemView*>(result)}) {
                bool semanticStyle = false;
                for (auto* style : view->findChildren<QProxyStyle*>()) {
                    if (style->inherits("ElaListViewStyle") || style->inherits("ElaTableViewStyle")) {
                        QCOMPARE(style->baseStyle()->objectName(), QString("fusion"));
                        semanticStyle = true;
                    }
                }
                QVERIFY(semanticStyle);
            }
            QStandardItemModel model(2, 2);
            model.setHorizontalHeaderLabels({"Severity", "Message"});
            model.setData(model.index(0, 0), "Error");
            model.setData(model.index(0, 0), QColor("#FF0033"), Qt::ForegroundRole);
            model.setData(model.index(0, 1), "A diagnostic with preserved model roles");
            result->setModel(&model);
            result->setEditTriggers(QAbstractItemView::NoEditTriggers);
            result->resizeColumnsToContents();
            layout->addWidget(list); layout->addWidget(result);
            host.resize(480, 360); host.show(); host.activateWindow(); QTest::qWait(30);
            QCOMPARE(list->model(), originalModel);
            QCOMPARE(item->checkState(), Qt::Checked);
            QCOMPARE(item->data(Qt::UserRole).toString(), QString("stable-id"));
            QVERIFY(hasRedText(list->viewport()->grab().toImage()));
            QVERIFY(hasRedText(result->viewport()->grab().toImage()));
            list->setFocus(); list->setCurrentRow(0);
            QTest::keyClick(list, Qt::Key_Down);
            QCOMPARE(list->currentRow(), 1);
            item->setSelected(true); list->item(1)->setSelected(true);
            QCOMPARE(list->selectedItems().size(), 2);
            item->setHidden(true); QVERIFY(item->isHidden()); item->setHidden(false);
            QCOMPARE(result->font().pointSizeF(), 10.0);
            QVERIFY(result->horizontalScrollBar()->inherits("ElaScrollBar"));
            QVERIFY(list->verticalScrollBar()->inherits("ElaScrollBar"));
            if (!output_.isEmpty()) QVERIFY(host.grab().save(output_ + (mode == WorkbenchTheme::Mode::light ? "/items-light.png" : "/items-dark.png")));
            QPointer<QWidget> guardedList(list), guardedTable(result);
            delete list; delete result;
            QVERIFY(guardedList.isNull() && guardedTable.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }

    void cellPopupInputAndDestruction() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* editor = WorkbenchControls::cellComboBox(&host, true);
        editor->setProperty("regmapTestNativePopup", true);
        editor->setEditable(true);
        editor->setInsertPolicy(QComboBox::NoInsert);
        editor->addItems({"uint32", "uint16", "int8"});
        layout->addWidget(editor);
        host.show(); host.activateWindow();
        QVERIFY(editor->inherits("ElaComboBox"));
        QTRY_VERIFY(editor->property("typePopupAutoOpened").toBool());
        QTRY_VERIFY(editor->view()->isVisible());
        QTest::keyClick(editor->view(), Qt::Key_Escape);
        QTRY_VERIFY(!editor->view()->isVisible());
        QCOMPARE(editor->currentText(), QString("uint32"));
        QSignalSpy activated(editor, &QComboBox::textActivated);
        editor->showPopup();
        QTest::keyClick(editor->view(), Qt::Key_Down);
        QTest::keyClick(editor->view(), Qt::Key_Return);
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(editor->currentText(), QString("uint16"));
        editor->lineEdit()->selectAll();
        QInputMethodEvent input;
        input.setCommitString("uint24");
        QApplication::sendEvent(editor->lineEdit(), &input);
        QCOMPARE(editor->currentText(), QString("uint24"));
        editor->showPopup();
        QTest::qWait(220); // Capture after the 180 ms popup transition, not its 1px start.
        if (!output_.isEmpty()) QVERIFY(editor->view()->window()->grab().save(output_ + "/cell-popup.png"));
        for (int row = 0; row < editor->count(); ++row) {
            const auto rect = editor->view()->visualRect(editor->model()->index(row, 0));
            QVERIFY(!rect.isEmpty());
            if (!editor->view()->viewport()->rect().contains(rect))
                qWarning() << "Popup row" << row << rect << "viewport" << editor->view()->viewport()->rect()
                           << "popup" << editor->view()->window()->size();
            QVERIFY(editor->view()->viewport()->rect().contains(rect));
        }
        QCOMPARE(editor->view()->font().pointSizeF(), 10.0);
        editor->hidePopup();
        editor->showPopup(); // Keep destruction-during-animation coverage.
        QPointer<QComboBox> guard(editor);
        delete editor;
        QVERIFY(guard.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QApplication::activePopupWidget());
    }

    void cellChoiceEditorsOpenAndCommit_data() {
        QTest::addColumn<bool>("field");
        QTest::addColumn<bool>("keyboard");
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("choice");
        QTest::addColumn<QString>("typedValue");
        QTest::newRow("register-type-double-click") << false << false << 4 << QString("uint16") << QString();
        QTest::newRow("field-type-double-click") << true << false << 5 << QString("bits") << QString();
        QTest::newRow("register-type-keyboard") << false << true << 4 << QString("uint16") << QString();
        QTest::newRow("field-type-keyboard") << true << true << 5 << QString("bits") << QString();
        QTest::newRow("software-access-double-click") << true << false << 8 << QString("NONE") << QString();
        QTest::newRow("hardware-access-double-click") << true << false << 9 << QString("NONE") << QString();
        QTest::newRow("software-access-keyboard") << true << true << 8 << QString("NONE") << QString();
        QTest::newRow("hardware-access-keyboard") << true << true << 9 << QString("NONE") << QString();
        QTest::newRow("register-typed-preset") << false << true << 4 << QString("uint16") << QString("uint8");
        QTest::newRow("field-typed-preset") << true << true << 5 << QString("bool") << QString("bits");
        QTest::newRow("register-custom-enter") << false << true << 4 << QString() << QString("uint24");
        QTest::newRow("field-custom-enter") << true << true << 5 << QString() << QString("uint1");
        QTest::newRow("register-access-double-click") << false << false << 9 << QString("NONE") << QString();
        QTest::newRow("register-access-keyboard") << false << true << 9 << QString("NONE") << QString();
        QTest::newRow("register-tags-double-click") << false << false << 10 << QString("control") << QString();
        QTest::newRow("register-tags-keyboard") << false << true << 10 << QString("control") << QString();
    }

    void cellChoiceEditorsOpenAndCommit() {
        QFETCH(bool, field);
        QFETCH(bool, keyboard);
        QFETCH(int, column);
        QFETCH(QString, choice);
        QFETCH(QString, typedValue);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto manifest = directory.filePath("type-double-click.regmap.yaml");
        QVERIFY(createFixture(manifest));
        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        QTest::qWait(50);

        auto* registers = window.findChild<QTableView*>("registerView");
        auto* fields = window.findChild<QTableView*>("fieldView");
        auto* controller = window.findChild<ProjectController*>();
        QVERIFY(registers && fields && controller);
        const auto rowNamed = [](QTableView* table, const QString& name) {
            for (int row = 0; row < table->model()->rowCount(); ++row)
                if (table->model()->index(row, 0).data().toString() == name) return row;
            return -1;
        };
        if (field) {
            const int controlRow = rowNamed(registers, "CONTROL");
            QVERIFY(controlRow >= 0);
            Q_EMIT registers->clicked(registers->model()->index(controlRow, 5));
            QTRY_VERIFY(fields->isVisible());
        }
        auto* table = field ? fields : registers;
        const int row = rowNamed(table, field ? "ENABLE" : "SAMPLE_COUNT");
        QVERIFY(row >= 0);
        table->setColumnHidden(column, false);
        const auto index = table->model()->index(row, column);
        const auto original = index.data().toString();
        table->scrollTo(index);
        const QPoint point = table->visualRect(index).center();
        QVERIFY(table->viewport()->rect().contains(point));
        NativeCellPopup enablePopup;
        const QPoint windowPoint = table->viewport()->mapTo(&window, point);
        if (keyboard) {
            table->setCurrentIndex(index);
            table->setFocus();
            QTest::keyClick(table, Qt::Key_F2);
        } else {
            // Use window events so Qt routes the full double-click and mouse grab.
            QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint, 10);
            QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint, 10);
            QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint, 10);
        }
        if (!field && column >= 9) {
            QTRY_VERIFY(QApplication::activePopupWidget());
            QPointer<QWidget> popup(QApplication::activePopupWidget());
            QCOMPARE(popup->objectName(), column == 9 ? QString("accessPopup") : QString("tagPopup"));
            if (!keyboard)
                QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint, 10);
            QTest::qWait(60);
            QVERIFY(popup && popup->isVisible());
            QCOMPARE(index.data().toString(), original);
            QVERIFY(!controller->isDirty());
            auto* options = popup->findChild<QListWidget*>(column == 9 ? "accessOptions" : "tagOptions");
            QVERIFY(options);
            const auto matches = options->findItems(choice, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            options->scrollToItem(matches.front());
            QTest::mouseClick(options->viewport(), Qt::LeftButton, Qt::NoModifier,
                options->visualItemRect(matches.front()).center());
            if (popup) popup->close();
            QTRY_COMPARE(table->model()->index(row, column).data().toString(), choice);
            QVERIFY(controller->isDirty());
            controller->undo();
            QTRY_COMPARE(table->model()->index(row, column).data().toString(), original);
            QVERIFY(!controller->isDirty());
            return;
        }
        QPointer<QComboBox> editor(table->findChild<QComboBox*>(
            field && column >= 8 ? "fieldAccessEditor" : "typeEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->currentIndex(), editor->findText(original));
        QTest::qWait(40);
        QVERIFY(editor && editor->isVisible());
        if (!keyboard) {
            QVERIFY(!editor->view()->isVisible());
            QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint, 10);
        }
        QTest::qWait(220);
        QVERIFY(editor && editor->isVisible());
        QVERIFY(editor->view()->isVisible());
        QCOMPARE(index.data().toString(), original);
        QVERIFY(!controller->isDirty());

        if (!typedValue.isEmpty()) {
            QTest::keyClick(editor->view(), Qt::Key_Escape);
            QVERIFY(editor && editor->isVisible());
            editor->lineEdit()->selectAll();
            QTest::keyClicks(editor->lineEdit(), typedValue);
            QCOMPARE(editor->currentText(), typedValue);
            if (!choice.isEmpty()) {
                QTest::keyClick(editor.data(), Qt::Key_Down, Qt::AltModifier);
                QTest::qWait(220);
                QVERIFY(editor && editor->isVisible());
                QVERIFY(editor->view()->isVisible());
                QCOMPARE(index.data().toString(), original);
                QVERIFY(!controller->isDirty());
            }
        }
        if (choice.isEmpty()) {
            QTest::keyClick(editor->lineEdit(), Qt::Key_Return);
        } else {
            const int choiceRow = editor->findText(choice);
            QVERIFY(choiceRow >= 0);
            auto* choices = editor->view();
            const auto choiceIndex = editor->model()->index(choiceRow, 0);
            choices->scrollTo(choiceIndex);
            QTest::mouseClick(choices->viewport(), Qt::LeftButton, Qt::NoModifier,
                choices->visualRect(choiceIndex).center());
        }
        const QString expected = choice.isEmpty() ? typedValue : choice;
        QTRY_COMPARE(table->model()->index(row, column).data().toString(), expected);
        QTRY_VERIFY(!editor || !editor->isVisible());
        QCOMPARE(controller->isDirty(), expected != original);
        if (expected != original) controller->undo();
        QTRY_COMPARE(table->model()->index(row, column).data().toString(), original);
        QVERIFY(!controller->isDirty());
    }

    void keepsTagFilterUnchangedOnFocusLoss() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto manifest = directory.filePath("tag-filter-focus.regmap.yaml");
        QVERIFY(createFixture(manifest));
        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        QTest::qWait(50);
        auto* filter = window.findChild<QComboBox*>("tagFilter");
        auto* table = window.findChild<QTableView*>("registerView");
        QVERIFY(filter && table && filter->lineEdit());
        const int rows = table->model()->rowCount();
        const auto original = filter->currentText();
        QVERIFY(filter->findText("control") >= 0);
        filter->lineEdit()->setFocus();
        filter->lineEdit()->selectAll();
        QTest::keyClicks(filter->lineEdit(), "control");
        filter->completer()->popup()->hide();
        table->setFocus();
        QCoreApplication::processEvents();
        QCOMPARE(filter->currentText(), original);
        QCOMPARE(table->model()->rowCount(), rows);

        filter->lineEdit()->setFocus();
        filter->lineEdit()->selectAll();
        QTest::keyClicks(filter->lineEdit(), "control");
        QTest::keyClick(filter->lineEdit(), Qt::Key_Return);
        QTRY_COMPARE(filter->currentText(), QString("control"));
        QVERIFY(table->model()->rowCount() < rows);

        filter->lineEdit()->setFocus();
        filter->lineEdit()->selectAll();
        QTest::keyClicks(filter->lineEdit(), "crit");
        auto* candidates = filter->completer()->popup();
        QTRY_VERIFY(candidates->isVisible());
        const auto candidate = candidates->model()->index(0, 0);
        QCOMPARE(candidate.data().toString(), QString("critical"));
        QTest::mouseClick(candidates->viewport(), Qt::LeftButton, Qt::NoModifier,
            candidates->visualRect(candidate).center());
        QTRY_COMPARE(filter->currentText(), QString("critical"));
    }

    void applicationIntegrationAndEditCancel() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto manifest = directory.filePath("remaining.regmap.yaml");
        QVERIFY(createFixture(manifest));
        MainWindow window;
        QVERIFY(window.openProjectPath(manifest));
        window.resize(960, 720); window.show(); window.activateWindow(); QTest::qWait(40);
        QCOMPARE(window.size(), QSize(960, 720));
        QVERIFY(!window.findChild<QLabel*>("windowTitleLabel")->text().contains("[*]"));
        QVERIFY(window.statusBar()->inherits("ElaStatusBar"));
        QCOMPARE(window.statusBar()->font().pointSizeF(), 10.0);
        QVERIFY(window.statusBar()->height() >= window.statusBar()->fontMetrics().height() + 4);
        window.statusBar()->showMessage("Authoritative diagnostic remains available");
        QCOMPARE(window.statusBar()->currentMessage(), QString("Authoritative diagnostic remains available"));
        auto* completion = window.findChild<QCompleter*>("searchCompleter");
        QVERIFY(completion && completion->popup()->inherits("ElaListView"));
        for (const auto* name : {"problemsView", "generatedView", "diffView"}) {
            auto* view = window.findChild<QTableView*>(name);
            QVERIFY(view && view->inherits("ElaTableView"));
            QCOMPARE(view->editTriggers(), QAbstractItemView::NoEditTriggers);
        }
        auto* table = window.findChild<QTableView*>("registerView");
        QVERIFY(table);
        int typeColumn = -1, targetRow = -1;
        for (int column = 0; column < table->model()->columnCount(); ++column) {
            if (table->model()->headerData(column, Qt::Horizontal).toString() == "Value Type") typeColumn = column;
            for (int row = 0; row < table->model()->rowCount(); ++row)
                if (table->model()->index(row, column).data().toString() == "SAMPLE_COUNT") targetRow = row;
        }
        QVERIFY(typeColumn >= 0 && targetRow >= 0);
        const auto index = table->model()->index(targetRow, typeColumn);
        const auto original = index.data();
        table->setCurrentIndex(index); table->scrollTo(index); table->setFocus(); table->edit(index);
        QTRY_VERIFY(table->findChild<QComboBox*>("typeEditor"));
        QPointer<QComboBox> editor(table->findChild<QComboBox*>("typeEditor"));
        QVERIFY(editor->inherits("ElaComboBox"));
        QTRY_VERIFY(editor->property("typePopupAutoOpened").toBool());
        editor->hidePopup(); editor->setEditText("uint16");
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(!editor || !editor->isVisible());
        QCOMPARE(index.data(), original);
        QVERIFY(!window.findChild<ProjectController*>()->isDirty());
        if (!output_.isEmpty()) QVERIFY(window.grab().save(output_ + "/application.png"));
    }

    void chromeBoundsStateAndCloseGuard() {
        GuardedWindow window;
        QVERIFY(window.chrome && window.chrome->inherits("ElaAppBar"));
        window.setCentralWidget(new QWidget(&window));
        window.setWindowTitle(QString(180, 'W') + " - Csrio");
        window.resize(960, 720); window.show(); QTest::qWait(30);
        auto* title = window.chrome->findChild<QLabel*>("windowTitleLabel");
        QVERIFY(title);
        QCOMPARE(title->font().pointSizeF(), 10.0);
        QCOMPARE(title->accessibleName(), window.windowTitle());
        window.setWindowTitle("Fixture[*]");
        window.setWindowModified(true);
        QCOMPARE(title->text(), QString("Fixture*"));
        window.setWindowModified(false);
        QCOMPARE(title->text(), QString("Fixture"));
        window.setWindowTitle(QString(180, 'W') + " - Csrio");
        for (const auto size : {QSize(960, 720), QSize(1440, 900)}) {
            window.resize(size); QTest::qWait(20);
            QCOMPARE(window.size(), size);
            QCOMPARE(window.chrome->width(), window.width());
            QVERIFY(title->isVisible());
            QVERIFY(title->width() >= 40);
            QVERIFY(title->height() >= title->fontMetrics().height());
            for (auto* name : {"windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"}) {
                auto* button = window.chrome->findChild<QAbstractButton*>(name);
                QVERIFY(button && !button->accessibleName().isEmpty());
                QVERIFY(window.chrome->rect().contains(QRect(button->mapTo(window.chrome, QPoint()), button->size())));
            }
            QVERIFY(title->mapTo(window.chrome, QPoint(title->width(), 0)).x()
                <= window.chrome->findChild<QWidget*>("windowMinimizeButton")->mapTo(window.chrome, QPoint()).x());
        }
        auto* maximize = window.chrome->findChild<QAbstractButton*>("windowMaximizeButton");
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(window.isMaximized());
        QCOMPARE(maximize->accessibleName(), QString("Restore window"));
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(!window.isMaximized());
        auto* minimize = window.chrome->findChild<QAbstractButton*>("windowMinimizeButton");
        QTest::mouseClick(minimize, Qt::LeftButton); QTRY_VERIFY(window.isMinimized());
        window.showNormal(); QTest::qWait(20);
        window.showFullScreen(); QTRY_VERIFY(!window.chrome->isVisible());
        QCOMPARE(window.contentsMargins().top(), 0);
        window.showNormal(); QTRY_VERIFY(window.chrome->isVisible());
        QCOMPARE(window.contentsMargins().top(), window.chrome->height());
        auto* close = window.chrome->findChild<QAbstractButton*>("windowCloseButton");
        QTest::mouseClick(close, Qt::LeftButton); QTest::qWait(20);
        QCOMPARE(window.closeRequests, 1); QVERIFY(window.isVisible());
        if (!output_.isEmpty()) QVERIFY(window.grab().save(output_ + "/chrome.png"));
        window.rejectClose = false;
        QTest::mouseClick(close, Qt::LeftButton);
        QCOMPARE(window.closeRequests, 2); QVERIFY(!window.isVisible());
    }

    void nativeWindowHitTests() {
#ifdef Q_OS_WIN
        if (QApplication::platformName() != "windows") QSKIP("Native Win32 checks run separately on the Windows platform.");
        GuardedWindow window;
        window.resize(960, 720); window.show(); QTest::qWait(50);
        const HWND handle = reinterpret_cast<HWND>(window.winId());
        const qreal ratio = window.devicePixelRatioF();
        const auto hit = [&](QPoint local) {
            POINT point{qRound(local.x() * ratio), qRound(local.y() * ratio)};
            ClientToScreen(handle, &point);
            MSG event{}; event.hwnd = handle; event.message = WM_NCHITTEST;
            event.lParam = MAKELPARAM(point.x, point.y);
            qintptr result = -999;
            if (!WorkbenchControls::windowChromeNativeEvent(window.chrome, "windows_generic_MSG", &event, &result)) return qintptr(-998);
            return result;
        };
        QCOMPARE(hit(QPoint(1, 1)), qintptr(HTTOPLEFT));
        QCOMPARE(hit(QPoint(window.width() - 2, window.height() - 2)), qintptr(HTBOTTOMRIGHT));
        QCOMPARE(hit(QPoint(200, window.chrome->height() / 2)), qintptr(HTCAPTION));
        QCOMPARE(hit(QPoint(200, 160)), qintptr(HTCLIENT));
        auto* maximize = window.chrome->findChild<QWidget*>("windowMaximizeButton");
        QCOMPARE(hit(maximize->mapTo(&window, maximize->rect().center())), qintptr(HTZOOM));
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfo(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
        MINMAXINFO bounds{};
        MSG event{}; event.hwnd = handle; event.message = WM_GETMINMAXINFO;
        event.lParam = reinterpret_cast<LPARAM>(&bounds);
        qintptr result = 0;
        QVERIFY(WorkbenchControls::windowChromeNativeEvent(window.chrome, "windows_generic_MSG", &event, &result));
        QCOMPARE(bounds.ptMaxSize.x, monitor.rcWork.right - monitor.rcWork.left);
        QCOMPARE(bounds.ptMaxSize.y, monitor.rcWork.bottom - monitor.rcWork.top);
        window.showMaximized(); QTest::qWait(40);
        const QRect available = window.screen()->availableGeometry();
        QVERIFY(available.contains(window.frameGeometry()));
#else
        QSKIP("Win32-only contract.");
#endif
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    app.setOrganizationName("RegMapTests"); app.setApplicationName("RegMapElaRemainingContracts");
    if (qEnvironmentVariableIsSet("REGMAP_UI_REVIEW")) {
        WorkbenchTheme::apply(app, WorkbenchTheme::Mode::light);
        QTemporaryDir project;
        const auto manifest = project.filePath("native.regmap.yaml");
        if (!project.isValid() || !createFixture(manifest)) return 3;
        MainWindow window;
        if (!window.openProjectPath(manifest)) return 4;
        window.resize(1150, 780);
        window.setWindowTitle("RegMap Ela native verification - temporary fixture");
        window.show();
        QTimer::singleShot(600000, &app, &QApplication::quit);
        return app.exec();
    }
    RemainingContracts tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ela_remaining_controls_test.moc"
