#include "workbench_theme.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QSettings>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>

#include <array>
#include <utility>

namespace WorkbenchTheme {
namespace {

constexpr auto themeSettingsKey = "ui/v2/theme";
constexpr auto themeProperty = "regmapWorkbenchTheme";

const Tokens lightTokens{
    QColor(QStringLiteral("#F3F6FA")),
    QColor(QStringLiteral("#FFFFFF")),
    QColor(QStringLiteral("#F7F9FC")),
    QColor(QStringLiteral("#FFFFFF")),
    QColor(QStringLiteral("#FFFFFF")),
    QColor(QStringLiteral("#FFFFFF")),
    QColor(QStringLiteral("#17365D")),
    QColor(QStringLiteral("#102A43")),
    QColor(QStringLiteral("#526579")),
    QColor(QStringLiteral("#FFFFFF")),
    QColor(QStringLiteral("#C7D2E0")),
    QColor(QStringLiteral("#DEE6EF")),
    QColor(QStringLiteral("#DCEAF7")),
    QColor(QStringLiteral("#2F6FA3")),
    QColor(QStringLiteral("#0B73C9")),
    QColor(QStringLiteral("#B42318")),
    QColor(QStringLiteral("#8A5A00")),
    QColor(QStringLiteral("#385D8A")),
    QColor(QStringLiteral("#1F7A4D")),
    QColor(QStringLiteral("#0B5C8E")),
    QColor(QStringLiteral("#5B3FA3")),
    QColor(QStringLiteral("#9C4A08")),
    QColor(QStringLiteral("#7B445C")),
    QColor(QStringLiteral("#B54708")),
    QColor(QStringLiteral("#1F7A4D")),
    QColor(QStringLiteral("#8A5A00")),
    QColor(QStringLiteral("#B42318")),
    {QColor(QStringLiteral("#397FB6")),
     QColor(QStringLiteral("#4E8F39")),
     QColor(QStringLiteral("#C95F1B")),
     QColor(QStringLiteral("#9A6B00")),
     QColor(QStringLiteral("#68798D")),
     QColor(QStringLiteral("#5268B7"))},
};

const Tokens darkTokens{
    QColor(QStringLiteral("#111827")),
    QColor(QStringLiteral("#0F172A")),
    QColor(QStringLiteral("#172033")),
    QColor(QStringLiteral("#1E293B")),
    QColor(QStringLiteral("#121C2E")),
    QColor(QStringLiteral("#121C2E")),
    QColor(QStringLiteral("#0B1220")),
    QColor(QStringLiteral("#E5EDF7")),
    QColor(QStringLiteral("#A9B8CA")),
    QColor(QStringLiteral("#F8FAFC")),
    QColor(QStringLiteral("#3B4A60")),
    QColor(QStringLiteral("#29364A")),
    QColor(QStringLiteral("#233F5D")),
    QColor(QStringLiteral("#62A8E5")),
    QColor(QStringLiteral("#79C0FF")),
    QColor(QStringLiteral("#FF8A80")),
    QColor(QStringLiteral("#F2C96D")),
    QColor(QStringLiteral("#8DC5F4")),
    QColor(QStringLiteral("#73D69A")),
    QColor(QStringLiteral("#75C7F0")),
    QColor(QStringLiteral("#C0A5FF")),
    QColor(QStringLiteral("#FFB86B")),
    QColor(QStringLiteral("#D9A6BE")),
    QColor(QStringLiteral("#FFB86B")),
    QColor(QStringLiteral("#73D69A")),
    QColor(QStringLiteral("#F2C96D")),
    QColor(QStringLiteral("#FF8A80")),
    {QColor(QStringLiteral("#4E91C6")),
     QColor(QStringLiteral("#65A850")),
     QColor(QStringLiteral("#DA7437")),
     QColor(QStringLiteral("#B88924")),
     QColor(QStringLiteral("#7F90A4")),
     QColor(QStringLiteral("#697FC8"))},
};

[[nodiscard]] QString css(const QColor& color)
{
    return color.name(QColor::HexRgb).toUpper();
}

void replaceToken(QString& sheet, const QString& name, const QColor& value)
{
    sheet.replace(QStringLiteral("{{%1}}").arg(name), css(value));
}

} // namespace

const Tokens& tokens(const Mode mode)
{
    return mode == Mode::dark ? darkTokens : lightTokens;
}

Mode currentMode()
{
    const QVariant value = qApp == nullptr
        ? QVariant{}
        : qApp->property(themeProperty);
    return value.toString() == QStringLiteral("dark")
        ? Mode::dark
        : Mode::light;
}

const Tokens& currentTokens()
{
    return tokens(currentMode());
}

Mode preferredMode()
{
    const QString saved = QSettings{}
                              .value(QString::fromLatin1(themeSettingsKey),
                                     QStringLiteral("light"))
                              .toString()
                              .trimmed()
                              .toLower();
    return saved == QStringLiteral("dark") ? Mode::dark : Mode::light;
}

QString modeName(const Mode mode)
{
    return mode == Mode::dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QString styleSheet(const Mode mode)
{
    const Tokens& token = tokens(mode);
    QString sheet = QStringLiteral(R"QSS(
QMainWindow, QDialog { background: {{application}}; color: {{text}}; }
QWidget#workbenchCanvas { background: {{application}}; }
QWidget#pageHeader {
    background: {{raised}};
    border: 1px solid {{border}};
    border-radius: 8px;
}
QLabel#projectTitleLabel { color: {{text}}; font-size: 14px; font-weight: 600; }
QLabel#projectPathLabel, QLabel#selectedFieldSummaryLabel { color: {{muted}}; }
QLabel#fileStateBadge, QLabel#syncStateBadge, QLabel#recoveryStateBadge,
QLabel#registerSelectionLabel, QLabel#fieldSelectionLabel {
    color: {{info}};
    background: {{selection}};
    border: 1px solid {{border}};
    border-radius: 10px;
    padding: 3px 9px;
    font-weight: 600;
}
QLabel#fileStateBadge[state="saved"], QLabel#syncStateBadge[state="synced"],
QLabel#recoveryStateBadge[state="saved"] { color: {{success}}; }
QLabel#fileStateBadge[state="dirty"], QLabel#syncStateBadge[state="dirty"],
QLabel#fileStateBadge[state="loading"], QLabel#syncStateBadge[state="busy"] { color: {{warning}}; }
QLabel#fileStateBadge[state="failed"], QLabel#syncStateBadge[state="blocked"],
QLabel#syncStateBadge[state="conflict"], QLabel#syncStateBadge[state="partial"],
QLabel#recoveryStateBadge[state="failed"] { color: {{error}}; }
QToolButton#saveSyncButton, QToolButton#synchronizeButton {
    min-height: 28px;
    padding: 2px 12px;
    color: {{onAccent}};
    background: {{selectionStrong}};
    border: 1px solid {{selectionStrong}};
    border-radius: 5px;
    font-weight: 600;
}
QToolButton#saveSyncButton:hover, QToolButton#synchronizeButton:hover { border-color: {{focus}}; }
QToolButton#generateButton {
    min-height: 28px;
    padding: 2px 12px;
    color: {{text}};
    background: {{panel}};
    border: 1px solid {{border}};
    border-radius: 5px;
    font-weight: 600;
}
QMenuBar {
    background: {{header}};
    color: {{onAccent}};
    border-bottom: 1px solid {{border}};
    padding: 1px 4px;
}
QMenuBar::item { background: transparent; padding: 5px 9px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: {{selectionStrong}}; }
QMenu { background: {{raised}}; color: {{text}}; border: 1px solid {{border}}; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 10px; }
QMenu::item:selected { background: {{selection}}; color: {{text}}; }
QMenu::separator { height: 1px; background: {{divider}}; margin: 4px 8px; }
QToolBar {
    background: {{header}};
    border: none;
    border-bottom: 1px solid {{border}};
    spacing: 4px;
    padding: 4px 6px;
}
QToolBar::separator { width: 1px; background: {{border}}; margin: 4px; }
QToolBar QToolButton {
    min-height: 26px;
    background: transparent;
    color: {{onAccent}};
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 2px 8px;
}
QToolBar QToolButton:hover { background: {{selectionStrong}}; border-color: {{focus}}; }
QToolBar QToolButton:pressed, QToolBar QToolButton:checked { background: {{canvas}}; color: {{text}}; }
QToolBar QToolButton:disabled { color: {{muted}}; }
QToolBar QToolButton#openXlsxButton[state="stale"] { color: {{warning}}; border-color: {{warning}}; }
QToolBar QLineEdit#globalSearchEdit {
    min-width: 210px;
    max-width: 330px;
    background: {{input}};
    color: {{text}};
    border: 1px solid {{border}};
    border-radius: 5px;
    padding: 4px 8px;
}
QToolBar QLabel#searchResultLabel { color: {{onAccent}}; min-width: 54px; padding: 0 3px; }
QToolBar QToolButton#resultsToggleButton { border-color: {{border}}; }
QHeaderView::section {
    background: {{header}};
    color: {{onAccent}};
    border: none;
    border-right: 1px solid {{border}};
    border-bottom: 1px solid {{border}};
    font-weight: 600;
    padding: 7px 8px;
}
QTableCornerButton::section { background: {{header}}; border: none; }
QTableView, QTreeView, QTreeWidget, QListWidget {
    background: {{table}};
    alternate-background-color: {{panel}};
    color: {{text}};
    border: 1px solid {{border}};
    gridline-color: {{divider}};
    selection-background-color: {{selection}};
    selection-color: {{text}};
    outline: 0;
}
QAbstractItemView::item { padding: 4px 7px; border: none; }
QAbstractItemView::item:selected { background: {{selection}}; color: {{text}}; }
QAbstractItemView::item:hover:!selected { background: {{panel}}; }
QAbstractItemView:focus { border: 2px solid {{focus}}; }
QWidget#hierarchyPanel, QWidget#fieldPanel { background: {{canvas}}; }
QWidget#hierarchyHeaderBar {
    background: {{header}};
    border: 1px solid {{header}};
    border-radius: 5px 5px 0 0;
}
QLabel#hierarchyTitle { color: {{onAccent}}; font-weight: 600; }
QPushButton#hierarchyAddButton { background: {{raised}}; color: {{header}}; border-color: {{border}}; }
QWidget#registerContextBar, QWidget#fieldHeaderBar, QWidget#registerToolsBar,
QWidget#diagnosticsToolbar {
    background: {{panel}};
    border: 1px solid {{border}};
    border-radius: 5px;
}
QLabel#contextTitle, QLabel#fieldContextLabel, QLabel#registerToolsTitle {
    color: {{text}};
    font-weight: 600;
}
QLabel#fixedAddressLegend { color: {{reset}}; }
QLabel#registerCountLabel, QLabel#problemsSummaryLabel { color: {{muted}}; }
QLabel#problemsSummaryLabel[state="error"] { color: {{error}}; font-weight: 600; }
QLabel#searchResultLabel[state="empty"] { color: {{error}}; font-weight: 600; }
QWidget#registerEmptyState {
    background: {{panel}};
    border: 1px dashed {{border}};
    border-radius: 6px;
}
QLabel#registerEmptyTitle { color: {{text}}; font-weight: 600; }
QLabel#registerEmptyHint { color: {{muted}}; }
QPushButton#registerEmptyPrimaryButton { background: {{selectionStrong}}; color: {{onAccent}}; border-color: {{selectionStrong}}; }
QWidget#registerFeedbackBar, QWidget#fieldFeedbackBar {
    background: {{panel}};
    border: 1px solid {{error}};
    border-radius: 5px;
}
QLabel#registerFeedbackLabel, QLabel#fieldFeedbackLabel,
QWidget#registerFeedbackBar QToolButton, QWidget#fieldFeedbackBar QToolButton { color: {{error}}; }
QWidget#conflictBar { background: {{panel}}; border: 1px solid {{warning}}; border-radius: 5px; }
QLabel#conflictSummaryLabel { color: {{warning}}; font-weight: 600; }
QTabWidget::pane { background: {{canvas}}; border: 1px solid {{border}}; top: -1px; }
QTabBar::tab { background: {{panel}}; color: {{muted}}; border: 1px solid {{border}}; border-bottom: none; padding: 7px 13px; }
QTabBar::tab:selected { background: {{header}}; color: {{onAccent}}; border-color: {{header}}; }
QTabBar::tab:hover:!selected { background: {{selection}}; color: {{text}}; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {
    min-height: 24px;
    background: {{input}};
    color: {{text}};
    border: 1px solid {{border}};
    border-radius: 4px;
    padding: 3px 7px;
    selection-background-color: {{selection}};
    selection-color: {{text}};
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QTextEdit:focus, QPlainTextEdit:focus { border: 2px solid {{focus}}; }
QLineEdit[readOnly="true"] { background: {{panel}}; color: {{muted}}; }
QPushButton, QToolButton {
    min-height: 26px;
    background: {{panel}};
    color: {{text}};
    border: 1px solid {{border}};
    border-radius: 4px;
    padding: 2px 9px;
}
QPushButton:hover, QToolButton:hover { background: {{selection}}; border-color: {{focus}}; }
QPushButton:pressed, QToolButton:pressed { background: {{divider}}; }
QPushButton:focus, QToolButton:focus, QComboBox:focus { border: 2px solid {{focus}}; }
QSplitter::handle { background: {{divider}}; border-radius: 2px; }
QSplitter::handle:hover, QSplitter::handle:focus { background: {{focus}}; }
QSplitter::handle:horizontal { width: 8px; margin: 1px 2px; }
QSplitter::handle:vertical { height: 8px; margin: 2px 1px; }
QStatusBar { background: {{panel}}; color: {{muted}}; border-top: 1px solid {{border}}; }
QStatusBar::item { border: none; }
QStatusBar QLabel#activeContextLabel { color: {{info}}; padding: 2px 7px; }
QToolTip { background: {{raised}}; color: {{text}}; border: 1px solid {{border}}; padding: 5px; }
)QSS");

    const std::array<std::pair<QString, QColor>, 25> replacements{{
        {QStringLiteral("application"), token.application},
        {QStringLiteral("canvas"), token.canvas},
        {QStringLiteral("panel"), token.panel},
        {QStringLiteral("raised"), token.raisedSurface},
        {QStringLiteral("input"), token.input},
        {QStringLiteral("table"), token.table},
        {QStringLiteral("header"), token.header},
        {QStringLiteral("text"), token.text},
        {QStringLiteral("muted"), token.mutedText},
        {QStringLiteral("onAccent"), token.onAccent},
        {QStringLiteral("border"), token.border},
        {QStringLiteral("divider"), token.divider},
        {QStringLiteral("selection"), token.selection},
        {QStringLiteral("selectionStrong"), token.selectionStrong},
        {QStringLiteral("focus"), token.focus},
        {QStringLiteral("error"), token.diagnosticError},
        {QStringLiteral("warning"), token.diagnosticWarning},
        {QStringLiteral("info"), token.diagnosticInfo},
        {QStringLiteral("success"), token.success},
        {QStringLiteral("address"), token.address},
        {QStringLiteral("access"), token.access},
        {QStringLiteral("reset"), token.reset},
        {QStringLiteral("reserved"), token.reserved},
        {QStringLiteral("modified"), token.modified},
        {QStringLiteral("rtlConflict"), token.rtlConflict},
    }};
    for (const auto& [name, value] : replacements) {
        replaceToken(sheet, name, value);
    }
    return sheet;
}

void apply(QApplication& application)
{
    apply(application, preferredMode());
}

void apply(QApplication& application, const Mode mode)
{
    if (application.style() == nullptr ||
        application.style()->objectName().compare(
            QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
        if (QStyle* style = QStyleFactory::create(QStringLiteral("Fusion"))) {
            application.setStyle(style);
        }
    }

    const QStringList availableFamilies = QFontDatabase::families();
    QFont font = application.font();
    for (const QString& candidate :
         {QStringLiteral("Aptos"), QStringLiteral("Segoe UI"), QStringLiteral("Carlito")}) {
        if (availableFamilies.contains(candidate, Qt::CaseInsensitive)) {
            font.setFamily(candidate);
            break;
        }
    }
    font.setPointSizeF(10.0);
    font.setWeight(QFont::Normal);
    application.setFont(font);

    const Tokens& token = tokens(mode);
    QPalette palette;
    palette.setColor(QPalette::Window, token.application);
    palette.setColor(QPalette::WindowText, token.text);
    palette.setColor(QPalette::Base, token.input);
    palette.setColor(QPalette::AlternateBase, token.panel);
    palette.setColor(QPalette::ToolTipBase, token.raisedSurface);
    palette.setColor(QPalette::ToolTipText, token.text);
    palette.setColor(QPalette::Text, token.text);
    palette.setColor(QPalette::Button, token.panel);
    palette.setColor(QPalette::ButtonText, token.text);
    palette.setColor(QPalette::BrightText, token.onAccent);
    palette.setColor(QPalette::Highlight, token.selection);
    palette.setColor(QPalette::HighlightedText, token.text);
    palette.setColor(QPalette::Light, token.raisedSurface);
    palette.setColor(QPalette::Midlight, token.selection);
    palette.setColor(QPalette::Mid, token.border);
    palette.setColor(QPalette::Dark, token.header);
    palette.setColor(QPalette::Shadow, token.header);
    palette.setColor(QPalette::Link, token.address);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, token.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::Text, token.mutedText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, token.mutedText);
    application.setPalette(palette);
    application.setStyleSheet(styleSheet(mode));
    application.setProperty(themeProperty, modeName(mode));
}

void storePreference(const Mode mode)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(themeSettingsKey), modeName(mode));
    settings.sync();
}

} // namespace WorkbenchTheme
