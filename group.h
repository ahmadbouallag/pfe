#ifndef GROUP_H
#define GROUP_H

#include "udoc_types.h"
#include <vector>
#include <memory>
#include <functional>

namespace UDoc {

class Element;

struct Group {
    std::vector<ID> childElementIds;
    std::vector<std::unique_ptr<Element>> ownedChildren;
    TransformMatrix groupTransform;
    bool clipChildren = false;
    Rect clipRect;
    Rect computeBounds(const std::function<Element*(ID)>& resolver) const;
    void flatten();
};

} // namespace UDoc

#endif // GROUP_H
