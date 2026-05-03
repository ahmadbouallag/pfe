#ifndef FIELD_H
#define FIELD_H

#include "udoc_types.h"
#include <optional>

namespace UDoc {

enum class FieldType {
    PageNumber, PageCount, Date, Time, DateTime, Author, Title,
    Subject, Keywords, Creator, FileName, FilePath,
    CrossReference, Formula, Conditional, Sequence, Custom
};

struct CrossRefFormat {
    enum Type { PageNumber, HeadingText, NumberNoContext, NumberFullContext, AboveBelow } type;
    bool includeHyperlink = true;
};

struct Field {
    FieldType type = FieldType::Custom;
    QString format;
    std::optional<ID> targetId;
    std::optional<CrossRefFormat> crossRefFormat;
    std::optional<QString> formula;
    std::optional<QString> condition;
    std::optional<QString> sequenceName;
    mutable QString cachedValue;
    mutable bool cacheValid = false;
    void update(const class Document* doc) const;
};

} // namespace UDoc

#endif // FIELD_H
