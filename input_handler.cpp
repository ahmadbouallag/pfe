#include "input_handler.h"
#include "document_view.h"
#include "document.h"
#include "page.h"
#include "element.h"
#include "tab.h"
#include <QMouseEvent>
#include <QKeyEvent>

namespace UDoc {

// -----------------------------------------------------------------------
// Coordinate helper — screen → document space on a specific page
// -----------------------------------------------------------------------
Point PageSequenceInputHandler::screenToDoc(const QPointF& screenPos,
                                            const Page* page) const {
    // screenToDoc gives us a point in the global "stacked pages" doc space.
    // Since each page's elements are stored in page-local coordinates
    // (0,0 = top-left of page), we subtract the page's screen origin.
    // The caller passes the screenRect of the page, so we do it there.
    // This overload just converts using the view transform.
    QPointF doc = m_view->screenToDoc(screenPos);
    return Point(doc.x(), doc.y());
}

// -----------------------------------------------------------------------
// Hit test — find which page and element is under a screen point
// -----------------------------------------------------------------------
PageSequenceInputHandler::HitResult
PageSequenceInputHandler::hitTest(const QPointF& screenPos) const {
    Document* doc = m_view->document();
    if (!doc) return { nullptr, nullptr };

    auto pages = m_view->visiblePageRects();
    // Iterate in reverse (top z-order first)
    for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
        const QRectF& sr = it->screenRect;
        if (!sr.contains(screenPos)) continue;

        // Convert screenPos to page-local document coordinates
        // pageLocal = (screenPos - pageScreenOrigin) / zoom
        double zoom = m_view->viewState().zoom;
        QPointF local = (screenPos - sr.topLeft()) / zoom;
        Point docLocal(local.x(), local.y());

        Element* hit = it->page->hitTest(docLocal);
        if (hit) return { it->page, hit };

        // Clicked on page background — no element
        return { it->page, nullptr };
    }

    return { nullptr, nullptr };
}

// -----------------------------------------------------------------------
// Mouse press
// -----------------------------------------------------------------------
void PageSequenceInputHandler::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    Document* doc = m_view->document();
    if (!doc) return;

    EditMode mode = m_view->mode();
    if (mode != EditMode::Select && mode != EditMode::View) return;

    auto [page, element] = hitTest(event->position());

    if (element) {
        bool shift = event->modifiers() & Qt::ShiftModifier;
        if (shift) {
            // Toggle element in/out of selection
            Selection sel = m_view->selection();
            if (sel.contains(element->id())) {
                m_view->selectElements(
                    [&]() {
                        std::vector<ID> ids = sel.elementIds;
                        ids.erase(std::remove(ids.begin(), ids.end(), element->id()), ids.end());
                        return ids;
                    }());
            } else {
                std::vector<ID> ids = m_view->selection().elementIds;
                ids.push_back(element->id());
                m_view->selectElements(ids);
            }
        } else {
            // Single select
            m_view->selectElement(element->id());
        }

        // Begin element drag
        if (!element->locked) {
            m_draggingElement  = true;
            m_dragStartScreen  = event->position();
            m_dragStartDocPos  = element->bounds
                                    ? Point(element->bounds->x, element->bounds->y)
                                    : Point(0, 0);
        }
    } else {
        // Clicked on empty space — begin rubber band
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            m_view->clearSelection();
        }
        m_rubberBanding  = true;
        m_rubberStart    = event->position();
        m_rubberCurrent  = event->position();
        m_view->showRubberBand(QRectF(m_rubberStart, m_rubberStart));
    }

    event->accept();
}

// -----------------------------------------------------------------------
// Mouse move
// -----------------------------------------------------------------------
void PageSequenceInputHandler::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBanding) {
        m_rubberCurrent = event->position();
        QRectF band = QRectF(m_rubberStart, m_rubberCurrent).normalized();
        m_view->showRubberBand(band);
        event->accept();
        return;
    }

    if (m_draggingElement) {
        // Move every selected element by the screen delta converted to doc space
        const Selection& sel = m_view->selection();
        if (sel.isEmpty()) { m_draggingElement = false; return; }

        Document* doc = m_view->document();
        if (!doc) return;

        QPointF screenDelta = event->position() - m_dragStartScreen;
        double zoom = m_view->viewState().zoom;
        double dx = screenDelta.x() / zoom;
        double dy = screenDelta.y() / zoom;

        Tab* tab = doc->activeTab();
        if (!tab) return;

        if (auto* ps = tab->pageSequence()) {
            for (auto& page : ps->pages) {
                for (ID id : sel.elementIds) {
                    if (Element* elem = page->findElement(id)) {
                        if (elem->bounds) {
                            // Apply delta from the drag start position
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

    // Hover — update cursor if over an element
    auto [page, element] = hitTest(event->position());
    if (element && !element->locked) {
        m_view->setCursor(Qt::SizeAllCursor);
    } else if (page) {
        m_view->setCursor(Qt::ArrowCursor);
    } else {
        m_view->setCursor(Qt::ArrowCursor);
    }
}

// -----------------------------------------------------------------------
// Mouse release
// -----------------------------------------------------------------------
void PageSequenceInputHandler::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    if (m_rubberBanding) {
        m_rubberBanding = false;
        m_view->hideRubberBand();

        // Select all elements whose bounds intersect the rubber band
        QRectF band = QRectF(m_rubberStart, m_rubberCurrent).normalized();
        if (band.width() > 4 && band.height() > 4) {
            Document* doc = m_view->document();
            Tab* tab = doc ? doc->activeTab() : nullptr;
            if (tab) {
                if (auto* ps = tab->pageSequence()) {
                    std::vector<ID> ids;
                    double zoom = m_view->viewState().zoom;

                    for (auto& pageInfo : m_view->visiblePageRects()) {
                        // Convert rubber band to page-local doc coords
                        QPointF localTL = (band.topLeft()     - pageInfo.screenRect.topLeft()) / zoom;
                        QPointF localBR = (band.bottomRight() - pageInfo.screenRect.topLeft()) / zoom;
                        Rect docBand(localTL.x(), localTL.y(),
                                     localBR.x() - localTL.x(),
                                     localBR.y() - localTL.y());

                        auto hits = pageInfo.page->elementsInRect(docBand);
                        for (Element* e : hits) ids.push_back(e->id());
                    }

                    if (!ids.empty()) m_view->selectElements(ids);
                }
            }
        }

        event->accept();
        return;
    }

    if (m_draggingElement) {
        m_draggingElement = false;
        // TODO Phase 4: wrap the move in an UndoCommand so it's undoable
        event->accept();
        return;
    }
}

// -----------------------------------------------------------------------
// Key press
// -----------------------------------------------------------------------
void PageSequenceInputHandler::keyPressEvent(QKeyEvent* event) {
    // Arrow keys nudge selected elements by 1pt (or 10pt with Shift)
    const Selection& sel = m_view->selection();
    if (sel.isEmpty()) return;

    double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    double dx = 0, dy = 0;

    switch (event->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default: return;
    }

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

// -----------------------------------------------------------------------
// Cursor
// -----------------------------------------------------------------------
QCursor PageSequenceInputHandler::cursor() const {
    switch (m_view->mode()) {
    case EditMode::Pan:      return Qt::OpenHandCursor;
    case EditMode::TextEdit: return Qt::IBeamCursor;
    default:                 return Qt::ArrowCursor;
    }
}

} // namespace UDoc
