#ifndef LINK_H
#define LINK_H

#include "udoc_types.h"
#include <variant>
#include <optional>

namespace UDoc {

struct LinkTarget {
    enum class Type {
        ExternalURL, Email, File, DocumentInternal,
        CrossTab, CrossPage, CrossElement, CrossDocument
    } type;

    std::variant<QString, ID> target;
    std::optional<QString> documentPath;
    std::optional<QString> tooltip;
    bool openInNewWindow = false;

    static LinkTarget url(const QString& url, bool newWindow = false);
    static LinkTarget email(const QString& address);
    static LinkTarget file(const QString& path);
    static LinkTarget bookmark(ID bookmarkId);
    static LinkTarget tab(ID tabId);
    static LinkTarget page(ID pageId);
    static LinkTarget element(ID elementId);
    static LinkTarget externalDocument(const QString& path, ID internalTarget);
};

struct Link {
    LinkTarget target;
    Color color = Color::blue();
    bool underline = true;
    Color hoverColor = Color::red();
    bool hoverUnderline = true;
};

struct BookmarkReference {
    ID bookmarkId;
    QString displayText;
};

} // namespace UDoc

#endif // LINK_H
