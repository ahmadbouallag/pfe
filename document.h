#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "udoc_object.h"
#include "tab.h"
#include "link.h"
#include "bookmark.h"
#include <QDateTime>

namespace UDoc {

class LinkGraph {
    Document* m_doc;
    struct LinkEdge { ID sourceElement; LinkTarget target; };
    std::unordered_map<ID, std::vector<LinkEdge>> m_outgoing;
    std::unordered_map<ID, std::vector<LinkEdge>> m_incoming;
public:
    explicit LinkGraph(Document* doc) : m_doc(doc) {}
    void addLink(ID sourceElement, const LinkTarget& target);
    void removeLink(ID sourceElement);
    void removeAllLinksTo(ID targetId);
    void updateTarget(ID sourceElement, const LinkTarget& newTarget);
    std::vector<LinkEdge> linksFrom(ID element) const;
    std::vector<LinkEdge> linksTo(ID target) const;
    struct BrokenLink { ID sourceElement; LinkTarget target; QString reason; };
    std::vector<BrokenLink> validate() const;
    bool followLink(ID sourceElement);
    void clear();
};

class Document : public UDocObject {
public:
    std::vector<std::unique_ptr<Tab>> tabs;
    std::unordered_map<ID, Tab*> tabMap;
    DocumentMetadata metadata;
    LinkGraph linkGraph;
    std::unordered_map<QString, Bookmark> globalBookmarks;
    ID nextId = 1;
    ID activeTabId = NULL_ID;
    bool modified = false;

    struct {
        bool autoSave = false;
        int autoSaveIntervalMinutes = 5;
        QString defaultFont = "Arial";
        double defaultFontSize = 12;
    } settings;

    explicit Document();
    ObjectType objectType() const override { return ObjectType::Document; }
    ID generateId() { return nextId++; }

    Tab* addTab(TabType type, const QString& name);
    void removeTab(ID tabId);
    void moveTab(ID tabId, size_t newIndex);
    Tab* findTab(ID tabId);
    Tab* findTabByName(const QString& name);
    Tab* activeTab();
    void setActiveTab(ID tabId);

    void addGlobalBookmark(const QString& name, ID targetId, const QString& description = QString());
    void removeGlobalBookmark(const QString& name);
    Bookmark* findGlobalBookmark(const QString& name);

    void markModified();
    bool hasUnsavedChanges() const;
    void clearModified();

    size_t totalElements() const;
    size_t totalPages() const;
    size_t wordCount() const;

    UDocObject* findObject(ID id) const;
};

} // namespace UDoc

#endif // DOCUMENT_H
