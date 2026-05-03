#include "page.h"
#include "element.h"
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

    int gx = static_cast<int>(p.x / SpatialIndex::GRID_SIZE);
    int gy = static_cast<int>(p.y / SpatialIndex::GRID_SIZE);

    auto it = spatialIndex.grid.find(spatialIndex.hashCell(gx, gy));
    if (it != spatialIndex.grid.end()) {
        for (auto eit = it->second.rbegin(); eit != it->second.rend(); ++eit) {
            if ((*eit)->visible && (*eit)->hitTest(p)) return *eit;
        }
    }

    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if ((*it)->visible && (*it)->hitTest(p)) return it->get();
    }
    return nullptr;
}

std::vector<Element*> Page::elementsInRect(const Rect& r) const {
    std::vector<Element*> result;
    if (!spatialIndex.valid) rebuildSpatialIndex();

    int x1 = static_cast<int>(r.x / SpatialIndex::GRID_SIZE);
    int y1 = static_cast<int>(r.y / SpatialIndex::GRID_SIZE);
    int x2 = static_cast<int>(r.right() / SpatialIndex::GRID_SIZE);
    int y2 = static_cast<int>(r.top() / SpatialIndex::GRID_SIZE);

    for (int gy = y1; gy <= y2; ++gy) {
        for (int gx = x1; gx <= x2; ++gx) {
            auto it = spatialIndex.grid.find(spatialIndex.hashCell(gx, gy));
            if (it != spatialIndex.grid.end()) {
                for (Element* elem : it->second) {
                    if (elem->visible && elem->transformedBounds().intersects(r)) {
                        result.push_back(elem);
                    }
                }
            }
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void Page::rebuildSpatialIndex() const {
    spatialIndex.idMap.clear();
    spatialIndex.grid.clear();
    for (const auto& elem : elements) addToSpatialIndex(elem.get());
    spatialIndex.valid = true;
}

void Page::addToSpatialIndex(Element* elem) const {
    spatialIndex.idMap[elem->id()] = elem;
    if (!elem->bounds) return;

    Rect bounds = elem->transformedBounds();
    int x1 = static_cast<int>(bounds.x / SpatialIndex::GRID_SIZE);
    int y1 = static_cast<int>(bounds.y / SpatialIndex::GRID_SIZE);
    int x2 = static_cast<int>(bounds.right() / SpatialIndex::GRID_SIZE);
    int y2 = static_cast<int>(bounds.top() / SpatialIndex::GRID_SIZE);

    for (int gy = y1; gy <= y2; ++gy) {
        for (int gx = x1; gx <= x2; ++gx) {
            spatialIndex.grid[spatialIndex.hashCell(gx, gy)].push_back(elem);
        }
    }
}

void Page::invalidateCache() const { cache.valid = false; }

bool Page::needsRender(double zoom) const {
    return !cache.valid || std::abs(cache.zoom - zoom) > 0.001;
}

Rect Page::contentRect() const {
    return Rect(marginLeft, marginBottom,
                width - marginLeft - marginRight,
                height - marginTop - marginBottom);
}

bool Page::containsPoint(const Point& p) const {
    return p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height;
}

} // namespace UDoc
