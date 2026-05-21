#include "block.h"
#include "table.h"

namespace UDoc {

Block::Block(ID id, Type type) : UDocObject(id), type(type) {}

TextBlockContent* Block::textContent() {
    return std::get_if<TextBlockContent>(&content);
}

const TextBlockContent* Block::textContent() const {
    return std::get_if<TextBlockContent>(&content);
}

Table* Block::tableContent() {
    auto* ptr = std::get_if<std::unique_ptr<Table>>(&content);
    return ptr ? ptr->get() : nullptr;
}

QString Block::fullText() const {
    if (auto* tc = textContent()) return tc->text;
    return QString();
}

void Block::setText(const QString& text) {
    if (auto* tc = textContent()) {
        tc->text = text;
        if (tc->runs.empty()) {
            tc->runs.push_back(InlineRun(0, text.length(), CharacterProperties()));
        } else {
            tc->runs[0] = InlineRun(0, text.length(), tc->runs[0].props);
            tc->runs.resize(1);
        }
        invalidateLayout();
    }
}

void Block::insertText(uint32_t pos, const QString& text) {
    if (auto* tc = textContent()) {
        tc->text.insert(pos, text);
        for (auto& run : tc->runs) {
            if (run.contains(pos)) {
                auto newRun = run.splitAt(pos);
                run.length += text.length();
                if (newRun) newRun->shift(text.length());
                break;
            } else if (run.start >= pos) {
                run.shift(text.length());
            }
        }
        invalidateLayout();
    }
}

void Block::deleteText(uint32_t pos, uint32_t len) {
    if (auto* tc = textContent()) {
        tc->text.remove(pos, len);
        auto it = tc->runs.begin();
        while (it != tc->runs.end()) {
            if (it->start >= pos + len) {
                it->shift(-(int)len); ++it;
            } else if (it->end() <= pos) {
                ++it;
            } else if (it->start >= pos && it->end() <= pos + len) {
                it = tc->runs.erase(it);
            } else if (it->start < pos && it->end() > pos + len) {
                it->length -= len; ++it;
            } else if (it->start < pos) {
                it->length = pos - it->start; ++it;
            } else {
                it->start  = pos;
                it->length = it->end() - (pos + len);
                it->shift(-(int)len); ++it;
            }
        }
        invalidateLayout();
    }
}

void Block::applyCharFormat(uint32_t start, uint32_t len,
                             const CharacterProperties& props) {
    if (auto* tc = textContent()) {
        uint32_t end = start + len;
        tc->splitRunAt(start);
        tc->splitRunAt(end);
        for (auto& run : tc->runs) {
            if (run.start >= start && run.end() <= end)
                run.props = props;
        }
        tc->coalesceRuns();
        invalidateLayout();
    }
}

CharacterProperties Block::charFormatAt(uint32_t pos) const {
    if (auto* tc = textContent()) {
        for (const auto& run : tc->runs)
            if (run.contains(pos)) return run.props;
    }
    return CharacterProperties();
}

// invalidateLayout now delegates to TextBlockContent::layoutCache
// since that's where the cache lives (moved from Block to TextBlockContent
// so Element-path text editing can access it without going through Block).
void Block::invalidateLayout() const {
    if (auto* tc = textContent())
        tc->layoutCache.invalidate();
}

// === TEXT BLOCK CONTENT ===

void TextBlockContent::setPlainText(const QString& t,
                                     const CharacterProperties& props) {
    text = t;
    runs.clear();
    if (!t.isEmpty()) runs.push_back(InlineRun(0, t.length(), props));
    layoutCache.invalidate();
}

void TextBlockContent::coalesceRuns() {
    if (runs.size() < 2) return;
    auto it = runs.begin();
    while (it != runs.end() - 1) {
        auto next = it + 1;
        if (it->end()  == next->start &&
            it->props  == next->props &&
            it->embedded.empty() && next->embedded.empty()) {
            it->length += next->length;
            it = runs.erase(next) - 1;
        } else {
            ++it;
        }
    }
}

InlineRun* TextBlockContent::runAt(uint32_t pos) {
    for (auto& run : runs) if (run.contains(pos)) return &run;
    return nullptr;
}

const InlineRun* TextBlockContent::runAt(uint32_t pos) const {
    for (const auto& run : runs) if (run.contains(pos)) return &run;
    return nullptr;
}

void TextBlockContent::splitRunAt(uint32_t pos) {
    auto* run = runAt(pos);
    if (!run || run->start == pos) return;
    auto newRun = run->splitAt(pos);
    if (newRun) {
        for (auto it = runs.begin(); it != runs.end(); ++it) {
            if (&*it == run) {
                runs.insert(it + 1, std::move(*newRun));
                return;
            }
        }
    }
}

} // namespace UDoc
