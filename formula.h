#ifndef FORMULA_H
#define FORMULA_H

#include "udoc_types.h"
#include <QString>

namespace UDoc {

struct Formula {
    enum Representation { MathML, LaTeX, OMML, UnicodeMath, SVG } representation = LaTeX;
    QString source;
    std::optional<QString> mathML;
    std::optional<QString> unicodeMath;
    bool displayMode = false;
    mutable std::optional<struct ImageData> renderedCache;
    // QImage render(double fontSize, const QString& fontFamily) const;
};

} // namespace UDoc

#endif // FORMULA_H
