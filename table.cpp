#include "table.h"
#include "element.h"
#include <algorithm>

namespace UDoc {

TableCell* Table::cellAt(uint32_t row, uint32_t col) {
    for (auto& cell : cells) if (cell.row == row && cell.col == col) return &cell;
    return nullptr;
}

const TableCell* Table::cellAt(uint32_t row, uint32_t col) const {
    for (const auto& cell : cells) if (cell.row == row && cell.col == col) return &cell;
    return nullptr;
}

void Table::insertRow(uint32_t beforeRow) {
    for (auto& cell : cells) if (cell.row >= beforeRow) cell.row++;
    for (uint32_t c = 0; c < cols; ++c) {
        TableCell newCell;
        newCell.row = beforeRow;
        newCell.col = c;
        cells.push_back(std::move(newCell));
    }
    rowHeights.insert(rowHeights.begin() + beforeRow, -1);
    rows++;
}

void Table::deleteRow(uint32_t row) {
    cells.erase(std::remove_if(cells.begin(), cells.end(),
                               [row](const auto& c) { return c.row == row; }), cells.end());
    for (auto& cell : cells) if (cell.row > row) cell.row--;
    if (row < rowHeights.size()) rowHeights.erase(rowHeights.begin() + row);
    rows--;
}

void Table::insertColumn(uint32_t beforeCol) {
    for (auto& cell : cells) if (cell.col >= beforeCol) cell.col++;
    for (uint32_t r = 0; r < rows; ++r) {
        TableCell newCell;
        newCell.row = r;
        newCell.col = beforeCol;
        cells.push_back(std::move(newCell));
    }
    colWidths.insert(colWidths.begin() + beforeCol, -1);
    cols++;
}

void Table::deleteColumn(uint32_t col) {
    cells.erase(std::remove_if(cells.begin(), cells.end(),
                               [col](const auto& c) { return c.col == col; }), cells.end());
    for (auto& cell : cells) if (cell.col > col) cell.col--;
    if (col < colWidths.size()) colWidths.erase(colWidths.begin() + col);
    cols--;
}

bool Table::mergeCells(uint32_t startRow, uint32_t startCol,
                       uint32_t endRow, uint32_t endCol) {
    if (startRow >= rows || startCol >= cols || endRow >= rows || endCol >= cols) return false;
    if (startRow > endRow || startCol > endCol) return false;

    auto it = cells.begin();
    while (it != cells.end()) {
        if (it->row >= startRow && it->row <= endRow &&
            it->col >= startCol && it->col <= endCol &&
            !(it->row == startRow && it->col == startCol)) {
            it = cells.erase(it);
        } else {
            ++it;
        }
    }

    auto* parent = cellAt(startRow, startCol);
    if (!parent) {
        TableCell newCell;
        newCell.row = startRow;
        newCell.col = startCol;
        newCell.rowSpan = endRow - startRow + 1;
        newCell.colSpan = endCol - startCol + 1;
        cells.push_back(std::move(newCell));
    } else {
        parent->rowSpan = endRow - startRow + 1;
        parent->colSpan = endCol - startCol + 1;
    }
    return true;
}

bool Table::unmergeCell(uint32_t row, uint32_t col) {
    auto* cell = cellAt(row, col);
    if (!cell || (cell->rowSpan == 1 && cell->colSpan == 1)) return false;

    uint32_t rs = cell->rowSpan, cs = cell->colSpan;
    cell->rowSpan = 1;
    cell->colSpan = 1;

    for (uint32_t r = row; r < row + rs; ++r) {
        for (uint32_t c = col; c < col + cs; ++c) {
            if (r == row && c == col) continue;
            if (!cellAt(r, c)) {
                TableCell newCell;
                newCell.row = r;
                newCell.col = c;
                cells.push_back(std::move(newCell));
            }
        }
    }
    return true;
}

double Table::totalWidth() const {
    double w = 0;
    for (double cw : colWidths) w += (cw < 0) ? 100 : cw;
    return w;
}

double Table::totalHeight() const {
    double h = 0;
    for (double rh : rowHeights) h += (rh < 0) ? 20 : rh;
    return h;
}

} // namespace UDoc
