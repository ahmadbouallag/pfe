#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "documentviewport.h"
#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QLabel>
#include <QSpinBox>
#include <QSlider>
#include <QFileInfo>

namespace UDoc {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // File menu
    void onNewDocument();
    void onOpen();
    void onSave();
    void onSaveAs();
    void onExport();
    void onPrint();
    void onExit();

    // Edit menu
    void onUndo();
    void onRedo();
    void onCut();
    void onCopy();
    void onPaste();
    void onSelectAll();
    void onDelete();
    void onPreferences();

    // View menu
    void onZoomIn();
    void onZoomOut();
    void onFitToWidth();
    void onFitToPage();
    void onActualSize();
    void onFullScreen();

    // Insert menu
    void onInsertText();
    void onInsertImage();
    void onInsertTable();
    void onInsertPage();
    void onInsertRectangle();

    // Page menu
    void onNextPage();
    void onPreviousPage();
    void onFirstPage();
    void onLastPage();
    void onGoToPage();

    // Help menu
    void onAbout();

    // Viewport signals
    void onPageChanged(PageIndex idx);
    void onZoomChanged(double zoom);
    void onSelectionChanged(const Selection& sel);

private:
    void setupUI();
    void setupMenus();
    void setupToolbars();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();

    void updateWindowTitle();
    bool confirmSave();

    // Central widget
    DocumentViewport* m_viewport;

    // Current document
    std::unique_ptr<Document> m_currentDoc;
    QString m_currentFilePath;
    bool m_isModified = false;

    // UI elements
    QLabel* m_pageLabel;
    QLabel* m_zoomLabel;
    QLabel* m_statusLabel;
    QSpinBox* m_pageSpinBox;
    QSlider* m_zoomSlider;
};

} // namespace UDoc

#endif // MAINWINDOW_H
