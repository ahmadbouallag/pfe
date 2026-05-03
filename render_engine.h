#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H

#include "udoc_types.h"
#include "tab.h"
#include "page.h"
#include "element.h"
#include <QPainter>
#include <QRectF>
#include <QTransform>
#include <QCache>
#include <QMutex>
#include <QTextLayout>
#include <QTextFormat>
#include <QPainterPath>
#include <memory>
#include <thread>
#include <queue>

namespace UDoc {

// Forward declarations
class Document;
class PageSequence;
class SlideSequence;
class GridSheet;
class FlowDocument;
class InfiniteCanvas;
class TextBlockContent;
class HeadingContent;
class ListContent;
class Table;
class Image;
class VectorGraphic;
class Media;
class Formula;
class Link;
class Bookmark;
class Field;
class Comment;
class Group;

// === VIEWPORT STATE ===
struct ViewportState {
    ID activeTabId;
    double zoom = 1.0;
    QPointF scrollOffset;   // screen pixels already applied
    QRectF visibleRect;
    double devicePixelRatio = 1.0;
    bool antialiasing = true;
    bool textAntialiasing = true;
    bool smoothPixmapTransform = true;
};

// === RENDER CACHE ===
struct PageCache {
    QPixmap cachedPixmap;
    QRectF cachedRect;
    double cachedZoom = 1.0;
    QDateTime cacheTime;
    bool isValid = false;

    bool isValidFor(const QRectF& rect, double zoom) const {
        return isValid &&
               qFuzzyCompare(cachedZoom, zoom) &&
               cachedRect.contains(rect);
    }
};

struct ElementCache {
    QPixmap cachedPixmap;
    QRectF bounds;
    double cachedZoom = 1.0;
    QDateTime cacheTime;
    bool isValid = false;

    bool isValidFor(const QRectF& rect, double zoom) const {
        return isValid &&
               qFuzzyCompare(cachedZoom, zoom) &&
               bounds.contains(rect);
    }
};

// === BACKGROUND RENDER THREAD ===
class BackgroundRenderThread : public QObject {
    Q_OBJECT
public:
    explicit BackgroundRenderThread(QObject* parent = nullptr);
    ~BackgroundRenderThread();

    void renderPageAsync(ID pageId, const Page* page, const ViewportState& view);
    void renderElementAsync(ID elementId, const Element* element, const ViewportState& view);
    void stop();

signals:
    void pageRendered(ID pageId, const QPixmap& pixmap);
    void elementRendered(ID elementId, const QPixmap& pixmap);

private slots:
    void processRenderQueue();

private:
    struct RenderTask {
        enum Type { Page, Element } type;
        ID id;
        const void* object;
        ViewportState view;
    };
    std::queue<RenderTask> m_renderQueue;
    QMutex m_queueMutex;
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
};

// === MAIN RENDER ENGINE ===
class RenderEngine {
public:
    explicit RenderEngine();
    ~RenderEngine();

    void renderViewport(QPainter* painter, const ViewportState& view, Document* doc);

    void renderPageSequence(QPainter* painter, const PageSequence& pages, const ViewportState& view);
    void renderSlideSequence(QPainter* painter, const SlideSequence& slides, const ViewportState& view);
    void renderGridSheet(QPainter* painter, const GridSheet& sheet, const ViewportState& view);
    void renderFlowDocument(QPainter* painter, const FlowDocument& flow, const ViewportState& view);
    void renderInfiniteCanvas(QPainter* painter, const InfiniteCanvas& canvas, const ViewportState& view);

    void renderElement(QPainter* painter, const Element* element, const ViewportState& view);
    void renderTextBlock(QPainter* painter, const TextBlockContent* text, const QRectF& bounds);
    void renderHeading(QPainter* painter, const HeadingContent* heading, const QRectF& bounds);
    void renderList(QPainter* painter, const ListContent* list, const QRectF& bounds);
    void renderTable(QPainter* painter, const Table* table, const QRectF& bounds);
    void renderImage(QPainter* painter, const Image* image, const QRectF& bounds);
    void renderVector(QPainter* painter, const VectorGraphic* vector, const QRectF& bounds);
    void renderLink(QPainter* painter, const Link* link, const QRectF& bounds);
    void renderField(QPainter* painter, const Field* field, const QRectF& bounds);
    void renderComment(QPainter* painter, const Comment* comment, const QRectF& bounds);

    std::unique_ptr<QTextLayout> layoutTextBlock(const TextBlockContent* text, double maxWidth);

    void invalidatePage(ID pageId);
    void invalidateElement(ID elementId);
    void invalidateAll();
    void clearCache();
    void setMaxCacheSize(int megabytes);
    int cacheSize() const;
    void enableBackgroundRendering(bool enable);
    bool isBackgroundRenderingEnabled() const;

    struct RenderStats {
        int pagesRendered = 0;
        int elementsRendered = 0;
        int cacheHits = 0;
        int cacheMisses = 0;
        double averageRenderTime = 0.0;
        double totalRenderTime = 0.0;
    };
    const RenderStats& stats() const { return m_stats; }
    void resetStats();

private:
    void setupPainter(QPainter* painter, const ViewportState& view);
    QTransform createViewTransform(const ViewportState& view) const;
    QRectF mapToViewport(const QRectF& rect, const ViewportState& view) const;

    PageCache* getPageCache(ID pageId);
    ElementCache* getElementCache(ID elementId);
    void updatePageCache(ID pageId, const QPixmap& pixmap, const QRectF& rect, double zoom);
    void updateElementCache(ID elementId, const QPixmap& pixmap, const QRectF& bounds, double zoom);

    void renderSelection(QPainter* painter, const Element* element, const ViewportState& view);
    void renderEditingHandles(QPainter* painter, const Element* element, const ViewportState& view);
    void renderLinkIndicators(QPainter* painter, const Element* element, const ViewportState& view);

    QCache<ID, PageCache> m_pageCache;
    QCache<ID, ElementCache> m_elementCache;
    QMutex m_cacheMutex;
    std::unique_ptr<BackgroundRenderThread> m_bgThread;
    bool m_backgroundRendering = true;
    RenderStats m_stats;
    QMutex m_statsMutex;
    int m_maxCacheSizeMB = 100;
};

// === FREE HELPER ===
QRectF toQRectF(const Rect& rect);

// === RENDER HELPERS ===
namespace RenderHelpers {

inline QPointF documentToScreen(const QPointF& docPoint, const ViewportState& view) {
    return (docPoint - view.scrollOffset) * view.zoom;
}

inline QRectF documentToScreen(const QRectF& docRect, const ViewportState& view) {
    return QRectF(documentToScreen(docRect.topLeft(), view),
                  documentToScreen(docRect.bottomRight(), view));
}

inline QPointF screenToDocument(const QPointF& screenPoint, const ViewportState& view) {
    return screenPoint / view.zoom + view.scrollOffset;
}

// --- Color ---
inline QColor toQColor(const Color& color) {
    return QColor::fromRgbF(
        std::clamp(color.r, 0.0, 1.0),
        std::clamp(color.g, 0.0, 1.0),
        std::clamp(color.b, 0.0, 1.0),
        std::clamp(color.a, 0.0, 1.0));
}

inline QBrush toQBrush(const Color& color, Qt::BrushStyle style = Qt::SolidPattern) {
    return QBrush(toQColor(color), style);
}

inline QPen toQPen(const Color& color, double width, Qt::PenStyle style = Qt::SolidLine) {
    return QPen(toQColor(color), width, style);
}

// --- Text formatting (implemented in render_engine.cpp) ---
QTextCharFormat toQTextCharFormat(const CharacterProperties& props);
QTextBlockFormat toQTextBlockFormat(const ParagraphProperties& props);

// --- Transform ---
// UDoc TransformMatrix convention (same as SVG/CSS):
//   x' = a*x + c*y + e
//   y' = b*x + d*y + f
//
// QTransform(m11, m12, m21, m22, dx, dy) produces:
//   x' = m11*x + m21*y + dx
//   y' = m12*x + m22*y + dy
//
// Therefore: m11=a, m12=b, m21=c, m22=d, dx=e, dy=f
inline QTransform toQTransform(const TransformMatrix& m) {
    return QTransform(m.a, m.b,   // m11, m12
                      m.c, m.d,   // m21, m22
                      m.e, m.f);  // dx,  dy
}

// --- Path commands → QPainterPath ---
inline QPainterPath toQPainterPath(const std::vector<PathCommand>& commands) {
    QPainterPath path;
    for (const auto& cmd : commands) {
        switch (cmd.type) {
        case PathCommandType::MoveTo:
            path.moveTo(cmd.x1, cmd.y1);
            break;
        case PathCommandType::LineTo:
            path.lineTo(cmd.x1, cmd.y1);
            break;
        case PathCommandType::CubicTo:
            // cx1,cy1 = first control point
            // cx2,cy2 = second control point
            // x1,y1  = end point
            path.cubicTo(cmd.cx1, cmd.cy1,
                         cmd.cx2, cmd.cy2,
                         cmd.x1,  cmd.y1);
            break;
        case PathCommandType::QuadTo:
            path.quadTo(cmd.cx1, cmd.cy1,
                        cmd.x1,  cmd.y1);
            break;
        case PathCommandType::ArcTo:
            // QPainterPath doesn't have a direct SVG arcTo;
            // approximate with arcMoveTo + arcTo
            path.arcTo(cmd.x1 - cmd.rx, cmd.y1 - cmd.ry,
                       cmd.rx * 2,      cmd.ry * 2,
                       cmd.rotation,    cmd.sweep ? 90.0 : -90.0);
            break;
        case PathCommandType::ClosePath:
            path.closeSubpath();
            break;
        }
    }
    return path;
}

// --- Clipping ---
inline void applyClipPath(QPainter* painter,
                          const std::vector<PathCommand>& clipPath,
                          const QTransform& transform) {
    if (clipPath.empty()) return;
    QPainterPath path = toQPainterPath(clipPath);
    painter->setClipPath(transform.map(path), Qt::IntersectClip);
}

} // namespace RenderHelpers

} // namespace UDoc

#endif // RENDER_ENGINE_H
