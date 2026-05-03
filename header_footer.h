#ifndef HEADER_FOOTER_H
#define HEADER_FOOTER_H

#include "udoc_types.h"
#include "story.h"
#include <memory>

namespace UDoc {

// === HEADER/FOOTER ===
struct HeaderFooter {
    std::unique_ptr<Story> header;
    std::unique_ptr<Story> footer;
    bool differentFirstPage = false;
    bool differentOddEven = false;
    std::unique_ptr<Story> firstPageHeader;
    std::unique_ptr<Story> firstPageFooter;
    std::unique_ptr<Story> evenPageHeader;
    std::unique_ptr<Story> evenPageFooter;
};

} // namespace UDoc

#endif // HEADER_FOOTER_H
