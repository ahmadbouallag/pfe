#include "page.h"
#include <algorithm>

namespace UDoc {

void Page::addElement(std::unique_ptr<Element> elem) {
    elem->setParent(this);
    elements.push_back(std::move(elem));
    invalidateCache();
    rebuildSpatialIndex();
}

void Page::removeElement(ID elemId) {
    auto it = std::find_if(elements.begin(), elements.end(),
                           [elemId](const auto& e) { return e->id() == elemId; });
    if (it != elements.end()) {
        elements.erase(it);
        invalidateCache();
        rebuildSpatialIndex();
    }
}

void Page::moveElement(ID elemId, size_t newIndex) {
    auto it = std::find_if(elements.begin(), elements.end(),
                           [elemId](const auto& e) { return e->id() == elemId; });
    if (it == elements.end() || newIndex >= elements.size()) return;

    auto elem = std::move(*it);
    elements.erase(it);
    elements.insert(elements.begin() + newIndex, std::move(elem));
    invalidateCache();
}

Element* Page::findElement(ID elemId) const {
    auto it = std::find_if(elements.begin(), elements.end(),
                           [elemId](const auto& e) { return e->id() == elemId; });
    return (it != elements.end()) ? it->get() : nullptr;
}

Element* Page::hitTest(const Point& p) const {
    if (!spatialIndex.valid) rebuildSpatialIndex();

    // Search from top (end) to bottom (beginning) for z-order
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if ((*it)->visible && (*it)->hitTest(p)) {
            return it->get();
        }
    }
    return nullptr;
}

void Page::rebuildSpatialIndex() const {
    spatialIndex.idToElement.clear();
    for (const auto& elem : elements) {
        spatialIndex.idToElement[elem->id()] = elem.get();
    }
    spatialIndex.valid = true;
}

void Page::invalidateCache() const {
    cache.valid = false;
}

bool Page::needsRender(double zoom) const {
    return !cache.valid || std::abs(cache.zoom - zoom) > 0.001;
}

Rect Page::contentRect() const {
    return Rect(marginLeft, marginBottom,
                width - marginLeft - marginRight,
                height - marginTop - marginBottom);
}

bool Page::isPointInPage(const Point& p) const {
    return p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height;
}

} // namespace UDoc
