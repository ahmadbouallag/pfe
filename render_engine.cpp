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
#include <QFontMetricsF>
#include <algorithm>

namespace UDoc {

QRectF toQRectF(const Rect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

BackgroundRenderThread::BackgroundRenderThread(QObject* parent) : QObject(parent) {}
BackgroundRenderThread::~BackgroundRenderThread() {}
void BackgroundRenderThread::processRenderQueue() {}

RenderEngine::RenderEngine() {}
RenderEngine::~RenderEngine() {}

// -----------------------------------------------------------------------
// renderViewport
// -----------------------------------------------------------------------
void RenderEngine::renderViewport(QPainter* painter,
                                  const ViewportState& view,
                                  Document* doc) {
    if (!doc || !painter) return;
    painter->fillRect(view.visibleRect, QColor(180, 180, 180));

    Tab* tab = doc->findTab(doc->activeTabId);
    if (!tab) {
        if (doc->tabs.empty()) return;
        tab = doc->tabs[0].get();
    }

    if      (auto* ps = tab->pageSequence())   renderPageSequence(painter, *ps, view);
    else if (auto* ss = tab->slideSequence())  renderSlideSequence(painter, *ss, view);
    else if (auto* gs = tab->gridSheet())      renderGridSheet(painter, *gs, view);
    else if (auto* fd = tab->flowDocument())   renderFlowDocument(painter, *fd, view);
    else if (auto* ic = tab->infiniteCanvas()) renderInfiniteCanvas(painter, *ic, view);
}

// -----------------------------------------------------------------------
// renderPageSequence
// -----------------------------------------------------------------------
void RenderEngine::renderPageSequence(QPainter* painter,
                                      const PageSequence& pages,
                                      const ViewportState& view) {
    const double zoom      = view.zoom;
    const double pagePad   = 20.0;
    const double shadowOff = 4.0;
    const QRectF visibleRect = view.visibleRect;

    painter->save();
    double screenY = pagePad - view.scrollOffset.y();

    for (const auto& page : pages.pages) {
        const double pageW  = page->width  * zoom;
        const double pageH  = page->height * zoom;
        const double screenX = (visibleRect.width() - pageW) / 2.0 - view.scrollOffset.x();

        QRectF screenPageRect(screenX, screenY, pageW, pageH);

        if (!visibleRect.intersects(
                screenPageRect.adjusted(-shadowOff-1, -shadowOff-1,
                                        shadowOff+1,  shadowOff+1))) {
            screenY += pageH + pagePad;
            continue;
        }

        // Drop shadow
        painter->fillRect(screenPageRect.translated(shadowOff, shadowOff),
                          QColor(0, 0, 0, 60));
        // Page background
        painter->fillRect(screenPageRect,
                          RenderHelpers::toQColor(page->backgroundColor));
        // Page border
        painter->setPen(QPen(QColor(160, 160, 160), 1.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(screenPageRect);

        PageCache* cache = getPageCache(page->id());
        // Don't use the page cache when any element is in editing mode —
        // the cursor needs to blink and can't be cached.
        bool anyEditing = false;
        for (const auto& e : page->elements) if (e->editing) { anyEditing = true; break; }

        if (!anyEditing && cache && cache->isValidFor(screenPageRect, zoom)) {
            painter->drawPixmap(screenPageRect.topLeft(), cache->cachedPixmap);
            m_stats.cacheHits++;
        } else {
            QSize pixSize(static_cast<int>(std::ceil(pageW)),
                          static_cast<int>(std::ceil(pageH)));
            if (pixSize.isEmpty()) { screenY += pageH + pagePad; continue; }

            QPixmap pagePixmap(pixSize);
            pagePixmap.fill(Qt::transparent);
            {
                QPainter pagePainter(&pagePixmap);
                pagePainter.setRenderHint(QPainter::Antialiasing);
                pagePainter.setRenderHint(QPainter::TextAntialiasing);
                pagePainter.setRenderHint(QPainter::SmoothPixmapTransform);
                pagePainter.scale(zoom, zoom);

                if (page->backgroundImage)
                    renderImage(&pagePainter, &(*page->backgroundImage),
                                QRectF(0, 0, page->width, page->height));

                for (const auto& element : page->elements)
                    renderElement(&pagePainter, element.get(), view);

                pagePainter.end();
            }

            if (!anyEditing)
                updatePageCache(page->id(), pagePixmap, screenPageRect, zoom);
            painter->drawPixmap(screenPageRect.topLeft(), pagePixmap);
            m_stats.cacheMisses++;
            m_stats.pagesRendered++;
        }

        screenY += pageH + pagePad;
    }
    painter->restore();
}

void RenderEngine::renderSlideSequence(QPainter*, const SlideSequence&, const ViewportState&) {}
void RenderEngine::renderGridSheet(QPainter*, const GridSheet&, const ViewportState&) {}
void RenderEngine::renderFlowDocument(QPainter*, const FlowDocument&, const ViewportState&) {}
void RenderEngine::renderInfiniteCanvas(QPainter*, const InfiniteCanvas&, const ViewportState&) {}

// -----------------------------------------------------------------------
// renderElement
// -----------------------------------------------------------------------
void RenderEngine::renderElement(QPainter* painter, const Element* element,
                                 const ViewportState& view) {
    if (!element || !element->visible) return;
    painter->save();

    QRectF bounds = element->bounds ? toQRectF(*element->bounds) : QRectF(0,0,100,20);

    if (element->transform)
        painter->setTransform(RenderHelpers::toQTransform(*element->transform), true);
    if (element->clipPath)
        RenderHelpers::applyClipPath(painter, *element->clipPath, painter->transform());

    painter->setOpacity(element->opacity);

    std::visit([&](auto&& content) {
        using T = std::decay_t<decltype(content)>;
        if constexpr      (std::is_same_v<T, TextBlockContent>)
            renderTextBlock(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, HeadingContent>)
            renderHeading(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, ListContent>)
            renderList(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, std::unique_ptr<Table>>)
            renderTable(painter, content.get(), bounds);
        else if constexpr (std::is_same_v<T, Image>)
            renderImage(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, VectorGraphic>)
            renderVector(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, Link>)
            renderLink(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, Field>)
            renderField(painter, &content, bounds);
        else if constexpr (std::is_same_v<T, Comment>)
            renderComment(painter, &content, bounds);
    }, element->content);

    if (element->selected)    renderSelection(painter, element, view);
    if (element->editing)     renderEditingHandles(painter, element, view);
    if (element->linkTarget)  renderLinkIndicators(painter, element, view);

    painter->restore();
    m_stats.elementsRendered++;
}

// -----------------------------------------------------------------------
// buildBaseFont — derive a QFont from a TextBlockContent's first run.
// This is the key fix for text size/family being wrong: the font must
// be set on the QTextLayout before beginLayout() or Qt uses its default.
// -----------------------------------------------------------------------
static QFont buildBaseFont(const TextBlockContent* text) {
    QFont f;
    if (text && !text->runs.empty()) {
        const CharacterProperties& p = text->runs[0].props;
        if (!p.fontFamily.isEmpty()) f.setFamily(p.fontFamily);
        if (p.fontSize > 0.0)        f.setPointSizeF(p.fontSize);
        f.setBold(p.bold);
        f.setItalic(p.italic);
    }
    return f;
}

// -----------------------------------------------------------------------
// renderTextBlock
// -----------------------------------------------------------------------
void RenderEngine::renderTextBlock(QPainter* painter,
                                   const TextBlockContent* text,
                                   const QRectF& bounds) {
    if (!text || text->text.isEmpty()) return;
    auto layout = layoutTextBlock(text, bounds.width(), buildBaseFont(text));
    if (!layout) return;
    layout->draw(painter, bounds.topLeft());
}

// -----------------------------------------------------------------------
// renderHeading — delegates to renderTextBlock.
// The heading's actual font size is stored in CharacterProperties on the
// run, so no hardcoded sizes needed.
// -----------------------------------------------------------------------
void RenderEngine::renderHeading(QPainter* painter,
                                 const HeadingContent* heading,
                                 const QRectF& bounds) {
    if (!heading) return;
    renderTextBlock(painter, heading, bounds);
}

// -----------------------------------------------------------------------
// renderList
// -----------------------------------------------------------------------
void RenderEngine::renderList(QPainter* painter,
                              const ListContent* list,
                              const QRectF& bounds) {
    if (!list) return;
    painter->save();
    painter->setPen(QPen(Qt::black, 1.0));

    const double lineHeight = 22.0;
    const double bulletColW = 18.0;
    double y = bounds.top();

    for (size_t i = 0; i < list->items.size(); ++i) {
        const auto& item = list->items[i];
        QString marker;
        if (list->type == ListType::Numbered)
            marker = QString(list->numberFormat).arg((int)(list->startNumber + i));
        else
            marker = list->bulletChar;
        painter->drawText(QPointF(bounds.left(), y + lineHeight - 4.0), marker);

        if (item.content) {
            QRectF itemBounds(bounds.left() + bulletColW, y,
                              bounds.width() - bulletColW, lineHeight);
            if (auto* tc = item.content->textContent())
                renderTextBlock(painter, tc, itemBounds);
            else if (auto* hc = item.content->headingContent())
                renderHeading(painter, hc, itemBounds);
        }
        y += lineHeight;
    }
    painter->restore();
}

void RenderEngine::renderTable(QPainter*, const Table*, const QRectF&) {}

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
        if (image->flipHorizontal || image->flipVertical)
            painter->scale(image->flipHorizontal ? -1.0 : 1.0,
                           image->flipVertical   ? -1.0 : 1.0);
        painter->drawImage(bounds, decoded);
        painter->restore();
    }
    painter->restore();
}

// -----------------------------------------------------------------------
// renderVector — full QPainterPath rendering with fill and stroke
// -----------------------------------------------------------------------
void RenderEngine::renderVector(QPainter* painter,
                                const VectorGraphic* vector,
                                const QRectF& bounds) {
    if (!vector || vector->commands.empty()) return;
    painter->save();

    // Path coordinates are element-local (translated to 0,0 by the parser).
    // Translate painter so they land at the element's page position.
    painter->translate(bounds.topLeft());

    QPainterPath path = RenderHelpers::toQPainterPath(vector->commands);

    if (vector->fill) {
        const Fill& fill = *vector->fill;
        painter->setOpacity(fill.opacity * painter->opacity());
        if (std::holds_alternative<Color>(fill.content)) {
            painter->fillPath(path,
                QBrush(RenderHelpers::toQColor(std::get<Color>(fill.content))));
        } else if (std::holds_alternative<Gradient>(fill.content)) {
            const Gradient& g = std::get<Gradient>(fill.content);
            if (g.type == Gradient::Linear) {
                QLinearGradient lg(g.start.x, g.start.y, g.end.x, g.end.y);
                for (auto& s : g.stops) lg.setColorAt(s.position, RenderHelpers::toQColor(s.color));
                painter->fillPath(path, QBrush(lg));
            } else {
                QRadialGradient rg(g.center.x, g.center.y, g.radius);
                for (auto& s : g.stops) rg.setColorAt(s.position, RenderHelpers::toQColor(s.color));
                painter->fillPath(path, QBrush(rg));
            }
        }
        painter->setOpacity(painter->opacity() / std::max(fill.opacity, 0.001)); // restore
    }

    if (vector->stroke) {
        const Stroke& stroke = *vector->stroke;
        QPen pen(RenderHelpers::toQColor(stroke.color), stroke.width);
        switch (stroke.cap) {
        case Stroke::CapRound:  pen.setCapStyle(Qt::RoundCap);  break;
        case Stroke::CapSquare: pen.setCapStyle(Qt::SquareCap); break;
        default:                pen.setCapStyle(Qt::FlatCap);   break;
        }
        switch (stroke.join) {
        case Stroke::JoinRound: pen.setJoinStyle(Qt::RoundJoin); break;
        case Stroke::JoinBevel: pen.setJoinStyle(Qt::BevelJoin); break;
        default:
            pen.setJoinStyle(Qt::MiterJoin);
            pen.setMiterLimit(stroke.miterLimit);
        }
        if (!stroke.dashArray.empty()) {
            QVector<qreal> dashes;
            for (double d : stroke.dashArray)
                dashes.append(d / std::max(stroke.width, 0.001));
            pen.setDashPattern(dashes);
            pen.setDashOffset(stroke.dashOffset / std::max(stroke.width, 0.001));
        }
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    painter->restore();
}

void RenderEngine::renderLink(QPainter* painter, const Link* link, const QRectF& bounds) {
    if (!link) return;
    painter->save();
    painter->setPen(QPen(RenderHelpers::toQColor(link->color), 1.0));
    painter->drawLine(QPointF(bounds.left(), bounds.bottom()-1.0),
                      QPointF(bounds.right(), bounds.bottom()-1.0));
    painter->restore();
}

void RenderEngine::renderField(QPainter*, const Field*, const QRectF&) {}
void RenderEngine::renderComment(QPainter*, const Comment*, const QRectF&) {}

// -----------------------------------------------------------------------
// layoutTextBlock — two overloads
//
// The overload that takes an explicit QFont is the real implementation.
// The simple overload derives the font from the first run and calls it.
//
// KEY FIX: layout->setFont(baseFont) before beginLayout() sets the
// reference font. Qt's format ranges are DELTAS from this base, so if
// the base is wrong (Qt's default 9pt) all sizes come out wrong.
// -----------------------------------------------------------------------
std::unique_ptr<QTextLayout> RenderEngine::layoutTextBlock(
    const TextBlockContent* text, double maxWidth, const QFont& baseFont) {
    if (!text || text->text.isEmpty()) return nullptr;

    auto layout = std::make_unique<QTextLayout>();
    layout->setText(text->text);
    layout->setFont(baseFont);          // ← THE FIX

    QVector<QTextLayout::FormatRange> formats;
    for (const auto& run : text->runs) {
        QTextLayout::FormatRange fr;
        fr.start  = static_cast<int>(run.start);
        fr.length = static_cast<int>(run.length);
        fr.format = RenderHelpers::toQTextCharFormat(run.props);
        formats.append(fr);
    }
    layout->setFormats(formats);

    QTextOption option;
    option.setAlignment(Qt::AlignLeft);
    option.setWrapMode(QTextOption::WordWrap);
    layout->setTextOption(option);

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

std::unique_ptr<QTextLayout> RenderEngine::layoutTextBlock(
    const TextBlockContent* text, double maxWidth) {
    return layoutTextBlock(text, maxWidth, buildBaseFont(text));
}

// -----------------------------------------------------------------------
// Cache management
// -----------------------------------------------------------------------
void RenderEngine::invalidatePage(ID id)    { m_pageCache.remove(id); }
void RenderEngine::invalidateElement(ID id)  { m_elementCache.remove(id); }
void RenderEngine::invalidateAll()           { m_pageCache.clear(); m_elementCache.clear(); }
void RenderEngine::clearCache()              { m_pageCache.clear(); m_elementCache.clear(); }
void RenderEngine::setMaxCacheSize(int mb)   { m_maxCacheSizeMB = mb; }
int  RenderEngine::cacheSize() const         { return m_maxCacheSizeMB; }
void RenderEngine::enableBackgroundRendering(bool e) { m_backgroundRendering = e; }
bool RenderEngine::isBackgroundRenderingEnabled() const { return m_backgroundRendering; }
void RenderEngine::resetStats()              { m_stats = RenderStats(); }
void RenderEngine::setupPainter(QPainter* p, const ViewportState&) {
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
}
QTransform RenderEngine::createViewTransform(const ViewportState& view) const {
    QTransform t;
    t.translate(-view.scrollOffset.x(), -view.scrollOffset.y());
    t.scale(view.zoom, view.zoom);
    return t;
}
QRectF RenderEngine::mapToViewport(const QRectF& rect, const ViewportState& view) const {
    return createViewTransform(view).mapRect(rect);
}
PageCache*    RenderEngine::getPageCache(ID id)    { return m_pageCache.object(id); }
ElementCache* RenderEngine::getElementCache(ID id) { return m_elementCache.object(id); }

void RenderEngine::updatePageCache(ID id, const QPixmap& px, const QRectF& r, double z) {
    auto* c = new PageCache();
    c->cachedPixmap = px; c->cachedRect = r; c->cachedZoom = z; c->isValid = true;
    m_pageCache.insert(id, c);
}
void RenderEngine::updateElementCache(ID id, const QPixmap& px, const QRectF& b, double z) {
    auto* c = new ElementCache();
    c->cachedPixmap = px; c->bounds = b; c->cachedZoom = z; c->isValid = true;
    m_elementCache.insert(id, c);
}

// -----------------------------------------------------------------------
// renderSelection — element bounding box + resize handles
// -----------------------------------------------------------------------
void RenderEngine::renderSelection(QPainter* painter,
                                   const Element* element,
                                   const ViewportState&) {
    if (!element || !element->bounds) return;
    painter->save();

    QRectF bounds = toQRectF(*element->bounds);
    QPen outlinePen(QColor(30, 120, 255));
    outlinePen.setCosmetic(true);
    painter->setPen(outlinePen);
    painter->setBrush(QColor(30, 120, 255, 25));
    painter->drawRect(bounds);

    const double hs = 6.0;
    const QRectF handles[4] = {
        { bounds.left(),       bounds.top(),         hs, hs },
        { bounds.right()-hs,   bounds.top(),         hs, hs },
        { bounds.left(),       bounds.bottom()-hs,   hs, hs },
        { bounds.right()-hs,   bounds.bottom()-hs,   hs, hs },
    };
    QPen hp(QColor(30, 120, 255)); hp.setCosmetic(true);
    painter->setBrush(Qt::white); painter->setPen(hp);
    for (const auto& h : handles) painter->drawRect(h);

    painter->restore();
}

// -----------------------------------------------------------------------
// renderEditingHandles — text cursor + selection highlight
// Reads cursor/selection state from TextLayoutCache set by the input handler.
// -----------------------------------------------------------------------
void RenderEngine::renderEditingHandles(QPainter* painter,
                                        const Element* element,
                                        const ViewportState&) {
    if (!element || !element->bounds) return;

    // const_cast is safe here — renderEditingHandles only reads the content,
    // never mutates it. The accessors just aren't marked const in element.h.
    Element* mutElem = const_cast<Element*>(element);
    const TextBlockContent* tc = nullptr;
    if (mutElem->isTextBlock())    tc = mutElem->textContent();
    else if (mutElem->isHeading()) tc = mutElem->headingContent();
    if (!tc || tc->text.isEmpty()) return;

    QRectF bounds = toQRectF(*element->bounds);
    QFont  base   = buildBaseFont(tc);

    auto layout = layoutTextBlock(tc, bounds.width(), base);
    if (!layout) return;

    painter->save();

    const TextLayoutCache& cache = tc->layoutCache;

    // ---- Selection highlight ----
    if (cache.hasSelection && cache.selStart < cache.selEnd) {
        uint32_t selS = cache.selStart;
        uint32_t selE = std::min(cache.selEnd, (uint32_t)tc->text.length());

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(51, 153, 255, 80));

        for (int li = 0; li < layout->lineCount(); ++li) {
            QTextLine line = layout->lineAt(li);
            int lStart = line.textStart();
            int lEnd   = lStart + line.textLength();
            int s = std::max((int)selS, lStart);
            int e = std::min((int)selE, lEnd);
            if (s < e) {
                double x1 = line.cursorToX(s);
                double x2 = line.cursorToX(e);
                painter->drawRect(QRectF(
                    bounds.left() + x1,
                    bounds.top()  + line.y(),
                    x2 - x1,
                    line.height()));
            }
        }
    }

    // ---- Text cursor (blinking caret) ----
    if (cache.cursorVisible) {
        int cursorIdx = (int)std::min(cache.cursorPos, (uint32_t)tc->text.length());
        double cursorX = 0, cursorY = 0, cursorH = 16;

        // Find the line containing the cursor
        for (int li = 0; li < layout->lineCount(); ++li) {
            QTextLine line = layout->lineAt(li);
            if (cursorIdx >= line.textStart() &&
                cursorIdx <= line.textStart() + line.textLength()) {
                cursorX = line.cursorToX(cursorIdx);
                cursorY = line.y();
                cursorH = line.height();
                break;
            }
        }

        QPen cursorPen(QColor(20, 20, 20));
        cursorPen.setCosmetic(true);
        cursorPen.setWidth(2);
        painter->setPen(cursorPen);
        painter->drawLine(
            QPointF(bounds.left() + cursorX, bounds.top() + cursorY),
            QPointF(bounds.left() + cursorX, bounds.top() + cursorY + cursorH));
    }

    // ---- Thin blue editing border ----
    {
        QPen ep(QColor(30, 120, 255, 180));
        ep.setCosmetic(true);
        painter->setPen(ep);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(bounds);
    }

    painter->restore();
}

void RenderEngine::renderLinkIndicators(QPainter*, const Element*, const ViewportState&) {}

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
    case Alignment::Center:  fmt.setAlignment(Qt::AlignHCenter); break;
    case Alignment::Right:   fmt.setAlignment(Qt::AlignRight);   break;
    case Alignment::Justify: fmt.setAlignment(Qt::AlignJustify); break;
    default:                 fmt.setAlignment(Qt::AlignLeft);
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
