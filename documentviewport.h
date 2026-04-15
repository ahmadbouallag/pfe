#ifndef DOCUMENTVIEWPORT_H
#define DOCUMENTVIEWPORT_H

#include "renderengine.h"
#include "commands.h"
#include <QWidget>
#include <QUndoStack>
#include <QMimeData>

namespace UDoc {

// === SELECTION ===
struct Selection {
    enum Type { None, SingleElement, MultiElement, TextRange } type = None;

    PageIndex pageIdx = 0;
    ID elementId = 0;
    std::vector<ID> multiSelect;

    uint32_t textCursorStart = 0;
    uint32_t textCursorEnd = 0;

    bool isEmpty() const { return type == None; }
    void clear() { type = None; }
};

// === MAIN VIEWPORT WIDGET ===
class DocumentViewport : public QWidget {
    Q_OBJECT

public:
    explicit DocumentViewport(QWidget* parent = nullptr);
    ~DocumentViewport();

    // Document management
    void setDocument(Document* doc);
    Document* document() const { return m_doc; }

    // Undo stack access
    QUndoStack* undoStack() const { return m_undoStack; }

    // View control
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void fitToWidth();
    void fitToPage();
    void actualSize();

    void goToPage(PageIndex idx);
    void scrollToElement(ID elemId);

    // Selection
    Selection selection() const { return m_selection; }
    void selectElement(ID elemId, PageIndex pageIdx);
    void clearSelection();

    // Editing mode
    enum class Mode { View, Select, TextEdit, DrawRect, DrawEllipse, InsertImage };
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // Tool activation
    void startTextTool();
    void startRectangleTool();
    void startImageTool(const QString& imagePath);

signals:
    void pageChanged(PageIndex idx);
    void zoomChanged(double zoom);
    void selectionChanged(const Selection& sel);
    void elementDoubleClicked(ID elemId);
    void contextMenuRequested(ID elemId, const QPoint& screenPos);
    void modeChanged(Mode mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // State
    Document* m_doc = nullptr;
    std::unique_ptr<RenderEngine> m_renderer;
    QUndoStack* m_undoStack;
    ViewportState m_view;
    Selection m_selection;
    Mode m_mode = Mode::View;

    // Tool state
    struct ToolState {
        bool active = false;
        Point startPoint;
        Point currentPoint;
        QString imagePath;
    } m_toolState;

    // Interaction state
    bool m_dragging = false;
    bool m_selecting = false;
    Point m_dragStartDoc;
    QPoint m_dragStartScreen;
    Element* m_dragElement = nullptr;

    // Methods
    void updateViewportState();
    void ensureVisible(const Rect& docRect);
    Point screenToDoc(const QPoint& screen) const;
    QPoint docToScreen(const Point& doc) const;
    Element* elementAt(const Point& docPoint, PageIndex* outPage = nullptr) const;
    void executeCommand(UDocCommand* cmd);
    void updateSelectionUI();
    void centerPage(PageIndex idx);
    void updateCursor();

    // Tool methods
    void finishCurrentTool();
    void cancelCurrentTool();
    void createTextElement(const Point& pos);
    void createRectangleElement(const Rect& bounds);
    void createImageElement(const Point& pos, const QString& path);

    // Preview rendering
    void renderToolPreview(QPainter* painter);
};

} // namespace UDoc

#endif // DOCUMENTVIEWPORT_H
