#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "udoc_types.h"
#include "selection.h"
#include "commands.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QTimer>
#include <QObject>
#include <QTextLayout>
#include <QMenu>
#include <memory>

namespace UDoc {

class Document;
class Page;
class Element;
class DocumentView;
class TextBlockContent;

// -----------------------------------------------------------------------
// Edit modes — arch spec section 10
// -----------------------------------------------------------------------
enum class EditMode {
    View,       // Read-only, pan only
    Select,     // Click/drag to select elements
    TextEdit,   // Cursor inside a text element
    Pan         // Pan regardless of button
};

// -----------------------------------------------------------------------
// InputHandler — polymorphic base
// -----------------------------------------------------------------------
class InputHandler {
public:
    explicit InputHandler(DocumentView* view) : m_view(view) {}
    virtual ~InputHandler() = default;

    virtual void mousePressEvent(QMouseEvent* event)       = 0;
    virtual void mouseMoveEvent(QMouseEvent* event)        = 0;
    virtual void mouseReleaseEvent(QMouseEvent* event)     = 0;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) { Q_UNUSED(event) }
    virtual void keyPressEvent(QKeyEvent* event)           = 0;

    virtual QCursor cursor() const { return Qt::ArrowCursor; }

protected:
    DocumentView* m_view;
};

// -----------------------------------------------------------------------
// TextEditor — per arch spec §9.2
// Owned by PageSequenceInputHandler while a text element is being edited.
// Manages cursor position, selection, and text mutation via the command stack.
// -----------------------------------------------------------------------
class TextEditor : public QObject {
    Q_OBJECT
public:
    explicit TextEditor(DocumentView* view, Element* element, QObject* parent = nullptr);
    ~TextEditor();

    Element* element() const { return m_element; }

    // ---- Cursor movement ----
    void moveCursorLeft (bool extendSelection);
    void moveCursorRight(bool extendSelection);
    void moveCursorUp   (bool extendSelection);
    void moveCursorDown (bool extendSelection);
    void moveCursorHome (bool extendSelection);   // start of line
    void moveCursorEnd  (bool extendSelection);   // end of line
    void moveCursorDocHome(bool extendSelection); // Ctrl+Home
    void moveCursorDocEnd (bool extendSelection); // Ctrl+End
    void moveCursorWordLeft (bool extendSelection);
    void moveCursorWordRight(bool extendSelection);

    // ---- Text mutation ----
    void insertText(const QString& text);
    void deleteCharForward();   // Delete key
    void deleteCharBackward();  // Backspace key
    void deleteWordForward();   // Ctrl+Delete
    void deleteWordBackward();  // Ctrl+Backspace
    void deleteSelection();

    // ---- Selection ----
    void selectAll();
    void selectWord();
    bool hasSelection() const;
    QString selectedText() const;
    void clearSelection();

    // ---- Clipboard ----
    void cut();
    void copy();
    void paste();

    // ---- Mouse ----
    // Place cursor at the given page-local point inside the element bounds.
    void placeAtPoint(const QPointF& pageLocalPoint);
    // Extend selection to point (mouse drag).
    void extendSelectionToPoint(const QPointF& pageLocalPoint);
    // Select word under click position.
    void selectWordAtPoint(const QPointF& pageLocalPoint);

    // ---- Commit / discard ----
    void commit();   // apply pending changes, push final undo command

    // Current cursor position (char index)
    uint32_t cursorPos()  const { return m_cursorPos; }
    uint32_t selAnchor()  const { return m_selAnchor; }

private slots:
    void blinkCursor();

private:
    DocumentView* m_view;
    Element*      m_element;
    QTimer*             m_blinkTimer = nullptr;
    EditTextBlockCmd*   m_undoCmd    = nullptr; // owned until commit()

    uint32_t m_cursorPos  = 0;   // insertion point
    uint32_t m_selAnchor  = 0;   // other end of selection (anchor stays when cursor moves)

    QString  m_originalText;     // text when editing began (for undo)
    bool     m_modified   = false;

    // ---- Helpers ----
    TextBlockContent* textContent() const;
    QString& text();
    const QString& textConst() const;
    uint32_t textLen() const;

    // Apply cursor+anchor to the layout cache so the render engine draws them.
    void syncToCursor();

    // Find char index for a pixel position using QTextLayout
    uint32_t pointToCharIndex(const QPointF& localPoint) const;

    // Move cursor, optionally extending selection.
    void setCursor(uint32_t pos, bool extendSelection);

    // Rebuild QTextLayout for hit testing
    std::unique_ptr<QTextLayout> buildLayout() const;

    // Word boundary helpers
    uint32_t wordStart(uint32_t pos) const;
    uint32_t wordEnd(uint32_t pos)   const;

    uint32_t selStart() const { return std::min(m_cursorPos, m_selAnchor); }
    uint32_t selEnd()   const { return std::max(m_cursorPos, m_selAnchor); }
};

// -----------------------------------------------------------------------
// PageSequenceInputHandler
// -----------------------------------------------------------------------
class PageSequenceInputHandler : public InputHandler {
public:
    explicit PageSequenceInputHandler(DocumentView* view);
    ~PageSequenceInputHandler();

    void mousePressEvent(QMouseEvent* event)       override;
    void mouseMoveEvent(QMouseEvent* event)        override;
    void mouseReleaseEvent(QMouseEvent* event)     override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event)           override;

    QCursor cursor() const override;

    // Called by DocumentView when mode changes externally
    void exitTextEdit();

private:
    // ---- Hit testing ----
    struct HitResult { Page* page; Element* element; };
    HitResult hitTest(const QPointF& screenPos) const;
    Point     screenToPageLocal(const QPointF& screenPos, const Page* page,
                                const QRectF& pageScreenRect) const;

    // ---- State: rubber-band selection ----
    bool    m_rubberBanding  = false;
    QPointF m_rubberStart;
    QPointF m_rubberCurrent;

    // ---- State: element drag/move ----
    bool    m_draggingElement = false;
    QPointF m_dragStartScreen;
    Point   m_dragStartDocPos;

    // ---- State: text editing ----
    std::unique_ptr<TextEditor> m_textEditor;
    bool m_textDragging = false; // mouse drag inside text to extend selection

    // ---- Enter text edit mode ----
    void enterTextEdit(Element* element, const QPointF& screenPos);

    // Create a new text element at a page-local position and enter edit mode.
    // This is what happens when you click on blank page space — Word-style.
    void insertTextAtPagePoint(Page* page, const Point& pageLocalPos);

    // Right-click context menu
    void showContextMenu(const QPointF& screenPos, Page* page, Element* element);

    // Insert helpers (called from context menu)
    void insertHeading(Page* page, const Point& pos, int level);
    void insertTextBlock(Page* page, const Point& pos);
    void insertHRule(Page* page, const Point& pos);
    void insertImage(Page* page, const Point& pos);

    // ---- Helpers ----
    Page*    pageForElement(ID elementId) const;
    QRectF   pageScreenRect(const Page* page) const;
};

} // namespace UDoc

#endif // INPUT_HANDLER_H
