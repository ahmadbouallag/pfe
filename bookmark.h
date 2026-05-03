#ifndef BOOKMARK_H
#define BOOKMARK_H

#include "udoc_types.h"

namespace UDoc {

struct BookmarkAnchor {
    ID containerId;
    Point position;
};

struct Bookmark {
    ID id = NULL_ID;
    QString name;
    QString description;
    std::variant<ID, BookmarkAnchor> anchor;
    int outlineLevel = 0;
    bool isCollapsed = false;
};

} // namespace UDoc

#endif // BOOKMARK_H
