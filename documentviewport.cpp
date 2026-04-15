#include "documentviewport.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QBuffer>
#include <QImageReader> // (for future use)

namespace UDoc {

DocumentViewport::DocumentViewport(QWidget* parent)
    : QWidget(parent), m_undoStack(new QUndoStack(this)) {

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);

    m_renderer = std::make_unique<RenderEngine>(this);
    m_renderer->startBackgroundThread();

    // Set default size
    m_view.screenWidth = width();
    m_view.screenHeight = height();
    updateCursor();
}

DocumentViewport::~DocumentViewport() = default;

void DocumentViewport::setDocument(Document* doc) {
    m_doc = doc;
    m_selection.clear();
    m_view.scrollX = 0;
    m_view.scrollY = 0;

    if (doc && !doc->pages.empty()) {
        // Fit first page to width
        fitToWidth();
    }

    update();
}

void DocumentViewport::setZoom(double zoom) {
    m_view.zoom = std::clamp(zoom, 0.1, 10.0);
    m_renderer->invalidateAll();
    update();
    emit zoomChanged(m_view.zoom);
}

void DocumentViewport::zoomIn() {
    setZoom(m_view.zoom * 1.25);
}

void DocumentViewport::zoomOut() {
    setZoom(m_view.zoom / 1.25);
}

void DocumentViewport::fitToWidth() {
    if (!m_doc || m_doc->pages.empty()) return;
    double pageWidth = m_doc->pages[0]->width;
    double margin = 40;  // pixels
    double availableWidth = width() - margin;
    setZoom(availableWidth / pageWidth);
    centerPage(0);
}

void DocumentViewport::fitToPage() {
    if (!m_doc || m_doc->pages.empty()) return;
    double pageWidth = m_doc->pages[0]->width;
    double pageHeight = m_doc->pages[0]->height;
    double scaleX = (width() - 40) / pageWidth;
    double scaleY = (height() - 40) / pageHeight;
    setZoom(std::min(scaleX, scaleY));
    centerPage(0);
}

void DocumentViewport::actualSize() {
    setZoom(1.0);
}

void DocumentViewport::goToPage(PageIndex idx) {
    if (!m_doc || idx >= m_doc->pages.size()) return;
    centerPage(idx);
    emit pageChanged(idx);
}

void DocumentViewport::scrollToElement(ID elemId) {
    // Find element and scroll to it
    for (PageIndex i = 0; i < m_doc->pages.size(); ++i) {
        if (Element* elem = m_doc->pages[i]->findElement(elemId)) {
            Rect bounds = elem->transformedBounds();
            m_view.scrollY = bounds.y * m_view.zoom - height() / 2;
            m_view.scrollX = bounds.x * m_view.zoom - width() / 2;
            selectElement(elemId, i);
            update();
            break;
        }
    }
}

void DocumentViewport::selectElement(ID elemId, PageIndex pageIdx) {
    // Clear previous selection
    if (!m_selection.isEmpty()) {
        for (const auto& page : m_doc->pages) {
            for (auto& elem : page->elements) {
                elem->selected = false;
            }
        }
    }

    // Select new
    m_selection.type = Selection::SingleElement;
    m_selection.elementId = elemId;
    m_selection.pageIdx = pageIdx;

    if (Page* page = m_doc->pageAt(pageIdx)) {
        if (Element* elem = page->findElement(elemId)) {
            elem->selected = true;
        }
    }

    update();
    emit selectionChanged(m_selection);
}

void DocumentViewport::clearSelection() {
    if (!m_selection.isEmpty()) {
        for (const auto& page : m_doc->pages) {
            for (auto& elem : page->elements) {
                elem->selected = false;
            }
        }
        m_selection.clear();
        update();
        emit selectionChanged(m_selection);
    }
}

void DocumentViewport::setMode(Mode mode) {
    m_mode = mode;
    if (mode != Mode::Select) {
        clearSelection();
    }
    updateCursor();
}

void DocumentViewport::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(128, 128, 128));

    if (!m_doc) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No document open");
        return;
    }

    m_renderer->renderViewport(&painter, m_view, m_doc);

    // Render tool preview on top
    renderToolPreview(&painter);
}

void DocumentViewport::resizeEvent(QResizeEvent* event) {
    m_view.screenWidth = width();
    m_view.screenHeight = height();
    QWidget::resizeEvent(event);
}

void DocumentViewport::mousePressEvent(QMouseEvent* event) {
    if (!m_doc) return;

    Point docPoint = screenToDoc(event->pos());

    // Handle tool activation
    if (m_toolState.active && event->button() == Qt::LeftButton) {
        m_toolState.startPoint = docPoint;
        m_toolState.currentPoint = docPoint;
        return;
    }

    PageIndex pageIdx;
    Element* elem = elementAt(docPoint, &pageIdx);

    if (event->button() == Qt::LeftButton) {
        m_dragStartDoc = docPoint;
        m_dragStartScreen = event->pos();

        if (m_mode == Mode::Select && elem) {
            m_dragging = true;
            m_dragElement = elem;
            if (!elem->selected) {
                selectElement(elem->id(), pageIdx);
            }
        } else {
            clearSelection();
        }
    }

    update();
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!m_doc) return;

    Point currentDoc = screenToDoc(event->pos());

    // Update tool preview
    if (m_toolState.active) {
        m_toolState.currentPoint = currentDoc;
        update();
        return;
    }

    // Handle element dragging
    if (m_dragging && m_dragElement && (event->buttons() & Qt::LeftButton)) {
        double dx = currentDoc.x - m_dragStartDoc.x;
        double dy = currentDoc.y - m_dragStartDoc.y;

        m_dragElement->bounds.x += dx;
        m_dragElement->bounds.y += dy;
        m_dragStartDoc = currentDoc;

        if (Page* page = m_doc->pageAt(m_selection.pageIdx)) {
            page->invalidateCache();
        }
        update();
    }
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (m_toolState.active && event->button() == Qt::LeftButton) {
        finishCurrentTool();
        return;
    }

    if (m_dragging && m_dragElement) {
        // Commit move
        executeCommand(new MoveElementCommand(m_doc, m_dragElement->id(),
                                              m_dragElement->bounds.x,
                                              m_dragElement->bounds.y));
    }

    m_dragging = false;
    m_dragElement = nullptr;
}

void DocumentViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    Point docPoint = screenToDoc(event->pos());
    Element* elem = elementAt(docPoint);
    if (elem) {
        emit elementDoubleClicked(elem->id());
    }
}

void DocumentViewport::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom
        if (event->angleDelta().y() > 0) {
            zoomIn();
        } else {
            zoomOut();
        }
    } else {
        // Scroll
        m_view.scrollY -= event->angleDelta().y();
        m_view.scrollX -= event->angleDelta().x();
        update();
    }
}

void DocumentViewport::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (!m_selection.isEmpty() && m_selection.type == Selection::SingleElement) {
                // executeCommand(new DeleteElementCommand(...));
            }
            break;
        case Qt::Key_Escape:
            clearSelection();
            break;
        case Qt::Key_PageDown:
            goToPage(std::min(m_selection.pageIdx + 1,
                             static_cast<PageIndex>(m_doc->pages.size()) - 1));
            break;
        case Qt::Key_PageUp:
            goToPage(m_selection.pageIdx > 0 ? m_selection.pageIdx - 1 : 0);
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void DocumentViewport::contextMenuEvent(QContextMenuEvent* event) {
    Point docPoint = screenToDoc(event->pos());
    Element* elem = elementAt(docPoint);
    if (elem) {
        emit contextMenuRequested(elem->id(), event->globalPos());
    }
}

void DocumentViewport::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void DocumentViewport::dropEvent(QDropEvent* event) {
    // Handle file drops
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString filePath = url.toLocalFile();
            // Emit signal or handle import
        }
    }
}

Point DocumentViewport::screenToDoc(const QPoint& screen) const {
    return m_view.screenToDoc(screen.x(), screen.y());
}

QPoint DocumentViewport::docToScreen(const Point& doc) const {
    return m_view.docToScreen(doc);
}

Element* DocumentViewport::elementAt(const Point& docPoint, PageIndex* outPage) const {
    if (!m_doc) return nullptr;

    // Find which page
    PageIndex pageIdx = m_view.pageAtPoint(docPoint);
    if (pageIdx >= m_doc->pages.size()) return nullptr;

    Page* page = m_doc->pages[pageIdx].get();

    // Convert to page-local coordinates
    double pageTop = pageIdx * (page->height + m_view.pageSpacing / m_view.zoom);
    Point localPoint(docPoint.x, docPoint.y - pageTop);

    if (outPage) *outPage = pageIdx;
    return page->hitTest(localPoint);
}

void DocumentViewport::centerPage(PageIndex idx) {
    if (!m_doc || idx >= m_doc->pages.size()) return;
    Page* page = m_doc->pages[idx].get();

    double pageTop = idx * (page->height + m_view.pageSpacing / m_view.zoom);
    m_view.scrollY = pageTop * m_view.zoom - height() / 2 + (page->height * m_view.zoom) / 2;
    m_view.scrollX = (page->width * m_view.zoom - width()) / 2;
    update();
}

void DocumentViewport::updateCursor() {
    switch (m_mode) {
    case Mode::View:
        setCursor(Qt::ArrowCursor);
        break;
    case Mode::Select:
        setCursor(Qt::ArrowCursor);
        break;
    case Mode::TextEdit:
        setCursor(Qt::IBeamCursor);
        break;
    case Mode::DrawRect:      // CHANGED from Mode::Draw
    case Mode::DrawEllipse:   // ADD this
    case Mode::InsertImage:   // ADD this
        setCursor(Qt::CrossCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
    }
}

void DocumentViewport::executeCommand(UDocCommand* cmd) {
    m_undoStack->push(cmd);
}

void DocumentViewport::updateSelectionUI() {
    // Update UI based on selection
}

void DocumentViewport::updateViewportState() {
    // Update viewport calculations
}

void DocumentViewport::ensureVisible(const Rect& docRect) {
    // Scroll to ensure rect is visible
}

void DocumentViewport::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}
// === TOOL METHODS ===

void DocumentViewport::startTextTool() {
    setMode(Mode::TextEdit);
    m_toolState.active = true;
    setCursor(Qt::CrossCursor);
}

void DocumentViewport::startRectangleTool() {
    setMode(Mode::DrawRect);
    m_toolState.active = true;
    setCursor(Qt::CrossCursor);
}

void DocumentViewport::startImageTool(const QString& imagePath) {
    setMode(Mode::InsertImage);
    m_toolState.active = true;
    m_toolState.imagePath = imagePath;
    setCursor(Qt::CrossCursor);
}

void DocumentViewport::finishCurrentTool() {
    if (!m_toolState.active) return;

    switch (m_mode) {
    case Mode::DrawRect: {
        double x = std::min(m_toolState.startPoint.x, m_toolState.currentPoint.x);
        double y = std::min(m_toolState.startPoint.y, m_toolState.currentPoint.y);
        double w = std::abs(m_toolState.currentPoint.x - m_toolState.startPoint.x);
        double h = std::abs(m_toolState.currentPoint.y - m_toolState.startPoint.y);

        if (w > 5 && h > 5) {  // Minimum size
            createRectangleElement(Rect(x, y, w, h));
        }
        break;
    }
    case Mode::TextEdit: {
        createTextElement(m_toolState.startPoint);
        break;
    }
    case Mode::InsertImage: {
        createImageElement(m_toolState.startPoint, m_toolState.imagePath);
        break;
    }
    default:
        break;
    }

    cancelCurrentTool();
}

void DocumentViewport::cancelCurrentTool() {
    m_toolState.active = false;
    m_toolState.imagePath.clear();
    setMode(Mode::Select);
    update();
}

void DocumentViewport::createTextElement(const Point& pos) {
    if (!m_doc) return;

    auto elem = std::make_unique<Element>(m_doc->generateId());
    TextElement text;
    text.text = "Double-click to edit";
    text.runs.push_back({0, 26, "Arial", 12, Color(0,0,0,1), false, false, false, false, 0, 0, 0});

    elem->content = std::move(text);
    elem->bounds = Rect(pos.x, pos.y, 200, 30);

    PageIndex pageIdx = m_view.pageAtPoint(pos);
    if (Page* page = m_doc->pageAt(pageIdx)) {
        executeCommand(new InsertElementCommand(m_doc, pageIdx, std::move(elem),
                                                page->elements.size()));
        update();
    }
}

void DocumentViewport::createRectangleElement(const Rect& bounds) {
    if (!m_doc) return;

    auto elem = std::make_unique<Element>(m_doc->generateId());
    VectorElement vec;

    // Rectangle path
    vec.commands.push_back({PathCommandType::MoveTo, bounds.x, bounds.y, 0, 0, 0, 0});
    vec.commands.push_back({PathCommandType::LineTo, bounds.right(), bounds.y, 0, 0, 0, 0});
    vec.commands.push_back({PathCommandType::LineTo, bounds.right(), bounds.top(), 0, 0, 0, 0});
    vec.commands.push_back({PathCommandType::LineTo, bounds.x, bounds.top(), 0, 0, 0, 0});
    vec.commands.push_back({PathCommandType::ClosePath, 0, 0, 0, 0, 0, 0});

    vec.fill = Fill{Color(0.8, 0.8, 0.9, 1)};  // Light blue fill
    vec.stroke = Stroke{Color(0.2, 0.2, 0.6, 1), 1};  // Dark blue stroke

    elem->content = std::move(vec);
    elem->bounds = bounds;

    PageIndex pageIdx = m_view.pageAtPoint(Point(bounds.x, bounds.y));
    if (Page* page = m_doc->pageAt(pageIdx)) {
        executeCommand(new InsertElementCommand(m_doc, pageIdx, std::move(elem),
                                                page->elements.size()));
        update();
    }
}

void DocumentViewport::createImageElement(const Point& pos, const QString& path) {
    if (!m_doc) return;

    QImage img(path);
    if (img.isNull()) return;

    auto elem = std::make_unique<Element>(m_doc->generateId());
    ImageElement imgElem;

    auto data = std::make_shared<ImageElement::ImageData>();
    data->decoded = img;
    data->decodedValid = true;
    data->width = img.width();
    data->height = img.height();

    // FIX: Get suffix safely
    QFileInfo fileInfo(path);
    data->format = fileInfo.suffix().toLower().toStdString();

    // Convert to bytes for storage
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    data->rawBytes.assign(ba.begin(), ba.end());

    imgElem.data = data;
    imgElem.displayWidth = std::min(300.0, static_cast<double>(img.width()));
    imgElem.displayHeight = imgElem.displayWidth * (static_cast<double>(img.height()) / img.width());

    elem->content = std::move(imgElem);
    elem->bounds = Rect(pos.x, pos.y, imgElem.displayWidth, imgElem.displayHeight);

    PageIndex pageIdx = m_view.pageAtPoint(pos);
    if (Page* page = m_doc->pageAt(pageIdx)) {
        executeCommand(new InsertElementCommand(m_doc, pageIdx, std::move(elem),
                                                page->elements.size()));
        update();
    }
}

void DocumentViewport::renderToolPreview(QPainter* painter) {
    if (!m_toolState.active) return;

    painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));
    painter->setBrush(Qt::NoBrush);

    switch (m_mode) {
    case Mode::DrawRect: {
        double x = std::min(m_toolState.startPoint.x, m_toolState.currentPoint.x);
        double y = std::min(m_toolState.startPoint.y, m_toolState.currentPoint.y);
        double w = std::abs(m_toolState.currentPoint.x - m_toolState.startPoint.x);
        double h = std::abs(m_toolState.currentPoint.y - m_toolState.startPoint.y);

        QPoint tl = docToScreen(Point(x, y));
        painter->drawRect(tl.x(), tl.y(),
                          static_cast<int>(w * m_view.zoom),
                          static_cast<int>(h * m_view.zoom));
        break;
    }
    case Mode::TextEdit:
    case Mode::InsertImage: {
        QPoint p = docToScreen(m_toolState.currentPoint);
        painter->drawEllipse(p.x() - 3, p.y() - 3, 6, 6);
        break;
    }
    default:
        break;
    }
}

} // namespace UDoc
