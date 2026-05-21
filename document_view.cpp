#include "document_view.h"
#include "input_handler.h"
#include "page.h"
#include "element.h"
#include "tab.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QRubberBand>

namespace UDoc {

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------
DocumentView::DocumentView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_viewState.zoom         = 1.0;
    m_viewState.scrollOffset = QPointF(0, 0);
    m_viewState.visibleRect  = QRectF(0, 0, width(), height());

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(180, 180, 180));
    setPalette(pal);

    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
}

DocumentView::~DocumentView() = default;

// -----------------------------------------------------------------------
// Document
// -----------------------------------------------------------------------
void DocumentView::setDocument(Document* doc) {
    m_document = doc;
    m_selection.clear();
    m_renderEngine.invalidateAll();
    rebuildInputHandler();
    update();
}

// -----------------------------------------------------------------------
// Tab switching
// -----------------------------------------------------------------------
void DocumentView::switchTab(ID tabId) {
    if (!m_document) return;
    m_document->setActiveTab(tabId);
    m_selection.clear();
    m_renderEngine.invalidateAll();
    rebuildInputHandler();
    update();
}

// -----------------------------------------------------------------------
// Zoom
// -----------------------------------------------------------------------
void DocumentView::setZoom(double zoom, std::optional<QPointF> screenOrigin) {
    zoom = qBound(0.05, zoom, 20.0);
    if (qFuzzyCompare(zoom, m_viewState.zoom)) return;

    const double oldZoom = m_viewState.zoom;
    QPointF anchor = screenOrigin.value_or(QPointF(width() / 2.0, height() / 2.0));
    const double pagePad = 20.0;

    double globalDocY = (anchor.y() + m_viewState.scrollOffset.y() - pagePad) / oldZoom;
    double newScrollY = globalDocY * zoom + pagePad - anchor.y();

    double docPageWidth = 0;
    if (m_document) {
        Tab* tab = m_document->activeTab();
        if (tab) {
            if (auto* ps = tab->pageSequence())
                if (!ps->pages.empty()) docPageWidth = ps->pages[0]->width;
        }
    }

    double oldPageW   = docPageWidth * oldZoom;
    double oldPageX   = (width() - oldPageW) / 2.0 - m_viewState.scrollOffset.x();
    double docX       = (anchor.x() - oldPageX) / oldZoom;
    double newPageW   = docPageWidth * zoom;
    double newScrollX = (width() - newPageW) / 2.0 - (anchor.x() - docX * zoom);

    m_viewState.zoom         = zoom;
    m_viewState.scrollOffset = QPointF(newScrollX, newScrollY);

    m_renderEngine.invalidateAll();
    emit zoomChanged(zoom);
    update();
}

void DocumentView::scrollBy(const QPointF& screenDelta) {
    m_viewState.scrollOffset -= screenDelta;
    update();
}

void DocumentView::scrollTo(const QPointF& docPos) {
    m_viewState.scrollOffset = QPointF(
        docPos.x() * m_viewState.zoom - width()  / 2.0,
        docPos.y() * m_viewState.zoom - height() / 2.0);
    update();
}

void DocumentView::scrollToElement(ID elementId) {
    if (!m_document) return;
    for (auto& info : visiblePageRects()) {
        if (Element* elem = info.page->findElement(elementId)) {
            if (elem->bounds)
                scrollTo(QPointF(elem->bounds->x + elem->bounds->width  / 2,
                                 elem->bounds->y + elem->bounds->height / 2));
            return;
        }
    }
}

void DocumentView::scrollToBookmark(const QString& name) {
    if (!m_document) return;
    Bookmark* bm = m_document->findGlobalBookmark(name);
    if (!bm) return;
    if (std::holds_alternative<ID>(bm->anchor))
        scrollToElement(std::get<ID>(bm->anchor));
}

// -----------------------------------------------------------------------
// Selection
// -----------------------------------------------------------------------
void DocumentView::selectElement(ID elementId) {
    if (!m_document) return;
    applySelectionToElements(false);
    m_selection.clear();

    Tab* tab = m_document->activeTab();
    if (!tab) return;

    if (auto* ps = tab->pageSequence()) {
        for (auto& page : ps->pages) {
            if (Element* elem = page->findElement(elementId)) {
                m_selection.setSingle(elementId, page->id());
                elem->selected = true;
                break;
            }
        }
    }
    onSelectionChanged();
}

void DocumentView::selectElements(const std::vector<ID>& ids) {
    applySelectionToElements(false);
    m_selection.clear();

    Tab* tab = m_document ? m_document->activeTab() : nullptr;
    if (!tab) return;

    if (auto* ps = tab->pageSequence()) {
        for (auto& page : ps->pages) {
            for (ID id : ids) {
                if (Element* elem = page->findElement(id)) {
                    m_selection.addElement(id);
                    m_selection.pageId = page->id();
                    elem->selected = true;
                }
            }
        }
    }
    onSelectionChanged();
}

void DocumentView::clearSelection() {
    applySelectionToElements(false);
    m_selection.clear();
    onSelectionChanged();
}

void DocumentView::onSelectionChanged() {
    m_renderEngine.invalidateAll();
    emit selectionChanged(m_selection);
    update();
}

// -----------------------------------------------------------------------
// Format application — called from toolbar when user changes bold/size/etc.
// If we are in text-edit mode, applies to the current selection.
// If nothing is selected, does nothing.
// -----------------------------------------------------------------------
void DocumentView::applyCharFormat(const CharacterProperties& props) {
    if (!m_document) return;

    Tab* tab = m_document->activeTab();
    if (!tab) return;

    // In text-edit mode the input handler owns the text editor.
    // We reach into it via the element's layout cache selection range.
    auto* ps = tab->pageSequence();
    if (!ps) return;

    for (auto& page : ps->pages) {
        for (ID id : m_selection.elementIds) {
            if (Element* elem = page->findElement(id)) {
                TextBlockContent* tc = nullptr;
                if (elem->isTextBlock())   tc = elem->textContent();
                else if (elem->isHeading()) tc = elem->headingContent();
                if (!tc) continue;

                uint32_t s = tc->layoutCache.selStart;
                uint32_t e = tc->layoutCache.selEnd;

                if (s == e) {
                    // No text selection — apply to whole element
                    s = 0;
                    e = (uint32_t)tc->text.length();
                }
                if (s >= e) continue;

                // Push an undo command
                CharacterProperties oldProps = tc->runs.empty()
                    ? CharacterProperties()
                    : tc->runs[0].props;

                m_commandStack.run(
                    new ApplyCharStyleCmd(m_document,
                                          page->id(), id,
                                          s, e - s,
                                          oldProps, props));

                page->invalidateCache();
                m_renderEngine.invalidateAll();
                update();

                // Emit so the toolbar reflects the new state
                emit textFormatChanged(props);
            }
        }
    }
}

// -----------------------------------------------------------------------
// Mode
// -----------------------------------------------------------------------
void DocumentView::setMode(EditMode mode) {
    m_mode = mode;
    rebuildInputHandler();
    setCursor(m_inputHandler ? m_inputHandler->cursor() : Qt::ArrowCursor);
    emit modeChanged(mode);
}

void DocumentView::startSelectTool() { setMode(EditMode::Select); }
void DocumentView::startPanTool()    { setMode(EditMode::Pan); }
void DocumentView::startTextTool()   { setMode(EditMode::TextEdit); }

// -----------------------------------------------------------------------
// Rubber band
// -----------------------------------------------------------------------
void DocumentView::showRubberBand(const QRectF& screenRect) {
    m_rubberBand->setGeometry(screenRect.toRect());
    m_rubberBand->show();
}

void DocumentView::hideRubberBand() {
    m_rubberBand->hide();
}

// -----------------------------------------------------------------------
// Coordinate helpers
// -----------------------------------------------------------------------
QPointF DocumentView::docToScreen(const QPointF& docPoint) const {
    return docPoint * m_viewState.zoom - m_viewState.scrollOffset;
}

QPointF DocumentView::screenToDoc(const QPointF& screenPoint) const {
    return (screenPoint + m_viewState.scrollOffset) / m_viewState.zoom;
}

std::vector<DocumentView::PageScreenInfo> DocumentView::visiblePageRects() const {
    std::vector<PageScreenInfo> result;
    if (!m_document) return result;

    Tab* tab = m_document->activeTab();
    if (!tab) return result;

    auto* ps = tab->pageSequence();
    if (!ps) return result;

    const double zoom    = m_viewState.zoom;
    const double pagePad = 20.0;
    double screenY = pagePad - m_viewState.scrollOffset.y();

    for (auto& page : ps->pages) {
        double pageW  = page->width  * zoom;
        double pageH  = page->height * zoom;
        double screenX = (width() - pageW) / 2.0 - m_viewState.scrollOffset.x();
        result.push_back({ page.get(), QRectF(screenX, screenY, pageW, pageH) });
        screenY += pageH + pagePad;
    }
    return result;
}

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------
void DocumentView::rebuildInputHandler() {
    if (!m_document) { m_inputHandler.reset(); return; }
    Tab* tab = m_document->activeTab();
    if (!tab) { m_inputHandler.reset(); return; }

    if (tab->pageSequence()) {
        // Preserve existing handler if it's already a PageSequenceInputHandler
        // so the text editor state survives mode changes (e.g. toolbar clicks)
        auto* existing = dynamic_cast<PageSequenceInputHandler*>(m_inputHandler.get());
        if (!existing)
            m_inputHandler = std::make_unique<PageSequenceInputHandler>(this);
    } else {
        m_inputHandler.reset();
    }
}

void DocumentView::applySelectionToElements(bool selected) {
    if (!m_document || m_selection.isEmpty()) return;
    Tab* tab = m_document->activeTab();
    if (!tab) return;

    if (auto* ps = tab->pageSequence()) {
        for (auto& page : ps->pages) {
            for (ID id : m_selection.elementIds) {
                if (Element* elem = page->findElement(id))
                    elem->selected = selected;
            }
        }
    }
}

// -----------------------------------------------------------------------
// Paint
// -----------------------------------------------------------------------
void DocumentView::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    m_viewState.visibleRect = QRectF(0, 0, width(), height());

    if (m_document)
        m_renderEngine.renderViewport(&painter, m_viewState, m_document);
    else {
        painter.setPen(Qt::darkGray);
        painter.drawText(rect(), Qt::AlignCenter, "No document loaded");
    }
}

// -----------------------------------------------------------------------
// Events
// -----------------------------------------------------------------------
void DocumentView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double delta  = event->angleDelta().y() / 120.0;
        double factor = 1.0 + delta * 0.1;
        setZoom(m_viewState.zoom * factor, event->position());
    } else {
        scrollBy(-QPointF(event->angleDelta().x() / 2.0,
                          event->angleDelta().y() / 2.0));
    }
    event->accept();
}

void DocumentView::mousePressEvent(QMouseEvent* event) {
    // Middle mouse or Shift+Left = pan
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::ShiftModifier &&
         m_mode != EditMode::TextEdit)) {
        m_panning      = true;
        m_lastMousePos = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (m_mode == EditMode::Pan) {
        m_panning      = true;
        m_lastMousePos = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (m_inputHandler) m_inputHandler->mousePressEvent(event);
}

void DocumentView::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPointF delta  = event->position() - m_lastMousePos;
        scrollBy(delta);
        m_lastMousePos = event->position();
        event->accept();
        return;
    }
    if (m_inputHandler) m_inputHandler->mouseMoveEvent(event);
}

void DocumentView::mouseReleaseEvent(QMouseEvent* event) {
    if (m_panning &&
        (event->button() == Qt::MiddleButton ||
         event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(m_inputHandler ? m_inputHandler->cursor() : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_inputHandler) m_inputHandler->mouseReleaseEvent(event);
}

// ---- Double click → routed to handler ----
void DocumentView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_inputHandler) m_inputHandler->mouseDoubleClickEvent(event);
}

void DocumentView::keyPressEvent(QKeyEvent* event) {
    // Undo / Redo — always available
    if (event->matches(QKeySequence::Undo)) {
        undo(); event->accept(); return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo(); event->accept(); return;
    }

    // Escape always clears selection and exits text edit
    if (event->key() == Qt::Key_Escape) {
        if (auto* ph = dynamic_cast<PageSequenceInputHandler*>(m_inputHandler.get()))
            ph->exitTextEdit();
        clearSelection();
        setMode(EditMode::Select);
        event->accept();
        return;
    }

    // Delete/Backspace on selected elements (not in text-edit mode)
    if (m_mode != EditMode::TextEdit &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        !m_selection.isEmpty()) {
        Tab* tab = m_document ? m_document->activeTab() : nullptr;
        if (tab) {
            if (auto* ps = tab->pageSequence()) {
                m_commandStack.beginMacro("Delete Elements");
                for (ID id : m_selection.elementIds) {
                    for (auto& page : ps->pages) {
                        if (page->findElement(id)) {
                            m_commandStack.run(
                                new DeleteElementCmd(m_document, page->id(), id));
                            break;
                        }
                    }
                }
                m_commandStack.endMacro();
                clearSelection();
            }
        }
        event->accept();
        return;
    }

    if (m_inputHandler) m_inputHandler->keyPressEvent(event);
}

void DocumentView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_viewState.visibleRect = QRectF(0, 0, width(), height());
    update();
}

} // namespace UDoc
