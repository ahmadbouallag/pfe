#include "udoc_object.h"
#include "document.h"
#include "tab.h"

namespace UDoc {

// Walk up the parent chain until we find the Document (objectType == Document).
// The chain looks like: Block → Story → Tab → Document
// or: Element → Page → Tab → Document
Document* UDocObject::document() const {
    UDocObject* node = m_parent;
    while (node) {
        if (node->objectType() == ObjectType::Document)
            return static_cast<Document*>(node);
        node = node->m_parent;
    }
    return nullptr;
}

// Same idea but stop at the Tab level.
Tab* UDocObject::tab() const {
    UDocObject* node = m_parent;
    while (node) {
        if (node->objectType() == ObjectType::Tab)
            return static_cast<Tab*>(node);
        node = node->m_parent;
    }
    return nullptr;
}

QString UDocObject::objectPath() const {
    // Build a path like "Document/Tab[1]/Page[2]/Element[5]"
    QString path = QString("Obj[%1]").arg(m_id);
    UDocObject* node = m_parent;
    while (node) {
        path = QString("Obj[%1]/").arg(node->m_id) + path;
        node = node->m_parent;
    }
    return path;
}

void UDocObject::markModified() {
    m_modified = true;
    // Propagate up so the Document knows it has unsaved changes
    if (m_parent) m_parent->markModified();
}

} // namespace UDoc
