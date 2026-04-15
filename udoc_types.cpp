#include "udoc_types.h"

namespace UDoc {

bool Rect::contains(const Point& p) const {
    return p.x >= x && p.x <= right() && p.y >= y && p.y <= top();
}

bool Rect::intersects(const Rect& other) const {
    return !(right() < other.x || x > other.right() ||
             top() < other.y || y > other.top());
}

Rect TransformMatrix::mapRect(const Rect& r) const {
    // FIX: Use r.x and r.y, not undefined x and y
    Point tl(r.x, r.y);
    Point tr(r.right(), r.y);
    Point bl(r.x, r.top());
    Point br(r.right(), r.top());

    tl = map(tl);
    tr = map(tr);
    bl = map(bl);
    br = map(br);

    double minX = std::min({tl.x, tr.x, bl.x, br.x});
    double maxX = std::max({tl.x, tr.x, bl.x, br.x});
    double minY = std::min({tl.y, tr.y, bl.y, br.y});
    double maxY = std::max({tl.y, tr.y, bl.y, br.y});

    return Rect(minX, minY, maxX - minX, maxY - minY);
}

// FIX: Remove this - ImageElement is defined in element.h, not here
// QImage ImageElement::ImageData::getDecoded() const {
//     if (!decodedValid) {
//         decoded = QImage::fromData(rawBytes.data(), rawBytes.size());
//         decodedValid = !decoded.isNull();
//     }
//     return decoded;
// }

} // namespace UDoc
