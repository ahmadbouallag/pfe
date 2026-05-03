#include "vector.h"
#include <algorithm>
#include <limits>

namespace UDoc {

Rect VectorGraphic::computeBoundingBox() const {
    if (boundingBox) return *boundingBox;
    if (commands.empty()) { boundingBox = Rect(); return *boundingBox; }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& cmd : commands) {
        switch (cmd.type) {
        case PathCommandType::MoveTo:
        case PathCommandType::LineTo:
            minX = std::min(minX, cmd.x1); minY = std::min(minY, cmd.y1);
            maxX = std::max(maxX, cmd.x1); maxY = std::max(maxY, cmd.y1);
            break;
        case PathCommandType::CubicTo:
            minX = std::min({minX, cmd.x1, cmd.cx1, cmd.cx2});
            minY = std::min({minY, cmd.y1, cmd.cy1, cmd.cy2});
            maxX = std::max({maxX, cmd.x1, cmd.cx1, cmd.cx2});
            maxY = std::max({maxY, cmd.y1, cmd.cy1, cmd.cy2});
            break;
        case PathCommandType::QuadTo:
            minX = std::min({minX, cmd.x1, cmd.cx1});
            minY = std::min({minY, cmd.y1, cmd.cy1});
            maxX = std::max({maxX, cmd.x1, cmd.cx1});
            maxY = std::max({maxY, cmd.y1, cmd.cy1});
            break;
        default: break;
        }
    }

    boundingBox = Rect(minX, minY, maxX - minX, maxY - minY);
    return *boundingBox;
}

} // namespace UDoc
