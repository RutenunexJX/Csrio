#include "table_selection_utils.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableView>

#include <cstddef>

namespace regmap::ui {

void configureDataTable(QTableView* view)
{
    if (view == nullptr) {
        return;
    }
    view->setSelectionBehavior(QAbstractItemView::SelectItems);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(false);
    view->setShowGrid(true);
    view->setWordWrap(false);
    view->setCornerButtonEnabled(false);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(28);
    view->horizontalHeader()->setMinimumSectionSize(72);
    view->horizontalHeader()->setStretchLastSection(true);
}

void configureRowSelectionGutter(QTableView* view)
{
    if (view == nullptr) {
        return;
    }
    auto* gutter = view->verticalHeader();
    gutter->setVisible(true);
    gutter->setSectionsClickable(true);
    gutter->setHighlightSections(true);
    gutter->setFixedWidth(38);
    gutter->setDefaultAlignment(Qt::AlignCenter);
    gutter->setToolTip(
        QStringLiteral(
            "Click a row number to select the complete object. Cell selections remain available for ordinary copy and paste."));
}

std::vector<int> fullySelectedRows(const QTableView* view)
{
    std::vector<int> rows;
    if (view == nullptr || view->model() == nullptr ||
        view->selectionModel() == nullptr) {
        return rows;
    }
    rows.reserve(
        static_cast<std::size_t>(view->model()->rowCount()));
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        if (view->selectionModel()->isRowSelected(row, {})) {
            rows.push_back(row);
        }
    }
    return rows;
}

int fullySelectedRowCount(const QTableView* view)
{
    return static_cast<int>(fullySelectedRows(view).size());
}

} // namespace regmap::ui
