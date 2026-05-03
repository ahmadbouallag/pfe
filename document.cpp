#include "document.h"
#include "page.h"
#include "element.h"

namespace UDoc {

Document::Document() : UDocObject(0), linkGraph(this) {
    metadata.creationDate = QDateTime::currentDateTime();
    metadata.modificationDate = metadata.creationDate;
    metadata.creator = "UDoc Editor v1.0";
}

Tab* Document::addTab(TabType type, const QString& name) {
    auto tab = std::make_unique<Tab>(generateId(), type, name);
    tab->setParent(this);
    Tab* raw = tab.get();
    tabs.push_back(std::move(tab));
    tabMap[raw->id()] = raw;
    if (activeTabId == NULL_ID) activeTabId = raw->id();
    markModified();
    return raw;
}

void Document::removeTab(ID tabId) {
    tabs.erase(std::remove_if(tabs.begin(), tabs.end(),
                              [tabId](const auto& t) { return t->id() == tabId; }), tabs.end());
    tabMap.erase(tabId);
    if (activeTabId == tabId && !tabs.empty()) activeTabId = tabs[0]->id();
    markModified();
}

void Document::moveTab(ID tabId, size_t newIndex) {
    auto it = std::find_if(tabs.begin(), tabs.end(),
                           [tabId](const auto& t) { return t->id() == tabId; });
    if (it == tabs.end() || newIndex >= tabs.size()) return;
    auto tab = std::move(*it);
    tabs.erase(it);
    tabs.insert(tabs.begin() + newIndex, std::move(tab));
    markModified();
}

Tab* Document::findTab(ID tabId) {
    auto it = tabMap.find(tabId);
    return (it != tabMap.end()) ? it->second : nullptr;
}

Tab* Document::findTabByName(const QString& name) {
    for (auto& tab : tabs) if (tab->name == name) return tab.get();
    return nullptr;
}

Tab* Document::activeTab() { return findTab(activeTabId); }

void Document::setActiveTab(ID tabId) {
    if (tabMap.find(tabId) != tabMap.end()) activeTabId = tabId;
}

void Document::addGlobalBookmark(const QString& name, ID targetId, const QString& description) {
    Bookmark bm;
    bm.id = generateId();
    bm.name = name;
    bm.description = description;
    bm.anchor = targetId;
    globalBookmarks[name] = bm;
}

void Document::removeGlobalBookmark(const QString& name) { globalBookmarks.erase(name); }

Bookmark* Document::findGlobalBookmark(const QString& name) {
    auto it = globalBookmarks.find(name);
    return (it != globalBookmarks.end()) ? &it->second : nullptr;
}

void Document::markModified() {
    modified = true;
    metadata.modificationDate = QDateTime::currentDateTime();
}

bool Document::hasUnsavedChanges() const { return modified; }
void Document::clearModified() { modified = false; }

size_t Document::totalElements() const {
    size_t count = 0;
    for (const auto& tab : tabs) {
        if (auto* ps = tab->pageSequence()) {
            for (const auto& page : ps->pages) count += page->elements.size();
        } else if (auto* ic = tab->infiniteCanvas()) {
            count += ic->elements.size();
        } else if (auto* fd = tab->flowDocument()) {
            count += fd->story->blocks.size();
        }
    }
    return count;
}

size_t Document::totalPages() const {
    size_t count = 0;
    for (const auto& tab : tabs) {
        if (auto* ps = tab->pageSequence()) count += ps->pages.size();
    }
    return count;
}

size_t Document::wordCount() const {
    size_t count = 0;
    for (const auto& tab : tabs) {
        if (auto* ps = tab->pageSequence()) {
            for (const auto& page : ps->pages) {
                for (const auto& elem : page->elements) {
                    if (auto* tc = elem->textContent()) {
                        count += tc->text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
                    }
                }
            }
        } else if (auto* fd = tab->flowDocument()) {
            count += fd->story->fullText().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
        }
    }
    return count;
}

UDocObject* Document::findObject(ID id) const {
    if (id == 0) return const_cast<Document*>(this);
    for (const auto& tab : tabs) {
        if (tab->id() == id) return tab.get();
        if (auto* ps = tab->pageSequence()) {
            for (const auto& page : ps->pages) {
                if (page->id() == id) return page.get();
                for (const auto& elem : page->elements) {
                    if (elem->id() == id) return elem.get();
                }
            }
        }
    }
    return nullptr;
}

// === LINK GRAPH ===
void LinkGraph::addLink(ID sourceElement, const LinkTarget& target) {
    removeLink(sourceElement);
    LinkEdge edge{sourceElement, target};
    m_outgoing[sourceElement].push_back(edge);
    if (std::holds_alternative<ID>(target.target)) {
        m_incoming[std::get<ID>(target.target)].push_back(edge);
    }
}

void LinkGraph::removeLink(ID sourceElement) {
    auto it = m_outgoing.find(sourceElement);
    if (it != m_outgoing.end()) {
        for (const auto& edge : it->second) {
            if (std::holds_alternative<ID>(edge.target.target)) {
                ID targetId = std::get<ID>(edge.target.target);
                auto& incoming = m_incoming[targetId];
                incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
                                              [sourceElement](const auto& e) { return e.sourceElement == sourceElement; }), incoming.end());
            }
        }
        m_outgoing.erase(it);
    }
}

void LinkGraph::removeAllLinksTo(ID targetId) {
    auto it = m_incoming.find(targetId);
    if (it != m_incoming.end()) {
        for (const auto& edge : it->second) m_outgoing.erase(edge.sourceElement);
        m_incoming.erase(it);
    }
}

void LinkGraph::updateTarget(ID sourceElement, const LinkTarget& newTarget) {
    removeLink(sourceElement);
    addLink(sourceElement, newTarget);
}

std::vector<LinkGraph::LinkEdge> LinkGraph::linksFrom(ID element) const {
    auto it = m_outgoing.find(element);
    return (it != m_outgoing.end()) ? it->second : std::vector<LinkEdge>();
}

std::vector<LinkGraph::LinkEdge> LinkGraph::linksTo(ID target) const {
    auto it = m_incoming.find(target);
    return (it != m_incoming.end()) ? it->second : std::vector<LinkEdge>();
}

std::vector<LinkGraph::BrokenLink> LinkGraph::validate() const {
    std::vector<BrokenLink> broken;
    for (const auto& [sourceId, edges] : m_outgoing) {
        for (const auto& edge : edges) {
            bool valid = false;
            switch (edge.target.type) {
            case LinkTarget::Type::ExternalURL:
            case LinkTarget::Type::Email:
            case LinkTarget::Type::File:
                valid = true; break;
            case LinkTarget::Type::DocumentInternal:
            case LinkTarget::Type::CrossTab:
            case LinkTarget::Type::CrossPage:
            case LinkTarget::Type::CrossElement:
                if (std::holds_alternative<ID>(edge.target.target))
                    valid = (m_doc->findObject(std::get<ID>(edge.target.target)) != nullptr);
                break;
            default: valid = true; break;
            }
            if (!valid) broken.push_back({sourceId, edge.target, "Target not found"});
        }
    }
    return broken;
}

bool LinkGraph::followLink(ID sourceElement) {
    auto edges = linksFrom(sourceElement);
    if (edges.empty()) return false;
    const auto& target = edges[0].target;
    if (target.type == LinkTarget::Type::CrossTab && std::holds_alternative<ID>(target.target)) {
        m_doc->setActiveTab(std::get<ID>(target.target));
        return true;
    }
    return false;
}

void LinkGraph::clear() {
    m_outgoing.clear();
    m_incoming.clear();
}

} // namespace UDoc
