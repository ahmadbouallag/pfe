#ifndef UDOC_OBJECT_H
#define UDOC_OBJECT_H

#include "udoc_types.h"

namespace UDoc {

class Document;
class Tab;

class UDocObject {
protected:
    ID m_id = NULL_ID;
    Metadata m_metadata;
    UDocObject* m_parent = nullptr;
    bool m_modified = false;

public:
    explicit UDocObject(ID id) : m_id(id) {}
    virtual ~UDocObject() = default;

    UDocObject(const UDocObject&) = delete;
    UDocObject& operator=(const UDocObject&) = delete;
    UDocObject(UDocObject&&) = default;
    UDocObject& operator=(UDocObject&&) = default;

    ID id() const { return m_id; }
    virtual ObjectType objectType() const = 0;

    UDocObject* parent() const { return m_parent; }
    void setParent(UDocObject* p) { m_parent = p; }

    Document* document() const;
    Tab* tab() const;
    QString objectPath() const;

    Metadata& metadata() { return m_metadata; }
    const Metadata& metadata() const { return m_metadata; }

    bool isModified() const { return m_modified; }
    void setModified(bool m) { m_modified = m; }
    void markModified();
};

} // namespace UDoc

#endif // UDOC_OBJECT_H
