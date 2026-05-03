#ifndef PAGE_H
#define PAGE_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "properties.h"
#include "image.h"
#include "element.h"
#include "header_footer.h"
#include <QPixmap>
#include <memory>
#include <vector>

namespace UDoc {

class Page : public UDocObject {
public:
    double width = 612;
    double height = 792;
    double marginTop = 72;
    double marginBottom = 72;
    double marginLeft = 72;
    double marginRight = 72;

    std::vector<std::unique_ptr<Element>> elements;
    Color backgroundColor = Color::white();
    std::optional<struct Image> backgroundImage;
    std::optional<ID> masterPageId;

    // Header/footer — defined in header_footer.h
    std::optional<HeaderFooter> headerFooter;

    PageOrientation orientation = PageOrientation::Portrait;
    bool pageBreakBefore = false;
    double sequenceY = 0;

    mutable struct SpatialIndex {
        bool valid = false;
        static constexpr double GRID_SIZE = 100.0;
        std::unordered_map<ID, Element*> idMap;
        std::unordered_map<uint64_t, std::vector<Element*>> grid;
        uint64_t hashCell(int gx, int gy) const {
            return (static_cast<uint64_t>(gx) << 32) | static_cast<uint32_t>(gy);
        }
    } spatialIndex;

    mutable struct Cache {
        QPixmap pixmap;
        double zoom = 0;
        bool valid = false;
        uint64_t generation = 0;
    } cache;

    explicit Page(ID id) : UDocObject(id) {}
    ObjectType objectType() const override { return ObjectType::Page; }

    void addElement(std::unique_ptr<Element> elem);
    void removeElement(ID elemId);
    void moveElement(ID elemId, size_t newIndex);
    Element* findElement(ID elemId) const;
    Element* hitTest(const Point& p) const;
    std::vector<Element*> elementsInRect(const Rect& r) const;

    void rebuildSpatialIndex() const;
    void addToSpatialIndex(Element* elem) const;
    void invalidateCache() const;
    bool needsRender(double zoom) const;

    Rect contentRect() const;
    Rect pageRect() const { return Rect(0, 0, width, height); }
    bool containsPoint(const Point& p) const;
};

} // namespace UDoc

#endif // PAGE_H
