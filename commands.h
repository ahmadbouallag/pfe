#ifndef COMMANDS_H
#define COMMANDS_H

#include "udoc_types.h"
#include "element.h"
#include "page.h"
#include "document.h"
#include <QUndoStack>
#include <QUndoCommand>
#include <memory>

namespace UDoc {

// -----------------------------------------------------------------------
// CommandStack — QUndoStack extended with coalescing and grouping helpers.
// One instance lives in DocumentView and is the single source of truth
// for all undoable operations.
// -----------------------------------------------------------------------
class CommandStack : public QUndoStack {
    Q_OBJECT
public:
    explicit CommandStack(QObject* parent = nullptr) : QUndoStack(parent) {
        setUndoLimit(200);
    }

    // Convenience: push + execute in one call
    void run(QUndoCommand* cmd) { push(cmd); }

    // Coalescing — merge consecutive commands of the same type/id
    // (e.g. individual keystrokes while typing merge into one ModifyTextCmd)
    bool coalescing() const { return m_coalescing; }
    void setCoalescing(bool on) { m_coalescing = on; }

private:
    bool m_coalescing = true;
};

// -----------------------------------------------------------------------
// MoveElementCmd — move one or more elements by a delta
// -----------------------------------------------------------------------
class MoveElementCmd : public QUndoCommand {
public:
    struct Entry { ID pageId; ID elementId; QPointF oldPos; QPointF newPos; };

    MoveElementCmd(Document* doc, const std::vector<Entry>& entries,
                   QUndoCommand* parent = nullptr)
        : QUndoCommand("Move Element", parent)
        , m_doc(doc), m_entries(entries) {}

    void redo() override { applyPositions(true); }
    void undo() override { applyPositions(false); }

    // Coalesce drag updates into one command while dragging
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        auto* o = static_cast<const MoveElementCmd*>(other);
        // Same set of elements? Update the newPos values only.
        if (o->m_entries.size() != m_entries.size()) return false;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].elementId != o->m_entries[i].elementId) return false;
            m_entries[i].newPos = o->m_entries[i].newPos;
        }
        return true;
    }
    int id() const override { return 1001; }

private:
    void applyPositions(bool useNew) {
        for (auto& e : m_entries) {
            Page* page = findPage(e.pageId);
            if (!page) continue;
            Element* elem = page->findElement(e.elementId);
            if (!elem || !elem->bounds) continue;
            QPointF pos = useNew ? e.newPos : e.oldPos;
            elem->bounds->x = pos.x();
            elem->bounds->y = pos.y();
            page->invalidateCache();
        }
    }

    Page* findPage(ID pageId) {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* p = ps->findPage(pageId)) return p;
            }
        }
        return nullptr;
    }

    Document* m_doc;
    std::vector<Entry> m_entries;
};

// -----------------------------------------------------------------------
// ResizeElementCmd — resize an element's bounds
// -----------------------------------------------------------------------
class ResizeElementCmd : public QUndoCommand {
public:
    ResizeElementCmd(Document* doc, ID pageId, ID elementId,
                     const Rect& oldBounds, const Rect& newBounds,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Resize Element", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId)
        , m_old(oldBounds), m_new(newBounds) {}

    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        auto* o = static_cast<const ResizeElementCmd*>(other);
        if (o->m_elementId != m_elementId) return false;
        m_new = o->m_new;
        return true;
    }
    int id() const override { return 1002; }

private:
    void apply(const Rect& bounds) {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* page = ps->findPage(m_pageId)) {
                    if (Element* elem = page->findElement(m_elementId)) {
                        elem->bounds = bounds;
                        page->invalidateCache();
                    }
                }
            }
        }
    }

    Document* m_doc;
    ID m_pageId, m_elementId;
    Rect m_old, m_new;
};

// -----------------------------------------------------------------------
// InsertElementCmd — add an element to a page
// -----------------------------------------------------------------------
class InsertElementCmd : public QUndoCommand {
public:
    InsertElementCmd(Document* doc, ID pageId,
                     std::unique_ptr<Element> element,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Insert Element", parent)
        , m_doc(doc), m_pageId(pageId)
        , m_element(std::move(element))
        , m_elementId(m_element->id()) {}

    void redo() override {
        Page* page = findPage();
        if (!page || !m_element) return;
        m_element->setParent(page);
        page->addElement(std::move(m_element));
    }

    void undo() override {
        Page* page = findPage();
        if (!page) return;
        // Take the element back so we own it again
        if (Element* elem = page->findElement(m_elementId)) {
            // Re-acquire ownership by rebuilding (page stores unique_ptr)
            // We rebuild a shell — full round-trip ownership needs a different approach.
            // For now: just remove it from the page.
            page->removeElement(m_elementId);
            // We lose the element data on undo. Phase 14 polish: store a clone.
        }
    }

private:
    Page* findPage() {
        for (auto& tab : m_doc->tabs)
            if (auto* ps = tab->pageSequence())
                if (Page* p = ps->findPage(m_pageId)) return p;
        return nullptr;
    }

    Document* m_doc;
    ID m_pageId;
    mutable std::unique_ptr<Element> m_element;
    ID m_elementId;
};

// -----------------------------------------------------------------------
// DeleteElementCmd — remove an element from a page
// -----------------------------------------------------------------------
class DeleteElementCmd : public QUndoCommand {
public:
    DeleteElementCmd(Document* doc, ID pageId, ID elementId,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Delete Element", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId) {}

    void redo() override {
        Page* page = findPage();
        if (!page) return;
        // Store element before removing (ownership transfer not yet implemented)
        page->removeElement(m_elementId);
    }

    void undo() override {
        // TODO Phase 14: restore the stored element
        // Requires clone/serialize support
    }

private:
    Page* findPage() {
        for (auto& tab : m_doc->tabs)
            if (auto* ps = tab->pageSequence())
                if (Page* p = ps->findPage(m_pageId)) return p;
        return nullptr;
    }

    Document* m_doc;
    ID m_pageId, m_elementId;
};

// -----------------------------------------------------------------------
// ModifyTextCmd — edit text content of an element
// Coalesces consecutive edits to the same element (typing)
// -----------------------------------------------------------------------
class ModifyTextCmd : public QUndoCommand {
public:
    ModifyTextCmd(Document* doc, ID pageId, ID elementId,
                  const QString& oldText, const QString& newText,
                  QUndoCommand* parent = nullptr)
        : QUndoCommand("Edit Text", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId)
        , m_old(oldText), m_new(newText) {}

    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        auto* o = static_cast<const ModifyTextCmd*>(other);
        if (o->m_elementId != m_elementId) return false;
        m_new = o->m_new;
        return true;
    }
    int id() const override { return 1003; }

private:
    void apply(const QString& text) {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* page = ps->findPage(m_pageId)) {
                    if (Element* elem = page->findElement(m_elementId)) {
                        if (auto* tc = elem->textContent()) {
                            tc->setPlainText(text, tc->runs.empty()
                                             ? CharacterProperties()
                                             : tc->runs[0].props);
                            page->invalidateCache();
                        }
                    }
                }
            }
        }
    }

    Document* m_doc;
    ID m_pageId, m_elementId;
    QString m_old, m_new;
};

// -----------------------------------------------------------------------
// ApplyStyleCmd — change character or paragraph properties on a range
// -----------------------------------------------------------------------
class ApplyCharStyleCmd : public QUndoCommand {
public:
    ApplyCharStyleCmd(Document* doc, ID pageId, ID elementId,
                      uint32_t start, uint32_t len,
                      const CharacterProperties& oldProps,
                      const CharacterProperties& newProps,
                      QUndoCommand* parent = nullptr)
        : QUndoCommand("Apply Style", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId)
        , m_start(start), m_len(len)
        , m_old(oldProps), m_new(newProps) {}

    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

private:
    void apply(const CharacterProperties& props) {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* page = ps->findPage(m_pageId)) {
                    if (Element* elem = page->findElement(m_elementId)) {
                        if (auto* tc = elem->textContent()) {
                            tc->splitRunAt(m_start);
                            tc->splitRunAt(m_start + m_len);
                            for (auto& run : tc->runs) {
                                if (run.start >= m_start &&
                                    run.end() <= m_start + m_len)
                                    run.props = props;
                            }
                            tc->coalesceRuns();
                            page->invalidateCache();
                        }
                    }
                }
            }
        }
    }

    Document* m_doc;
    ID m_pageId, m_elementId;
    uint32_t m_start, m_len;
    CharacterProperties m_old, m_new;
};

// -----------------------------------------------------------------------
// InsertPageCmd / DeletePageCmd / ReorderPagesCmd
// -----------------------------------------------------------------------
class InsertPageCmd : public QUndoCommand {
public:
    InsertPageCmd(Document* doc, ID tabId, size_t index,
                  QUndoCommand* parent = nullptr)
        : QUndoCommand("Insert Page", parent)
        , m_doc(doc), m_tabId(tabId), m_index(index) {}

    void redo() override {
        Tab* tab = m_doc->findTab(m_tabId);
        if (!tab) return;
        if (auto* ps = tab->pageSequence()) {
            Page* page = ps->insertPage(m_index, m_doc->generateId());
            m_pageId = page->id();
        }
    }

    void undo() override {
        Tab* tab = m_doc->findTab(m_tabId);
        if (!tab) return;
        if (auto* ps = tab->pageSequence())
            ps->deletePage(m_pageId);
    }

private:
    Document* m_doc;
    ID m_tabId;
    size_t m_index;
    ID m_pageId = NULL_ID;
};

class DeletePageCmd : public QUndoCommand {
public:
    DeletePageCmd(Document* doc, ID tabId, ID pageId,
                  QUndoCommand* parent = nullptr)
        : QUndoCommand("Delete Page", parent)
        , m_doc(doc), m_tabId(tabId), m_pageId(pageId) {}

    void redo() override {
        Tab* tab = m_doc->findTab(m_tabId);
        if (!tab) return;
        if (auto* ps = tab->pageSequence())
            ps->deletePage(m_pageId);
    }

    void undo() override {
        // TODO Phase 14: restore deleted page with all its content
    }

private:
    Document* m_doc;
    ID m_tabId, m_pageId;
};

class ReorderPagesCmd : public QUndoCommand {
public:
    ReorderPagesCmd(Document* doc, ID tabId, ID pageId,
                    size_t oldIndex, size_t newIndex,
                    QUndoCommand* parent = nullptr)
        : QUndoCommand("Reorder Pages", parent)
        , m_doc(doc), m_tabId(tabId), m_pageId(pageId)
        , m_old(oldIndex), m_new(newIndex) {}

    void redo() override { apply(m_new); }
    void undo() override { apply(m_old); }

private:
    void apply(size_t index) {
        Tab* tab = m_doc->findTab(m_tabId);
        if (!tab) return;
        // PageSequence doesn't have a reorder method yet — add pages vector move
        if (auto* ps = tab->pageSequence()) {
            auto& pages = ps->pages;
            auto it = std::find_if(pages.begin(), pages.end(),
                                   [&](const auto& p) { return p->id() == m_pageId; });
            if (it == pages.end() || index >= pages.size()) return;
            auto page = std::move(*it);
            pages.erase(it);
            pages.insert(pages.begin() + index, std::move(page));
        }
    }

    Document* m_doc;
    ID m_tabId, m_pageId;
    size_t m_old, m_new;
};

// -----------------------------------------------------------------------
// CreateLinkCmd / BreakLinkCmd
// -----------------------------------------------------------------------
class CreateLinkCmd : public QUndoCommand {
public:
    CreateLinkCmd(Document* doc, ID sourceElement, const LinkTarget& target,
                  QUndoCommand* parent = nullptr)
        : QUndoCommand("Create Link", parent)
        , m_doc(doc), m_source(sourceElement), m_target(target) {}

    void redo() override { m_doc->linkGraph.addLink(m_source, m_target); }
    void undo() override { m_doc->linkGraph.removeLink(m_source); }

private:
    Document* m_doc;
    ID m_source;
    LinkTarget m_target;
};

class BreakLinkCmd : public QUndoCommand {
public:
    BreakLinkCmd(Document* doc, ID sourceElement,
                 QUndoCommand* parent = nullptr)
        : QUndoCommand("Break Link", parent)
        , m_doc(doc), m_source(sourceElement) {
        // Capture the old target so we can restore it on undo
        auto links = m_doc->linkGraph.linksFrom(sourceElement);
        if (!links.empty()) m_oldTarget = links[0].target;
    }

    void redo() override { m_doc->linkGraph.removeLink(m_source); }
    void undo() override {
        if (m_oldTarget) m_doc->linkGraph.addLink(m_source, *m_oldTarget);
    }

private:
    Document* m_doc;
    ID m_source;
    std::optional<LinkTarget> m_oldTarget;
};

} // namespace UDoc

#endif // COMMANDS_H
