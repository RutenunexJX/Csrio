#pragma once

#include <vector>

class QTableView;

namespace regmap::ui {

void configureDataTable(QTableView* view);
void configureRowSelectionGutter(QTableView* view);

[[nodiscard]] std::vector<int>
fullySelectedRows(const QTableView* view);

[[nodiscard]] int
fullySelectedRowCount(const QTableView* view);

} // namespace regmap::ui
