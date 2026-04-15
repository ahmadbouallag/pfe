#include "document.h"

namespace UDoc {

Page* Document::addPage(size_t index) {
    auto page = std::make_unique<Page>(generateId());
    page->setParent(this);

    Page* raw = page.get();
    if (index >= pages.size()) {
        pages.push_back(std::move(page));
    } else {
        pages.insert(pages.begin() + index, std::move(page));
    }
    markModified();
    return raw;
}

void Document::removePage(PageIndex index) {
    if (index < pages.size()) {
        pages.erase(pages.begin() + index);
        markModified();
    }
}

void Document::movePage(PageIndex from, PageIndex to) {
    if (from >= pages.size() || to >= pages.size()) return;
    auto page = std::move(pages[from]);
    pages.erase(pages.begin() + from);
    pages.insert(pages.begin() + to, std::move(page));
    markModified();
}

Page* Document::pageAt(PageIndex index) const {
    return (index < pages.size()) ? pages[index].get() : nullptr;
}

Tab* Document::addTab(const QString& name) {
    auto tab = std::make_unique<Tab>();
    tab->id = generateId();
    tab->name = name;
    Tab* raw = tab.get();
    tabs[tab->id] = std::move(tab);
    return raw;
}

void Document::removeTab(ID tabId) {
    tabs.erase(tabId);
}

UDocObject* Document::findObject(ID id) const {
    if (id == 0) return const_cast<Document*>(this);

    // Search pages
    for (const auto& page : pages) {
        if (page->id() == id) return page.get();
        for (const auto& elem : page->elements) {
            if (elem->id() == id) return elem.get();
        }
    }

    // FIX: Return nullptr for tabs (Tab is not a UDocObject in current design)
    // If you need Tab to be searchable, it needs to inherit from UDocObject
    return nullptr;
}

void Document::markModified() {
    m_modified = true;
    modificationDate = QDateTime::currentDateTime();
}

bool Document::hasUnsavedChanges() const {
    return m_modified;
}

size_t Document::totalElements() const {
    size_t count = 0;
    for (const auto& page : pages) {
        count += page->elements.size();
    }
    return count;
}

} // namespace UDoc
