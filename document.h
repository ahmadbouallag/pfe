#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "udoc_object.h"
#include "page.h"
#include <QDateTime>

namespace UDoc {

struct Tab {
    ID id;
    QString name;
    std::vector<PageIndex> pageIndices;  // Pages in this tab
    Metadata metadata;
};

class Document : public UDocObject {
public:
    // Content
    std::vector<std::unique_ptr<Page>> pages;
    std::unordered_map<ID, std::unique_ptr<Tab>> tabs;

    // Document metadata
    QString title;
    QString author;
    QString subject;
    QString keywords;
    QString creator;        // "UDoc Editor v1.0"
    QDateTime creationDate;
    QDateTime modificationDate;

    // State
    ID nextId = 1;  // Monotonic ID generator

    // Current view state (not serialized)
    PageIndex currentPage = 0;
    ID currentTab = 0;
    double currentZoom = 1.0;

    explicit Document() : UDocObject(0) {}  // Document ID is always 0

    ObjectType objectType() const override { return ObjectType::Document; }

    // ID generation
    ID generateId() { return nextId++; }

    // Page management
    Page* addPage(size_t index = std::string::npos);  // Insert at index, or append
    void removePage(PageIndex index);
    void movePage(PageIndex from, PageIndex to);
    Page* pageAt(PageIndex index) const;

    // Tab management
    Tab* addTab(const QString& name);
    void removeTab(ID tabId);

    // Lookup
    UDocObject* findObject(ID id) const;

    // Modification tracking
    void markModified();
    bool hasUnsavedChanges() const;

    // Statistics
    size_t totalElements() const;
};

} // namespace UDoc

#endif // DOCUMENT_H
