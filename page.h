#ifndef PAGE_H
#define PAGE_H

#include "udoc_object.h"
#include "element.h"
#include <QPixmap>

namespace UDoc {

class Page : public UDocObject {
public:
    // Page dimensions (points, 72 per inch)
    double width = 612.0;   // Default: US Letter width
    double height = 792.0;  // Default: US Letter height

    // Margins
    double marginTop = 72.0;
    double marginBottom = 72.0;
    double marginLeft = 72.0;
    double marginRight = 72.0;

    // Content
    std::vector<std::unique_ptr<Element>> elements;

    // Background
    Color backgroundColor{1, 1, 1, 1};  // White

    // Render cache (runtime only)
    mutable struct Cache {
        QPixmap pixmap;
        double zoom = 0.0;  // Zoom level of cached pixmap
        bool valid = false;
        uint64_t generation = 0;  // For cache coherency
    } cache;

    // Spatial index for hit testing (rebuilt on edit)
    mutable struct SpatialIndex {
        bool valid = false;
        // Simple grid or R-tree
        std::unordered_map<ID, Element*> idToElement;
    } spatialIndex;

    explicit Page(ID id) : UDocObject(id) {}

    ObjectType objectType() const override { return ObjectType::Page; }

    // Element management
    void addElement(std::unique_ptr<Element> elem);
    void removeElement(ID elemId);
    void moveElement(ID elemId, size_t newIndex);  // Reorder (z-index)
    Element* findElement(ID elemId) const;
    Element* hitTest(const Point& p) const;

    // Spatial index management
    void rebuildSpatialIndex() const;

    // Cache management
    void invalidateCache() const;
    bool needsRender(double zoom) const;

    // Helpers
    Rect contentRect() const;  // Page rect minus margins
    bool isPointInPage(const Point& p) const;
};

} // namespace UDoc

#endif // PAGE_H
