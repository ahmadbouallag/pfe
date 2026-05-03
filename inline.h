#ifndef INLINE_H
#define INLINE_H

#include "udoc_types.h"
#include "properties.h"
#include <vector>
#include <optional>

namespace UDoc {

// Forward declare for embedded objects
struct ImageData;
struct Formula;
struct Field;

struct EmbeddedObject {
    enum class Type { Image, Formula, Field, Symbol, Break } type;
    uint32_t textPosition = 0;

    // Using shared_ptr for ImageData, raw pointers for others (owned elsewhere)
    std::variant<
        std::shared_ptr<ImageData>,
        Formula*,
        Field*,
        QChar
        > content;

    Size displaySize;
    VerticalAlignment baselineAlign = VerticalAlignment::Bottom;
};

struct InlineRun {
    uint32_t start = 0;
    uint32_t length = 0;
    CharacterProperties props;
    std::vector<EmbeddedObject> embedded;

    InlineRun() = default;
    InlineRun(uint32_t start, uint32_t len, const CharacterProperties& props)
        : start(start), length(len), props(props) {}

    uint32_t end() const { return start + length; }
    bool contains(uint32_t pos) const { return pos >= start && pos < end(); }

    std::optional<InlineRun> splitAt(uint32_t pos);
    void shift(int delta);
};

struct TextLayoutCache {
    bool valid = false;
    struct LineInfo {
        uint32_t startChar;
        uint32_t endChar;
        double y;
        double height;
        double width;
    };
    std::vector<LineInfo> lines;
    struct GlyphPosition {
        uint32_t charIndex;
        double x;
        double advance;
    };
    std::vector<GlyphPosition> glyphs;
    double totalWidth = 0;
    double totalHeight = 0;
    void invalidate() { valid = false; lines.clear(); glyphs.clear(); }
};

} // namespace UDoc

#endif // INLINE_H
