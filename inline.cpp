#include "inline.h"

namespace UDoc {

std::optional<InlineRun> InlineRun::splitAt(uint32_t pos) {
    if (pos <= start || pos >= end()) return std::nullopt;

    uint32_t newStart = pos;
    uint32_t newLen = end() - pos;
    uint32_t oldLen = pos - start;

    InlineRun newRun(newStart, newLen, props);

    for (auto& emb : embedded) {
        if (emb.textPosition >= pos) {
            newRun.embedded.push_back(std::move(emb));
            emb.textPosition = 0;
        }
    }

    embedded.erase(
        std::remove_if(embedded.begin(), embedded.end(),
                       [](const auto& e) { return e.textPosition == 0; }),
        embedded.end()
        );

    for (auto& emb : newRun.embedded) {
        emb.textPosition -= pos;
    }

    length = oldLen;
    return newRun;
}

void InlineRun::shift(int delta) {
    start += delta;
    for (auto& emb : embedded) {
        emb.textPosition += delta;
    }
}

} // namespace UDoc
