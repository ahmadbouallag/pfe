#include "renderengine.h"
#include <QtMath>
#include <QThread>
#include <QPainterPath>
#include <deque>

namespace UDoc {

// === BACKGROUND THREAD ===
class RenderEngine::BackgroundThread : public QThread {
    RenderEngine* m_engine;
    std::atomic<bool> m_running{true};
    std::deque<PageIndex> m_queue;
    QMutex m_queueMutex;

public:
    explicit BackgroundThread(RenderEngine* engine) : m_engine(engine) {}

    void enqueue(PageIndex idx) {
        QMutexLocker lock(&m_queueMutex);
        if (std::find(m_queue.begin(), m_queue.end(), idx) == m_queue.end()) {
            m_queue.push_back(idx);
        }
    }

    void stop() {
        m_running = false;
        QThread::quit();
        QThread::wait();
    }

protected:
    void run() override {
        while (m_running) {
            PageIndex idx;
            {
                QMutexLocker lock(&m_queueMutex);
                if (m_queue.empty()) {
                    lock.unlock();
                    msleep(10);
                    continue;
                }
                idx = m_queue.front();
                m_queue.pop_front();
            }

            // Render page (implementation needed)
            // m_engine->renderPageBackground(idx);

            emit m_engine->pageRendered(idx);
        }
    }
};

// === VIEWPORT STATE ===
Point ViewportState::screenToDoc(int screenX, int screenY) const {
    double docX = (screenX + scrollX) / zoom;
    double docY = (screenY + scrollY) / zoom;
    return Point(docX, docY);
}

QPoint ViewportState::docToScreen(const Point& docPoint) const {
    int screenX = static_cast<int>((docPoint.x * zoom) - scrollX);
    int screenY = static_cast<int>((docPoint.y * zoom) - scrollY);
    return QPoint(screenX, screenY);
}

PageIndex ViewportState::pageAtPoint(const Point& docPoint) const {
    // TODO: Account for page positioning in continuous scroll mode
    return static_cast<PageIndex>(docPoint.y / 792.0);  // Assuming 792pt pages
}

// === RENDER ENGINE ===
RenderEngine::RenderEngine(QObject* parent) : QObject(parent) {}

RenderEngine::~RenderEngine() {
    stopBackgroundThread();
}

void RenderEngine::renderViewport(QPainter* painter, const ViewportState& view, Document* doc) {
    if (!doc) return;

    painter->setRenderHint(QPainter::Antialiasing, m_antialiasing);

    // Calculate visible page range
    Point topLeft = view.screenToDoc(0, 0);
    Point bottomRight = view.screenToDoc(view.screenWidth, view.screenHeight);

    PageIndex startPage = std::max(0, static_cast<int>(topLeft.y / 792.0));
    PageIndex endPage = std::min(static_cast<int>(doc->pages.size()) - 1,
                                 static_cast<int>(bottomRight.y / 792.0) + 1);

    // Render visible pages
    int yOffset = 0;
    for (PageIndex i = 0; i < doc->pages.size(); ++i) {
        Page* page = doc->pages[i].get();

        // Check if page is visible
        double pageTop = i * (page->height + view.pageSpacing / view.zoom);
        double pageBottom = pageTop + page->height;

        bool visible = (pageBottom >= topLeft.y && pageTop <= bottomRight.y);

        if (visible) {
            // Check cache
            if (page->needsRender(view.zoom)) {
                renderPageToCache(page, view.zoom);
            }

            // Draw cached pixmap
            if (page->cache.valid) {
                QPoint pos = view.docToScreen(Point(0, pageTop));
                painter->drawPixmap(pos, page->cache.pixmap);
            }
        }

        yOffset += static_cast<int>(page->height * view.zoom + view.pageSpacing);
    }

    // Prefetch adjacent pages in background
    if (m_bgThread) {
        for (PageIndex i = startPage; i <= endPage + 2 && i < doc->pages.size(); ++i) {
            if (doc->pages[i]->needsRender(view.zoom)) {
                m_bgThread->enqueue(i);
            }
        }
    }
}

void RenderEngine::renderPageToCache(Page* page, double zoom) {
    int pixWidth = static_cast<int>(page->width * zoom);
    int pixHeight = static_cast<int>(page->height * zoom);

    if (pixWidth <= 0 || pixHeight <= 0) return;

    QPixmap pixmap(pixWidth, pixHeight);
    pixmap.fill(Qt::white);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, m_antialiasing);
    painter.scale(zoom, zoom);

    // Draw background
    painter.fillRect(0, 0, page->width, page->height,
                     page->backgroundColor.toQColor());

    // Draw margins (visual guide)
    painter.setPen(QPen(Qt::lightGray, 0.5 / zoom));
    painter.drawRect(page->marginLeft, page->marginBottom,
                     page->width - page->marginLeft - page->marginRight,
                     page->height - page->marginTop - page->marginBottom);

    // Render elements in order (respecting z-index)
    std::vector<Element*> sortedElems;
    for (const auto& elem : page->elements) {
        if (elem->visible) sortedElems.push_back(elem.get());
    }
    std::sort(sortedElems.begin(), sortedElems.end(),
              [](Element* a, Element* b) { return a->zIndex < b->zIndex; });

    for (Element* elem : sortedElems) {
        renderElement(&painter, elem, page);
    }

    painter.end();

    QMutexLocker lock(&m_cacheMutex);
    page->cache.pixmap = pixmap;
    page->cache.zoom = zoom;
    page->cache.valid = true;
    page->cache.generation = m_cacheGeneration;
}

void RenderEngine::renderElement(QPainter* painter, const Element* elem, const Page* page) {
    painter->save();

    // Apply transform
    const TransformMatrix& t = elem->transform;
    QTransform qt(t.a, t.b, t.c, t.d, t.e, t.f);
    painter->setTransform(qt, true);

    // Apply clip if present
    if (elem->clipPath.has_value()) {
        QPainterPath path;
        bool first = true;
        for (const auto& cmd : elem->clipPath.value()) {
            switch (cmd.type) {
            case PathCommandType::MoveTo:
                path.moveTo(cmd.x1, cmd.y1);
                first = false;
                break;
            case PathCommandType::LineTo:
                if (first) { path.moveTo(cmd.x1, cmd.y1); first = false; }
                else path.lineTo(cmd.x1, cmd.y1);
                break;
            case PathCommandType::CubicTo:
                path.cubicTo(cmd.cx1, cmd.cy1, cmd.cx2, cmd.cy2, cmd.x1, cmd.y1);
                break;
            case PathCommandType::QuadTo:
                path.quadTo(cmd.cx1, cmd.cy1, cmd.x1, cmd.y1);
                break;
            case PathCommandType::ClosePath:
                path.closeSubpath();
                break;
            }
        }
        painter->setClipPath(path);
    }

    // Apply opacity
    painter->setOpacity(elem->opacity);

    // Dispatch to type-specific renderer
    Rect b = elem->bounds;
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, TextElement>) {
            renderText(painter, &arg, b);
        } else if constexpr (std::is_same_v<T, ImageElement>) {
            renderImage(painter, &arg, b);
        } else if constexpr (std::is_same_v<T, VectorElement>) {
            renderVector(painter, &arg, b);
        } else if constexpr (std::is_same_v<T, TableElement>) {
            renderTable(painter, &arg, b);
        }
    }, elem->content);

    // Draw selection indicator
    if (elem->selected) {
        painter->setPen(QPen(Qt::blue, 1.0 / painter->transform().m11()));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(b.x, b.y, b.width, b.height);

        // Draw resize handles
        double handleSize = 6.0 / painter->transform().m11();
        painter->fillRect(b.x - handleSize/2, b.y - handleSize/2, handleSize, handleSize, Qt::blue);
        painter->fillRect(b.right() - handleSize/2, b.y - handleSize/2, handleSize, handleSize, Qt::blue);
        painter->fillRect(b.x - handleSize/2, b.top() - handleSize/2, handleSize, handleSize, Qt::blue);
        painter->fillRect(b.right() - handleSize/2, b.top() - handleSize/2, handleSize, handleSize, Qt::blue);
    }

    painter->restore();
}

void RenderEngine::renderText(QPainter* painter, const TextElement* text, const Rect& bounds) {
    if (text->text.isEmpty()) return;

    // Simple single-line text for now
    // Full implementation needs QTextLayout for wrapping
    QString displayText = text->text;
    if (displayText.length() > 1000) {
        displayText = displayText.left(1000) + "...";
    }

    QFont font(text->runs.empty() ? "Arial" : text->runs[0].fontFamily);
    if (!text->runs.empty()) {
        font.setPointSizeF(text->runs[0].fontSize);
        font.setBold(text->runs[0].bold);
        font.setItalic(text->runs[0].italic);
        font.setUnderline(text->runs[0].underline);
        font.setStrikeOut(text->runs[0].strikethrough);
    }

    painter->setFont(font);

    QColor color = text->runs.empty() ? Qt::black :
                       QColor::fromRgbF(text->runs[0].color.r, text->runs[0].color.g,
                                        text->runs[0].color.b, text->runs[0].color.a);
    painter->setPen(color);

    // Alignment
    int flags = Qt::AlignLeft | Qt::AlignTop;
    if (text->alignment == 1) flags = Qt::AlignCenter | Qt::AlignTop;
    else if (text->alignment == 2) flags = Qt::AlignRight | Qt::AlignTop;

    painter->drawText(QRectF(bounds.x, bounds.y, bounds.width, bounds.height),
                      flags, displayText);
}

void RenderEngine::renderImage(QPainter* painter, const ImageElement* img, const Rect& bounds) {
    if (!img->data) return;

    QImage image = img->data->getDecoded();
    if (image.isNull()) return;

    QRectF target(bounds.x, bounds.y, bounds.width, bounds.height);
    QRectF source(0, 0, image.width(), image.height());

    // FIX: Remove the mode parameter - drawImage doesn't take TransformationMode directly
    painter->drawImage(target, image, source);
}

void RenderEngine::renderVector(QPainter* painter, const VectorElement* vec, const Rect& bounds) {
    QPainterPath path;
    bool first = true;

    for (const auto& cmd : vec->commands) {
        switch (cmd.type) {
        case PathCommandType::MoveTo:
            path.moveTo(cmd.x1, cmd.y1);
            first = false;
            break;
        case PathCommandType::LineTo:
            if (first) { path.moveTo(cmd.x1, cmd.y1); first = false; }
            else path.lineTo(cmd.x1, cmd.y1);
            break;
        case PathCommandType::CubicTo:
            path.cubicTo(cmd.cx1, cmd.cy1, cmd.cx2, cmd.cy2, cmd.x1, cmd.y1);
            break;
        case PathCommandType::QuadTo:
            path.quadTo(cmd.cx1, cmd.cy1, cmd.x1, cmd.y1);
            break;
        case PathCommandType::ClosePath:
            path.closeSubpath();
            break;
        }
    }

    if (vec->fill.has_value()) {
        painter->fillPath(path, QBrush(vec->fill->color.toQColor()));
    }

    if (vec->stroke.has_value()) {
        QPen pen(vec->stroke->color.toQColor());
        pen.setWidthF(vec->stroke->width);
        painter->setPen(pen);
        painter->drawPath(path);
    }
}

void RenderEngine::renderTable(QPainter* painter, const TableElement* table, const Rect& bounds) {
    // Draw cells
    for (const auto& cell : table->cells) {
        // Cell background
        painter->fillRect(QRectF(cell.bounds.x, cell.bounds.y,
                                 cell.bounds.width, cell.bounds.height),
                          QColor(250, 250, 250));

        // Cell border
        painter->setPen(QPen(table->borderColor.toQColor(), table->borderWidth));
        painter->drawRect(QRectF(cell.bounds.x, cell.bounds.y,
                                 cell.bounds.width, cell.bounds.height));

        // Render nested content using indices
        for (size_t idx : cell.childElementIndices) {
            if (idx < table->childElements.size()) {
                // renderElement(painter, table->childElements[idx].get(), nullptr);
            }
        }
    }
}

void RenderEngine::invalidatePage(PageIndex idx) {
    // TODO: Mark page cache invalid
    m_cacheGeneration++;
}

void RenderEngine::invalidateElement(ID elemId) {
    // Find element and invalidate its page
    m_cacheGeneration++;
}

void RenderEngine::invalidateAll() {
    m_cacheGeneration++;
}

void RenderEngine::startBackgroundThread() {
    if (!m_bgThread) {
        m_bgThread = std::make_unique<BackgroundThread>(this);
        m_bgThread->start(QThread::LowPriority);
    }
}

void RenderEngine::stopBackgroundThread() {
    if (m_bgThread) {
        m_bgThread->stop();
        m_bgThread.reset();
    }
}

void RenderEngine::prefetchPages(PageIndex start, PageIndex count) {
    if (!m_bgThread) return;
    for (PageIndex i = start; i < start + count; ++i) {
        m_bgThread->enqueue(i);
    }
}

} // namespace UDoc
