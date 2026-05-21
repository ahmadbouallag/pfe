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
    void setZoom(double zoom, std::optional<QPointF> screenOrigin = std::nullopt);
    double zoom() const { return m_viewState.zoom; }

    void scrollBy(const QPointF& screenDelta);
    void scrollTo(const QPointF& docPos);
    void scrollToElement(ID elementId);
    void scrollToBookmark(const QString& name);

    // --- Selection ---
    const Selection& selection() const { return m_selection; }
    void selectElement(ID elementId);
    void selectElements(const std::vector<ID>& ids);
    void clearSelection();
    void onSelectionChanged();

    // --- Edit mode ---
    void setMode(EditMode mode);
    EditMode mode() const { return m_mode; }

    void startTextTool();
    void startPanTool();
    void startSelectTool();

    // --- Rubber band ---
    void showRubberBand(const QRectF& screenRect);
    void hideRubberBand();

    // --- Coordinate helpers ---
    QPointF docToScreen(const QPointF& docPoint) const;
    QPointF screenToDoc(const QPointF& screenPoint) const;

    struct PageScreenInfo { Page* page; QRectF screenRect; };
    std::vector<PageScreenInfo> visiblePageRects() const;

    // --- Render engine access ---
    RenderEngine& renderEngine() { return m_renderEngine; }
    const ViewportState& viewState() const { return m_viewState; }

    // --- Command stack ---
    CommandStack* commandStack() { return &m_commandStack; }
    void undo() { m_commandStack.undo(); m_renderEngine.invalidateAll(); update(); }
    void redo() { m_commandStack.redo(); m_renderEngine.invalidateAll(); update(); }

    // --- Format application (called from toolbar/properties panel) ---
    // Applies character properties to the current text selection or
    // sets the "typing format" for the next inserted character.
    void applyCharFormat(const CharacterProperties& props);

signals:
    void selectionChanged(const Selection& selection);
    void modeChanged(EditMode mode);
    void zoomChanged(double zoom);
    void documentModified();

    // Emitted when text cursor moves — lets the toolbar update its state
    void textFormatChanged(const CharacterProperties& props);

protected:
    void paintEvent(QPaintEvent* event)             override;
    void wheelEvent(QWheelEvent* event)             override;
    void mousePressEvent(QMouseEvent* event)        override;
    void mouseMoveEvent(QMouseEvent* event)         override;
    void mouseReleaseEvent(QMouseEvent* event)      override;
    void mouseDoubleClickEvent(QMouseEvent* event)  override;  // ← new
    void keyPressEvent(QKeyEvent* event)            override;
    void resizeEvent(QResizeEvent* event)           override;

private:
    void rebuildInputHandler();
    void applySelectionToElements(bool selected);

    bool    m_panning = false;
    QPointF m_lastMousePos;

    Document*     m_document    = nullptr;
    RenderEngine  m_renderEngine;
    ViewportState m_viewState;
    Selection     m_selection;
    EditMode      m_mode = EditMode::Select;

    std::unique_ptr<InputHandler> m_inputHandler;
    QRubberBand*  m_rubberBand   = nullptr;
    CommandStack  m_commandStack;
};

} // namespace UDoc

#endif // DOCUMENT_VIEW_H
