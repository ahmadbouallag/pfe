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
    setFocusPolicy(Qt::StrongFocus);  // needed to receive key events

    m_viewState.zoom = 1.0;
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
// Zoom — zoom toward screenOrigin (or viewport centre if not provided)
// -----------------------------------------------------------------------
void DocumentView::setZoom(double zoom, std::optional<QPointF> screenOrigin) {
    zoom = qBound(0.05, zoom, 20.0);
    if (qFuzzyCompare(zoom, m_viewState.zoom)) return;

    // Pick the anchor point on screen (cursor pos, or viewport centre)
    QPointF anchor = screenOrigin.value_or(QPointF(width() / 2.0, height() / 2.0));

    // The renderer places page content at:
    //   screenPos = docPos * zoom - scrollOffset   (for vertical: +pagePad offset)
    // So for any screen point:   docPos = (screenPos + scrollOffset) / zoom
    // We want docPos to map to the same anchor after the zoom change.

    const double oldZoom = m_viewState.zoom;

    // Doc coordinate under the anchor at the old zoom
    QPointF docUnderAnchor = (anchor + m_viewState.scrollOffset) / oldZoom;

    // Apply new zoom
    m_viewState.zoom = zoom;

    // Adjust scrollOffset so docUnderAnchor still maps to anchor:
    //   anchor = docUnderAnchor * newZoom - newScrollOffset
    //   => newScrollOffset = docUnderAnchor * newZoom - anchor
    m_viewState.scrollOffset = docUnderAnchor * zoom - anchor;

    m_renderEngine.invalidateAll();
    emit zoomChanged(zoom);
    update();
}

void DocumentView::scrollBy(const QPointF& screenDelta) {
    m_viewState.scrollOffset -= screenDelta; // subtract: dragging right moves content right
    update();
}

void DocumentView::scrollTo(const QPointF& docPos) {
    // Centre the given doc position in the viewport
    m_viewState.scrollOffset = QPointF(
        docPos.x() * m_viewState.zoom - width()  / 2.0,
        docPos.y() * m_viewState.zoom - height() / 2.0);
    update();
}

void DocumentView::scrollToElement(ID elementId) {
    if (!m_document) return;
    auto pages = visiblePageRects();
    for (auto& info : pages) {
        if (Element* elem = info.page->findElement(elementId)) {
            if (elem->bounds) {
                Rect b = *elem->bounds;
                scrollTo(QPointF(b.x + b.width / 2, b.y + b.height / 2));
            }
            return;
        }
    }
}

void DocumentView::scrollToBookmark(const QString& name) {
    if (!m_document) return;
    Bookmark* bm = m_document->findGlobalBookmark(name);
    if (!bm) return;
    if (std::holds_alternative<ID>(bm->anchor)) {
        scrollToElement(std::get<ID>(bm->anchor));
    }
}

// -----------------------------------------------------------------------
// Selection
// -----------------------------------------------------------------------
void DocumentView::selectElement(ID elementId) {
    if (!m_document) return;

    // Deselect old
    applySelectionToElements(false);

    m_selection.clear();

    // Find which page owns this element
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

// Returns all pages with their current screen rectangles.
// The layout mirrors renderPageSequence: pages stacked vertically, centred.
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
        double pageW = page->width  * zoom;
        double pageH = page->height * zoom;
        double screenX = (width() - pageW) / 2.0 - m_viewState.scrollOffset.x();

        QRectF screenRect(screenX, screenY, pageW, pageH);
        result.push_back({ page.get(), screenRect });

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
    if (!tab)        { m_inputHandler.reset(); return; }

    // For now only PageSequence gets a real handler;
    // other tab types get nullptr (Phase 12/13).
    if (tab->pageSequence()) {
        m_inputHandler = std::make_unique<PageSequenceInputHandler>(this);
    } else {
        m_inputHandler.reset();
    }
}

// Set the selected flag on all elements in the current selection
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

    if (m_document) {
        m_renderEngine.renderViewport(&painter, m_viewState, m_document);
    } else {
        painter.setPen(Qt::darkGray);
        painter.drawText(rect(), Qt::AlignCenter, "No document loaded");
    }
}

// -----------------------------------------------------------------------
// Events — pan is always on middle mouse; other input goes to handler
// -----------------------------------------------------------------------
void DocumentView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom toward cursor
        double delta  = event->angleDelta().y() / 120.0;
        double factor = 1.0 + delta * 0.1;
        setZoom(m_viewState.zoom * factor, event->position());
    } else {
        // Scroll
        scrollBy(-QPointF(event->angleDelta().x() / 2.0,
                          event->angleDelta().y() / 2.0));
    }
    event->accept();
}

void DocumentView::mousePressEvent(QMouseEvent* event) {
    // Middle mouse (or Shift+Left) = pan, always
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::ShiftModifier)) {
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

void DocumentView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
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
