#ifndef UDOC_TYPES_H
#define UDOC_TYPES_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <QColor>
#include <QImage>

namespace UDoc {

// === CORE IDENTIFIERS ===
using ID = uint64_t;
using PageIndex = uint32_t;
using ElementIndex = uint32_t;

// === FORWARD DECLARATIONS ===
class Document;
class Page;
class Element;
class UDocObject;

// === GEOMETRY ===
struct Point {
    double x, y;
    Point(double x = 0, double y = 0) : x(x), y(y) {}
};

struct Size {
    double width, height;
    Size(double w = 0, double h = 0) : width(w), height(h) {}
};

struct Rect {
    double x, y, width, height;
    Rect(double x = 0, double y = 0, double w = 0, double h = 0)
        : x(x), y(y), width(w), height(h) {}

    double right() const { return x + width; }
    double top() const { return y + height; }  // PDF: y is bottom, so top = y + height
    bool contains(const Point& p) const;
    bool intersects(const Rect& other) const;
};

// === TRANSFORMATION ===
// 3x2 matrix for 2D affine transforms (PDF-style)
struct TransformMatrix {
    double a, b, c, d, e, f;  // [a b 0; c d 0; e f 1]

    TransformMatrix() : a(1), b(0), c(0), d(1), e(0), f(0) {}  // Identity

    Point map(const Point& p) const {
        return Point(a * p.x + c * p.y + e, b * p.x + d * p.y + f);
    }

    Rect mapRect(const Rect& r) const;
};

// === METADATA ===
using MetadataValue = std::variant<std::string, double, int64_t, bool>;
using Metadata = std::unordered_map<std::string, MetadataValue>;

// === ENUMS ===
enum class ObjectType { Document, Page, Element };

enum class ElementType {
    Text,
    Image,
    Vector,
    Table,
    Media,
    Group
};

enum class ColorSpace {
    RGB,
    CMYK,
    Gray,
    Unknown
};

struct Color {
    double r, g, b, a;  // 0.0 - 1.0
    Color(double r=0, double g=0, double b=0, double a=1)
        : r(r), g(g), b(b), a(a) {}

    static Color fromQColor(const QColor& c) {
        return Color(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }

    QColor toQColor() const {
        return QColor::fromRgbF(r, g, b, a);
    }
};

} // namespace UDoc

#endif // UDOC_TYPES_H
