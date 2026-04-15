#ifndef COMMANDS_H
#define COMMANDS_H

#include "document.h"
#include <QUndoCommand>

namespace UDoc {

// === ABSTRACT BASE ===
class UDocCommand : public QUndoCommand {
protected:
    Document* m_doc;

public:
    explicit UDocCommand(Document* doc, const QString& text = "")
        : QUndoCommand(text), m_doc(doc) {}

    Document* document() const { return m_doc; }

    // Override these instead of redo()/undo()
    virtual void execute() = 0;   // Called on first do and on redo
    virtual void revert() = 0;    // Called on undo

    void redo() override { execute(); }
    void undo() override { revert(); }

    // For coalescing (e.g., dragging)
    virtual bool canMergeWith(const UDocCommand* other) const { return false; }
    virtual void mergeWith(const UDocCommand* other) {}
};

// === CONCRETE COMMANDS ===

// Move element
class MoveElementCommand : public UDocCommand {
    ID m_elementId;
    double m_oldX, m_oldY;
    double m_newX, m_newY;

public:
    MoveElementCommand(Document* doc, ID elemId, double newX, double newY);

    void execute() override;
    void revert() override;
    bool canMergeWith(const UDocCommand* other) const override;
    void mergeWith(const UDocCommand* other) override;
};

// Resize element
class ResizeElementCommand : public UDocCommand {
    ID m_elementId;
    Rect m_oldBounds;
    Rect m_newBounds;

public:
    ResizeElementCommand(Document* doc, ID elemId, const Rect& newBounds);

    void execute() override;
    void revert() override;
};

// Insert element
class InsertElementCommand : public UDocCommand {
    PageIndex m_pageIndex;
    std::unique_ptr<Element> m_element;  // Owned by command when not in doc
    size_t m_position;

public:
    InsertElementCommand(Document* doc, PageIndex pageIdx,
                         std::unique_ptr<Element> elem, size_t pos);

    void execute() override;
    void revert() override;
};

// Delete element
class DeleteElementCommand : public UDocCommand {
    PageIndex m_pageIndex;
    std::unique_ptr<Element> m_element;  // Owned by command after delete
    size_t m_oldPosition;

public:
    DeleteElementCommand(Document* doc, PageIndex pageIdx, ID elemId);

    void execute() override;
    void revert() override;
};

// Modify text
class ModifyTextCommand : public UDocCommand {
    ID m_elementId;
    QString m_oldText;
    QString m_newText;

public:
    ModifyTextCommand(Document* doc, ID elemId, const QString& newText);

    void execute() override;
    void revert() override;
    bool canMergeWith(const UDocCommand* other) const override;
    void mergeWith(const UDocCommand* other) override;
};

} // namespace UDoc

#endif // COMMANDS_H
