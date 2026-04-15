#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QCloseEvent>

namespace UDoc {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUI();
    setupMenus();
    setupToolbars();
    setupStatusBar();
    setupConnections();

    setWindowTitle("UDoc Editor");
    resize(1200, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI() {
    // Central widget
    m_viewport = new DocumentViewport(this);
    setCentralWidget(m_viewport);

    // Create initial empty document
    m_currentDoc = std::make_unique<Document>();
    m_currentDoc->addPage();
    m_viewport->setDocument(m_currentDoc.get());
}

void MainWindow::setupMenus() {
    QMenuBar* menuBar = this->menuBar();

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* newAction = new QAction("&New", this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewDocument);
    fileMenu->addAction(newAction);

    QAction* openAction = new QAction("&Open...", this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);
    fileMenu->addAction(openAction);

    QAction* saveAction = new QAction("&Save", this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);
    fileMenu->addAction(saveAction);

    QAction* saveAsAction = new QAction("Save &As...", this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);
    fileMenu->addAction(saveAsAction);

    fileMenu->addSeparator();

    QAction* exportAction = new QAction("&Export...", this);
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExport);
    fileMenu->addAction(exportAction);

    QAction* printAction = new QAction("&Print...", this);
    printAction->setShortcut(QKeySequence::Print);
    connect(printAction, &QAction::triggered, this, &MainWindow::onPrint);
    fileMenu->addAction(printAction);

    fileMenu->addSeparator();

    QAction* exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);
    fileMenu->addAction(exitAction);

    // Edit menu
    QMenu* editMenu = menuBar->addMenu("&Edit");

    QAction* undoAction = new QAction("&Undo", this);
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, &MainWindow::onUndo);
    editMenu->addAction(undoAction);

    QAction* redoAction = new QAction("&Redo", this);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::onRedo);
    editMenu->addAction(redoAction);

    editMenu->addSeparator();

    QAction* cutAction = new QAction("Cu&t", this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, &MainWindow::onCut);
    editMenu->addAction(cutAction);

    QAction* copyAction = new QAction("&Copy", this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, &MainWindow::onCopy);
    editMenu->addAction(copyAction);

    QAction* pasteAction = new QAction("&Paste", this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::onPaste);
    editMenu->addAction(pasteAction);

    QAction* selectAllAction = new QAction("Select &All", this);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, &MainWindow::onSelectAll);
    editMenu->addAction(selectAllAction);

    editMenu->addSeparator();

    QAction* deleteAction = new QAction("&Delete", this);
    deleteAction->setShortcut(QKeySequence::Delete);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDelete);
    editMenu->addAction(deleteAction);

    editMenu->addSeparator();

    QAction* prefsAction = new QAction("Pre&ferences...", this);
    connect(prefsAction, &QAction::triggered, this, &MainWindow::onPreferences);
    editMenu->addAction(prefsAction);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    QAction* zoomInAction = new QAction("Zoom &In", this);
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);
    viewMenu->addAction(zoomInAction);

    QAction* zoomOutAction = new QAction("Zoom &Out", this);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
    viewMenu->addAction(zoomOutAction);

    viewMenu->addSeparator();

    QAction* fitWidthAction = new QAction("&Fit to Width", this);
    connect(fitWidthAction, &QAction::triggered, this, &MainWindow::onFitToWidth);
    viewMenu->addAction(fitWidthAction);

    QAction* fitPageAction = new QAction("Fit to &Page", this);
    connect(fitPageAction, &QAction::triggered, this, &MainWindow::onFitToPage);
    viewMenu->addAction(fitPageAction);

    QAction* actualSizeAction = new QAction("&Actual Size", this);
    connect(actualSizeAction, &QAction::triggered, this, &MainWindow::onActualSize);
    viewMenu->addAction(actualSizeAction);

    viewMenu->addSeparator();

    QAction* fullScreenAction = new QAction("&Full Screen", this);
    fullScreenAction->setShortcut(Qt::Key_F11);
    connect(fullScreenAction, &QAction::triggered, this, &MainWindow::onFullScreen);
    viewMenu->addAction(fullScreenAction);

    // Insert menu
    QMenu* insertMenu = menuBar->addMenu("&Insert");

    QAction* insertTextAction = new QAction("Text &Box", this);
    connect(insertTextAction, &QAction::triggered, this, &MainWindow::onInsertText);
    insertMenu->addAction(insertTextAction);

    QAction* insertImageAction = new QAction("&Image...", this);
    connect(insertImageAction, &QAction::triggered, this, &MainWindow::onInsertImage);
    insertMenu->addAction(insertImageAction);

    QAction* insertTableAction = new QAction("&Table...", this);
    connect(insertTableAction, &QAction::triggered, this, &MainWindow::onInsertTable);
    insertMenu->addAction(insertTableAction);

    insertMenu->addSeparator();

    QAction* insertPageAction = new QAction("&Page", this);
    connect(insertPageAction, &QAction::triggered, this, &MainWindow::onInsertPage);
    insertMenu->addAction(insertPageAction);

    // Page menu
    QMenu* pageMenu = menuBar->addMenu("&Page");

    QAction* nextPageAction = new QAction("&Next", this);
    nextPageAction->setShortcut(QKeySequence::MoveToNextPage);
    connect(nextPageAction, &QAction::triggered, this, &MainWindow::onNextPage);
    pageMenu->addAction(nextPageAction);

    QAction* prevPageAction = new QAction("&Previous", this);
    prevPageAction->setShortcut(QKeySequence::MoveToPreviousPage);
    connect(prevPageAction, &QAction::triggered, this, &MainWindow::onPreviousPage);
    pageMenu->addAction(prevPageAction);

    pageMenu->addSeparator();

    QAction* firstPageAction = new QAction("&First", this);
    connect(firstPageAction, &QAction::triggered, this, &MainWindow::onFirstPage);
    pageMenu->addAction(firstPageAction);

    QAction* lastPageAction = new QAction("&Last", this);
    connect(lastPageAction, &QAction::triggered, this, &MainWindow::onLastPage);
    pageMenu->addAction(lastPageAction);

    QAction* gotoPageAction = new QAction("&Go to Page...", this);
    connect(gotoPageAction, &QAction::triggered, this, &MainWindow::onGoToPage);
    pageMenu->addAction(gotoPageAction);

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");

    QAction* aboutAction = new QAction("&About", this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(aboutAction);

    QAction* insertRectAction = new QAction("&Rectangle", this);
    connect(insertRectAction, &QAction::triggered, this, &MainWindow::onInsertRectangle);
    insertMenu->addAction(insertRectAction);

}

void MainWindow::setupToolbars() {
    QToolBar* fileToolBar = addToolBar("File");

    QAction* newTb = new QAction("New", this);
    connect(newTb, &QAction::triggered, this, &MainWindow::onNewDocument);
    fileToolBar->addAction(newTb);

    QAction* openTb = new QAction("Open", this);
    connect(openTb, &QAction::triggered, this, &MainWindow::onOpen);
    fileToolBar->addAction(openTb);

    QAction* saveTb = new QAction("Save", this);
    connect(saveTb, &QAction::triggered, this, &MainWindow::onSave);
    fileToolBar->addAction(saveTb);

    QToolBar* viewToolBar = addToolBar("View");

    QAction* zoomInTb = new QAction("Zoom In", this);
    connect(zoomInTb, &QAction::triggered, this, &MainWindow::onZoomIn);
    viewToolBar->addAction(zoomInTb);

    QAction* zoomOutTb = new QAction("Zoom Out", this);
    connect(zoomOutTb, &QAction::triggered, this, &MainWindow::onZoomOut);
    viewToolBar->addAction(zoomOutTb);

    QAction* fitWidthTb = new QAction("Fit Width", this);
    connect(fitWidthTb, &QAction::triggered, this, &MainWindow::onFitToWidth);
    viewToolBar->addAction(fitWidthTb);

    QToolBar* editToolBar = addToolBar("Edit");

    QAction* undoTb = new QAction("Undo", this);
    connect(undoTb, &QAction::triggered, this, &MainWindow::onUndo);
    editToolBar->addAction(undoTb);

    QAction* redoTb = new QAction("Redo", this);
    connect(redoTb, &QAction::triggered, this, &MainWindow::onRedo);
    editToolBar->addAction(redoTb);
}

void MainWindow::setupStatusBar() {
    m_pageLabel = new QLabel("Page 1 of 1", this);
    m_zoomLabel = new QLabel("100%", this);
    m_statusLabel = new QLabel("Ready", this);

    statusBar()->addWidget(m_pageLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->addPermanentWidget(m_statusLabel);

    // Page spinner
    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setValue(1);
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int val) { m_viewport->goToPage(val - 1); });
    statusBar()->addWidget(m_pageSpinBox);
}

void MainWindow::setupConnections() {
    connect(m_viewport, &DocumentViewport::pageChanged,
            this, &MainWindow::onPageChanged);
    connect(m_viewport, &DocumentViewport::zoomChanged,
            this, &MainWindow::onZoomChanged);
    connect(m_viewport, &DocumentViewport::selectionChanged,
            this, &MainWindow::onSelectionChanged);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

// === SLOTS ===

void MainWindow::onNewDocument() {
    if (!confirmSave()) return;

    m_currentDoc = std::make_unique<Document>();
    m_currentDoc->addPage();
    m_viewport->setDocument(m_currentDoc.get());
    m_currentFilePath.clear();
    m_isModified = false;
    updateWindowTitle();
}

void MainWindow::onOpen() {
    if (!confirmSave()) return;

    QString path = QFileDialog::getOpenFileName(this, "Open Document", QString(),
                                                "UDoc files (*.udoc);;PDF files (*.pdf);;All files (*.*)");
    if (path.isEmpty()) return;

    // TODO: Parse file based on extension
    // For now, just create new
    m_currentDoc = std::make_unique<Document>();
    m_currentDoc->addPage();
    m_viewport->setDocument(m_currentDoc.get());
    m_currentFilePath = path;
    m_isModified = false;
    updateWindowTitle();
}

void MainWindow::onSave() {
    if (m_currentFilePath.isEmpty()) {
        onSaveAs();
    } else {
        // TODO: Serialize to .udoc format
        m_isModified = false;
        updateWindowTitle();
    }
}

void MainWindow::onSaveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Save Document", QString(),
                                                "UDoc files (*.udoc)");
    if (path.isEmpty()) return;

    m_currentFilePath = path;
    onSave();
}

void MainWindow::onExport() {
    QString path = QFileDialog::getSaveFileName(this, "Export", QString(),
                                                "PDF files (*.pdf);;Images (*.png *.jpg)");
    // TODO: Export logic
}

void MainWindow::onPrint() {
    // TODO: Print dialog
}

void MainWindow::onExit() {
    close();
}

void MainWindow::onUndo() {
    m_viewport->undoStack()->undo();
}

void MainWindow::onRedo() {
    m_viewport->undoStack()->redo();
}

void MainWindow::onCut() {
    // TODO: Cut selection
}

void MainWindow::onCopy() {
    // TODO: Copy selection
}

void MainWindow::onPaste() {
    // TODO: Paste
}

void MainWindow::onSelectAll() {
    // TODO: Select all elements on current page
}

void MainWindow::onDelete() {
    // TODO: Delete selection
}

void MainWindow::onPreferences() {
    // TODO: Preferences dialog
}

void MainWindow::onZoomIn() {
    m_viewport->zoomIn();
}

void MainWindow::onZoomOut() {
    m_viewport->zoomOut();
}

void MainWindow::onFitToWidth() {
    m_viewport->fitToWidth();
}

void MainWindow::onFitToPage() {
    m_viewport->fitToPage();
}

void MainWindow::onActualSize() {
    m_viewport->actualSize();
}

void MainWindow::onFullScreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::onInsertText() {
    m_viewport->startTextTool();
}

/*void MainWindow::onInsertImage() {
    QString path = QFileDialog::getOpenFileName(this, "Insert Image", QString(),
                                                "Images (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (path.isEmpty()) return;

    // Create image element
    auto elem = std::make_unique<Element>(m_currentDoc->generateId());
    ImageElement img;
    // TODO: Load image file into img.data
    elem->content = img;
    elem->bounds = Rect(100, 100, 200, 200);  // Default size

    // Add to current page
    if (Page* page = m_currentDoc->pageAt(m_viewport->selection().pageIdx)) {
        page->addElement(std::move(elem));
        m_viewport->update();
    }
}*/
void MainWindow::onInsertImage() {
    QString path = QFileDialog::getOpenFileName(this, "Insert Image", QString(),
                                                "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tiff)");
    if (path.isEmpty()) return;

    m_viewport->startImageTool(path);
}

void MainWindow::onInsertRectangle() {
    m_viewport->startRectangleTool();
}

void MainWindow::onInsertTable() {
    bool ok;
    int rows = QInputDialog::getInt(this, "Insert Table", "Rows:", 3, 1, 100, 1, &ok);
    if (!ok) return;
    int cols = QInputDialog::getInt(this, "Insert Table", "Columns:", 3, 1, 100, 1, &ok);
    if (!ok) return;

    // Create table element
    auto elem = std::make_unique<Element>(m_currentDoc->generateId());
    TableElement table;
    table.rows = rows;
    table.cols = cols;

    // Initialize row/column heights/widths
    for (uint32_t i = 0; i < rows; ++i) {
        table.rowHeights.push_back(30.0);  // 30 points per row
    }
    for (uint32_t i = 0; i < cols; ++i) {
        table.colWidths.push_back(100.0);  // 100 points per column
    }

    // Create cells
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            TableCell cell;
            cell.row = r;
            cell.col = c;

            // Calculate position
            double x = 100.0;  // Offset from page
            for (uint32_t i = 0; i < c; ++i) x += table.colWidths[i];

            double y = 100.0;
            for (uint32_t i = 0; i < r; ++i) y += table.rowHeights[i];

            cell.bounds = Rect(x, y, table.colWidths[c], table.rowHeights[r]);
            table.cells.push_back(cell);
        }
    }

    elem->content = std::move(table);  // FIX: Use std::move
    elem->bounds = Rect(100, 100, cols * 100, rows * 30);

    if (Page* page = m_currentDoc->pageAt(m_viewport->selection().pageIdx)) {
        page->addElement(std::move(elem));
        m_viewport->update();
    }
}

void MainWindow::onInsertPage() {
    m_currentDoc->addPage(m_viewport->selection().pageIdx + 1);
    m_viewport->update();
    onPageChanged(m_viewport->selection().pageIdx + 1);
}

void MainWindow::onNextPage() {
    m_viewport->goToPage(m_viewport->selection().pageIdx + 1);
}

void MainWindow::onPreviousPage() {
    m_viewport->goToPage(m_viewport->selection().pageIdx > 0 ?
                             m_viewport->selection().pageIdx - 1 : 0);
}

void MainWindow::onFirstPage() {
    m_viewport->goToPage(0);
}

void MainWindow::onLastPage() {
    m_viewport->goToPage(m_currentDoc->pages.size() - 1);
}

void MainWindow::onGoToPage() {
    bool ok;
    int page = QInputDialog::getInt(this, "Go to Page", "Page number:",
                                    m_viewport->selection().pageIdx + 1,
                                    1, m_currentDoc->pages.size(), 1, &ok);
    if (ok) {
        m_viewport->goToPage(page - 1);
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, "About UDoc Editor",
                       "<h2>UDoc Editor 0.1</h2>"
                       "<p>A document-agnostic editor with custom rendering engine.</p>"
                       "<p>Built with Qt and C++.</p>");
}

void MainWindow::onPageChanged(PageIndex idx) {
    m_pageLabel->setText(QString("Page %1 of %2")
                             .arg(idx + 1).arg(m_currentDoc->pages.size()));
    m_pageSpinBox->setValue(idx + 1);
}

void MainWindow::onZoomChanged(double zoom) {
    m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
}

void MainWindow::onSelectionChanged(const Selection& sel) {
    if (sel.isEmpty()) {
        m_statusLabel->setText("Ready");
    } else {
        m_statusLabel->setText(QString("Selected element %1").arg(sel.elementId));
    }
}

void MainWindow::updateWindowTitle() {
    QString title = m_currentFilePath.isEmpty() ? "Untitled" :
                        QFileInfo(m_currentFilePath).fileName();
    if (m_isModified) title += " *";
    title += " - UDoc Editor";
    setWindowTitle(title);
}

bool MainWindow::confirmSave() {
    if (!m_isModified) return true;

    int ret = QMessageBox::warning(this, "Unsaved Changes",
                                   "The document has been modified. Do you want to save your changes?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save) {
        onSave();
        return !m_isModified;  // If save failed, still modified
    } else if (ret == QMessageBox::Cancel) {
        return false;
    }
    return true;
}

void MainWindow::setupDockWidgets() {
    // TODO: Implement dock widgets
}

} // namespace UDoc
