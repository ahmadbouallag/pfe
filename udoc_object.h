#ifndef UDOC_OBJECT_H
#define UDOC_OBJECT_H

#include "udoc_types.h"

namespace UDoc {

class UDocObject {
protected:
    ID m_id;
    Metadata m_metadata;
    UDocObject* m_parent = nullptr;
    bool m_modified = false;

public:
    explicit UDocObject(ID id) : m_id(id) {}
    virtual ~UDocObject() = default;

    // Non-copyable (unique IDs)
    UDocObject(const UDocObject&) = delete;
    UDocObject& operator=(const UDocObject&) = delete;

    // Movable
    UDocObject(UDocObject&&) = default;
    UDocObject& operator=(UDocObject&&) = default;

    // Core interface
    ID id() const { return m_id; }
    ObjectType virtual objectType() const = 0;

    UDocObject* parent() const { return m_parent; }
    void setParent(UDocObject* p) { m_parent = p; }

    Metadata& metadata() { return m_metadata; }
    const Metadata& metadata() const { return m_metadata; }

    bool isModified() const { return m_modified; }
    void setModified(bool m) { m_modified = m; }

    // Tree navigation
    Document* document() const;
};

} // namespace UDoc

#endif // UDOC_OBJECT_H
