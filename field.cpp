#include "field.h"
#include "document.h"

namespace UDoc {

void Field::update(const Document* doc) const {
    // Stub: will be implemented with field engine
    cacheValid = true;
    cachedValue = QString("[%1]").arg(static_cast<int>(type));
}

} // namespace UDoc
