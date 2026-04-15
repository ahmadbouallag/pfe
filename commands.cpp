#include "commands.h"

namespace UDoc {

// === MOVE ELEMENT ===
MoveElementCommand::MoveElementCommand(Document* doc, ID elemId, double newX, double newY)
    : UDocCommand(doc, "Move Element"), m_elementId(elemId), m_newX(newX), m_newY(newY) {

    // Find element and store old position
    for (const auto& page : doc->pages) {
        if (Element* elem = page->findElement(elemId)) {
            m_oldX = elem->bounds.x;
            m_oldY = elem->bounds.y;
            break;
        }
    }
}

void MoveElementCommand::execute() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            elem->bounds.x = m_newX;
            elem->bounds.y = m_newY;
            elem->setModified(true);
            page->invalidateCache();
            break;
        }
    }
}

void MoveElementCommand::revert() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            elem->bounds.x = m_oldX;
            elem->bounds.y = m_oldY;
            elem->setModified(true);
            page->invalidateCache();
            break;
        }
    }
}

bool MoveElementCommand::canMergeWith(const UDocCommand* other) const {
    const MoveElementCommand* otherMove = dynamic_cast<const MoveElementCommand*>(other);
    return otherMove && otherMove->m_elementId == m_elementId;
}

void MoveElementCommand::mergeWith(const UDocCommand* other) {
    const MoveElementCommand* otherMove = static_cast<const MoveElementCommand*>(other);
    m_newX = otherMove->m_newX;
    m_newY = otherMove->m_newY;
    setText("Move Element (continued)");
}

// === RESIZE ELEMENT ===
ResizeElementCommand::ResizeElementCommand(Document* doc, ID elemId, const Rect& newBounds)
    : UDocCommand(doc, "Resize Element"), m_elementId(elemId), m_newBounds(newBounds) {

    for (const auto& page : doc->pages) {
        if (Element* elem = page->findElement(elemId)) {
            m_oldBounds = elem->bounds;
            break;
        }
    }
}

void ResizeElementCommand::execute() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            elem->bounds = m_newBounds;
            elem->setModified(true);
            page->invalidateCache();
            break;
        }
    }
}

void ResizeElementCommand::revert() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            elem->bounds = m_oldBounds;
            elem->setModified(true);
            page->invalidateCache();
            break;
        }
    }
}

// === INSERT ELEMENT ===
InsertElementCommand::InsertElementCommand(Document* doc, PageIndex pageIdx,
                                           std::unique_ptr<Element> elem, size_t pos)
    : UDocCommand(doc, "Insert Element"), m_pageIndex(pageIdx),
    m_element(std::move(elem)), m_position(pos) {}

void InsertElementCommand::execute() {
    Page* page = m_doc->pageAt(m_pageIndex);
    if (!page) return;

    m_element->setParent(page);
    if (m_position >= page->elements.size()) {
        page->elements.push_back(std::move(m_element));
    } else {
        page->elements.insert(page->elements.begin() + m_position, std::move(m_element));
    }
    page->invalidateCache();
    page->rebuildSpatialIndex();
    m_doc->markModified();
}

void InsertElementCommand::revert() {
    Page* page = m_doc->pageAt(m_pageIndex);
    if (!page) return;

    auto it = std::find_if(page->elements.begin(), page->elements.end(),
                           [this](const auto& e) { return e->id() == m_element->id(); });
    if (it != page->elements.end()) {
        m_element = std::move(*it);
        page->elements.erase(it);
        page->invalidateCache();
        page->rebuildSpatialIndex();
    }
}

// === DELETE ELEMENT ===
DeleteElementCommand::DeleteElementCommand(Document* doc, PageIndex pageIdx, ID elemId)
    : UDocCommand(doc, "Delete Element"), m_pageIndex(pageIdx) {

    Page* page = doc->pageAt(pageIdx);
    if (page) {
        auto it = std::find_if(page->elements.begin(), page->elements.end(),
                               [elemId](const auto& e) { return e->id() == elemId; });
        if (it != page->elements.end()) {
            m_oldPosition = std::distance(page->elements.begin(), it);
        }
    }
}

void DeleteElementCommand::execute() {
    Page* page = m_doc->pageAt(m_pageIndex);
    if (!page) return;

    for (size_t i = 0; i < page->elements.size(); ++i) {
        if (page->elements[i]->id() == m_element->id()) {
            m_element = std::move(page->elements[i]);
            page->elements.erase(page->elements.begin() + i);
            page->invalidateCache();
            page->rebuildSpatialIndex();
            m_doc->markModified();
            break;
        }
    }
}

void DeleteElementCommand::revert() {
    if (!m_element) return;
    Page* page = m_doc->pageAt(m_pageIndex);
    if (!page) return;

    m_element->setParent(page);
    if (m_oldPosition >= page->elements.size()) {
        page->elements.push_back(std::move(m_element));
    } else {
        page->elements.insert(page->elements.begin() + m_oldPosition, std::move(m_element));
    }
    page->invalidateCache();
    page->rebuildSpatialIndex();
    m_doc->markModified();
}

// === MODIFY TEXT ===
ModifyTextCommand::ModifyTextCommand(Document* doc, ID elemId, const QString& newText)
    : UDocCommand(doc, "Edit Text"), m_elementId(elemId), m_newText(newText) {

    for (const auto& page : doc->pages) {
        if (Element* elem = page->findElement(elemId)) {
            if (auto* text = elem->text()) {
                m_oldText = text->text;
            }
            break;
        }
    }
}

void ModifyTextCommand::execute() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            if (auto* text = elem->text()) {
                text->text = m_newText;
                text->cache.valid = false;  // Invalidate layout
                elem->setModified(true);
                page->invalidateCache();
            }
            break;
        }
    }
}

void ModifyTextCommand::revert() {
    for (const auto& page : m_doc->pages) {
        if (Element* elem = page->findElement(m_elementId)) {
            if (auto* text = elem->text()) {
                text->text = m_oldText;
                text->cache.valid = false;
                elem->setModified(true);
                page->invalidateCache();
            }
            break;
        }
    }
}

bool ModifyTextCommand::canMergeWith(const UDocCommand* other) const {
    const ModifyTextCommand* otherMod = dynamic_cast<const ModifyTextCommand*>(other);
    return otherMod && otherMod->m_elementId == m_elementId;
}

void ModifyTextCommand::mergeWith(const UDocCommand* other) {
    const ModifyTextCommand* otherMod = static_cast<const ModifyTextCommand*>(other);
    m_newText = otherMod->m_newText;
    setText("Edit Text (continued)");
}

} // namespace UDoc
