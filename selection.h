#ifndef SELECTION_H
#define SELECTION_H

#include "udoc_types.h"
#include <vector>

namespace UDoc {

struct Selection {
    enum class Type {
        None,
        SingleElement,
        MultiElement,
        TextRange,
        TableCells
    } type = Type::None;

    // Element selection
    std::vector<ID> elementIds;
    ID pageId = NULL_ID;

    // Text selection (within one element)
    struct TextRange {
        ID elementId = NULL_ID;
        uint32_t start = 0;
        uint32_t end   = 0;
        bool isEmpty() const { return start == end; }
    } textRange;

    // Table cell selection
    struct CellRange {
        ID tableElementId = NULL_ID;
        uint32_t startRow = 0, startCol = 0;
        uint32_t endRow   = 0, endCol   = 0;
    } cellRange;

    // Helpers
    bool isEmpty()  const { return type == Type::None; }
    bool hasText()  const { return type == Type::TextRange; }
    bool isSingle() const { return type == Type::SingleElement && elementIds.size() == 1; }
    bool isMulti()  const { return type == Type::MultiElement  || elementIds.size() > 1; }

    ID firstElementId() const {
        return elementIds.empty() ? NULL_ID : elementIds.front();
    }

    void clear() {
        type = Type::None;
        elementIds.clear();
        pageId = NULL_ID;
        textRange = {};
        cellRange = {};
    }

    void setSingle(ID elemId, ID pgId) {
        type = Type::SingleElement;
        elementIds = { elemId };
        pageId = pgId;
    }

    void addElement(ID elemId) {
        if (std::find(elementIds.begin(), elementIds.end(), elemId) == elementIds.end())
            elementIds.push_back(elemId);
        type = elementIds.size() == 1 ? Type::SingleElement : Type::MultiElement;
    }

    void removeElement(ID elemId) {
        elementIds.erase(std::remove(elementIds.begin(), elementIds.end(), elemId),
                         elementIds.end());
        if (elementIds.empty())      type = Type::None;
        else if (elementIds.size() == 1) type = Type::SingleElement;
    }

    bool contains(ID elemId) const {
        return std::find(elementIds.begin(), elementIds.end(), elemId) != elementIds.end();
    }
};

} // namespace UDoc

#endif // SELECTION_H
