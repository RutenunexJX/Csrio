#include "workbench_theme.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>

namespace WorkbenchTheme {

void apply(QApplication& application)
{
    if (QStyle* style = QStyleFactory::create(QStringLiteral("Fusion"))) {
        application.setStyle(style);
    }

    const QStringList availableFamilies = QFontDatabase::families();
    QString fontFamily = QStringLiteral("Segoe UI");
    for (const QString& candidate :
         {QStringLiteral("Aptos"), QStringLiteral("Segoe UI"), QStringLiteral("Carlito")}) {
        if (availableFamilies.contains(candidate, Qt::CaseInsensitive)) {
            fontFamily = candidate;
            break;
        }
    }
    QFont font(fontFamily);
    font.setPointSizeF(10.0);
    font.setWeight(QFont::Normal);
    application.setFont(font);

    const QColor ink(QStringLiteral("#0B1F33"));
    const QColor bodyText(QStringLiteral("#243447"));
    const QColor canvas(QStringLiteral("#F4F7FB"));
    const QColor editable(QStringLiteral("#FFF9E6"));
    const QColor alternate(QStringLiteral("#FFFDF4"));
    const QColor metadata(QStringLiteral("#E7ECF2"));
    const QColor selection(QStringLiteral("#DCE6F1"));
    const QColor border(QStringLiteral("#C6D2E1"));
    const QColor muted(QStringLiteral("#5B6573"));

    QPalette palette;
    palette.setColor(QPalette::Window, canvas);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, editable);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, editable);
    palette.setColor(QPalette::ToolTipText, ink);
    palette.setColor(QPalette::Text, bodyText);
    palette.setColor(QPalette::Button, metadata);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#FFFFFF")));
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, ink);
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#FFFFFF")));
    palette.setColor(QPalette::Midlight, selection);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, ink);
    palette.setColor(QPalette::Shadow, ink);
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#385D8A")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    application.setPalette(palette);

    application.setStyleSheet(QStringLiteral(R"QSS(
QMainWindow, QDialog {
    background: #F4F7FB;
    color: #0B1F33;
}

QMenuBar {
    background: #17365D;
    color: white;
    border-bottom: 2px solid #0B1F33;
    padding: 1px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 9px;
}
QMenuBar::item:selected, QMenuBar::item:pressed {
    background: #385D8A;
}
QMenu {
    background: white;
    color: #0B1F33;
    border: 1px solid #C6D2E1;
    padding: 4px;
}
QMenu::item {
    padding: 5px 26px 5px 10px;
}
QMenu::item:selected {
    background: #DCE6F1;
    color: #0B1F33;
}
QMenu::separator {
    height: 1px;
    background: #C6D2E1;
    margin: 4px 8px;
}

QToolBar {
    background: #17365D;
    border: none;
    border-bottom: 2px solid #0B1F33;
    spacing: 3px;
    padding: 3px 5px;
}
QToolBar::separator {
    width: 1px;
    background: #5B789A;
    margin: 4px;
}
QToolBar QToolButton {
    background: transparent;
    color: white;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 4px 8px;
    font-weight: 500;
}
QToolBar QToolButton:hover {
    background: #385D8A;
    border-color: #6F8FB2;
}
QToolBar QToolButton:pressed,
QToolBar QToolButton:checked {
    background: #0B1F33;
}
QToolBar QToolButton:disabled {
    color: #9FB0C4;
}

QHeaderView::section {
    background: #17365D;
    color: white;
    border: none;
    border-right: 1px solid #C6D2E1;
    border-bottom: 2px solid #0B1F33;
    font-weight: 600;
    padding: 6px 8px;
}
QTableCornerButton::section {
    background: #17365D;
    border: none;
    border-bottom: 2px solid #0B1F33;
}

QTableView, QTreeView, QTreeWidget {
    background: #FFF9E6;
    alternate-background-color: #FFFDF4;
    color: #243447;
    border: 1px solid #C6D2E1;
    gridline-color: #C6D2E1;
    selection-background-color: #DCE6F1;
    selection-color: #0B1F33;
    outline: 0;
}
QTreeView#hierarchyView,
QTableView#problemsView,
QTableView#generatedView,
QTableView#diffView {
    background: #F4F7FB;
    alternate-background-color: #E7ECF2;
}
QAbstractItemView::item {
    padding: 3px 6px;
    border: none;
}
QAbstractItemView::item:selected {
    background: #DCE6F1;
    color: #0B1F33;
}
QAbstractItemView::item:hover:!selected {
    background: #EDF3F9;
}

QWidget#registerContextBar {
    background: #DCE6F1;
    border: 1px solid #9FB0C4;
    border-radius: 3px;
}
QLabel#contextTitle {
    color: #17365D;
    font-weight: 600;
}
QLineEdit#pageBaseEdit,
QLineEdit#blockBaseEdit {
    background: #FFFFFF;
    color: #17365D;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-weight: 500;
}

QTabWidget::pane {
    background: #F4F7FB;
    border: 1px solid #C6D2E1;
    top: -1px;
}
QTabBar::tab {
    background: #E7ECF2;
    color: #385D8A;
    border: 1px solid #C6D2E1;
    border-bottom: none;
    padding: 6px 12px;
    margin-right: 2px;
}
QTabBar::tab:selected {
    background: #17365D;
    color: white;
    border-color: #17365D;
}
QTabBar::tab:hover:!selected {
    background: #DCE6F1;
    color: #0B1F33;
}

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {
    background: #FFF9E6;
    color: #243447;
    border: 1px solid #9FB0C4;
    border-radius: 2px;
    padding: 4px 6px;
    selection-background-color: #DCE6F1;
    selection-color: #0B1F33;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus,
QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
    border: 1px solid #385D8A;
}
QPushButton {
    background: #E7ECF2;
    color: #0B1F33;
    border: 1px solid #9FB0C4;
    border-radius: 3px;
    padding: 4px 10px;
}
QPushButton:hover {
    background: #DCE6F1;
    border-color: #385D8A;
}
QPushButton:pressed {
    background: #C6D2E1;
}

QSplitter::handle {
    background: #D7E0EA;
}
QSplitter::handle:hover {
    background: #9FB0C4;
}
QSplitter::handle:horizontal {
    width: 4px;
}
QSplitter::handle:vertical {
    height: 4px;
}
QStatusBar {
    background: #DCE6F1;
    color: #385D8A;
    border-top: 1px solid #C6D2E1;
}
QStatusBar::item {
    border: none;
}
QToolTip {
    background: #FFF9E6;
    color: #0B1F33;
    border: 1px solid #9FB0C4;
    padding: 4px;
}
)QSS"));
}

} // namespace WorkbenchTheme
