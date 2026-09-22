#pragma once

#include "workbench_theme.hpp"
#include <QDialogButtonBox>

class QAbstractScrollArea;
class QAction;
class QApplication;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListView;
class QListWidget;
class QMenu;
class QMainWindow;
class QMenuBar;
class QPushButton;
class QTabWidget;
class QTableView;
class QStatusBar;
class QToolBar;
class QToolButton;
class QWidget;

namespace WorkbenchControls {
enum class Backend { classic, suiteUi, ela };

// Fixed at first use, before creating controls. Theme changes never replace it.
[[nodiscard]] Backend backend();
void initialize(QApplication& application);
void updateTheme(WorkbenchTheme::Mode mode);

QPushButton* pushButton(QWidget* parent);
QPushButton* pushButton(const QString& text, QWidget* parent);
QToolButton* toolButton(QWidget* parent);
QLineEdit* lineEdit(QWidget* parent);
QComboBox* comboBox(QWidget* parent);
QComboBox* cellComboBox(QWidget* parent, bool autoPopup = false);
QCheckBox* checkBox(const QString& text, QWidget* parent);
QMenu* menu(QWidget* parent);
QMenuBar* menuBar(QWidget* parent);
QMenu* addMenu(QMenuBar* parent, const QString& title);
QMenu* addMenu(QMenu* parent, const QString& title);
QToolBar* toolBar(const QString& title, QWidget* parent);
void addAction(QToolBar* toolbar, QAction* action);
QTabWidget* tabWidget(QWidget* parent);
QStatusBar* statusBar(QWidget* parent);
QWidget* installWindowChrome(QMainWindow* window);
bool windowChromeNativeEvent(QWidget* chrome, const QByteArray& type, void* message, qintptr* result);
QListView* listView(QWidget* parent);
QListWidget* listWidget(QWidget* parent);
QTableView* resultTable(QWidget* parent);
void styleScrollArea(QAbstractScrollArea* area);
QDialogButtonBox* dialogButtons(QWidget* parent);
QPushButton* standardButton(QDialogButtonBox* box, QDialogButtonBox::StandardButton which);
void installToolTip(QWidget* widget);
QWidget* successMessage(QWidget* parent, const QString& title, const QString& text, int duration = 4000);
void dismissMessage(QWidget* parent);
} // namespace WorkbenchControls
