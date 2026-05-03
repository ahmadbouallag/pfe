#ifndef ELEMENT_H
#define ELEMENT_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "properties.h"
#include "inline.h"
#include "image.h"
#include "vector.h"
#include "link.h"
#include "bookmark.h"
#include "field.h"
#include "comment.h"
#include "formula.h"
#include "media.h"
#include "group.h"
#include "block.h"
#include <variant>

namespace UDoc {

// Forward declaration
class Table;
class Page;

// === HEADING CONTENT ===
struct HeadingContent : TextBlockContent {
    int outlineLevel = 1;
    bool includeInTOC = true;
    QString bookmarkName;
};

// === LIST CONTENT ===
struct ListContent {
    ListType type = ListType::Bullet;
    QString numberFormat = "%1.";
    int startNumber = 1;
    QString bulletChar = "•";

    struct Item {
        std::unique_ptr<class Element> content;
        std::unique_ptr<ListContent> subList;
        std::optional<bool> checked;
    };

    std::vector<Item> items;
};

// === THE UNIVERSAL ELEMENT ===
class Element : public UDocObject {
public:
    std::optional<Rect> bounds;
    std::optional<TransformMatrix> transform;
    std::optional<AnchorProperties> anchor;
    int32_t zIndex = 0;
    double opacity = 1.0;
    std::optional<std::vector<PathCommand>> clipPath;
    bool visible = true;

    std::variant<
        TextBlockContent,
        HeadingContent,
        ListContent,
        std::unique_ptr<Table>,
        Image,
        VectorGraphic,
        Media,
        Formula,
        Link,
        Bookmark,
        Field,
        Comment,
        Group
        > content;

    bool selected = false;
    bool locked = false;
    bool editing = false;
    std::optional<LinkTarget> linkTarget;

    explicit Element(ID id) : UDocObject(id) {}
    ObjectType objectType() const override { return ObjectType::Element; }

    bool isTextBlock() const;
    bool isHeading() const;
    bool isList() const;
    bool isTable() const;
    bool isImage() const;
    bool isVector() const;
    bool isMedia() const;
    bool isFormula() const;
    bool isLink() const;
    bool isBookmark() const;
    bool isField() const;
    bool isComment() const;
    bool isGroup() const;

    TextBlockContent* textContent();
    HeadingContent* headingContent();
    ListContent* listContent();
    Table* tableContent();
    Image* imageContent();
    VectorGraphic* vectorContent();
    Media* mediaContent();
    Formula* formulaContent();
    Link* linkContent();
    Bookmark* bookmarkContent();
    Field* fieldContent();
    Comment* commentContent();
    Group* groupContent();

    bool hasFixedGeometry() const { return bounds.has_value(); }
    bool isFlowElement() const { return !hasFixedGeometry(); }

    Rect transformedBounds() const;
    bool hitTest(const Point& p) const;

    void setComputedBounds(const Rect& r) { bounds = r; }
    void clearComputedBounds() { bounds = std::nullopt; }

    bool isEditableText() const;
    void startEditing();
    void stopEditing();
};

} // namespace UDoc

#endif // ELEMENT_H
