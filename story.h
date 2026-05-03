#ifndef STORY_H
#define STORY_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "block.h"
#include "bookmark.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace UDoc {

class Story : public UDocObject {
public:
    std::vector<std::unique_ptr<Block>> blocks;
    std::unordered_map<QString, Bookmark> bookmarks;
    std::unordered_map<QString, ParagraphProperties> paragraphStyles;
    std::unordered_map<QString, CharacterProperties> characterStyles;
    ParagraphProperties defaultParagraphProps;
    CharacterProperties defaultCharacterProps;

    explicit Story(ID id) : UDocObject(id) {}
    ObjectType objectType() const override { return ObjectType::Story; }

    Block* appendBlock(Block::Type type);
    Block* insertBlock(size_t index, Block::Type type);
    void deleteBlock(ID blockId);
    Block* findBlock(ID blockId);
    Block* blockAt(size_t index) { return (index < blocks.size()) ? blocks[index].get() : nullptr; }

    QString fullText() const;
    void appendText(const QString& text);

    void addBookmark(const QString& name, ID anchorId, const QString& description = QString());
    void removeBookmark(const QString& name);
    bool hasBookmark(const QString& name) const;
    Bookmark* findBookmark(const QString& name);

    void defineParagraphStyle(const QString& name, const ParagraphProperties& props);
    void defineCharacterStyle(const QString& name, const CharacterProperties& props);

    Block* blockContaining(ID elementId);
    std::vector<Block*> buildOutline() const;
};

} // namespace UDoc

#endif // STORY_H
