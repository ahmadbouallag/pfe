#ifndef COMMANDS_H
#define COMMANDS_H

#include "udoc_types.h"
#include "element.h"
#include "page.h"
#include "document.h"
#include "inline.h"
#include <QUndoStack>
#include <QUndoCommand>
#include <memory>

namespace UDoc {

// -----------------------------------------------------------------------
// CommandStack
// -----------------------------------------------------------------------
class CommandStack : public QUndoStack {
    Q_OBJECT
public:
    explicit CommandStack(QObject* parent = nullptr) : QUndoStack(parent) {
        setUndoLimit(200);
    }
    void run(QUndoCommand* cmd) { push(cmd); }
    bool coalescing() const { return m_coalescing; }
    void setCoalescing(bool on) { m_coalescing = on; }
private:
    bool m_coalescing = true;
};

// -----------------------------------------------------------------------
// MoveElementCmd
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

    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        auto* o = static_cast<const MoveElementCmd*>(other);
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
        for (auto& tab : m_doc->tabs)
            if (auto* ps = tab->pageSequence())
                if (Page* p = ps->findPage(pageId)) return p;
        return nullptr;
    }
    Document* m_doc;
    std::vector<Entry> m_entries;
};

// -----------------------------------------------------------------------
// ResizeElementCmd
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
        for (auto& tab : m_doc->tabs)
            if (auto* ps = tab->pageSequence())
                if (Page* page = ps->findPage(m_pageId))
                    if (Element* elem = page->findElement(m_elementId)) {
                        elem->bounds = bounds;
                        page->invalidateCache();
                    }
    }
    Document* m_doc;
    ID m_pageId, m_elementId;
    Rect m_old, m_new;
};

// -----------------------------------------------------------------------
// InsertElementCmd
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
        page->removeElement(m_elementId);
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
// DeleteElementCmd
// -----------------------------------------------------------------------
class DeleteElementCmd : public QUndoCommand {
public:
    DeleteElementCmd(Document* doc, ID pageId, ID elementId,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Delete Element", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId) {}

    void redo() override {
        Page* page = findPage();
        if (page) page->removeElement(m_elementId);
    }
    void undo() override {
        // TODO Phase 14: restore the stored element
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
// ModifyTextCmd — legacy simple string-only undo (kept for compatibility).
// For new text editing use EditTextBlockCmd below.
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
                            // Preserve the first run's props rather than wiping them
                            CharacterProperties props = tc->runs.empty()
                                ? CharacterProperties() : tc->runs[0].props;
                            tc->setPlainText(text, props);
                            tc->layoutCache.invalidate();
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
// EditTextBlockCmd — full snapshot undo for the text editor.
//
// Stores a complete copy of the TextBlockContent (text string + all runs)
// before and after an editing session.  This preserves ALL inline
// formatting across undo/redo — bold words, size changes, color spans etc.
//
// The TextEditor calls this once at commit() time, not on every keystroke.
// -----------------------------------------------------------------------

// Helper: shallow-copy all InlineRuns (no embedded objects are deep-copied —
// those are shared_ptr so they're ref-counted safely).
static inline std::vector<InlineRun> cloneRuns(const std::vector<InlineRun>& src) {
    return src; // InlineRun is value-copyable; embedded uses shared_ptr
}

class EditTextBlockCmd : public QUndoCommand {
public:
    // Snapshot the element state RIGHT NOW and store as "before".
    // The caller then mutates the element, and when done calls setAfter().
    EditTextBlockCmd(Document* doc, ID pageId, ID elementId,
                     QUndoCommand* parent = nullptr)
        : QUndoCommand("Edit Text", parent)
        , m_doc(doc), m_pageId(pageId), m_elementId(elementId)
    {
        // Capture before-state immediately
        if (auto* tc = getContent()) {
            m_oldText = tc->text;
            m_oldRuns = cloneRuns(tc->runs);
        }
    }

    // Call this after editing is done to capture the after-state.
    void setAfter() {
        if (auto* tc = getContent()) {
            m_newText = tc->text;
            m_newRuns = cloneRuns(tc->runs);
        }
        m_afterSet = true;
    }

    void redo() override {
        if (!m_afterSet) return;
        applyState(m_newText, m_newRuns);
    }
    void undo() override {
        applyState(m_oldText, m_oldRuns);
    }

    // Coalesce: two consecutive edits to the same element merge into one
    // (only the final "after" state of the second is kept).
    bool mergeWith(const QUndoCommand* other) override {
        if (other->id() != id()) return false;
        auto* o = static_cast<const EditTextBlockCmd*>(other);
        if (o->m_elementId != m_elementId) return false;
        // Keep our "before" (oldest), take their "after" (newest)
        m_newText = o->m_newText;
        m_newRuns = o->m_newRuns;
        m_afterSet = o->m_afterSet;
        return true;
    }
    int id() const override { return 1005; }

private:
    TextBlockContent* getContent() const {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* page = ps->findPage(m_pageId)) {
                    if (Element* elem = page->findElement(m_elementId)) {
                        if (auto* tc = elem->textContent())    return tc;
                        if (auto* hc = elem->headingContent()) return hc;
                    }
                }
            }
        }
        return nullptr;
    }

    void applyState(const QString& text, const std::vector<InlineRun>& runs) {
        for (auto& tab : m_doc->tabs) {
            if (auto* ps = tab->pageSequence()) {
                if (Page* page = ps->findPage(m_pageId)) {
                    if (Element* elem = page->findElement(m_elementId)) {
                        TextBlockContent* tc = nullptr;
                        if (elem->isTextBlock())    tc = elem->textContent();
                        else if (elem->isHeading()) tc = elem->headingContent();
                        if (!tc) continue;
                        tc->text = text;
                        tc->runs = cloneRuns(runs);
                        tc->layoutCache.invalidate();
                        page->invalidateCache();
                    }
                }
            }
        }
    }

    Document* m_doc;
    ID m_pageId, m_elementId;

    QString              m_oldText, m_newText;
    std::vector<InlineRun> m_oldRuns, m_newRuns;
    bool m_afterSet = false;
};

// -----------------------------------------------------------------------
// ApplyCharStyleCmd — change character properties on a text range
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
                        TextBlockContent* tc = nullptr;
                        if (elem->isTextBlock())    tc = elem->textContent();
                        else if (elem->isHeading()) tc = elem->headingContent();
                        if (!tc) continue;
                        tc->splitRunAt(m_start);
                        tc->splitRunAt(m_start + m_len);
                        for (auto& run : tc->runs) {
                            if (run.start >= m_start &&
                                run.end() <= m_start + m_len)
                                run.props = props;
                        }
                        tc->coalesceRuns();
                        tc->layoutCache.invalidate();
                        page->invalidateCache();
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
        // TODO Phase 14
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
        if (auto* ps = tab->pageSequence()) {
            auto& pages = ps->pages;
            auto it = std::find_if(pages.begin(), pages.end(),
                                   [&](const auto& p){ return p->id() == m_pageId; });
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
