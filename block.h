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

// Forward declarations
class Element;

// === TEXT BLOCK CONTENT (defined BEFORE Block class) ===
struct TextBlockContent {
    QString text;
    std::vector<InlineRun> runs;

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

    // Content variant — TextBlockContent must be complete here
    std::variant<
        TextBlockContent,
        std::unique_ptr<Table>,
        std::monostate
        > content;

    // For Heading
    std::optional<int> outlineLevel;
    std::optional<QString> bookmarkName;

    // For SectionBreak
    std::optional<PageLayout> newSectionLayout;

    // Layout cache
    mutable TextLayoutCache layoutCache;

    explicit Block(ID id, Type type);
    ObjectType objectType() const override { return ObjectType::Block; }

    bool isText() const { return type == Type::TextBlock || type == Type::Heading; }
    bool isHeading() const { return type == Type::Heading; }
    bool isTable() const { return type == Type::Table; }

    TextBlockContent* textContent();
    const TextBlockContent* textContent() const;
    Table* tableContent();

    QString fullText() const;
    void setText(const QString& text);
    void insertText(uint32_t pos, const QString& text);
    void deleteText(uint32_t pos, uint32_t len);
    void applyCharFormat(uint32_t start, uint32_t len, const CharacterProperties& props);
    CharacterProperties charFormatAt(uint32_t pos) const;
    void invalidateLayout() const;
};

} // namespace UDoc

#endif // BLOCK_H
