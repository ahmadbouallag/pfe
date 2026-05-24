#include "input_handler.h"
#include "commands.h"
#include "document_view.h"
#include "document.h"
#include "page.h"
#include "element.h"
#include "tab.h"
#include "render_engine.h"
#include "block.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QTextLayout>
#include <QFontMetricsF>
#include <QMenu>
#include <QFileDialog>
#include <QFile>
#include "image.h"
#include "vector.h"
#include <algorithm>

namespace UDoc {

// ============================================================
//  TextEditor
// ============================================================

TextEditor::TextEditor(DocumentView* view, Element* element, QObject* parent)
    : QObject(parent)
    , m_view(view)
    , m_element(element)
{
    // Snapshot original text for undo
    if (auto* tc = textContent())
        m_originalText = tc->text;

    // Create the undo command NOW so it captures the before-state
    // while the text is still unmodified.
    ID pageId = m_view->selection().pageId;
    m_undoCmd = new EditTextBlockCmd(m_view->document(), pageId, element->id());

    // Place cursor at end
    m_cursorPos = m_selAnchor = textLen();

    // Start blink timer — 500ms standard blink rate
    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, &TextEditor::blinkCursor);
    m_blinkTimer->start(530);

    // Mark element as editing and show initial cursor
    m_element->editing = true;
    if (auto* tc = textContent()) {
        tc->layoutCache.cursorPos     = m_cursorPos;
        tc->layoutCache.cursorVisible = true;
        tc->layoutCache.selStart      = m_selAnchor;
        tc->layoutCache.selEnd        = m_cursorPos;
        tc->layoutCache.hasSelection  = false;
    }
}

TextEditor::~TextEditor() {
    if (m_blinkTimer) m_blinkTimer->stop();
    if (m_element) {
        m_element->editing = false;
        if (auto* tc = textContent())
            tc->layoutCache.resetEditing();
    }
    // If commit() was never called (e.g. Escape without changes),
    // discard the pending undo command.
    delete m_undoCmd;
    m_undoCmd = nullptr;
}

// ---- Cursor blink ----
void TextEditor::blinkCursor() {
    if (auto* tc = textContent()) {
        tc->layoutCache.cursorVisible = !tc->layoutCache.cursorVisible;
        if (auto* tc = textContent()) tc->layoutCache.invalidate();
        m_view->renderEngine().invalidateAll();
        m_view->update();
    }
}

// ---- Helpers ----
TextBlockContent* TextEditor::textContent() const {
    if (!m_element) return nullptr;
    if (m_element->isTextBlock())    return m_element->textContent();
    if (m_element->isHeading())      return m_element->headingContent();
    return nullptr;
}

QString& TextEditor::text() {
    static QString dummy;
    auto* tc = textContent();
    return tc ? tc->text : dummy;
}

const QString& TextEditor::textConst() const {
    static const QString dummy;
    auto* tc = textContent();
    return tc ? tc->text : dummy;
}

uint32_t TextEditor::textLen() const {
    auto* tc = textContent();
    return tc ? (uint32_t)tc->text.length() : 0;
}

void TextEditor::syncToCursor() {
    auto* tc = textContent();
    if (!tc) return;
    tc->layoutCache.cursorPos    = m_cursorPos;
    tc->layoutCache.cursorVisible = true;
    tc->layoutCache.selStart     = selStart();
    tc->layoutCache.selEnd       = selEnd();
    tc->layoutCache.hasSelection = (m_cursorPos != m_selAnchor);
    if (auto* tc2 = textContent()) tc2->layoutCache.invalidate();
    m_view->renderEngine().invalidateAll();
    m_view->update();
}

// ---- Cursor movement ----

void TextEditor::setCursor(uint32_t pos, bool extendSelection) {
    pos = std::min(pos, textLen());
    if (!extendSelection) m_selAnchor = pos;
    m_cursorPos = pos;
    // Reset blink so cursor is always visible right after moving
    if (auto* tc = textContent()) tc->layoutCache.cursorVisible = true;
    syncToCursor();
}

void TextEditor::moveCursorLeft(bool ext) {
    if (!ext && m_cursorPos != m_selAnchor) {
        setCursor(selStart(), false); return;
    }
    if (m_cursorPos > 0) setCursor(m_cursorPos - 1, ext);
}

void TextEditor::moveCursorRight(bool ext) {
    if (!ext && m_cursorPos != m_selAnchor) {
        setCursor(selEnd(), false); return;
    }
    if (m_cursorPos < textLen()) setCursor(m_cursorPos + 1, ext);
}

void TextEditor::moveCursorHome(bool ext)    { setCursor(0,       ext); }
void TextEditor::moveCursorEnd(bool ext)     { setCursor(textLen(), ext); }
void TextEditor::moveCursorDocHome(bool ext) { setCursor(0,       ext); }
void TextEditor::moveCursorDocEnd(bool ext)  { setCursor(textLen(), ext); }

void TextEditor::moveCursorWordLeft(bool ext) {
    uint32_t p = m_cursorPos;
    const QString& t = text();
    while (p > 0 && t[p-1].isSpace()) --p;
    while (p > 0 && !t[p-1].isSpace()) --p;
    setCursor(p, ext);
}

void TextEditor::moveCursorWordRight(bool ext) {
    uint32_t p = m_cursorPos;
    const QString& t = text();
    uint32_t len = textLen();
    while (p < len && t[p].isSpace()) ++p;
    while (p < len && !t[p].isSpace()) ++p;
    setCursor(p, ext);
}

void TextEditor::moveCursorUp(bool ext) {
    // Use QTextLayout to find the line above
    auto* tc = textContent();
    if (!tc || !m_element->bounds) return;
    auto layout = buildLayout();
    if (!layout) return;

    int idx = (int)m_cursorPos;
    for (int li = 0; li < layout->lineCount(); ++li) {
        QTextLine line = layout->lineAt(li);
        if (idx >= line.textStart() && idx <= line.textStart() + line.textLength()) {
            if (li == 0) { setCursor(0, ext); return; }
            QTextLine prev = layout->lineAt(li - 1);
            double x = line.cursorToX(idx);
            int newIdx = prev.xToCursor(x);
            setCursor((uint32_t)std::max(newIdx, 0), ext);
            return;
        }
    }
}

void TextEditor::moveCursorDown(bool ext) {
    auto* tc = textContent();
    if (!tc || !m_element->bounds) return;
    auto layout = buildLayout();
    if (!layout) return;

    int idx = (int)m_cursorPos;
    for (int li = 0; li < layout->lineCount(); ++li) {
        QTextLine line = layout->lineAt(li);
        if (idx >= line.textStart() && idx <= line.textStart() + line.textLength()) {
            if (li == layout->lineCount() - 1) { setCursor(textLen(), ext); return; }
            QTextLine next = layout->lineAt(li + 1);
            double x = line.cursorToX(idx);
            int newIdx = next.xToCursor(x);
            setCursor((uint32_t)std::max(newIdx, 0), ext);
            return;
        }
    }
}

// ---- Selection ----

bool TextEditor::hasSelection() const { return m_cursorPos != m_selAnchor; }

QString TextEditor::selectedText() const {
    auto* tc = textContent();
    if (!tc || !hasSelection()) return {};
    return tc->text.mid((int)selStart(), (int)(selEnd() - selStart()));
}

void TextEditor::clearSelection() {
    m_selAnchor = m_cursorPos;
    syncToCursor();
}

void TextEditor::selectAll() {
    m_selAnchor = 0;
    m_cursorPos = textLen();
    syncToCursor();
}

void TextEditor::selectWord() {
    m_selAnchor = wordStart(m_cursorPos);
    m_cursorPos = wordEnd(m_cursorPos);
    syncToCursor();
}

uint32_t TextEditor::wordStart(uint32_t pos) const {
    const QString& t = textConst();
    while (pos > 0 && !t[pos-1].isSpace()) --pos;
    return pos;
}

uint32_t TextEditor::wordEnd(uint32_t pos) const {
    const QString& t = textConst();
    uint32_t len = textLen();
    while (pos < len && !t[pos].isSpace()) ++pos;
    return pos;
}

// ---- Text mutation ----

void TextEditor::deleteSelection() {
    if (!hasSelection()) return;
    auto* tc = textContent();
    if (!tc) return;
    uint32_t s = selStart(), e = selEnd();
    tc->text.remove((int)s, (int)(e - s));
    // Adjust all runs
    auto& runs = tc->runs;
    for (auto it = runs.begin(); it != runs.end(); ) {
        if (it->start >= e) {
            it->start -= (e - s); ++it;
        } else if (it->end() <= s) {
            ++it;
        } else if (it->start >= s && it->end() <= e) {
            it = runs.erase(it);
        } else {
            if (it->start < s && it->end() > e)
                it->length -= (e - s);
            else if (it->start < s)
                it->length = s - it->start;
            else { // start >= s, end > e
                it->length = it->end() - e;
                it->start  = s;
            }
            ++it;
        }
    }
    tc->coalesceRuns();
    m_cursorPos = m_selAnchor = s;
    m_modified = true;
    tc->layoutCache.invalidate();
    syncToCursor();
}

void TextEditor::insertText(const QString& newText) {
    if (newText.isEmpty()) return;
    auto* tc = textContent();
    if (!tc) return;

    deleteSelection(); // delete any selected text first

    uint32_t pos = m_cursorPos;
    tc->text.insert((int)pos, newText);

    // Find the run at pos and expand it, or create one
    CharacterProperties props;
    if (!tc->runs.empty()) {
        // Use props from the run at cursor, or the last run
        for (const auto& r : tc->runs)
            if (r.contains(pos) || r.start == pos) { props = r.props; break; }
        if (tc->runs.back().end() <= pos) props = tc->runs.back().props;
    }

    // Shift all runs that start at or after pos
    for (auto& r : tc->runs) {
        if (r.start >= pos) r.start += newText.length();
        else if (r.end() > pos) r.length += newText.length();
    }

    if (tc->runs.empty())
        tc->runs.push_back(InlineRun(0, tc->text.length(), props));

    tc->coalesceRuns();
    m_cursorPos = m_selAnchor = pos + newText.length();
    m_modified = true;
    tc->layoutCache.invalidate();
    syncToCursor();
}

void TextEditor::deleteCharBackward() {
    if (hasSelection()) { deleteSelection(); return; }
    if (m_cursorPos == 0) return;
    auto* tc = textContent();
    if (!tc) return;
    uint32_t pos = m_cursorPos - 1;
    tc->text.remove((int)pos, 1);
    for (auto it = tc->runs.begin(); it != tc->runs.end(); ) {
        if (it->start > pos)      { --it->start; ++it; }
        else if (it->end() <= pos){ ++it; }
        else if (it->length == 1) { it = tc->runs.erase(it); }
        else                      { --it->length; ++it; }
    }
    tc->coalesceRuns();
    m_cursorPos = m_selAnchor = pos;
    m_modified = true;
    tc->layoutCache.invalidate();
    syncToCursor();
}

void TextEditor::deleteCharForward() {
    if (hasSelection()) { deleteSelection(); return; }
    if (m_cursorPos >= textLen()) return;
    auto* tc = textContent();
    if (!tc) return;
    uint32_t pos = m_cursorPos;
    tc->text.remove((int)pos, 1);
    for (auto it = tc->runs.begin(); it != tc->runs.end(); ) {
        if (it->start > pos)       { --it->start; ++it; }
        else if (it->end() <= pos) { ++it; }
        else if (it->length == 1)  { it = tc->runs.erase(it); }
        else                       { --it->length; ++it; }
    }
    tc->coalesceRuns();
    m_modified = true;
    tc->layoutCache.invalidate();
    syncToCursor();
}

void TextEditor::deleteWordBackward() {
    uint32_t newPos = wordStart(m_cursorPos);
    m_selAnchor = m_cursorPos;
    m_cursorPos = newPos;
    deleteSelection();
}

void TextEditor::deleteWordForward() {
    uint32_t newPos = wordEnd(m_cursorPos);
    m_selAnchor = newPos;
    deleteSelection();
}

// ---- Clipboard ----

void TextEditor::cut() {
    if (!hasSelection()) return;
    copy();
    deleteSelection();
}

void TextEditor::copy() {
    QString sel = selectedText();
    if (!sel.isEmpty())
        QApplication::clipboard()->setText(sel);
}

void TextEditor::paste() {
    QString text = QApplication::clipboard()->text();
    if (!text.isEmpty()) insertText(text);
}

// ---- Mouse ----

std::unique_ptr<QTextLayout> TextEditor::buildLayout() const {
    auto* tc = textContent();
    if (!tc || !m_element->bounds) return nullptr;

    QFont base;
    if (!tc->runs.empty()) {
        const auto& p = tc->runs[0].props;
        if (!p.fontFamily.isEmpty()) base.setFamily(p.fontFamily);
        if (p.fontSize > 0) base.setPointSizeF(p.fontSize);
        base.setBold(p.bold); base.setItalic(p.italic);
    }

    auto layout = std::make_unique<QTextLayout>();
    layout->setText(tc->text);
    layout->setFont(base);

    QVector<QTextLayout::FormatRange> formats;
    for (const auto& run : tc->runs) {
        QTextLayout::FormatRange fr;
        fr.start  = (int)run.start;
        fr.length = (int)run.length;
        QTextCharFormat fmt;
        if (!run.props.fontFamily.isEmpty()) fmt.setFontFamilies({run.props.fontFamily});
        if (run.props.fontSize > 0)          fmt.setFontPointSize(run.props.fontSize);
        fmt.setFontWeight(run.props.bold ? QFont::Bold : QFont::Normal);
        fmt.setFontItalic(run.props.italic);
        fr.format = fmt;
        formats.append(fr);
    }
    layout->setFormats(formats);

    QTextOption opt; opt.setWrapMode(QTextOption::WordWrap);
    layout->setTextOption(opt);
    layout->beginLayout();
    QPointF lp(0,0);
    double maxW = m_element->bounds->width;
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(maxW > 0 ? maxW : 9999.0);
        line.setPosition(lp);
        lp.setY(lp.y() + line.height());
    }
    layout->endLayout();
    return layout;
}

uint32_t TextEditor::pointToCharIndex(const QPointF& localPoint) const {
    auto layout = buildLayout();
    if (!layout) return 0;

    // Find which line the point falls on
    for (int li = 0; li < layout->lineCount(); ++li) {
        QTextLine line = layout->lineAt(li);
        double lineTop = line.y();
        double lineBtm = lineTop + line.height();
        if (localPoint.y() >= lineTop && localPoint.y() < lineBtm) {
            return (uint32_t)std::max(0, line.xToCursor(localPoint.x()));
        }
    }
    // Below all lines → end of text
    return textLen();
}

void TextEditor::placeAtPoint(const QPointF& localPoint) {
    uint32_t idx = pointToCharIndex(localPoint);
    setCursor(idx, false);
}

void TextEditor::extendSelectionToPoint(const QPointF& localPoint) {
    uint32_t idx = pointToCharIndex(localPoint);
    // Move cursor but keep anchor fixed
    m_cursorPos = idx;
    if (auto* tc = textContent()) tc->layoutCache.cursorVisible = true;
    syncToCursor();
}

void TextEditor::selectWordAtPoint(const QPointF& localPoint) {
    uint32_t idx = pointToCharIndex(localPoint);
    m_selAnchor = wordStart(idx);
    m_cursorPos = wordEnd(idx);
    syncToCursor();
}

// ---- Commit ----

void TextEditor::commit() {
    if (!m_modified) return;
    auto* tc = textContent();
    if (!tc) return;

    ID pageId = m_view->selection().pageId;
    ID elemId = m_element->id();

    // m_undoCmd was created at TextEditor construction time,
    // so it already holds the before-state snapshot.
    // Now capture the after-state and push it.
    if (m_undoCmd) {
        m_undoCmd->setAfter();
        m_view->commandStack()->run(m_undoCmd);
        m_undoCmd = nullptr; // stack owns it now
    }

    m_modified = false;
    m_originalText = tc->text;
}

// ============================================================
//  PageSequenceInputHandler
// ============================================================

PageSequenceInputHandler::PageSequenceInputHandler(DocumentView* view)
    : InputHandler(view)
{}

PageSequenceInputHandler::~PageSequenceInputHandler() {
    exitTextEdit();
}

void PageSequenceInputHandler::exitTextEdit() {
    if (m_textEditor) {
        m_textEditor->commit();
        m_textEditor.reset();
        m_textDragging = false;
        m_view->renderEngine().invalidateAll();
        m_view->update();
    }
}

// ---- Hit testing ----

QRectF PageSequenceInputHandler::pageScreenRect(const Page* page) const {
    for (auto& info : m_view->visiblePageRects())
        if (info.page == page) return info.screenRect;
    return {};
}

PageSequenceInputHandler::HitResult
PageSequenceInputHandler::hitTest(const QPointF& screenPos) const {
    Document* doc = m_view->document();
    if (!doc) return {nullptr, nullptr};

    auto pages = m_view->visiblePageRects();
    for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
        if (!it->screenRect.contains(screenPos)) continue;
        double zoom = m_view->viewState().zoom;
        QPointF local = (screenPos - it->screenRect.topLeft()) / zoom;
        Point docLocal(local.x(), local.y());
        Element* hit = it->page->hitTest(docLocal);
        if (hit) return { it->page, hit };
        return { it->page, nullptr };
    }
    return {nullptr, nullptr};
}

Point PageSequenceInputHandler::screenToPageLocal(const QPointF& screenPos,
                                                   const Page* page,
                                                   const QRectF& pageScreenRect) const {
    double zoom = m_view->viewState().zoom;
    QPointF local = (screenPos - pageScreenRect.topLeft()) / zoom;
    return Point(local.x(), local.y());
}

Page* PageSequenceInputHandler::pageForElement(ID elementId) const {
    Document* doc = m_view->document();
    if (!doc) return nullptr;
    Tab* tab = doc->activeTab();
    if (!tab) return nullptr;
    if (auto* ps = tab->pageSequence()) {
        for (auto& page : ps->pages)
            if (page->findElement(elementId)) return page.get();
    }
    return nullptr;
}

// ---- Mouse press ----

void PageSequenceInputHandler::mousePressEvent(QMouseEvent* event) {
    Document* doc = m_view->document();
    if (!doc) return;

    auto [page, element] = hitTest(event->position());

    // ---- Right-click → context menu ----
    if (event->button() == Qt::RightButton) {
        if (m_textEditor && element != m_textEditor->element())
            exitTextEdit();
        showContextMenu(event->position(), page, element);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // ---- If we are in text-edit mode ----
    if (m_textEditor) {
        if (element && element == m_textEditor->element()) {
            // Click inside the same element → place cursor
            QRectF sr = pageScreenRect(page);
            Point local = screenToPageLocal(event->position(), page, sr);
            QPointF localQ(local.x - m_textEditor->element()->bounds->x,
                           local.y - m_textEditor->element()->bounds->y);
            m_textEditor->placeAtPoint(localQ);
            m_textDragging = true;
            event->accept();
            return;
        }
        // Click outside → commit and exit text edit
        exitTextEdit();
    }

    if (element) {
        bool shift = event->modifiers() & Qt::ShiftModifier;
        if (shift) {
            Selection sel = m_view->selection();
            if (sel.contains(element->id())) {
                std::vector<ID> ids = sel.elementIds;
                ids.erase(std::remove(ids.begin(), ids.end(), element->id()), ids.end());
                m_view->selectElements(ids);
            } else {
                std::vector<ID> ids = m_view->selection().elementIds;
                ids.push_back(element->id());
                m_view->selectElements(ids);
            }
        } else {
            m_view->selectElement(element->id());
        }

        // Begin drag if not locked
        if (!element->locked) {
            m_draggingElement  = true;
            m_dragStartScreen  = event->position();
            m_dragStartDocPos  = element->bounds
                ? Point(element->bounds->x, element->bounds->y)
                : Point(0, 0);
        }
    } else if (page) {
        // ---- Click on blank page space ----
        // Word-style: single click on blank area → create a text element
        // there and immediately enter edit mode.
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            m_view->clearSelection();
            QRectF sr = pageScreenRect(page);
            Point local = screenToPageLocal(event->position(), page, sr);
            insertTextAtPagePoint(page, local);
        }
    } else {
        // Click outside any page — just clear selection
        if (!(event->modifiers() & Qt::ShiftModifier))
            m_view->clearSelection();
    }

    event->accept();
}

// ---- Double click → enter text edit ----

void PageSequenceInputHandler::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    auto [page, element] = hitTest(event->position());
    if (!element) return;

    if (element->isEditableText()) {
        enterTextEdit(element, event->position());

        // Double-click also selects the word under the cursor
        if (m_textEditor && page) {
            QRectF sr = pageScreenRect(page);
            Point local = screenToPageLocal(event->position(), page, sr);
            QPointF localQ(local.x - element->bounds->x,
                           local.y - element->bounds->y);
            m_textEditor->selectWordAtPoint(localQ);
        }
        event->accept();
    }
}

void PageSequenceInputHandler::enterTextEdit(Element* element, const QPointF& screenPos) {
    // Exit any existing text edit first
    exitTextEdit();

    if (!element || !element->isEditableText()) return;

    // Select the element
    m_view->selectElement(element->id());

    // Create the text editor
    m_textEditor = std::make_unique<TextEditor>(m_view, element);
    m_view->setMode(EditMode::TextEdit);

    // Place cursor at click position
    Page* page = pageForElement(element->id());
    if (page) {
        QRectF sr = pageScreenRect(page);
        Point local = screenToPageLocal(screenPos, page, sr);
        QPointF localQ(local.x - element->bounds->x,
                       local.y - element->bounds->y);
        m_textEditor->placeAtPoint(localQ);
    }

    m_view->renderEngine().invalidateAll();
    m_view->update();
}

// ---- Mouse move ----

void PageSequenceInputHandler::mouseMoveEvent(QMouseEvent* event) {
    // Text selection drag
    if (m_textEditor && m_textDragging) {
        Element* elem = m_textEditor->element();
        if (elem && elem->bounds) {
            Page* page = pageForElement(elem->id());
            if (page) {
                QRectF sr = pageScreenRect(page);
                Point local = screenToPageLocal(event->position(), page, sr);
                QPointF localQ(local.x - elem->bounds->x,
                               local.y - elem->bounds->y);
                m_textEditor->extendSelectionToPoint(localQ);
            }
        }
        event->accept();
        return;
    }

    // Rubber-band
    if (m_rubberBanding) {
        m_rubberCurrent = event->position();
        m_view->showRubberBand(QRectF(m_rubberStart, m_rubberCurrent).normalized());
        event->accept();
        return;
    }

    // Element drag
    if (m_draggingElement) {
        const Selection& sel = m_view->selection();
        if (sel.isEmpty()) { m_draggingElement = false; return; }

        Document* doc = m_view->document();
        if (!doc) return;

        QPointF delta = event->position() - m_dragStartScreen;
        double zoom = m_view->viewState().zoom;
        double dx = delta.x() / zoom, dy = delta.y() / zoom;

        Tab* tab = doc->activeTab();
        if (!tab) return;
        if (auto* ps = tab->pageSequence()) {
            for (auto& page : ps->pages) {
                for (ID id : sel.elementIds) {
                    if (Element* elem = page->findElement(id)) {
                        if (elem->bounds) {
                            elem->bounds->x = m_dragStartDocPos.x + dx;
                            elem->bounds->y = m_dragStartDocPos.y + dy;
                            page->invalidateCache();
                        }
                    }
                }
            }
        }
        m_view->renderEngine().invalidateAll();
        m_view->update();
        event->accept();
        return;
    }

    // Hover cursor update
    auto [page, element] = hitTest(event->position());
    if (m_textEditor && element == m_textEditor->element())
        m_view->setCursor(Qt::IBeamCursor);
    else if (element && element->isEditableText())
        m_view->setCursor(Qt::IBeamCursor);
    else if (element && !element->locked)
        m_view->setCursor(Qt::SizeAllCursor);
    else
        m_view->setCursor(Qt::ArrowCursor);
}

// ---- Mouse release ----

void PageSequenceInputHandler::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    // Text drag end
    if (m_textDragging) {
        m_textDragging = false;
        event->accept();
        return;
    }

    // Rubber-band end
    if (m_rubberBanding) {
        m_rubberBanding = false;
        m_view->hideRubberBand();

        QRectF band = QRectF(m_rubberStart, m_rubberCurrent).normalized();
        if (band.width() > 4 && band.height() > 4) {
            Document* doc = m_view->document();
            Tab* tab = doc ? doc->activeTab() : nullptr;
            if (tab) {
                if (auto* ps = tab->pageSequence()) {
                    std::vector<ID> ids;
                    double zoom = m_view->viewState().zoom;
                    for (auto& pageInfo : m_view->visiblePageRects()) {
                        QPointF tl = (band.topLeft()     - pageInfo.screenRect.topLeft()) / zoom;
                        QPointF br = (band.bottomRight() - pageInfo.screenRect.topLeft()) / zoom;
                        Rect docBand(tl.x(), tl.y(), br.x()-tl.x(), br.y()-tl.y());
                        for (Element* e : pageInfo.page->elementsInRect(docBand))
                            ids.push_back(e->id());
                    }
                    if (!ids.empty()) m_view->selectElements(ids);
                }
            }
        }
        event->accept();
        return;
    }

    // Element drag end → push undo command
    if (m_draggingElement) {
        m_draggingElement = false;
        const Selection& sel = m_view->selection();
        Document* doc = m_view->document();
        Tab* tab = doc ? doc->activeTab() : nullptr;

        if (tab && !sel.isEmpty()) {
            QPointF delta = event->position() - m_dragStartScreen;
            double zoom = m_view->viewState().zoom;
            double dx = delta.x() / zoom, dy = delta.y() / zoom;

            std::vector<MoveElementCmd::Entry> entries;
            if (auto* ps = tab->pageSequence()) {
                for (auto& page : ps->pages) {
                    for (ID id : sel.elementIds) {
                        if (Element* elem = page->findElement(id)) {
                            if (elem->bounds) {
                                entries.push_back({
                                    page->id(), id,
                                    QPointF(m_dragStartDocPos.x, m_dragStartDocPos.y),
                                    QPointF(m_dragStartDocPos.x + dx, m_dragStartDocPos.y + dy)
                                });
                            }
                        }
                    }
                }
            }
            if (!entries.empty()) {
                // Restore old positions then push (so redo re-applies correctly)
                if (auto* ps = tab->pageSequence()) {
                    for (auto& e : entries) {
                        if (Page* page = ps->findPage(e.pageId)) {
                            if (Element* elem = page->findElement(e.elementId)) {
                                if (elem->bounds) {
                                    elem->bounds->x = e.oldPos.x();
                                    elem->bounds->y = e.oldPos.y();
                                }
                            }
                        }
                    }
                }
                m_view->commandStack()->run(new MoveElementCmd(doc, entries));
            }
        }
        m_view->renderEngine().invalidateAll();
        m_view->update();
        event->accept();
    }
}

// ---- Key press ----

void PageSequenceInputHandler::keyPressEvent(QKeyEvent* event) {
    // ---- Text editing keys ----
    if (m_textEditor) {
        bool shift = event->modifiers() & Qt::ShiftModifier;
        bool ctrl  = event->modifiers() & Qt::ControlModifier;

        switch (event->key()) {
        case Qt::Key_Escape:
            m_textEditor->commit();
            exitTextEdit();
            m_view->setMode(EditMode::Select);
            event->accept(); return;

        case Qt::Key_Left:
            if (ctrl) m_textEditor->moveCursorWordLeft(shift);
            else      m_textEditor->moveCursorLeft(shift);
            event->accept(); return;

        case Qt::Key_Right:
            if (ctrl) m_textEditor->moveCursorWordRight(shift);
            else      m_textEditor->moveCursorRight(shift);
            event->accept(); return;

        case Qt::Key_Up:
            m_textEditor->moveCursorUp(shift);
            event->accept(); return;

        case Qt::Key_Down:
            m_textEditor->moveCursorDown(shift);
            event->accept(); return;

        case Qt::Key_Home:
            if (ctrl) m_textEditor->moveCursorDocHome(shift);
            else      m_textEditor->moveCursorHome(shift);
            event->accept(); return;

        case Qt::Key_End:
            if (ctrl) m_textEditor->moveCursorDocEnd(shift);
            else      m_textEditor->moveCursorEnd(shift);
            event->accept(); return;

        case Qt::Key_Backspace:
            if (ctrl) m_textEditor->deleteWordBackward();
            else      m_textEditor->deleteCharBackward();
            event->accept(); return;

        case Qt::Key_Delete:
            if (ctrl) m_textEditor->deleteWordForward();
            else      m_textEditor->deleteCharForward();
            event->accept(); return;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_textEditor->insertText("\n");
            event->accept(); return;

        case Qt::Key_Tab:
            m_textEditor->insertText("\t");
            event->accept(); return;

        case Qt::Key_A:
            if (ctrl) { m_textEditor->selectAll(); event->accept(); return; }
            break;
        case Qt::Key_C:
            if (ctrl) { m_textEditor->copy(); event->accept(); return; }
            break;
        case Qt::Key_X:
            if (ctrl) { m_textEditor->cut(); event->accept(); return; }
            break;
        case Qt::Key_V:
            if (ctrl) { m_textEditor->paste(); event->accept(); return; }
            break;
        case Qt::Key_Z:
            if (ctrl && !shift) {
                m_textEditor->commit();
                m_view->undo();
                event->accept(); return;
            }
            if (ctrl && shift) {
                m_textEditor->commit();
                m_view->redo();
                event->accept(); return;
            }
            break;
        default:
            break;
        }

        // Printable characters
        if (!event->text().isEmpty() && !ctrl) {
            QString ch = event->text();
            // Filter control characters except newline/tab (already handled above)
            if (ch[0].isPrint() || ch[0] == '\n' || ch[0] == '\t') {
                m_textEditor->insertText(ch);
                event->accept();
                return;
            }
        }

        event->accept();
        return;
    }

    // ---- Non-text-edit keys ----
    bool shift = event->modifiers() & Qt::ShiftModifier;
    double step = shift ? 10.0 : 1.0;
    double dx = 0, dy = 0;

    switch (event->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default: return;
    }

    const Selection& sel = m_view->selection();
    if (sel.isEmpty()) return;

    Document* doc = m_view->document();
    Tab* tab = doc ? doc->activeTab() : nullptr;
    if (!tab) return;

    if (auto* ps = tab->pageSequence()) {
        for (auto& page : ps->pages) {
            for (ID id : sel.elementIds) {
                if (Element* elem = page->findElement(id)) {
                    if (elem->bounds) {
                        elem->bounds->x += dx;
                        elem->bounds->y += dy;
                        page->invalidateCache();
                    }
                }
            }
        }
    }

    m_view->renderEngine().invalidateAll();
    m_view->update();
    event->accept();
}

// ---- Cursor ----

QCursor PageSequenceInputHandler::cursor() const {
    if (m_textEditor) return Qt::IBeamCursor;
    switch (m_view->mode()) {
    case EditMode::Pan:      return Qt::OpenHandCursor;
    case EditMode::TextEdit: return Qt::IBeamCursor;
    default:                 return Qt::ArrowCursor;
    }
}


// ============================================================
//  Insert text element at page-local position — Word-style click-to-type
// ============================================================

void PageSequenceInputHandler::insertTextAtPagePoint(Page* page, const Point& pos) {
    Document* doc = m_view->document();
    if (!doc || !page) return;

    // Default text element: full content width, auto height.
    // Position it at the click point, snapped to a sensible top-left.
    double pageMargin = 60.0;
    double elemX = std::max(pageMargin, std::min(pos.x, page->width - pageMargin * 2));
    double elemY = std::max(pageMargin, pos.y);
    double elemW = page->width - elemX - pageMargin;
    double elemH = 24.0;  // one line height; will grow as user types

    auto elem = std::make_unique<Element>(doc->generateId());
    elem->bounds = Rect(elemX, elemY, elemW, elemH);

    TextBlockContent tc;
    CharacterProperties props;
    props.fontFamily = "Arial";
    props.fontSize   = 12.0;
    tc.setPlainText("", props);
    elem->content = std::move(tc);

    ID newElemId = elem->id();
    Element* raw = elem.get();
    page->addElement(std::move(elem));

    // Select it and enter text edit immediately
    m_view->selectElement(newElemId);
    m_view->renderEngine().invalidateAll();

    m_textEditor = std::make_unique<TextEditor>(m_view, raw);
    m_view->setMode(EditMode::TextEdit);
    m_view->update();
}

// ============================================================
//  Insert heading
// ============================================================

void PageSequenceInputHandler::insertHeading(Page* page, const Point& pos, int level) {
    Document* doc = m_view->document();
    if (!doc || !page) return;

    double pageMargin = 60.0;
    double fontSize   = (level == 1) ? 24.0 : (level == 2) ? 18.0 : 14.0;
    double elemX = pageMargin;
    double elemY = pos.y;
    double elemW = page->width - pageMargin * 2;
    double elemH = fontSize * 1.6;

    auto elem = std::make_unique<Element>(doc->generateId());
    elem->bounds = Rect(elemX, elemY, elemW, elemH);

    HeadingContent hc;
    CharacterProperties props;
    props.fontFamily = "Arial";
    props.fontSize   = fontSize;
    props.bold       = true;
    hc.setPlainText("", props);
    hc.outlineLevel  = level;
    elem->content = std::move(hc);

    ID headingElemId = elem->id();
    Element* raw = elem.get();
    page->addElement(std::move(elem));

    m_view->selectElement(headingElemId);
    m_view->renderEngine().invalidateAll();

    m_textEditor = std::make_unique<TextEditor>(m_view, raw);
    m_view->setMode(EditMode::TextEdit);
    m_view->update();
}

// ============================================================
//  Insert plain text block (from menu)
// ============================================================

void PageSequenceInputHandler::insertTextBlock(Page* page, const Point& pos) {
    insertTextAtPagePoint(page, pos);
}

// ============================================================
//  Insert horizontal rule
// ============================================================

void PageSequenceInputHandler::insertHRule(Page* page, const Point& pos) {
    Document* doc = m_view->document();
    if (!doc || !page) return;

    double pageMargin = 60.0;
    double x = pageMargin;
    double y = pos.y;
    double w = page->width - pageMargin * 2;
    double h = 2.0;

    // Represent as a thin filled VectorGraphic rectangle
    VectorGraphic vg;
    UDoc::PathCommand m, l1, l2, l3, cl;
    m.type  = PathCommandType::MoveTo;  m.x1  = 0; m.y1  = 0;
    l1.type = PathCommandType::LineTo;  l1.x1 = w; l1.y1 = 0;
    l2.type = PathCommandType::LineTo;  l2.x1 = w; l2.y1 = h;
    l3.type = PathCommandType::LineTo;  l3.x1 = 0; l3.y1 = h;
    cl.type = PathCommandType::ClosePath;
    vg.commands = {m, l1, l2, l3, cl};

    UDoc::Fill fill;
    fill.content = UDoc::Color(0.7, 0.7, 0.7, 1.0);
    fill.opacity = 1.0;
    vg.fill = fill;

    auto elem = std::make_unique<Element>(doc->generateId());
    elem->bounds  = Rect(x, y, w, h);
    elem->content = std::move(vg);

    m_view->clearSelection();
    m_view->renderEngine().invalidateAll();
    page->addElement(std::move(elem));
    m_view->update();
}

// ============================================================
//  Insert image from file dialog
// ============================================================

void PageSequenceInputHandler::insertImage(Page* page, const Point& pos) {
    Document* doc = m_view->document();
    if (!doc || !page) return;

    QString path = QFileDialog::getOpenFileName(
        m_view, "Insert Image", QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray ba = file.readAll();

    auto imgData = std::make_shared<ImageData>();
    imgData->rawBytes.assign(
        reinterpret_cast<const uint8_t*>(ba.constData()),
        reinterpret_cast<const uint8_t*>(ba.constData()) + ba.size());

    // Detect format from extension
    QString lower = path.toLower();
    if      (lower.endsWith(".png"))              imgData->format = ImageFormat::PNG;
    else if (lower.endsWith(".jpg")||lower.endsWith(".jpeg")) imgData->format = ImageFormat::JPEG;
    else if (lower.endsWith(".bmp"))              imgData->format = ImageFormat::BMP;
    else if (lower.endsWith(".gif"))              imgData->format = ImageFormat::GIF;
    else                                           imgData->format = ImageFormat::Unknown;

    // Decode to get natural dimensions
    QImage decoded = imgData->getDecoded();
    double dispW = decoded.isNull() ? 200.0 : std::min((double)decoded.width(),  page->width  - 120.0);
    double dispH = decoded.isNull() ? 150.0 : std::min((double)decoded.height(), page->height - 120.0);

    UDoc::Image img;
    img.data          = imgData;
    img.displayWidth  = dispW;
    img.displayHeight = dispH;

    double pageMargin = 60.0;
    double x = std::max(pageMargin, pos.x);
    double y = std::max(pageMargin, pos.y);

    auto elem = std::make_unique<Element>(doc->generateId());
    elem->bounds  = Rect(x, y, dispW, dispH);
    elem->content = std::move(img);

    ID newId = elem->id();
    page->addElement(std::move(elem));
    m_view->selectElement(newId);
    m_view->renderEngine().invalidateAll();
    m_view->update();
}

// ============================================================
//  Right-click context menu
// ============================================================

void PageSequenceInputHandler::showContextMenu(const QPointF& screenPos,
                                                Page* page,
                                                Element* element) {
    QMenu menu(m_view);

    // ---- Element-specific actions ----
    if (element) {
        if (element->isEditableText()) {
            menu.addAction("✏  Edit Text", m_view, [this, element, screenPos](){
                enterTextEdit(element, screenPos);
            });
            menu.addSeparator();
        }

        QAction* cutAct = menu.addAction("✂  Cut",    m_view, [this, element](){
            m_view->clearSelection();
            m_view->selectElement(element->id());
            // Copy text to clipboard if text element
            if (auto* tc = element->textContent()) {
                QApplication::clipboard()->setText(tc->text);
            }
            ID pageId = m_view->selection().pageId;
            m_view->commandStack()->run(
                new DeleteElementCmd(m_view->document(), pageId, element->id()));
            m_view->clearSelection();
            m_view->renderEngine().invalidateAll();
            m_view->update();
        });
        cutAct->setShortcut(QKeySequence::Cut);

        QAction* copyAct = menu.addAction("⎘  Copy", m_view, [this, element](){
            if (auto* tc = element->textContent())
                QApplication::clipboard()->setText(tc->text);
        });
        copyAct->setShortcut(QKeySequence::Copy);

        menu.addSeparator();

        QAction* delAct = menu.addAction("🗑  Delete Element", m_view, [this, element](){
            ID pageId = m_view->selection().pageId;
            if (pageId == NULL_ID) {
                // Find page
                Page* p = pageForElement(element->id());
                if (p) pageId = p->id();
            }
            m_view->commandStack()->run(
                new DeleteElementCmd(m_view->document(), pageId, element->id()));
            m_view->clearSelection();
            m_view->renderEngine().invalidateAll();
            m_view->update();
        });
        delAct->setShortcut(Qt::Key_Delete);

        menu.addSeparator();
    }

    // ---- Insert submenu (always shown when on a page) ----
    if (page) {
        QRectF sr = pageScreenRect(page);
        Point localPos = screenToPageLocal(screenPos, page, sr);

        QMenu* insertMenu = menu.addMenu("➕  Insert");

        insertMenu->addAction("📝  Text Box", m_view, [this, page, localPos](){
            insertTextBlock(page, localPos);
        });

        QMenu* headingMenu = insertMenu->addMenu("🔤  Heading");
        headingMenu->addAction("H1 — Title",    m_view, [this, page, localPos](){ insertHeading(page, localPos, 1); });
        headingMenu->addAction("H2 — Section",  m_view, [this, page, localPos](){ insertHeading(page, localPos, 2); });
        headingMenu->addAction("H3 — Subsection",m_view,[this, page, localPos](){ insertHeading(page, localPos, 3); });

        insertMenu->addSeparator();

        insertMenu->addAction("🖼  Image…", m_view, [this, page, localPos](){
            insertImage(page, localPos);
        });

        insertMenu->addAction("━  Horizontal Rule", m_view, [this, page, localPos](){
            insertHRule(page, localPos);
        });

        menu.addSeparator();
    }

    // ---- Paste (always) ----
    {
        QString clipText = QApplication::clipboard()->text();
        QAction* pasteAct = menu.addAction("📋  Paste", m_view, [this, page, screenPos, clipText](){
            if (clipText.isEmpty() || !page) return;
            // If in text edit mode, paste into editor
            if (m_textEditor) {
                m_textEditor->paste();
                return;
            }
            // Otherwise paste as a new text element
            QRectF sr = pageScreenRect(page);
            Point local = screenToPageLocal(screenPos, page, sr);
            insertTextAtPagePoint(page, local);
            if (m_textEditor) m_textEditor->insertText(clipText);
        });
        pasteAct->setShortcut(QKeySequence::Paste);
        pasteAct->setEnabled(!clipText.isEmpty());
    }

    // ---- Undo / Redo ----
    menu.addSeparator();
    QAction* undoAct = menu.addAction("↩  Undo", m_view, [this](){
        if (m_textEditor) m_textEditor->commit();
        m_view->undo();
    });
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setEnabled(m_view->commandStack()->canUndo());

    QAction* redoAct = menu.addAction("↪  Redo", m_view, [this](){ m_view->redo(); });
    redoAct->setShortcut(QKeySequence::Redo);
    redoAct->setEnabled(m_view->commandStack()->canRedo());

    menu.exec(m_view->mapToGlobal(screenPos.toPoint()));
}
} // namespace UDoc
