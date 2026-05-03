#include "element.h"
#include "table.h"
#include "block.h"
#include <QPainterPath>

namespace UDoc {

#define TYPE_CHECK(name, Type) \
bool Element::name() const { return std::holds_alternative<Type>(content); }

TYPE_CHECK(isTextBlock, TextBlockContent)
TYPE_CHECK(isHeading, HeadingContent)
TYPE_CHECK(isList, ListContent)
TYPE_CHECK(isTable, std::unique_ptr<Table>)
TYPE_CHECK(isImage, Image)
TYPE_CHECK(isVector, VectorGraphic)
TYPE_CHECK(isMedia, Media)
TYPE_CHECK(isFormula, Formula)
TYPE_CHECK(isLink, Link)
TYPE_CHECK(isBookmark, Bookmark)
TYPE_CHECK(isField, Field)
TYPE_CHECK(isComment, Comment)
TYPE_CHECK(isGroup, Group)

#define CONTENT_ACCESSOR(name, Type, var) \
Type* Element::name() { return std::get_if<Type>(&content); }

CONTENT_ACCESSOR(textContent, TextBlockContent, content)
CONTENT_ACCESSOR(headingContent, HeadingContent, content)
CONTENT_ACCESSOR(listContent, ListContent, content)

Table* Element::tableContent() {
    auto* ptr = std::get_if<std::unique_ptr<Table>>(&content);
    return ptr ? ptr->get() : nullptr;
}

CONTENT_ACCESSOR(imageContent, Image, content)
CONTENT_ACCESSOR(vectorContent, VectorGraphic, content)
CONTENT_ACCESSOR(mediaContent, Media, content)
CONTENT_ACCESSOR(formulaContent, Formula, content)
CONTENT_ACCESSOR(linkContent, Link, content)
CONTENT_ACCESSOR(bookmarkContent, Bookmark, content)
CONTENT_ACCESSOR(fieldContent, Field, content)
CONTENT_ACCESSOR(commentContent, Comment, content)
CONTENT_ACCESSOR(groupContent, Group, content)

Rect Element::transformedBounds() const {
    if (!bounds) return Rect();
    if (transform && !transform->isIdentity()) return transform->mapRect(*bounds);
    return *bounds;
}

bool Element::hitTest(const Point& p) const {
    Rect b = transformedBounds();
    if (clipPath.has_value()) {
        QPainterPath path;
        bool first = true;
        for (const auto& cmd : clipPath.value()) {
            switch (cmd.type) {
            case PathCommandType::MoveTo:
                path.moveTo(cmd.x1, cmd.y1); first = false; break;
            case PathCommandType::LineTo:
                if (first) { path.moveTo(cmd.x1, cmd.y1); first = false; }
                else path.lineTo(cmd.x1, cmd.y1); break;
            case PathCommandType::ClosePath:
                path.closeSubpath(); break;
            default: break;
            }
        }
        if (!path.contains(QPointF(p.x, p.y))) return false;
    }
    return b.contains(p);
}

bool Element::isEditableText() const {
    return isTextBlock() || isHeading() || isList();
}

void Element::startEditing() { editing = true; }
void Element::stopEditing() { editing = false; }

} // namespace UDoc
