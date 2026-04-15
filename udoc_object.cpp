#include "udoc_object.h"
#include "document.h"

namespace UDoc {

Document* UDocObject::document() const {
    if (!m_parent) return nullptr;
    if (m_parent->objectType() == ObjectType::Document) {
        return static_cast<Document*>(m_parent);
    }
    return m_parent->document();
}

} // namespace UDoc
