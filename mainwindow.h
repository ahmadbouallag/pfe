#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QStatusBar>
#include "document_view.h"

using namespace UDoc;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void openPdf();
    void onSelectionChanged(const UDoc::Selection& sel);
    void onZoomChanged(double zoom);

private:
    void buildMenuBar();
    void buildStatusBar();
    void loadDocument(std::unique_ptr<UDoc::Document> doc);

    DocumentView* m_documentView = nullptr;
    QLabel*       m_statusLabel  = nullptr;
    QLabel*       m_zoomLabel    = nullptr;

    // Keep document alive for the lifetime of the view
    std::unique_ptr<UDoc::Document> m_document;
};

#endif // MAINWINDOW_H
