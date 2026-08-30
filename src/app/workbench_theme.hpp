#pragma once

#include <QColor>
#include <QString>

#include <array>

class QApplication;

namespace WorkbenchTheme {

enum class Mode {
    light,
    dark,
};

struct Tokens {
    QColor application;
    QColor canvas;
    QColor panel;
    QColor raisedSurface;
    QColor input;
    QColor table;
    QColor header;
    QColor text;
    QColor mutedText;
    QColor onAccent;
    QColor border;
    QColor divider;
    QColor selection;
    QColor selectionStrong;
    QColor focus;
    QColor accent;
    QColor warning;
    QColor error;
    QColor diagnosticError;
    QColor diagnosticWarning;
    QColor diagnosticInfo;
    QColor success;
    QColor address;
    QColor access;
    QColor reset;
    QColor reserved;
    QColor modified;
    QColor rtlSynced;
    QColor rtlStale;
    QColor rtlConflict;
    std::array<QColor, 6> bitfield;
};

struct DensityMetrics {
    int baseSpacing{4};
    int compactControlHeight{28};
    int standardControlHeight{32};
    int primaryControlHeight{36};
    int smallRadius{6};
    int largeRadius{8};
    int panelHeaderHorizontalPadding{8};
    int panelHeaderVerticalPadding{4};
};

[[nodiscard]] const Tokens& tokens(Mode mode);
[[nodiscard]] const DensityMetrics& metrics();
[[nodiscard]] const Tokens& currentTokens();
[[nodiscard]] Mode currentMode();
[[nodiscard]] Mode preferredMode();
[[nodiscard]] QString modeName(Mode mode);
[[nodiscard]] QString styleSheet(Mode mode);
[[nodiscard]] double contrastRatio(const QColor& foreground,
                                   const QColor& background);
[[nodiscard]] QColor contrastingText(const QColor& background);
[[nodiscard]] bool reducedMotionEnabled();

void apply(QApplication& application);
void apply(QApplication& application, Mode mode);
void storePreference(Mode mode);

} // namespace WorkbenchTheme
