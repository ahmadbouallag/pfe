#ifndef ELEMENT_H
#define ELEMENT_H

#include "udoc_object.h"
#include <QString>
#include <QImage>
#include <vector>
#include <memory>
#include <variant>

namespace UDoc {

// Forward declaration
class Element;

// === TEXT ELEMENT ===
struct TextRun {
    uint32_t start = 0;
    uint32_t length = 0;

    QString fontFamily;
    double fontSize = 12.0;
    Color color;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;

    double letterSpacing = 0;
    double wordSpacing = 0;
    double baselineShift = 0;
};

struct TextElement {
    QString text;
    std::vector<TextRun> runs;

    double lineHeight = 1.2;
    int32_t alignment = 0;
    double firstLineIndent = 0;
    double leftIndent = 0;
    double rightIndent = 0;
    double spaceBefore = 0;
    double spaceAfter = 0;

    mutable struct LayoutCache {
        bool valid = false;
    } cache;
};

// === IMAGE ELEMENT ===
struct ImageElement {
    struct ImageData {
        std::vector<uint8_t> rawBytes;
        std::string format;
        int32_t width = 0;
        int32_t height = 0;
        ColorSpace colorSpace = ColorSpace::RGB;

        mutable QImage decoded;
        mutable bool decodedValid = false;

        QImage getDecoded() const;
    };

    std::shared_ptr<ImageData> data;

    double displayWidth = 0;
    double displayHeight = 0;
    int32_t interpolation = 0;
};

// === VECTOR ELEMENT ===
enum class PathCommandType {
    MoveTo,
    LineTo,
    CubicTo,
    QuadTo,
    ClosePath
};

struct PathCommand {
    PathCommandType type;
    double x1, y1;
    double cx1, cy1;
    double cx2, cy2;
};

enum class FillRule {
    NonZero,
    EvenOdd
};

struct Fill {
    Color color;
};

struct Stroke {
    Color color;
    double width = 1.0;
};

struct VectorElement {
    std::vector<PathCommand> commands;
    FillRule fillRule = FillRule::NonZero;
    std::optional<Fill> fill;
    std::optional<Stroke> stroke;
};

// === TABLE ELEMENT ===
// FIX: TableCell can't hold unique_ptr because it needs to be copyable for variant
// Instead, store indices or raw pointers, and manage lifetime through TableElement
struct TableCell {
    uint32_t row = 0;
    uint32_t col = 0;
    uint32_t rowSpan = 1;
    uint32_t colSpan = 1;
    Rect bounds;

    // Indices into TableElement's childElements vector
    std::vector<size_t> childElementIndices;
};

struct TableElement {
    uint32_t rows = 0;
    uint32_t cols = 0;
    std::vector<double> rowHeights;
    std::vector<double> colWidths;
    std::vector<TableCell> cells;

    // All child elements stored here (flattened)
    std::vector<std::unique_ptr<Element>> childElements;

    double borderWidth = 0;
    Color borderColor;
};

// === MEDIA ELEMENT ===
struct MediaElement {
    QString sourceURI;
    enum class Type { Video, Audio, Unknown } type = Type::Unknown;
    bool autoPlay = false;
    bool loop = false;
};

// === GROUP ELEMENT ===
struct GroupElement {
    // Same issue - use indices or store children separately
    std::vector<size_t> childElementIndices;
    // Actual elements stored in Document or Page
};

// === THE ELEMENT CLASS ===
class Element : public UDocObject {
public:
    Rect bounds;
    TransformMatrix transform;
    int32_t zIndex = 0;
    double opacity = 1.0;
    std::optional<std::vector<PathCommand>> clipPath;
    bool visible = true;

    std::variant<
        TextElement,
        ImageElement,
        VectorElement,
        TableElement,
        MediaElement,
        GroupElement
        > content;

    bool selected = false;
    bool locked = false;

    explicit Element(ID id) : UDocObject(id) {}

    ObjectType objectType() const override { return ObjectType::Element; }

    bool isText() const { return std::holds_alternative<TextElement>(content); }
    bool isImage() const { return std::holds_alternative<ImageElement>(content); }
    bool isVector() const { return std::holds_alternative<VectorElement>(content); }
    bool isTable() const { return std::holds_alternative<TableElement>(content); }
    bool isMedia() const { return std::holds_alternative<MediaElement>(content); }
    bool isGroup() const { return std::holds_alternative<GroupElement>(content); }

    TextElement* text() { return std::get_if<TextElement>(&content); }
    ImageElement* image() { return std::get_if<ImageElement>(&content); }
    VectorElement* vector() { return std::get_if<VectorElement>(&content); }
    TableElement* table() { return std::get_if<TableElement>(&content); }
    MediaElement* media() { return std::get_if<MediaElement>(&content); }
    GroupElement* group() { return std::get_if<GroupElement>(&content); }

    Rect transformedBounds() const;
    bool hitTest(const Point& p) const;
};

} // namespace UDoc

#endif // ELEMENT_H
