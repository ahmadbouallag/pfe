#ifndef UDOC_TYPES_H
#define UDOC_TYPES_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <functional>
#include <algorithm>
#include <cmath>

// Qt includes — minimal set
#include <QColor>
#include <QImage>
#include <QString>
#include <QDateTime>
#include <QRegularExpression>

namespace UDoc {

// === CORE IDENTIFIERS ===
using ID = uint64_t;
using PageIndex = uint32_t;
using ElementIndex = uint32_t;
using BlockIndex = uint32_t;
using TabIndex = uint32_t;

constexpr ID NULL_ID = 0;

// === GEOMETRY ===
struct Point {
    double x = 0, y = 0;
    Point() = default;
    Point(double x, double y) : x(x), y(y) {}

    Point operator+(const Point& o) const { return Point(x + o.x, y + o.y); }
    Point operator-(const Point& o) const { return Point(x - o.x, y - o.y); }
    Point operator*(double s) const { return Point(x * s, y * s); }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

struct Size {
    double width = 0, height = 0;
    Size() = default;
    Size(double w, double h) : width(w), height(h) {}
};

struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
    Rect() = default;
    Rect(double x, double y, double w, double h) : x(x), y(y), width(w), height(h) {}

    double right() const { return x + width; }
    double top() const { return y + height; }
    double bottom() const { return y; }
    double left() const { return x; }
    Point center() const { return Point(x + width/2, y + height/2); }
    Point topLeft() const { return Point(x, y); }
    Point topRight() const { return Point(right(), y); }
    Point bottomLeft() const { return Point(x, top()); }
    Point bottomRight() const { return Point(right(), top()); }

    bool contains(const Point& p) const {
        return p.x >= x && p.x <= right() && p.y >= y && p.y <= top();
    }
    bool contains(const Rect& o) const {
        return contains(o.topLeft()) && contains(o.bottomRight());
    }
    bool intersects(const Rect& o) const {
        return !(right() < o.x || x > o.right() || top() < o.y || y > o.top());
    }
    void expandToInclude(const Point& p) {
        if (p.x < x) { width += (x - p.x); x = p.x; }
        if (p.x > right()) { width = p.x - x; }
        if (p.y < y) { height += (y - p.y); y = p.y; }
        if (p.y > top()) { height = p.y - y; }
    }
    void expandToInclude(const Rect& o) {
        expandToInclude(o.topLeft());
        expandToInclude(o.bottomRight());
    }
    Rect intersected(const Rect& o) const {
        if (!intersects(o)) return Rect();
        double nx = std::max(x, o.x);
        double ny = std::max(y, o.y);
        double nw = std::min(right(), o.right()) - nx;
        double nh = std::min(top(), o.top()) - ny;
        return Rect(nx, ny, nw, nh);
    }
    Rect united(const Rect& o) const {
        Rect result = *this;
        result.expandToInclude(o);
        return result;
    }
    bool isEmpty() const { return width <= 0 || height <= 0; }
    bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
};

// === TRANSFORMATION ===
struct TransformMatrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    TransformMatrix() = default;
    TransformMatrix(double a, double b, double c, double d, double e, double f)
        : a(a), b(b), c(c), d(d), e(e), f(f) {}

    static TransformMatrix identity() { return TransformMatrix(); }
    static TransformMatrix translation(double tx, double ty) {
        return TransformMatrix(1, 0, 0, 1, tx, ty);
    }
    static TransformMatrix scaling(double sx, double sy) {
        return TransformMatrix(sx, 0, 0, sy, 0, 0);
    }
    static TransformMatrix rotation(double degrees) {
        double rad = degrees * M_PI / 180.0;
        double c = std::cos(rad);
        double s = std::sin(rad);
        return TransformMatrix(c, s, -s, c, 0, 0);
    }

    Point map(const Point& p) const {
        return Point(a * p.x + c * p.y + e, b * p.x + d * p.y + f);
    }
    Rect mapRect(const Rect& r) const {
        Point tl = map(r.topLeft());
        Point tr = map(r.topRight());
        Point bl = map(r.bottomLeft());
        Point br = map(r.bottomRight());
        double minX = std::min({tl.x, tr.x, bl.x, br.x});
        double maxX = std::max({tl.x, tr.x, bl.x, br.x});
        double minY = std::min({tl.y, tr.y, bl.y, br.y});
        double maxY = std::max({tl.y, tr.y, bl.y, br.y});
        return Rect(minX, minY, maxX - minX, maxY - minY);
    }
    TransformMatrix inverted() const {
        double det = a * d - b * c;
        if (std::abs(det) < 1e-10) return identity();
        double invDet = 1.0 / det;
        return TransformMatrix(
            d * invDet, -b * invDet,
            -c * invDet, a * invDet,
            (c * f - d * e) * invDet,
            (b * e - a * f) * invDet
            );
    }
    TransformMatrix operator*(const TransformMatrix& o) const {
        return TransformMatrix(
            a * o.a + c * o.b,      b * o.a + d * o.b,
            a * o.c + c * o.d,      b * o.c + d * o.d,
            a * o.e + c * o.f + e,  b * o.e + d * o.f + f
            );
    }
    bool isIdentity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
    double determinant() const { return a * d - b * c; }
};

// === COLOR ===
struct Color {
    double r = 0, g = 0, b = 0, a = 1;
    Color() = default;
    Color(double r, double g, double b, double a = 1) : r(r), g(g), b(b), a(a) {}

    static Color fromQColor(const QColor& c) {
        return Color(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }
    QColor toQColor() const {
        return QColor::fromRgbF(
            std::clamp(r, 0.0, 1.0),
            std::clamp(g, 0.0, 1.0),
            std::clamp(b, 0.0, 1.0),
            std::clamp(a, 0.0, 1.0)
            );
    }
    static Color black() { return Color(0, 0, 0, 1); }
    static Color white() { return Color(1, 1, 1, 1); }
    static Color transparent() { return Color(0, 0, 0, 0); }
    static Color red() { return Color(1, 0, 0, 1); }
    static Color green() { return Color(0, 1, 0, 1); }
    static Color blue() { return Color(0, 0, 1, 1); }
    static Color yellow() { return Color(1, 1, 0, 1); }
    Color withAlpha(double newAlpha) const { return Color(r, g, b, newAlpha); }
    bool operator==(const Color& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
};

// === ENUMS ===
enum class ObjectType {
    Document, Tab, Page, Slide, Sheet, Canvas, Flow, Story, Block, Element
};

enum class ElementType {
    TextBlock, Heading, List, Table, Image, VectorGraphic, Media,
    Formula, Link, Bookmark, Field, Comment, Group
};

enum class TabType {
    PageSequence, SlideSequence, GridSheet, InfiniteCanvas, FlowDocument
};

enum class Alignment { Left, Center, Right, Justify, Distributed };
enum class VerticalAlignment { Top, Middle, Bottom };

enum class LineSpacingType {
    Single, OneAndHalf, Double, AtLeast, Exactly, Multiple
};

enum class ListType {
    Bullet, Numbered, LowerRoman, UpperRoman, LowerAlpha, UpperAlpha, Checklist
};

enum class BorderStyle {
    None, Solid, Dashed, Dotted, Double, Thick, Thin
};

enum class PageOrientation { Portrait, Landscape };

enum class AnchorType {
    Inline, Block, Floating, BehindText, InFrontOfText
};

enum class AnchorRelativeTo {
    Page, Margin, Paragraph, Line, Character
};

// === METADATA ===
using MetadataValue = std::variant<std::string, double, int64_t, bool, QString>;
using Metadata = std::unordered_map<std::string, MetadataValue>;

struct DocumentMetadata {
    QString title;
    QString author;
    QString subject;
    QString keywords;
    QString creator;
    QString producer;
    QDateTime creationDate;
    QDateTime modificationDate;
    int pageCount = 0;
    int wordCount = 0;
    int charCount = 0;
};

// === PATH COMMANDS (for clip paths and vectors) ===
enum class PathCommandType {
    MoveTo, LineTo, CubicTo, QuadTo, ArcTo, ClosePath
};

struct PathCommand {
    PathCommandType type;
    double x1 = 0, y1 = 0;
    double cx1 = 0, cy1 = 0;
    double cx2 = 0, cy2 = 0;
    double rx = 0, ry = 0;
    double rotation = 0;
    bool largeArc = false;
    bool sweep = false;
};

} // namespace UDoc

#endif // UDOC_TYPES_H
