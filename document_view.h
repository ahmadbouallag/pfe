#ifndef DOCUMENT_VIEW_H
#define DOCUMENT_VIEW_H

#include <QWidget>
#include <QPainter>
#include <QRubberBand>
#include "render_engine.h"
#include "document.h"
#include "selection.h"
#include "input_handler.h"
#include "commands.h"

namespace UDoc {

// -----------------------------------------------------------------------
// DocumentView — the main viewport widget
// Implements the DocumentViewport spec from arch section 10.
// -----------------------------------------------------------------------
class DocumentView : public QWidget {
    Q_OBJECT
public:
    explicit DocumentView(QWidget* parent = nullptr);
    ~DocumentView();

    // --- Document ---
    void setDocument(Document* doc);
    Document* document() const { return m_document; }

    // --- Tab management ---
    void switchTab(ID tabId);

    // --- Zoom / scroll ---
    // ZoomOrigin: if a screen point is provided, zoom keeps that point fixed.
    // If not provided, defaults to viewport centre.
    void setZoom(double zoom, std::optional<QPointF> screenOrigin = std::nullopt);
    double zoom() const { return m_viewState.zoom; }

    void scrollBy(const QPointF& screenDelta);
    void scrollTo(const QPointF& docPos);
    void scrollToElement(ID elementId);
    void scrollToBookmark(const QString& name);

    // --- Selection (used by InputHandler and external callers) ---
    const Selection& selection() const { return m_selection; }
    void selectElement(ID elementId);
    void selectElements(const std::vector<ID>& ids);
    void clearSelection();

    // Notify view that selection changed so it repaints
    void onSelectionChanged();

    // --- Edit mode ---
    void setMode(EditMode mode);
    EditMode mode() const { return m_mode; }

    // --- Tool activation ---
    void startTextTool();
    void startPanTool();
    void startSelectTool();

    // --- Rubber band (called by PageSequenceInputHandler) ---
    void showRubberBand(const QRectF& screenRect);
    void hideRubberBand();

    // --- Coordinate helpers (public so InputHandler can use them) ---
    // Convert a screen point to document coordinates on the active page.
    // pageOriginScreen is where the page's top-left corner is on screen.
    QPointF docToScreen(const QPointF& docPoint) const;
    QPointF screenToDoc(const QPointF& screenPoint) const;

    // Get screen rect of a given page (so InputHandler can do hit tests)
    // Returns QRectF() if page not found or tab not a PageSequence.
    struct PageScreenInfo { Page* page; QRectF screenRect; };
    std::vector<PageScreenInfo> visiblePageRects() const;

    // Access render engine (for cache invalidation from InputHandler)
    RenderEngine& renderEngine() { return m_renderEngine; }
    const ViewportState& viewState() const { return m_viewState; }

    // Command stack — undo/redo
    CommandStack* commandStack() { return &m_commandStack; }
    void undo() { m_commandStack.undo(); m_renderEngine.invalidateAll(); update(); }
    void redo() { m_commandStack.redo(); m_renderEngine.invalidateAll(); update(); }

signals:
    void selectionChanged(const Selection& selection);
    void modeChanged(EditMode mode);
    void zoomChanged(double zoom);
    void documentModified();

protected:
    void paintEvent(QPaintEvent* event)        override;
    void wheelEvent(QWheelEvent* event)        override;
    void mousePressEvent(QMouseEvent* event)   override;
    void mouseMoveEvent(QMouseEvent* event)    override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event)       override;
    void resizeEvent(QResizeEvent* event)      override;

private:
    // Helpers
    void rebuildInputHandler();
    void applySelectionToElements(bool selected);

    // Pan state (always available regardless of mode via middle mouse)
    bool    m_panning = false;
    QPointF m_lastMousePos;

    // Core state
    Document*    m_document   = nullptr;
    RenderEngine m_renderEngine;
    ViewportState m_viewState;
    Selection     m_selection;
    EditMode      m_mode = EditMode::Select;

    // Input handler (swapped based on active tab type + mode)
    std::unique_ptr<InputHandler> m_inputHandler;

    // Rubber band widget for drag-select
    QRubberBand* m_rubberBand = nullptr;

    // Command stack
    CommandStack m_commandStack;
};

} // namespace UDoc

#endif // DOCUMENT_VIEW_H
