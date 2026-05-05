#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "udoc_types.h"
#include "selection.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>

namespace UDoc {

class Document;
class Page;
class Element;

// -----------------------------------------------------------------------
// Edit modes — matches arch spec section 10
// -----------------------------------------------------------------------
enum class EditMode {
    View,       // Read-only, pan only
    Select,     // Click/drag to select elements
    TextEdit,   // Cursor inside a text element
    Pan         // Middle-mouse-style pan regardless of button
};

// -----------------------------------------------------------------------
// InputHandler — polymorphic base, one subclass per tab type
// DocumentView owns one of these and forwards all input events to it.
// -----------------------------------------------------------------------
class DocumentView;  // forward

class InputHandler {
public:
    explicit InputHandler(DocumentView* view) : m_view(view) {}
    virtual ~InputHandler() = default;

    virtual void mousePressEvent(QMouseEvent* event)   = 0;
    virtual void mouseMoveEvent(QMouseEvent* event)    = 0;
    virtual void mouseReleaseEvent(QMouseEvent* event) = 0;
    virtual void keyPressEvent(QKeyEvent* event)       = 0;

    virtual QCursor cursor() const { return Qt::ArrowCursor; }

protected:
    DocumentView* m_view;
};

// -----------------------------------------------------------------------
// PageSequenceInputHandler
// Handles: click-to-select, shift-click multi-select, rubber-band drag,
// element move (drag selected element), Escape to deselect.
// -----------------------------------------------------------------------
class PageSequenceInputHandler : public InputHandler {
public:
    explicit PageSequenceInputHandler(DocumentView* view)
        : InputHandler(view) {}

    void mousePressEvent(QMouseEvent* event)   override;
    void mouseMoveEvent(QMouseEvent* event)    override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event)       override;

    QCursor cursor() const override;

private:
    // Rubber-band drag state
    bool    m_rubberBanding  = false;
    QPointF m_rubberStart;   // screen coords where drag started
    QPointF m_rubberCurrent; // screen coords current position

    // Element drag (move) state
    bool    m_draggingElement = false;
    QPointF m_dragStartScreen;
    Point   m_dragStartDocPos; // element's original doc position at drag start

    // Hit-test helpers — implemented in .cpp
    // Returns {page, element} or {nullptr, nullptr}
    struct HitResult { Page* page; Element* element; };
    HitResult hitTest(const QPointF& screenPos) const;

    // Convert screen pos → document pos on a given page
    Point screenToDoc(const QPointF& screenPos, const Page* page) const;
};

} // namespace UDoc

#endif // INPUT_HANDLER_H
