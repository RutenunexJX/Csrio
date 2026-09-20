#pragma once
#include "workbench_theme.hpp"
#include <QApplication>
#include <QStyle>

namespace RegMapSuiteUi {
bool enabled();
QStyle* install(QApplication& application);
void update(QStyle* style, WorkbenchTheme::Mode mode);
}
