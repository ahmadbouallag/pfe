#include "document_view.h"
#include <QWheelEvent>
#include <QMouseEvent>

namespace UDoc {

DocumentView::DocumentView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    m_viewState.zoom = 1.0;
    m_viewState.scrollOffset = QPointF(0, 0);
    m_viewState.visibleRect = QRectF(0, 0, width(), height());

    setAutoFillBackground(true);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, Qt::white);
    setPalette(palette);
}

void DocumentView::setDocument(Document* doc) {
    m_document = doc;
    m_renderEngine.invalidateAll();
    update();
}

void DocumentView::setZoom(double zoom) {
    m_viewState.zoom = zoom;
    m_renderEngine.invalidateAll(); // cached pixmaps are at wrong resolution — drop them
    update();
}

void DocumentView::scrollBy(const QPointF& delta) {
    m_viewState.scrollOffset.rx() += delta.x();
    m_viewState.scrollOffset.ry() += delta.y();
    update();
}

void DocumentView::scrollTo(const QPointF& pos) {
    m_viewState.scrollOffset = pos;
    update();
}

void DocumentView::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    m_viewState.visibleRect = QRectF(0, 0, width(), height());

    if (m_document) {
        m_renderEngine.renderViewport(&painter, m_viewState, m_document);
    } else {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "No document loaded");
    }
}

void DocumentView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y() / 120.0;
        double newZoom = m_viewState.zoom * (1.0 + delta * 0.1);
        newZoom = qBound(0.1, newZoom, 10.0);
        setZoom(newZoom);
    } else {
        scrollBy(QPointF(event->angleDelta().x() / 8.0,
                         event->angleDelta().y() / 8.0));
    }
    event->accept();
}

void DocumentView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::ShiftModifier)) {
        m_panning = true;
        m_lastMousePos = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void DocumentView::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPointF delta = event->position() - m_lastMousePos;
        scrollBy(delta);
        m_lastMousePos = event->position();
        event->accept();
    }
}

void DocumentView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         event->modifiers() & Qt::ShiftModifier)) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    }
}

} // namespace UDoc
