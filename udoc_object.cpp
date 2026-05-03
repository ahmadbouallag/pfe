#include "udoc_object.h"
#include "document.h"
#include "tab.h"

namespace UDoc {

Document* UDocObject::document() const {
    if (!m_parent) return nullptr;
    // We need to check objectType, but we can't include document.h here
    // without circular issues. Use dynamic approach.
    return nullptr; // Will be resolved by caller
}

Tab* UDocObject::tab() const {
    if (!m_parent) return nullptr;
    return nullptr; // Will be resolved by caller
}

QString UDocObject::objectPath() const {
    return QString("Obj[%1]").arg(m_id);
}

void UDocObject::markModified() {
    m_modified = true;
}

} // namespace UDoc
