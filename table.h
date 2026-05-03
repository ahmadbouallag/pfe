#ifndef TABLE_H
#define TABLE_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "properties.h"
#include <vector>
#include <memory>

namespace UDoc {

// Forward declaration — we only store pointers, not own
class Element;

struct TableCell {
    uint32_t row = 0, col = 0;
    uint32_t rowSpan = 1, colSpan = 1;

    // Store raw pointers — elements owned by page/canvas, not cell
    std::vector<Element*> content;

    CellProperties props;
    mutable Rect computedBounds;

    bool isMerged() const { return rowSpan > 1 || colSpan > 1; }
    uint64_t hash() const { return (static_cast<uint64_t>(row) << 32) | col; }
};

class Table : public UDocObject {
public:
    uint32_t rows = 0, cols = 0;
    std::vector<double> rowHeights;
    std::vector<double> colWidths;
    std::vector<TableCell> cells;
    TableProperties props;

    explicit Table(ID id) : UDocObject(id) {}
    ObjectType objectType() const override { return ObjectType::Block; }

    TableCell* cellAt(uint32_t row, uint32_t col);
    const TableCell* cellAt(uint32_t row, uint32_t col) const;
    void insertRow(uint32_t beforeRow);
    void deleteRow(uint32_t row);
    void insertColumn(uint32_t beforeCol);
    void deleteColumn(uint32_t col);
    bool mergeCells(uint32_t sr, uint32_t sc, uint32_t er, uint32_t ec);
    bool unmergeCell(uint32_t row, uint32_t col);
    double totalWidth() const;
    double totalHeight() const;
};

} // namespace UDoc

#endif // TABLE_H
