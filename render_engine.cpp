#include "render_engine.h"
#include "document.h"
#include "story.h"
#include "block.h"
#include "inline.h"
#include "image.h"
#include "vector.h"
#include "link.h"
#include "bookmark.h"
#include "field.h"
#include "comment.h"
#include "formula.h"
#include "media.h"
#include "group.h"
#include "table.h"
#include <QTextLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QApplication>
#include <QDebug>
#include <QPainterPath>

namespace UDoc {

// === INTERNAL HELPERS ===
QRectF toQRectF(const Rect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

// === BACKGROUND RENDER THREAD ===
BackgroundRenderThread::BackgroundRenderThread(QObject* parent)
    : QObject(parent)
{
}

BackgroundRenderThread::~BackgroundRenderThread() {
}

void BackgroundRenderThread::processRenderQueue() {
    // TODO: Implement background render queue processing
}

// === MAIN RENDER ENGINE ===
RenderEngine::RenderEngine() {
}

RenderEngine::~RenderEngine() {
}

// -----------------------------------------------------------------------
// renderViewport — THE MISSING LINK
// Finds the active tab, checks its type, dispatches to the right renderer.
// Pages are centered horizontally with a margin above them and a drop shadow.
// -----------------------------------------------------------------------
void RenderEngine::renderViewport(QPainter* painter,
                                  const ViewportState& view,
                                  Document* doc) {
    if (!doc || !painter) return;

    // Fill the viewport background (light grey, like a document canvas)
    painter->fillRect(view.visibleRect, QColor(180, 180, 180));

    // Get the active tab
    Tab* tab = doc->findTab(doc->activeTabId);
    if (!tab) {
        // No active tab — try the first one
        if (doc->tabs.empty()) return;
        tab = doc->tabs[0].get();
    }

    // Dispatch based on tab content type
    if (auto* ps = tab->pageSequence()) {
        renderPageSequence(painter, *ps, view);
    } else if (auto* ss = tab->slideSequence()) {
        renderSlideSequence(painter, *ss, view);
    } else if (auto* gs = tab->gridSheet()) {
        renderGridSheet(painter, *gs, view);
    } else if (auto* fd = tab->flowDocument()) {
        renderFlowDocument(painter, *fd, view);
    } else if (auto* ic = tab->infiniteCanvas()) {
        renderInfiniteCanvas(painter, *ic, view);
    }
}

// -----------------------------------------------------------------------
// renderPageSequence
// Lays pages out vertically, centered horizontally in the viewport.
// Each page gets a drop shadow and a white background before elements.
// -----------------------------------------------------------------------
void RenderEngine::renderPageSequence(QPainter* painter,
                                      const PageSequence& pages,
                                      const ViewportState& view) {
    const double zoom       = view.zoom;
    const double pagePad    = 20.0;          // gap between pages (screen px)
    const double shadowOff  = 4.0;           // drop shadow offset (screen px)
    const QRectF visibleRect = view.visibleRect;

    painter->save();

    // Walk pages top-to-bottom, accumulating Y in screen space
    double screenY = pagePad - view.scrollOffset.y() * zoom;

    for (const auto& page : pages.pages) {
        const double pageW = page->width  * zoom;
        const double pageH = page->height * zoom;

        // Centre the page horizontally in the viewport
        double screenX = (visibleRect.width() - pageW) / 2.0
                         - view.scrollOffset.x() * zoom;

        QRectF screenPageRect(screenX, screenY, pageW, pageH);

        // Skip pages that are completely outside the visible area
        if (!visibleRect.intersects(screenPageRect.adjusted(-shadowOff - 1,
                                                            -shadowOff - 1,
                                                            shadowOff + 1,
                                                            shadowOff + 1))) {
            screenY += pageH + pagePad;
            continue;
        }

        // --- Drop shadow ---
        painter->fillRect(screenPageRect.translated(shadowOff, shadowOff),
                          QColor(0, 0, 0, 60));

        // --- Page background ---
        painter->fillRect(screenPageRect,
                          RenderHelpers::toQColor(page->backgroundColor));

        // --- Page border ---
        painter->setPen(QPen(QColor(160, 160, 160), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(screenPageRect);

        // --- Render elements into an offscreen pixmap at document coords,
        //     then draw it scaled into screenPageRect ---
        PageCache* cache = getPageCache(page->id());
        if (cache && cache->isValidFor(screenPageRect, zoom)) {
            painter->drawPixmap(screenPageRect.topLeft(), cache->cachedPixmap);
            m_stats.cacheHits++;
        } else {
            // Render at 1:1 document coordinates into a pixmap
            QSize pixSize(static_cast<int>(page->width),
                          static_cast<int>(page->height));
            if (pixSize.isEmpty()) {
                screenY += pageH + pagePad;
                continue;
            }

            QPixmap pagePixmap(pixSize);
            pagePixmap.fill(Qt::transparent);

            {
                QPainter pagePainter(&pagePixmap);
                pagePainter.setRenderHint(QPainter::Antialiasing);
                pagePainter.setRenderHint(QPainter::TextAntialiasing);

                // Background image
                if (page->backgroundImage) {
                    QRectF docRect(0, 0, page->width, page->height);
                    renderImage(&pagePainter, &(*page->backgroundImage), docRect);
                }

                // Elements
                for (const auto& element : page->elements) {
                    renderElement(&pagePainter, element.get(), view);
                }

                pagePainter.end();
            }

            // Cache and draw
            updatePageCache(page->id(), pagePixmap, screenPageRect, zoom);
            painter->drawPixmap(screenPageRect, pagePixmap,
                                QRectF(QPointF(0, 0), QSizeF(pixSize)));
            m_stats.cacheMisses++;
            m_stats.pagesRendered++;
        }

        screenY += pageH + pagePad;
    }

    painter->restore();
}

void RenderEngine::renderSlideSequence(QPainter* painter,
                                       const SlideSequence& slides,
                                       const ViewportState& view) {
    // TODO: Phase 13 — slide rendering
}

void RenderEngine::renderGridSheet(QPainter* painter,
                                   const GridSheet& sheet,
                                   const ViewportState& view) {
    // TODO: Phase 12 — grid sheet rendering
}

void RenderEngine::renderFlowDocument(QPainter* painter,
                                      const FlowDocument& flow,
                                      const ViewportState& view) {
    // TODO: Phase 11 — flow document rendering
}

void RenderEngine::renderInfiniteCanvas(QPainter* painter,
                                        const InfiniteCanvas& canvas,
                                        const ViewportState& view) {
    // TODO: Phase 3 — infinite canvas rendering
}

// -----------------------------------------------------------------------
// renderElement — dispatches to per-type renderers
// Coordinates are in document space (page pixels at 1:1).
// -----------------------------------------------------------------------
void RenderEngine::renderElement(QPainter* painter, const Element* element,
                                 const ViewportState& view) {
    if (!element || !element->visible) return;

    painter->save();

    // Bounds in document coordinates
    QRectF bounds;
    if (element->bounds) {
        bounds = toQRectF(*element->bounds);
    } else {
        bounds = QRectF(0, 0, 100, 20); // flow element placeholder
    }

    // Transform
    if (element->transform) {
        painter->setTransform(RenderHelpers::toQTransform(*element->transform), true);
    }

    // Clip path
    if (element->clipPath) {
        RenderHelpers::applyClipPath(painter, *element->clipPath, painter->transform());
    }

    // Opacity
    painter->setOpacity(element->opacity);

    // Content dispatch
    std::visit([&](auto&& content) {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, TextBlockContent>) {
            renderTextBlock(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, HeadingContent>) {
            renderHeading(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, ListContent>) {
            renderList(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<Table>>) {
            renderTable(painter, content.get(), bounds);
        } else if constexpr (std::is_same_v<T, Image>) {
            renderImage(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, VectorGraphic>) {
            renderVector(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Link>) {
            renderLink(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Field>) {
            renderField(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Comment>) {
            renderComment(painter, &content, bounds);
        }
        // Bookmark, Media, Formula, Group — no visible rendering yet
    }, element->content);

    if (element->selected)   renderSelection(painter, element, view);
    if (element->linkTarget) renderLinkIndicators(painter, element, view);
    if (element->editing)    renderEditingHandles(painter, element, view);

    painter->restore();
    m_stats.elementsRendered++;
}

// -----------------------------------------------------------------------
// renderTextBlock — QTextLayout-based text rendering
// -----------------------------------------------------------------------
void RenderEngine::renderTextBlock(QPainter* painter,
                                   const TextBlockContent* text,
                                   const QRectF& bounds) {
    if (!text || text->text.isEmpty()) return;

    auto layout = layoutTextBlock(text, bounds.width());
    if (!layout) return;

    layout->draw(painter, bounds.topLeft());
}

// -----------------------------------------------------------------------
// renderHeading — bold, size-scaled by outline level, then text block
// -----------------------------------------------------------------------
void RenderEngine::renderHeading(QPainter* painter,
                                 const HeadingContent* heading,
                                 const QRectF& bounds) {
    if (!heading) return;

    painter->save();

    QFont font = painter->font();
    font.setBold(true);
    switch (heading->outlineLevel) {
    case 1: font.setPointSize(24); break;
    case 2: font.setPointSize(20); break;
    case 3: font.setPointSize(18); break;
    case 4: font.setPointSize(16); break;
    case 5: font.setPointSize(14); break;
    default: font.setPointSize(12); break;
    }
    painter->setFont(font);

    // HeadingContent inherits TextBlockContent, so re-use its renderer
    renderTextBlock(painter, heading, bounds);

    painter->restore();
}

// -----------------------------------------------------------------------
// renderList — bullet/number + item text, all in document coordinates
// -----------------------------------------------------------------------
void RenderEngine::renderList(QPainter* painter,
                              const ListContent* list,
                              const QRectF& bounds) {
    if (!list) return;

    painter->save();
    painter->setPen(QPen(Qt::black, 1.0));

    const double lineHeight = 22.0;
    const double bulletColW = 18.0;  // width reserved for bullet/number
    double y = bounds.top();

    for (size_t i = 0; i < list->items.size(); ++i) {
        const auto& item = list->items[i];

        // Bullet or number
        QString marker;
        if (list->type == ListType::Numbered) {
            marker = QString(list->numberFormat).arg(
                static_cast<int>(list->startNumber + i));
        } else {
            marker = list->bulletChar;
        }
        painter->drawText(QPointF(bounds.left(), y + lineHeight - 4.0), marker);

        // Item text — render via TextBlockContent if the inner element has one
        if (item.content) {
            QRectF itemBounds(bounds.left() + bulletColW, y,
                              bounds.width() - bulletColW, lineHeight);

            // Extract TextBlockContent from the item's Element and draw directly
            // (avoids the broken ViewportState() call from before)
            if (auto* tc = item.content->textContent()) {
                renderTextBlock(painter, tc, itemBounds);
            } else if (auto* hc = item.content->headingContent()) {
                renderHeading(painter, hc, itemBounds);
            }
        }

        y += lineHeight;
    }

    painter->restore();
}

void RenderEngine::renderTable(QPainter* painter,
                               const Table* table,
                               const QRectF& bounds) {
    // TODO: Phase 2 follow-up / Phase 3 polish
}

// -----------------------------------------------------------------------
// renderImage
// -----------------------------------------------------------------------
void RenderEngine::renderImage(QPainter* painter,
                               const Image* image,
                               const QRectF& bounds) {
    if (!image || !image->data) return;

    painter->save();

    QImage decoded = image->data->getDecoded();
    if (!decoded.isNull()) {
        painter->save();

        if (image->rotation != 0) {
            painter->translate(bounds.center());
            painter->rotate(image->rotation);
            painter->translate(-bounds.center());
        }

        if (image->flipHorizontal || image->flipVertical) {
            painter->scale(image->flipHorizontal ? -1.0 : 1.0,
                           image->flipVertical   ? -1.0 : 1.0);
        }

        painter->drawImage(bounds, decoded);
        painter->restore();
    }

    painter->restore();
}

void RenderEngine::renderVector(QPainter* painter,
                                const VectorGraphic* vector,
                                const QRectF& bounds) {
    // TODO: Phase 2 follow-up
}

void RenderEngine::renderLink(QPainter* painter,
                              const Link* link,
                              const QRectF& bounds) {
    if (!link) return;

    painter->save();
    painter->setPen(QPen(RenderHelpers::toQColor(link->color), 1.0, Qt::SolidLine));
    double underlineY = bounds.bottom() - 1.0;
    painter->drawLine(QPointF(bounds.left(), underlineY),
                      QPointF(bounds.right(), underlineY));
    painter->restore();
}

void RenderEngine::renderField(QPainter* painter,
                               const Field* field,
                               const QRectF& bounds) {
    // TODO: Phase 9
}

void RenderEngine::renderComment(QPainter* painter,
                                 const Comment* comment,
                                 const QRectF& bounds) {
    // TODO: Phase 9
}

// -----------------------------------------------------------------------
// layoutTextBlock — QTextLayout integration
// Returns a fully laid-out QTextLayout ready to draw at bounds.topLeft().
// -----------------------------------------------------------------------
std::unique_ptr<QTextLayout> RenderEngine::layoutTextBlock(
    const TextBlockContent* text, double maxWidth) {
    if (!text || text->text.isEmpty()) return nullptr;

    auto layout = std::make_unique<QTextLayout>();
    layout->setText(text->text);

    // Apply per-run formatting
    QVector<QTextLayout::FormatRange> formats;
    for (const auto& run : text->runs) {
        QTextLayout::FormatRange fr;
        fr.start  = static_cast<int>(run.start);
        fr.length = static_cast<int>(run.length);
        fr.format = RenderHelpers::toQTextCharFormat(run.props);
        formats.append(fr);
    }
    layout->setFormats(formats);

    // Text options
    QTextOption option;
    option.setAlignment(Qt::AlignLeft);
    option.setWrapMode(QTextOption::WordWrap);
    layout->setTextOption(option);

    // Line break pass
    layout->beginLayout();
    QPointF linePos(0, 0);
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(maxWidth > 0 ? maxWidth : 9999.0);
        line.setPosition(linePos);
        linePos.setY(linePos.y() + line.height());
    }
    layout->endLayout();

    return layout;
}

// -----------------------------------------------------------------------
// Cache management
// -----------------------------------------------------------------------
void RenderEngine::invalidatePage(ID pageId) {
    m_pageCache.remove(pageId);
}

void RenderEngine::invalidateElement(ID elementId) {
    m_elementCache.remove(elementId);
}

void RenderEngine::invalidateAll() {
    m_pageCache.clear();
    m_elementCache.clear();
}

void RenderEngine::clearCache() {
    m_pageCache.clear();
    m_elementCache.clear();
}

void RenderEngine::setMaxCacheSize(int megabytes) {
    m_maxCacheSizeMB = megabytes;
}

int RenderEngine::cacheSize() const {
    return m_maxCacheSizeMB;
}

void RenderEngine::enableBackgroundRendering(bool enable) {
    m_backgroundRendering = enable;
}

bool RenderEngine::isBackgroundRenderingEnabled() const {
    return m_backgroundRendering;
}

void RenderEngine::resetStats() {
    m_stats = RenderStats();
}

void RenderEngine::setupPainter(QPainter* painter, const ViewportState& view) {
    // TODO: antialiasing hints etc.
}

QTransform RenderEngine::createViewTransform(const ViewportState& view) const {
    QTransform t;
    t.translate(-view.scrollOffset.x(), -view.scrollOffset.y());
    t.scale(view.zoom, view.zoom);
    return t;
}

QRectF RenderEngine::mapToViewport(const QRectF& rect,
                                   const ViewportState& view) const {
    return createViewTransform(view).mapRect(rect);
}

PageCache* RenderEngine::getPageCache(ID pageId) {
    return m_pageCache.object(pageId);
}

ElementCache* RenderEngine::getElementCache(ID elementId) {
    return m_elementCache.object(elementId);
}

void RenderEngine::updatePageCache(ID pageId, const QPixmap& pixmap,
                                   const QRectF& rect, double zoom) {
    auto* cache = new PageCache();
    cache->cachedPixmap = pixmap;
    cache->cachedRect   = rect;
    cache->cachedZoom   = zoom;
    cache->isValid      = true;
    m_pageCache.insert(pageId, cache);
}

void RenderEngine::updateElementCache(ID elementId, const QPixmap& pixmap,
                                      const QRectF& bounds, double zoom) {
    auto* cache = new ElementCache();
    cache->cachedPixmap = pixmap;
    cache->bounds       = bounds;
    cache->cachedZoom   = zoom;
    cache->isValid      = true;
    m_elementCache.insert(elementId, cache);
}

// -----------------------------------------------------------------------
// Selection / editing handles / link indicators
// -----------------------------------------------------------------------
void RenderEngine::renderSelection(QPainter* painter,
                                   const Element* element,
                                   const ViewportState& view) {
    if (!element || !element->bounds) return;

    painter->save();
    QRectF bounds = toQRectF(*element->bounds);
    painter->setPen(QPen(Qt::blue, 2.0));
    painter->setBrush(QColor(0, 120, 255, 40));
    painter->drawRect(bounds);

    const double hs = 8.0;
    QRectF handles[4] = {
                         { bounds.left()  - hs/2, bounds.top()    - hs/2, hs, hs },
                         { bounds.right() - hs/2, bounds.top()    - hs/2, hs, hs },
                         { bounds.left()  - hs/2, bounds.bottom() - hs/2, hs, hs },
                         { bounds.right() - hs/2, bounds.bottom() - hs/2, hs, hs },
                         };
    painter->setBrush(Qt::white);
    painter->setPen(QPen(Qt::blue, 1.0));
    for (const auto& h : handles) painter->drawRect(h);

    painter->restore();
}

void RenderEngine::renderEditingHandles(QPainter* painter,
                                        const Element* element,
                                        const ViewportState& view) {
    // TODO: text cursor / handles
}

void RenderEngine::renderLinkIndicators(QPainter* painter,
                                        const Element* element,
                                        const ViewportState& view) {
    // TODO: link underline overlay
}

// -----------------------------------------------------------------------
// RenderHelpers
// -----------------------------------------------------------------------
namespace RenderHelpers {

QTextCharFormat toQTextCharFormat(const CharacterProperties& props) {
    QTextCharFormat fmt;

    if (!props.fontFamily.isEmpty())
        fmt.setFontFamilies({props.fontFamily});

    if (props.fontSize > 0)
        fmt.setFontPointSize(props.fontSize);

    fmt.setForeground(toQBrush(props.color));

    if (props.bold)          fmt.setFontWeight(QFont::Bold);
    if (props.italic)        fmt.setFontItalic(true);
    if (props.underline)     fmt.setFontUnderline(true);
    if (props.strikethrough) fmt.setFontStrikeOut(true);
    if (props.superscript)   fmt.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    if (props.subscript)     fmt.setVerticalAlignment(QTextCharFormat::AlignSubScript);

    if (props.highlightColor)
        fmt.setBackground(toQBrush(*props.highlightColor));

    return fmt;
}

QTextBlockFormat toQTextBlockFormat(const ParagraphProperties& props) {
    QTextBlockFormat fmt;

    switch (props.alignment) {
    case Alignment::Left:      fmt.setAlignment(Qt::AlignLeft);    break;
    case Alignment::Center:    fmt.setAlignment(Qt::AlignHCenter); break;
    case Alignment::Right:     fmt.setAlignment(Qt::AlignRight);   break;
    case Alignment::Justify:   fmt.setAlignment(Qt::AlignJustify); break;
    default:                   fmt.setAlignment(Qt::AlignLeft);    break;
    }

    fmt.setTopMargin(props.spaceBefore);
    fmt.setBottomMargin(props.spaceAfter);
    fmt.setLeftMargin(props.leftIndent);
    fmt.setRightMargin(props.rightIndent);
    fmt.setTextIndent(props.firstLineIndent);

    return fmt;
}

} // namespace RenderHelpers

} // namespace UDoc
