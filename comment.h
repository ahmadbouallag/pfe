#ifndef COMMENT_H
#define COMMENT_H

#include "udoc_types.h"
#include <vector>
#include <optional>

namespace UDoc {

enum class CommentStatus { Active, Resolved, Rejected };

struct CommentTextRange {
    ID elementId;
    uint32_t startChar;
    uint32_t endChar;
};

struct Comment {
    ID id = NULL_ID;
    ID authorId = NULL_ID;
    QString authorName;
    QString authorInitials;
    QDateTime timestamp;
    QDateTime modifiedTime;
    QString text;
    std::variant<ID, CommentTextRange, Rect> target;
    std::optional<ID> parentCommentId;
    std::vector<ID> replyIds;
    CommentStatus status = CommentStatus::Active;
    bool resolved = false;
    QString resolvedBy;
    QDateTime resolvedTime;
    Color highlightColor = Color::yellow().withAlpha(0.3);
    bool showHighlight = true;
};

} // namespace UDoc

#endif // COMMENT_H
