#ifndef DOCUMENT_VIEW_H
#define DOCUMENT_VIEW_H

#include <QWidget>
#include <QPainter>
#include "render_engine.h"
#include "document.h"

namespace UDoc {

class DocumentView : public QWidget {
    Q_OBJECT
public:
    explicit DocumentView(QWidget* parent = nullptr);
    
    void setDocument(Document* doc);
    Document* document() const { return m_document; }
    
    void setZoom(double zoom);
    double zoom() const { return m_viewState.zoom; }
    
    void scrollBy(const QPointF& delta);
    void scrollTo(const QPointF& pos);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    Document* m_document = nullptr;
    RenderEngine m_renderEngine;
    ViewportState m_viewState;
    bool m_panning = false;
    QPointF m_lastMousePos;
};

} // namespace UDoc

#endif // DOCUMENT_VIEW_H
