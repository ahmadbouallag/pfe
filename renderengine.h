#ifndef RENDERENGINE_H
#define RENDERENGINE_H

#include "document.h"
#include <QPainter>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QTextLayout>

namespace UDoc {

// === VIEWPORT STATE ===
struct ViewportState {
    // Scroll position (in points, unzoomed)
    double scrollX = 0;
    double scrollY = 0;

    // Zoom: 1.0 = 100%, 0.5 = 50%, 2.0 = 200%
    double zoom = 1.0;

    // Visible page range (computed)
    PageIndex firstVisiblePage = 0;
    PageIndex lastVisiblePage = 0;

    // Page spacing in screen pixels
    int pageSpacing = 20;

    // Screen dimensions
    int screenWidth = 0;
    int screenHeight = 0;

    // Convert screen pixel to document point
    Point screenToDoc(int screenX, int screenY) const;

    // Convert document point to screen pixel
    QPoint docToScreen(const Point& docPoint) const;

    // Which page contains this point?
    PageIndex pageAtPoint(const Point& docPoint) const;
};

// === RENDER ENGINE ===
class RenderEngine : public QObject {
    Q_OBJECT

public:
    explicit RenderEngine(QObject* parent = nullptr);
    ~RenderEngine();

    // Main render entry point (called from paintEvent)
    void renderViewport(QPainter* painter, const ViewportState& view, Document* doc);

    // Cache management
    void invalidatePage(PageIndex idx);
    void invalidateElement(ID elemId);
    void invalidateAll();

    // Background rendering
    void startBackgroundThread();
    void stopBackgroundThread();
    void prefetchPages(PageIndex start, PageIndex count);

    // Settings
    void setAntialiasing(bool enabled) { m_antialiasing = enabled; }
    void setImageInterpolation(int quality) { m_imageInterpolation = quality; }

signals:
    void pageRendered(PageIndex idx);  // Emitted when background finishes a page
    void renderProgress(int percent);

private:
    // Page rendering (can be called from main or background thread)
    void renderPageToCache(Page* page, double zoom);

    // Element rendering
    void renderElement(QPainter* painter, const Element* elem, const Page* page);
    void renderText(QPainter* painter, const TextElement* text, const Rect& bounds);
    void renderImage(QPainter* painter, const ImageElement* img, const Rect& bounds);
    void renderVector(QPainter* painter, const VectorElement* vec, const Rect& bounds);
    void renderTable(QPainter* painter, const TableElement* table, const Rect& bounds);

    // Text layout (uses QTextLayout internally)
    void layoutText(const TextElement* text, double maxWidth,
                    std::vector<QTextLayout>* layouts) const;

    // Threading
    class BackgroundThread;
    std::unique_ptr<BackgroundThread> m_bgThread;
    QMutex m_cacheMutex;  // Protects page caches during background writes

    // Settings
    bool m_antialiasing = true;
    int m_imageInterpolation = 1;  // 0=nearest, 1=bilinear, 2=bicubic

    // Statistics
    uint64_t m_cacheGeneration = 0;
};

} // namespace UDoc

#endif // RENDERENGINE_H
