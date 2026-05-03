#include "link.h"

namespace UDoc {

LinkTarget LinkTarget::url(const QString& url, bool newWindow) {
    LinkTarget lt; lt.type = Type::ExternalURL; lt.target = url; lt.openInNewWindow = newWindow; return lt;
}
LinkTarget LinkTarget::email(const QString& address) {
    LinkTarget lt; lt.type = Type::Email; lt.target = "mailto:" + address; return lt;
}
LinkTarget LinkTarget::file(const QString& path) {
    LinkTarget lt; lt.type = Type::File; lt.target = path; return lt;
}
LinkTarget LinkTarget::bookmark(ID bookmarkId) {
    LinkTarget lt; lt.type = Type::DocumentInternal; lt.target = bookmarkId; return lt;
}
LinkTarget LinkTarget::tab(ID tabId) {
    LinkTarget lt; lt.type = Type::CrossTab; lt.target = tabId; return lt;
}
LinkTarget LinkTarget::page(ID pageId) {
    LinkTarget lt; lt.type = Type::CrossPage; lt.target = pageId; return lt;
}
LinkTarget LinkTarget::element(ID elementId) {
    LinkTarget lt; lt.type = Type::CrossElement; lt.target = elementId; return lt;
}
LinkTarget LinkTarget::externalDocument(const QString& path, ID internalTarget) {
    LinkTarget lt; lt.type = Type::CrossDocument; lt.target = internalTarget; lt.documentPath = path; return lt;
}

} // namespace UDoc
