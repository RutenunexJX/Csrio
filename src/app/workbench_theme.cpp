#include "workbench_theme.hpp"
#include "workbench_controls.hpp"
#include "suiteui_adapter.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPalette>
#include <QSettings>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace WorkbenchTheme {
namespace {

constexpr auto themeSettingsKey = "ui/v2/theme";
constexpr auto themeProperty = "regmapWorkbenchTheme";
constexpr auto reducedMotionProperty = "regmapWorkbenchReducedMotion";

constexpr DensityMetrics densityMetrics{};

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
    QColor(QStringLiteral("#2F6FA3")),
    QColor(QStringLiteral("#8A5A00")),
    QColor(QStringLiteral("#B42318")),
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
    QColor(QStringLiteral("#2D6F9F")),
    QColor(QStringLiteral("#79C0FF")),
    QColor(QStringLiteral("#2D6F9F")),
    QColor(QStringLiteral("#F2C96D")),
    QColor(QStringLiteral("#FF8A80")),
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

void replaceMetric(QString& sheet, const QString& name, const int value)
{
    sheet.replace(
        QStringLiteral("{{%1}}").arg(name),
        QString::number(value));
}

[[nodiscard]] double linearChannel(const int value)
{
    const double component = static_cast<double>(value) / 255.0;
    return component <= 0.04045
        ? component / 12.92
        : std::pow((component + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double relativeLuminance(const QColor& color)
{
    return 0.2126 * linearChannel(color.red()) +
        0.7152 * linearChannel(color.green()) +
        0.0722 * linearChannel(color.blue());
}

} // namespace

const Tokens& tokens(const Mode mode)
{
    return mode == Mode::dark ? darkTokens : lightTokens;
}

const DensityMetrics& metrics()
{
    return densityMetrics;
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
    const QSettings settings;
    if (settings.contains(QString::fromLatin1(themeSettingsKey))) {
        const QString saved = settings
                                  .value(QString::fromLatin1(themeSettingsKey))
                                  .toString()
                                  .trimmed()
                                  .toLower();
        return saved == QStringLiteral("dark") ? Mode::dark : Mode::light;
    }
    return qApp != nullptr && qApp->styleHints() != nullptr &&
            qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark
        ? Mode::dark
        : Mode::light;
}

QString modeName(const Mode mode)
{
    return mode == Mode::dark ? QStringLiteral("dark") : QStringLiteral("light");
}

static QString styleSheetImpl(const Mode mode, bool sdkControls, bool elaControls = false)
{
    const Tokens& token = tokens(mode);
    const QString headerButtons = sdkControls ? QString() : QStringLiteral(R"QSS(
QToolButton#saveSyncButton, QToolButton#synchronizeButton {
    min-height: {{primary}}px;
    padding: 0 {{space3}}px;
    color: {{onAccent}};
    background: {{accent}};
    border: 1px solid {{accent}};
    border-radius: {{radiusSmall}}px;
    font-weight: 600;
}
QToolButton#saveSyncButton:hover, QToolButton#synchronizeButton:hover { border-color: {{focus}}; }
QToolButton#generateButton {
    min-height: {{standard}}px;
    padding: 0 {{space3}}px;
    color: {{text}};
    background: {{panel}};
    border: 1px solid {{border}};
    border-radius: {{radiusSmall}}px;
    font-weight: 600;
}
)QSS");
    const QString controls = sdkControls ? QString() : QStringLiteral(R"QSS(
QPushButton, QToolButton {
    min-height: {{standard}}px;
    background: {{panel}};
    color: {{text}};
    border: 1px solid {{border}};
    border-radius: {{radiusSmall}}px;
    padding: 0 {{space2}}px;
}
QPushButton:hover, QToolButton:hover { background: {{selection}}; border-color: {{focus}}; }
QPushButton:pressed, QToolButton:pressed { background: {{divider}}; }
QPushButton:checked, QToolButton:checked { background: {{selection}}; border-color: {{accent}}; }
QPushButton:disabled, QToolButton:disabled { color: {{muted}}; background: {{panel}}; border-color: {{divider}}; }
QPushButton:focus, QToolButton:focus, QComboBox:focus { border: 2px solid {{focus}}; }
)QSS");
    const QString navigationControls = elaControls ? QString() : QStringLiteral(R"QSS(
QMenuBar {
    background: {{header}};
    color: {{onAccent}};
    border-bottom: 1px solid {{border}};
    padding: 0 {{space}}px;
}
QMenuBar::item { background: transparent; padding: {{space}}px {{space2}}px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: {{selectionStrong}}; }
QMenu { background: {{raised}}; color: {{text}}; border: 1px solid {{border}}; padding: {{space}}px; }
QMenu::item { padding: {{space}}px {{compact}}px {{space}}px {{space2}}px; }
QMenu::item:selected { background: {{selection}}; color: {{text}}; }
QMenu::separator { height: 1px; background: {{divider}}; margin: {{space}}px {{space2}}px; }
QToolBar {
    background: {{header}};
    border: none;
    border-bottom: 1px solid {{border}};
    spacing: {{space}}px;
    padding: {{space}}px {{space2}}px;
}
QToolBar::separator { width: 1px; background: {{border}}; margin: {{space}}px; }
QToolBar QToolButton {
    min-height: {{compact}}px;
    background: transparent;
    color: {{onAccent}};
    border: 1px solid transparent;
    border-radius: {{radiusSmall}}px;
    padding: 0 {{space2}}px;
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
    border-radius: {{radiusSmall}}px;
    padding: 0 {{space2}}px;
}
QToolBar QLabel#searchResultLabel { color: {{onAccent}}; min-width: 54px; padding: 0 3px; }
QToolBar QToolButton#resultsToggleButton { border-color: {{border}}; }
)QSS");
    const QString formControls = elaControls ? QString() : QStringLiteral(R"QSS(
QTabWidget::pane { background: {{canvas}}; border: 1px solid {{border}}; top: -1px; }
QTabBar::tab { background: {{panel}}; color: {{muted}}; border: 1px solid {{border}}; border-bottom: none; padding: {{space2}}px {{space3}}px; }
QTabBar::tab:selected { background: {{header}}; color: {{onAccent}}; border-color: {{header}}; }
QTabBar::tab:hover:!selected { background: {{selection}}; color: {{text}}; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit {
    min-height: {{compact}}px;
    background: {{input}};
    color: {{text}};
    border: 1px solid {{border}};
    border-radius: {{radiusSmall}}px;
    padding: 0 {{space2}}px;
    selection-background-color: {{selection}};
    selection-color: {{text}};
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QTextEdit:focus, QPlainTextEdit:focus { border: 2px solid {{focus}}; }
QLineEdit[readOnly="true"] { background: {{panel}}; color: {{muted}}; }
)QSS");
    const QString viewActions = elaControls ? QString() : QStringLiteral(R"QSS(
QPushButton#hierarchyAddButton { background: {{raised}}; color: {{header}}; border-color: {{border}}; }
QPushButton#registerEmptyPrimaryButton { background: {{selectionStrong}}; color: {{onAccent}}; border-color: {{selectionStrong}}; }
QWidget#externalDecisionBar QPushButton { min-height: {{compact}}px; }
)QSS");
    const QString dataViewItems = elaControls ? QStringLiteral(R"QSS(
QTableView::item, QTreeView::item, QListWidget::item { padding: {{space}}px {{space2}}px; border: none; }
QTableView::item:selected, QTreeView::item:selected, QListWidget::item:selected { background: {{selection}}; color: {{text}}; }
QTableView::item:hover:!selected, QTreeView::item:hover:!selected, QListWidget::item:hover:!selected { background: {{panel}}; }
QTableView:focus, QTreeView:focus, QListWidget:focus { border: 2px solid {{focus}}; }
)QSS") : QStringLiteral(R"QSS(
QAbstractItemView::item { padding: {{space}}px {{space2}}px; border: none; }
QAbstractItemView::item:selected { background: {{selection}}; color: {{text}}; }
QAbstractItemView::item:hover:!selected { background: {{panel}}; }
QAbstractItemView:focus { border: 2px solid {{focus}}; }
)QSS");
    QString sheet = QStringLiteral(R"QSS(
QMainWindow, QDialog { background: {{application}}; color: {{text}}; }
QWidget#workbenchCanvas { background: {{application}}; }
QWidget#pageHeader {
    background: {{raised}};
    border: 1px solid {{border}};
    border-radius: {{radiusLarge}}px;
}
QLabel#projectTitleLabel { color: {{text}}; font-size: 14px; font-weight: 600; }
QLabel#projectPathLabel, QLabel#selectedFieldSummaryLabel { color: {{muted}}; }
QLabel#fileStateBadge, QLabel#syncStateBadge, QLabel#recoveryStateBadge,
QLabel#registerSelectionLabel, QLabel#fieldSelectionLabel {
    color: {{info}};
    background: {{selection}};
    border: 1px solid {{border}};
    border-radius: {{radiusLarge}}px;
    padding: {{space}}px {{space2}}px;
    font-weight: 600;
}
QLabel#fileStateBadge[state="saved"], QLabel#syncStateBadge[state="synced"],
QLabel#recoveryStateBadge[state="saved"] { color: {{success}}; }
QLabel#fileStateBadge[state="dirty"], QLabel#syncStateBadge[state="dirty"],
QLabel#fileStateBadge[state="loading"], QLabel#syncStateBadge[state="busy"] { color: {{warning}}; }
QLabel#fileStateBadge[state="failed"], QLabel#syncStateBadge[state="blocked"],
QLabel#syncStateBadge[state="conflict"], QLabel#syncStateBadge[state="partial"],
QLabel#recoveryStateBadge[state="failed"] { color: {{error}}; }
{{headerButtons}}
{{navigationControls}}
QHeaderView::section {
    background: {{header}};
    color: {{onAccent}};
    border: none;
    border-right: 1px solid {{border}};
    border-bottom: 1px solid {{border}};
    font-weight: 600;
    padding: {{space2}}px;
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
{{dataViewItems}}
QWidget#hierarchyPanel, QWidget#fieldPanel { background: {{canvas}}; }
QWidget#hierarchyHeaderBar {
    background: {{header}};
    border: 1px solid {{header}};
    border-radius: {{radiusSmall}}px {{radiusSmall}}px 0 0;
}
QLabel#hierarchyTitle { color: {{onAccent}}; font-weight: 600; }

QWidget#registerContextBar, QWidget#fieldHeaderBar, QWidget#registerToolsBar,
QWidget#diagnosticsToolbar {
    background: {{panel}};
    border: 1px solid {{border}};
    border-radius: {{radiusSmall}}px;
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
    border-radius: {{radiusLarge}}px;
}
QLabel#registerEmptyTitle { color: {{text}}; font-weight: 600; }
QLabel#registerEmptyHint { color: {{muted}}; }

QWidget#registerFeedbackBar, QWidget#fieldFeedbackBar {
    background: {{panel}};
    border: 1px solid {{error}};
    border-radius: {{radiusSmall}}px;
}
QLabel#registerFeedbackLabel, QLabel#fieldFeedbackLabel { color: {{error}}; }
QWidget#conflictBar, QWidget#externalDecisionBar {
    background: {{panel}};
    border: 1px solid {{warning}};
    border-radius: {{radiusSmall}}px;
}
QLabel#conflictSummaryLabel { color: {{warning}}; font-weight: 600; }
QLabel#externalDecisionStatusLabel[state="pending"],
QLabel#externalDecisionStatusLabel[state="warning"] { color: {{warning}}; font-weight: 600; }
QLabel#externalDecisionStatusLabel[state="error"] { color: {{error}}; font-weight: 600; }
QLabel#externalDecisionStatusLabel[state="success"] { color: {{success}}; font-weight: 600; }

{{formControls}}
{{viewActions}}
{{controls}}
QSplitter::handle { background: {{divider}}; border-radius: {{space}}px; }
QSplitter::handle:hover, QSplitter::handle:focus { background: {{focus}}; }
QSplitter::handle:horizontal { width: 8px; margin: 1px 2px; }
QSplitter::handle:vertical { height: 8px; margin: 2px 1px; }
QStatusBar { background: {{panel}}; color: {{muted}}; border-top: 1px solid {{border}}; }
QStatusBar::item { border: none; }
QStatusBar QLabel#activeContextLabel { color: {{info}}; padding: 0 {{space2}}px; }
QToolTip { background: {{raised}}; color: {{text}}; border: 1px solid {{border}}; padding: {{space}}px; }
)QSS");

    sheet.replace(QStringLiteral("{{headerButtons}}"), headerButtons);
    sheet.replace(QStringLiteral("{{controls}}"), controls);
    sheet.replace(QStringLiteral("{{navigationControls}}"), navigationControls);
    sheet.replace(QStringLiteral("{{formControls}}"), formControls);
    sheet.replace(QStringLiteral("{{viewActions}}"), viewActions);
    sheet.replace(QStringLiteral("{{dataViewItems}}"), dataViewItems);
    if (elaControls) {
        // Ela owns migrated view/status painting; native editor views retain QSS.
        sheet.replace(QStringLiteral("QTableView"), QStringLiteral("QTableView[regmapElaItemView=\"false\"]"));
        sheet.replace(QStringLiteral("QListWidget"), QStringLiteral("QListWidget[regmapElaItemView=\"false\"]"));
        sheet.replace(QStringLiteral("QHeaderView::section"), QStringLiteral("QHeaderView[regmapElaItemView=\"false\"]::section"));
        sheet.replace(QStringLiteral("QTableCornerButton::section"), QStringLiteral("QTableView[regmapElaItemView=\"false\"] QTableCornerButton::section"));
        sheet.replace(QStringLiteral("QStatusBar { background: {{panel}}; color: {{muted}}; border-top: 1px solid {{border}}; }"), QString());
        sheet.replace(QStringLiteral("QStatusBar::item { border: none; }"), QString());
    }
    const std::array<std::pair<QString, QColor>, 26> replacements{{
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
        {QStringLiteral("accent"), token.accent},
        {QStringLiteral("error"), token.error},
        {QStringLiteral("warning"), token.warning},
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
    const std::array<std::pair<QString, int>, 9> metricReplacements{{
        {QStringLiteral("space"), densityMetrics.baseSpacing},
        {QStringLiteral("space2"), densityMetrics.baseSpacing * 2},
        {QStringLiteral("space3"), densityMetrics.baseSpacing * 3},
        {QStringLiteral("compact"), densityMetrics.compactControlHeight},
        {QStringLiteral("standard"), densityMetrics.standardControlHeight},
        {QStringLiteral("primary"), densityMetrics.primaryControlHeight},
        {QStringLiteral("radiusSmall"), densityMetrics.smallRadius},
        {QStringLiteral("radiusLarge"), densityMetrics.largeRadius},
        {QStringLiteral("panelHeaderPadding"),
         densityMetrics.panelHeaderHorizontalPadding},
    }};
    for (const auto& [name, value] : metricReplacements) {
        replaceMetric(sheet, name, value);
    }
    return sheet;
}

QString styleSheet(const Mode mode) { return styleSheetImpl(mode, false); }

double contrastRatio(const QColor& foreground, const QColor& background)
{
    const double foregroundLuminance = relativeLuminance(foreground);
    const double backgroundLuminance = relativeLuminance(background);
    const double lighter = std::max(foregroundLuminance, backgroundLuminance);
    const double darker = std::min(foregroundLuminance, backgroundLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor contrastingText(const QColor& background)
{
    const QColor light(QStringLiteral("#FFFFFF"));
    const QColor dark(QStringLiteral("#000000"));
    return contrastRatio(dark, background) >=
            contrastRatio(light, background)
        ? dark
        : light;
}

bool reducedMotionEnabled()
{
    const QString environment = qEnvironmentVariable("QT_REDUCE_MOTION")
                                    .trimmed()
                                    .toLower();
    if (!environment.isEmpty()) {
        return environment == QStringLiteral("1") ||
            environment == QStringLiteral("true") ||
            environment == QStringLiteral("yes") ||
            environment == QStringLiteral("on");
    }
    if (qApp != nullptr &&
        qApp->property(reducedMotionProperty).isValid()) {
        return qApp->property(reducedMotionProperty).toBool();
    }
#ifdef Q_OS_WIN
    const QSettings accessibility(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Control Panel\\Desktop\\WindowMetrics"),
        QSettings::NativeFormat);
    return accessibility.value(QStringLiteral("MinAnimate"), 1).toInt() == 0;
#else
    return false;
#endif
}

void apply(QApplication& application)
{
    apply(application, preferredMode());
}

QFont monospaceFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto families = QFontDatabase::families();
    for (const auto& candidate : {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                                 QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono")}) {
        if (families.contains(candidate, Qt::CaseInsensitive)) {
            font.setFamily(candidate);
            break;
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setPointSizeF(qApp->font().pointSizeF());
    return font;
}

void apply(QApplication& application, const Mode mode)
{
    WorkbenchControls::initialize(application);
    QStyle* backend = nullptr;
    if (RegMapSuiteUi::enabled()) {
        backend = RegMapSuiteUi::install(application);
    } else if (application.style() == nullptr ||
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
    if (backend) RegMapSuiteUi::update(backend, mode);
    WorkbenchControls::updateTheme(mode);
    const bool ela = WorkbenchControls::backend() == WorkbenchControls::Backend::ela;
    application.setStyleSheet(styleSheetImpl(mode, backend != nullptr || ela, ela));
    application.setProperty(themeProperty, modeName(mode));
    application.setProperty(
        reducedMotionProperty, reducedMotionEnabled());
}

void storePreference(const Mode mode)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(themeSettingsKey), modeName(mode));
    settings.sync();
}

} // namespace WorkbenchTheme
