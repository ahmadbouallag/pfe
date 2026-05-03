#ifndef VECTOR_H
#define VECTOR_H

#include "udoc_types.h"
#include <vector>
#include <optional>

namespace UDoc {

struct GradientStop {
    double position = 0;
    Color color;
};

struct Gradient {
    enum Type { Linear, Radial } type = Linear;
    Point start, end;
    Point center;
    double radius = 0;
    std::vector<GradientStop> stops;
};

struct Fill {
    std::variant<Color, Gradient> content;
    enum Rule { NonZero, EvenOdd } rule = NonZero;
    double opacity = 1.0;
};

struct Stroke {
    Color color;
    double width = 1.0;
    enum CapStyle { CapButt, CapRound, CapSquare } cap = CapButt;
    enum JoinStyle { JoinMiter, JoinRound, JoinBevel } join = JoinMiter;
    double miterLimit = 4.0;
    std::vector<double> dashArray;
    double dashOffset = 0;
};

struct VectorGraphic {
    std::vector<PathCommand> commands;
    std::optional<Fill> fill;
    std::optional<Stroke> stroke;
    mutable std::optional<Rect> boundingBox;

    Rect computeBoundingBox() const;
};

} // namespace UDoc

#endif // VECTOR_H
