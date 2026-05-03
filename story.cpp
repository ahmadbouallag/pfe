#include "story.h"
#include "table.h"
#include "element.h"
#include "document.h"

namespace UDoc {

Block* Story::appendBlock(Block::Type type) {
    ID newId = document() ? document()->generateId() : 0;
    auto block = std::make_unique<Block>(newId, type);
    block->setParent(this);
    Block* raw = block.get();
    blocks.push_back(std::move(block));
    return raw;
}

Block* Story::insertBlock(size_t index, Block::Type type) {
    ID newId = document() ? document()->generateId() : 0;
    auto block = std::make_unique<Block>(newId, type);
    block->setParent(this);
    Block* raw = block.get();
    if (index >= blocks.size()) blocks.push_back(std::move(block));
    else blocks.insert(blocks.begin() + index, std::move(block));
    return raw;
}

void Story::deleteBlock(ID blockId) {
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [blockId](const auto& b) { return b->id() == blockId; }), blocks.end());
}

Block* Story::findBlock(ID blockId) {
    for (auto& block : blocks) if (block->id() == blockId) return block.get();
    return nullptr;
}

QString Story::fullText() const {
    QString result;
    for (const auto& block : blocks) {
        if (auto* tc = block->textContent()) result += tc->text + "\n";
    }
    return result;
}

void Story::appendText(const QString& text) {
    if (blocks.empty() || !blocks.back()->isText()) {
        appendBlock(Block::Type::TextBlock);
    }
    if (auto* tc = blocks.back()->textContent()) {
        tc->text += text;
        if (tc->runs.empty()) {
            tc->runs.push_back(InlineRun(0, tc->text.length(), defaultCharacterProps));
        } else {
            tc->runs.back().length += text.length();
        }
    }
}

void Story::addBookmark(const QString& name, ID anchorId, const QString& description) {
    Bookmark bm;
    bm.id = document() ? document()->generateId() : 0;
    bm.name = name;
    bm.description = description;
    bm.anchor = anchorId;
    bookmarks[name] = bm;
}

void Story::removeBookmark(const QString& name) { bookmarks.erase(name); }
bool Story::hasBookmark(const QString& name) const { return bookmarks.find(name) != bookmarks.end(); }

Bookmark* Story::findBookmark(const QString& name) {
    auto it = bookmarks.find(name);
    return (it != bookmarks.end()) ? &it->second : nullptr;
}

void Story::defineParagraphStyle(const QString& name, const ParagraphProperties& props) {
    paragraphStyles[name] = props;
}

void Story::defineCharacterStyle(const QString& name, const CharacterProperties& props) {
    characterStyles[name] = props;
}

Block* Story::blockContaining(ID elementId) {
    for (auto& block : blocks) {
        if (block->id() == elementId) return block.get();
        if (auto* table = block->tableContent()) {
            for (auto& cell : table->cells) {
                for (auto& elem : cell.content) {
                    if (elem->id() == elementId) return block.get();
                }
            }
        }
    }
    return nullptr;
}

std::vector<Block*> Story::buildOutline() const {
    std::vector<Block*> outline;
    for (const auto& block : blocks) {
        if (block->isHeading()) outline.push_back(block.get());
    }
    return outline;
}

} // namespace UDoc
