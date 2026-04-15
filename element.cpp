#include "element.h"
#include <QPainterPath>
#include <QImage>

namespace UDoc {

Rect Element::transformedBounds() const {
    return transform.mapRect(bounds);
}

bool Element::hitTest(const Point& p) const {
    // Transform point to local coordinates
    // Inverse transform
    double det = transform.a * transform.d - transform.b * transform.c;
    if (std::abs(det) < 1e-10) return false;  // Singular

    double invA = transform.d / det;
    double invB = -transform.b / det;
    double invC = -transform.c / det;
    double invD = transform.a / det;
    double invE = (transform.c * transform.f - transform.d * transform.e) / det;
    double invF = (transform.b * transform.e - transform.a * transform.f) / det;

    double localX = invA * p.x + invC * p.y + invE;
    double localY = invB * p.x + invD * p.y + invF;

    // Check clip path if present
    if (clipPath.has_value()) {
        QPainterPath path;
        bool first = true;
        for (const auto& cmd : clipPath.value()) {
            switch (cmd.type) {
            case PathCommandType::MoveTo:
                path.moveTo(cmd.x1, cmd.y1);
                first = false;
                break;
            case PathCommandType::LineTo:
                if (first) { path.moveTo(cmd.x1, cmd.y1); first = false; }
                else path.lineTo(cmd.x1, cmd.y1);
                break;
            case PathCommandType::ClosePath:
                path.closeSubpath();
                break;
            default:
                break;  // Curves not handled in hit test for now
            }
        }
        // FIX: Use QPointF instead of two doubles
        if (!path.contains(QPointF(localX, localY))) return false;
    }

    return localX >= 0 && localX <= bounds.width &&
           localY >= 0 && localY <= bounds.height;
}
QImage ImageElement::ImageData::getDecoded() const {
    if (!decodedValid) {
        decoded = QImage::fromData(rawBytes.data(), rawBytes.size());
        decodedValid = !decoded.isNull();
    }
    return decoded;
}

} // namespace UDoc
