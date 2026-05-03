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
#include <chrono>

namespace UDoc {

// === BACKGROUND RENDER THREAD ===
BackgroundRenderThread::BackgroundRenderThread(QObject* parent)
    : QObject(parent)
{
    m_running = true;
    m_thread = std::make_unique<std::thread>(&BackgroundRenderThread::processRenderQueue, this);
}

BackgroundRenderThread::~BackgroundRenderThread() {
    stop();
}

void BackgroundRenderThread::stop() {
    m_running = false;
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

void BackgroundRenderThread::renderPageAsync(ID pageId, const Page* page, const ViewportState& view) {
    QMutexLocker locker(&m_queueMutex);
    m_renderQueue.push({RenderTask::Page, pageId, page, view});
}

void BackgroundRenderThread::renderElementAsync(ID elementId, const Element* element, const ViewportState& view) {
    QMutexLocker locker(&m_queueMutex);
    m_renderQueue.push({RenderTask::Element, elementId, element, view});
}

void BackgroundRenderThread::processRenderQueue() {
    while (m_running) {
        RenderTask task;
        bool hasTask = false;
        
        {
            QMutexLocker locker(&m_queueMutex);
            if (!m_renderQueue.empty()) {
                task = m_renderQueue.front();
                m_renderQueue.pop();
                hasTask = true;
            }
        }
        
        if (hasTask) {
            // Render in background
            QPixmap pixmap;
            if (task.type == RenderTask::Page) {
                const Page* page = static_cast<const Page*>(task.object);
                // TODO: Implement background page rendering
                emit pageRendered(task.id, pixmap);
            } else {
                const Element* element = static_cast<const Element*>(task.object);
                // TODO: Implement background element rendering
                emit elementRendered(task.id, pixmap);
            }
        } else {
            // No tasks, wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// === MAIN RENDER ENGINE ===
RenderEngine::RenderEngine() {
    m_pageCache.setMaxCost(1000);  // Max 1000 pages in cache
    m_elementCache.setMaxCost(10000);  // Max 10000 elements in cache
    
    if (m_backgroundRendering) {
        m_bgThread = std::make_unique<BackgroundRenderThread>();
    }
}

RenderEngine::~RenderEngine() {
    if (m_bgThread) {
        m_bgThread->stop();
    }
}

void RenderEngine::renderViewport(QPainter* painter, 
                                 const ViewportState& view,
                                 Document* doc) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Setup painter
    setupPainter(painter, view);
    
    // Get active tab
    Tab* activeTab = doc->findTab(view.activeTabId);
    if (!activeTab) return;
    
    // Render based on tab type
    std::visit([&](const auto& content) {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, PageSequence>) {
            renderPageSequence(painter, content, view);
        } else if constexpr (std::is_same_v<T, SlideSequence>) {
            renderSlideSequence(painter, content, view);
        } else if constexpr (std::is_same_v<T, GridSheet>) {
            renderGridSheet(painter, content, view);
        } else if constexpr (std::is_same_v<T, FlowDocument>) {
            renderFlowDocument(painter, content, view);
        } else if constexpr (std::is_same_v<T, InfiniteCanvas>) {
            renderInfiniteCanvas(painter, content, view);
        }
    }, activeTab->content);
    
    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    QMutexLocker locker(&m_statsMutex);
    m_stats.totalRenderTime += duration;
    m_stats.averageRenderTime = m_stats.totalRenderTime / ++m_stats.pagesRendered;
}

void RenderEngine::renderPageSequence(QPainter* painter, 
                                      const PageSequence& pages,
                                      const ViewportState& view) {
    const double zoom = view.zoom;
    const QRectF visibleRect = view.visibleRect;
    
    // Find visible pages
    for (const auto& page : pages.pages) {
        const QRectF pageRect(0, 0, page->width * zoom, page->height * zoom);
        
        if (visibleRect.intersects(pageRect)) {
            // Check cache first
            PageCache* cache = getPageCache(page->id());
            if (cache && cache->isValidFor(pageRect, zoom)) {
                painter->drawPixmap(pageRect.topLeft(), cache->cachedPixmap);
                m_stats.cacheHits++;
            } else {
                // Render page
                QPixmap pagePixmap(pageRect.size().toSize());
                pagePixmap.fill(Qt::white);
                
                QPainter pagePainter(&pagePixmap);
                pagePainter.setRenderHint(QPainter::Antialiasing, view.antialiasing);
                pagePainter.setRenderHint(QPainter::TextAntialiasing, view.textAntialiasing);
                pagePainter.setRenderHint(QPainter::SmoothPixmapTransform, view.smoothPixmapTransform);
                
                // Scale painter for zoom
                pagePainter.scale(zoom, zoom);
                
                // Render page background
                if (!(page->backgroundColor == Color::transparent())) {
                    pagePainter.fillRect(0, 0, page->width, page->height, 
                                       RenderHelpers::toQColor(page->backgroundColor));
                }
                
                // Render background image if any
                if (page->backgroundImage) {
                    // TODO: Render background image
                }
                
                // Render elements
                for (const auto& element : page->elements) {
                    renderElement(&pagePainter, element.get(), view);
                }
                
                pagePainter.end();
                
                // Update cache
                updatePageCache(page->id(), pagePixmap, pageRect, zoom);
                
                // Draw to main painter
                painter->drawPixmap(pageRect.topLeft(), pagePixmap);
                m_stats.cacheMisses++;
                m_stats.elementsRendered += page->elements.size();
            }
        }
    }
}

void RenderEngine::renderElement(QPainter* painter, 
                                const Element* element,
                                const ViewportState& view) {
    if (!element || !element->visible) return;
    
    // Save painter state
    painter->save();
    
    // Apply transform if any
    if (element->transform) {
        painter->setTransform(RenderHelpers::toQTransform(*element->transform), true);
    }
    
    // Apply opacity
    if (element->opacity < 1.0) {
        painter->setOpacity(element->opacity);
    }
    
    // Apply clipping if any
    if (element->clipPath) {
        RenderHelpers::applyClipPath(painter, *element->clipPath, painter->transform());
    }
    
    // Get element bounds
    QRectF bounds;
    if (element->bounds) {
        bounds = toQRectF(*element->bounds);
    } else {
        // Flow element - bounds should be computed during layout
        bounds = QRectF(0, 0, 100, 20);  // Default fallback
    }
    
    // Render based on element type
    std::visit([&](const auto& content) {
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
        } else if constexpr (std::is_same_v<T, Media>) {
            // TODO: Render media
        } else if constexpr (std::is_same_v<T, Formula>) {
            // TODO: Render formula
        } else if constexpr (std::is_same_v<T, Link>) {
            renderLink(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Bookmark>) {
            // Bookmarks don't render visually
        } else if constexpr (std::is_same_v<T, Field>) {
            renderField(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Comment>) {
            renderComment(painter, &content, bounds);
        } else if constexpr (std::is_same_v<T, Group>) {
            // TODO: Render group (render nested elements)
        }
    }, element->content);
    
    // Render selection if selected
    if (element->selected) {
        renderSelection(painter, element, view);
    }
    
    // Render editing handles if editing
    if (element->editing) {
        renderEditingHandles(painter, element, view);
    }
    
    // Render link indicators if has link
    if (element->linkTarget) {
        renderLinkIndicators(painter, element, view);
    }
    
    // Restore painter state
    painter->restore();
}

void RenderEngine::renderTextBlock(QPainter* painter, 
                                   const TextBlockContent* text,
                                   const QRectF& bounds) {
    if (!text || text->text.isEmpty()) return;
    
    // Create text layout
    auto layout = layoutTextBlock(text, bounds.width());
    if (!layout) return;
    
    // Setup layout
    layout->beginLayout();
    
    // Draw text
    painter->save();
    layout->draw(painter, QPointF(bounds.left(), bounds.top()));
    layout->endLayout();
    painter->restore();
}

std::unique_ptr<QTextLayout> RenderEngine::layoutTextBlock(const TextBlockContent* text, double maxWidth) {
    auto layout = std::make_unique<QTextLayout>();
    
    // Set text
    layout->setText(text->text);
    
    // Apply formatting from runs
    QTextOption option;
    option.setAlignment(Qt::AlignLeft);
    option.setWrapMode(QTextOption::WordWrap);
    layout->setTextOption(option);
    
    // Create layout
    layout->beginLayout();
    QPointF linePos(0, 0);
    
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid()) break;
        
        line.setLineWidth(maxWidth);
        line.setPosition(linePos);
        linePos.setY(linePos.y() + line.height());
    }
    
    layout->endLayout();
    
    return layout;
}

void RenderEngine::renderHeading(QPainter* painter, 
                                const HeadingContent* heading,
                                const QRectF& bounds) {
    // Render as text block with heading-specific formatting
    renderTextBlock(painter, heading, bounds);
    
    // TODO: Add heading-specific visual indicators (outline level markers, etc.)
}

void RenderEngine::renderList(QPainter* painter, 
                             const ListContent* list,
                             const QRectF& bounds) {
    if (!list) return;
    
    painter->save();
    
    double y = bounds.top();
    const double indent = 20.0;
    const double lineHeight = 20.0;
    
    for (const auto& item : list->items) {
        // Render bullet/number
        QString marker;
        if (list->type == ListType::Bullet) {
            marker = list->bulletChar;
        } else {
            // TODO: Generate numbered marker
            marker = QString::number(1) + ".";
        }
        
        painter->drawText(bounds.left(), y, marker);
        
        // Render item content
        if (item.content) {
            QRectF itemBounds(bounds.left() + indent, y, bounds.width() - indent, lineHeight);
            renderElement(painter, item.content.get(), ViewportState{});  // TODO: Proper view state
        }
        
        y += lineHeight;
    }
    
    painter->restore();
}

void RenderEngine::renderTable(QPainter* painter, 
                              const Table* table,
                              const QRectF& bounds) {
    if (!table) return;
    
    painter->save();
    
    // TODO: Implement table rendering
    // - Draw cell borders
    // - Render cell contents
    // - Handle merged cells
    
    // For now, draw a placeholder rectangle
    painter->setPen(QPen(Qt::black, 1.0));
    painter->setBrush(Qt::white);
    painter->drawRect(bounds);
    painter->drawText(bounds.center(), "Table");
    
    painter->restore();
}

void RenderEngine::renderImage(QPainter* painter, 
                               const Image* image,
                               const QRectF& bounds) {
    if (!image || !image->data) return;
    
    painter->save();
    
    // Get decoded image
    QImage decoded = image->data->getDecoded();
    if (!decoded.isNull()) {
        // TODO: Apply image transformations (crop, rotation, flip, interpolation)
        painter->drawImage(bounds, decoded);
    }
    
    painter->restore();
}

void RenderEngine::renderVector(QPainter* painter, 
                                const VectorGraphic* vector,
                                const QRectF& bounds) {
    if (!vector) return;
    
    painter->save();
    
    // TODO: Implement vector rendering
    // - Parse SVG-like path commands
    // - Apply transforms and styling
    
    // For now, draw a placeholder
    painter->setPen(QPen(Qt::blue, 1.0));
    painter->setBrush(Qt::transparent);
    painter->drawRect(bounds);
    painter->drawText(bounds.center(), "Vector");
    
    painter->restore();
}

void RenderEngine::renderLink(QPainter* painter, 
                              const Link* link,
                              const QRectF& bounds) {
    if (!link) return;
    
    painter->save();
    
    // Draw link underline
    painter->setPen(QPen(RenderHelpers::toQColor(link->color), 1.0, Qt::DashLine));
    double underlineY = bounds.bottom() - 2.0;
    painter->drawLine(QPointF(bounds.left(), underlineY), QPointF(bounds.right(), underlineY));
    
    painter->restore();
}

void RenderEngine::renderField(QPainter* painter, 
                               const Field* field,
                               const QRectF& bounds) {
    if (!field) return;
    
    painter->save();
    
    // TODO: Implement field rendering based on type
    // - Page numbers, dates, cross-references, etc.
    
    // For now, draw placeholder
    painter->setPen(QPen(Qt::gray, 1.0));
    painter->drawText(bounds, Qt::AlignCenter, "Field");
    
    painter->restore();
}

void RenderEngine::renderComment(QPainter* painter, 
                                const Comment* comment,
                                const QRectF& bounds) {
    if (!comment) return;
    
    painter->save();
    
    // Draw comment indicator (small colored rectangle)
    painter->setPen(QPen(Qt::yellow, 2.0));
    painter->setBrush(QColor(255, 255, 0, 100));
    QRectF indicator(bounds.right() - 10, bounds.top(), 8, 8);
    painter->drawRect(indicator);
    
    painter->restore();
}

void RenderEngine::renderSelection(QPainter* painter, const Element* element, const ViewportState& view) {
    if (!element || !element->bounds) return;
    
    painter->save();
    
    // Draw selection handles
    painter->setPen(QPen(Qt::blue, 2.0));
    painter->setBrush(QColor(0, 120, 255, 100));
    painter->drawRect(toQRectF(*element->bounds));
    
    // Draw resize handles at corners
    const double handleSize = 8.0;
    QRectF bounds = toQRectF(*element->bounds);
    
    QRectF handles[4] = {
        QRectF(bounds.left() - handleSize/2, bounds.top() - handleSize/2, handleSize, handleSize),  // Top-left
        QRectF(bounds.right() - handleSize/2, bounds.top() - handleSize/2, handleSize, handleSize),   // Top-right
        QRectF(bounds.left() - handleSize/2, bounds.bottom() - handleSize/2, handleSize, handleSize), // Bottom-left
        QRectF(bounds.right() - handleSize/2, bounds.bottom() - handleSize/2, handleSize, handleSize)  // Bottom-right
    };
    
    painter->setBrush(Qt::blue);
    for (const auto& handle : handles) {
        painter->drawRect(handle);
    }
    
    painter->restore();
}

void RenderEngine::renderEditingHandles(QPainter* painter, const Element* element, const ViewportState& view) {
    // TODO: Implement text editing handles (cursor, selection range, etc.)
}

void RenderEngine::renderLinkIndicators(QPainter* painter, const Element* element, const ViewportState& view) {
    // TODO: Implement hover preview, tooltips, etc.
}

// === CACHE MANAGEMENT ===
void RenderEngine::invalidatePage(ID pageId) {
    QMutexLocker locker(&m_cacheMutex);
    PageCache* cache = m_pageCache.object(pageId);
    if (cache) {
        cache->isValid = false;
    }
}

void RenderEngine::invalidateElement(ID elementId) {
    QMutexLocker locker(&m_cacheMutex);
    ElementCache* cache = m_elementCache.object(elementId);
    if (cache) {
        cache->isValid = false;
    }
}

void RenderEngine::invalidateAll() {
    QMutexLocker locker(&m_cacheMutex);
    m_pageCache.clear();
    m_elementCache.clear();
}

void RenderEngine::clearCache() {
    QMutexLocker locker(&m_cacheMutex);
    m_pageCache.clear();
    m_elementCache.clear();
}

PageCache* RenderEngine::getPageCache(ID pageId) {
    QMutexLocker locker(&m_cacheMutex);
    return m_pageCache.object(pageId);
}

ElementCache* RenderEngine::getElementCache(ID elementId) {
    QMutexLocker locker(&m_cacheMutex);
    return m_elementCache.object(elementId);
}

void RenderEngine::updatePageCache(ID pageId, const QPixmap& pixmap, const QRectF& rect, double zoom) {
    QMutexLocker locker(&m_cacheMutex);
    auto cache = std::make_unique<PageCache>();
    cache->cachedPixmap = pixmap;
    cache->cachedRect = rect;
    cache->cachedZoom = zoom;
    cache->cacheTime = QDateTime::currentDateTime();
    cache->isValid = true;
    
    m_pageCache.insert(pageId, cache.release());
}

void RenderEngine::updateElementCache(ID elementId, const QPixmap& pixmap, const QRectF& bounds, double zoom) {
    QMutexLocker locker(&m_cacheMutex);
    auto cache = std::make_unique<ElementCache>();
    cache->cachedPixmap = pixmap;
    cache->bounds = bounds;
    cache->cachedZoom = zoom;
    cache->cacheTime = QDateTime::currentDateTime();
    cache->isValid = true;
    
    m_elementCache.insert(elementId, cache.release());
}

// === CONFIGURATION ===
void RenderEngine::setMaxCacheSize(int megabytes) {
    m_maxCacheSizeMB = megabytes;
    // TODO: Update cache limits based on memory usage
}

int RenderEngine::cacheSize() const {
    // TODO: Calculate actual cache memory usage
    return 0;
}

void RenderEngine::enableBackgroundRendering(bool enable) {
    if (enable && !m_bgThread) {
        m_bgThread = std::make_unique<BackgroundRenderThread>();
    } else if (!enable && m_bgThread) {
        m_bgThread->stop();
        m_bgThread.reset();
    }
    m_backgroundRendering = enable;
}

bool RenderEngine::isBackgroundRenderingEnabled() const {
    return m_backgroundRendering;
}

void RenderEngine::resetStats() {
    QMutexLocker locker(&m_statsMutex);
    m_stats = RenderStats{};
}

// === HELPER METHODS ===
void RenderEngine::setupPainter(QPainter* painter, const ViewportState& view) {
    painter->setRenderHint(QPainter::Antialiasing, view.antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing, view.textAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, view.smoothPixmapTransform);
    
    // Apply view transform
    QTransform transform = createViewTransform(view);
    painter->setTransform(transform);
}

QTransform RenderEngine::createViewTransform(const ViewportState& view) const {
    QTransform transform;
    transform.translate(-view.scrollOffset.x(), -view.scrollOffset.y());
    transform.scale(view.zoom, view.zoom);
    return transform;
}

// === INTERNAL HELPERS ===
namespace UDoc {
    Rect fromQRectF(const QRectF& rect) {
        return Rect(rect.x(), rect.y(), rect.width(), rect.height());
    }
}

// === RENDER HELPERS IMPLEMENTATION ===
namespace UDoc {
    QBrush toQBrush(const Color& color, Qt::BrushStyle style) {
        return QBrush(QColor::fromRgbF(color.r, color.g, color.b, color.a), style);
    }
}

namespace UDoc::RenderHelpers {

QRectF toQRectF(const Rect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

QTextCharFormat toQTextCharFormat(const CharacterProperties& props) {

}
