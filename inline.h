#ifndef INLINE_H
#define INLINE_H

#include "udoc_types.h"
#include "properties.h"
#include <vector>
#include <optional>

namespace UDoc {

struct ImageData;
struct Formula;
struct Field;

struct EmbeddedObject {
    enum class Type { Image, Formula, Field, Symbol, Break } type;
    uint32_t textPosition = 0;
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

// -----------------------------------------------------------------------
// TextLayoutCache — holds both layout geometry AND text-editing state.
// The render engine reads cursorPos/selStart/selEnd/cursorVisible
// to draw the caret and selection highlight without any extra storage.
// The input handler writes these fields directly.
// -----------------------------------------------------------------------
struct TextLayoutCache {
    bool valid = false;

    struct LineInfo {
        uint32_t startChar = 0;
        uint32_t endChar   = 0;
        double   y         = 0;
        double   height    = 0;
        double   width     = 0;
    };
    std::vector<LineInfo> lines;

    struct GlyphPosition {
        uint32_t charIndex = 0;
        double   x         = 0;
        double   advance   = 0;
    };
    std::vector<GlyphPosition> glyphs;

    double totalWidth  = 0;
    double totalHeight = 0;

    // ---- Text editing state (written by InputHandler, read by RenderEngine) ----
    uint32_t cursorPos    = 0;     // insertion caret, in char units
    uint32_t selStart     = 0;     // selection start  (selStart == selEnd → no selection)
    uint32_t selEnd       = 0;     // selection end    (exclusive)
    bool     cursorVisible = false; // true when element is being edited
    bool     hasSelection  = false; // convenience flag

    void invalidate() {
        valid = false;
        lines.clear();
        glyphs.clear();
        // Note: do NOT reset cursor/selection here — those are editing state,
        // not layout cache.  Only reset them when editing ends.
    }

    void clearSelection() {
        selStart = selEnd = cursorPos;
        hasSelection = false;
    }

    void setSelection(uint32_t start, uint32_t end) {
        selStart = start;
        selEnd   = end;
        hasSelection = (start != end);
    }

    void resetEditing() {
        cursorPos     = 0;
        selStart      = 0;
        selEnd        = 0;
        cursorVisible = false;
        hasSelection  = false;
    }
};

} // namespace UDoc

#endif // INLINE_H
