#ifndef BLOCK_H
#define BLOCK_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "properties.h"
#include "inline.h"
#include "table.h"
#include <vector>
#include <variant>
#include <memory>

namespace UDoc {

class Element;

// === TEXT BLOCK CONTENT ===
// layoutCache lives HERE so that Element-level text blocks (which store
// TextBlockContent directly in their variant) can be edited without going
// through Block.  Block also keeps its own layoutCache for the flow-doc
// path, but all Element-path code now uses tc->layoutCache.
struct TextBlockContent {
    QString text;
    std::vector<InlineRun> runs;

    // Layout + editing state — read by RenderEngine, written by TextEditor.
    mutable TextLayoutCache layoutCache;

    void setPlainText(const QString& t, const CharacterProperties& props);
    void coalesceRuns();
    InlineRun* runAt(uint32_t pos);
    const InlineRun* runAt(uint32_t pos) const;
    void splitRunAt(uint32_t pos);
};

// === BLOCK: The fundamental content unit ===
class Block : public UDocObject {
public:
    enum class Type {
        TextBlock, Heading, List, Table, Image, VectorGraphic,
        Media, Formula, HorizontalRule, PageBreak, ColumnBreak, SectionBreak
    };

    Type type;
    ParagraphProperties paragraphProps;

    std::variant<
        TextBlockContent,
        std::unique_ptr<Table>,
        std::monostate
        > content;

    // For Heading
    std::optional<int>     outlineLevel;
    std::optional<QString> bookmarkName;

    // For SectionBreak
    std::optional<PageLayout> newSectionLayout;

    explicit Block(ID id, Type type);
    ObjectType objectType() const override { return ObjectType::Block; }

    bool isText()    const { return type == Type::TextBlock || type == Type::Heading; }
    bool isHeading() const { return type == Type::Heading; }
    bool isTable()   const { return type == Type::Table; }

    TextBlockContent* textContent();
    const TextBlockContent* textContent() const;
    Table* tableContent();

    QString fullText() const;
    void setText(const QString& text);
    void insertText(uint32_t pos, const QString& text);
    void deleteText(uint32_t pos, uint32_t len);
    void applyCharFormat(uint32_t start, uint32_t len, const CharacterProperties& props);
    CharacterProperties charFormatAt(uint32_t pos) const;

    // Invalidates the layout cache stored inside the TextBlockContent.
    void invalidateLayout() const;
};

} // namespace UDoc

#endif // BLOCK_H
