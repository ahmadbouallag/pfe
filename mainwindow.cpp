#include "mainwindow.h"
#include "document.h"
#include "tab.h"
#include "page.h"
#include "element.h"
#include "block.h"

#include "parsers/pdf/pdf_parser.h"

#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QKeySequence>
#include <cmath>

using namespace UDoc;

// ============================================================
//  Construction
// ============================================================

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("UDoc — Universal Document Engine");
    resize(1280, 900);

    m_documentView = new DocumentView(this);
    setCentralWidget(m_documentView);

    buildMenuBar();
    buildStatusBar();

    connect(m_documentView, &DocumentView::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_documentView, &DocumentView::zoomChanged,
            this, &MainWindow::onZoomChanged);

    // ---- Built-in demo document ----
    auto doc = std::make_unique<Document>();
    Tab* tab = doc->addTab(TabType::PageSequence, "Document 1");
    PageSequence* ps = tab->initPageSequence();

    Page* page = ps->addPage(doc->generateId());
    page->width  = 800;
    page->height = 1000;
    page->backgroundColor = Color::white();

    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(100, 50, 600, 40);
        HeadingContent hc;
        CharacterProperties p;
        p.fontSize = 24.0; p.bold = true; p.fontFamily = "Arial";
        hc.setPlainText("Welcome to UDoc", p);
        hc.outlineLevel = 1;
        elem->content = std::move(hc);
        page->addElement(std::move(elem));
    }
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(100, 110, 600, 65);
        TextBlockContent tc;
        tc.setPlainText(
            "Hello UDoc! This is the universal document engine. "
            "Open a PDF via File → Open PDF to see the parser in action.",
            CharacterProperties());
        elem->content = std::move(tc);
        page->addElement(std::move(elem));
    }
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(100, 200, 600, 66);
        ListContent lc;
        lc.type = ListType::Bullet;
        auto makeItem = [&](const QString& text) {
            ListContent::Item item;
            item.content = std::make_unique<Element>(doc->generateId());
            TextBlockContent itemTc;
            itemTc.setPlainText(text, CharacterProperties());
            item.content->content = std::move(itemTc);
            return item;
        };
        lc.items.push_back(makeItem("Phase 5: PDF Parser — complete"));
        lc.items.push_back(makeItem("Text, vectors, images extracted from scratch"));
        lc.items.push_back(makeItem("No poppler, no pdfium — pure C++ + zlib"));
        elem->content = std::move(lc);
        page->addElement(std::move(elem));
    }

    loadDocument(std::move(doc));
}

// ============================================================
//  Menu bar
// ============================================================

void MainWindow::buildMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* openPdfAction = fileMenu->addAction("&Open PDF…");
    openPdfAction->setShortcut(QKeySequence::Open);
    connect(openPdfAction, &QAction::triggered, this, &MainWindow::openPdf);

    fileMenu->addSeparator();

    QAction* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    QAction* zoomInAction  = viewMenu->addAction("Zoom &In");
    QAction* zoomOutAction = viewMenu->addAction("Zoom &Out");
    QAction* zoomFitAction = viewMenu->addAction("&Fit Page");
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);

    connect(zoomInAction,  &QAction::triggered, this, [this]{
        m_documentView->setZoom(m_documentView->zoom() * 1.25);
    });
    connect(zoomOutAction, &QAction::triggered, this, [this]{
        m_documentView->setZoom(m_documentView->zoom() / 1.25);
    });
    connect(zoomFitAction, &QAction::triggered, this, [this]{
        m_documentView->setZoom(1.0);
    });
}

// ============================================================
//  Status bar
// ============================================================

void MainWindow::buildStatusBar() {
    m_statusLabel = new QLabel("Ready");
    m_zoomLabel   = new QLabel("100%");
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

// ============================================================
//  Open PDF — all exceptions caught, nothing can crash the app
// ============================================================

void MainWindow::openPdf() {
    QString path;
    try {
        path = QFileDialog::getOpenFileName(
            this, "Open PDF", QString(),
            "PDF Files (*.pdf);;All Files (*)");
    } catch (...) {
        QMessageBox::critical(this, "Error", "Failed to open file dialog.");
        return;
    }

    if (path.isEmpty()) return;

    statusBar()->showMessage("Parsing " + QFileInfo(path).fileName() + "…");
    QApplication::setOverrideCursor(Qt::WaitCursor);

    Pdf::ParseResult result;
    try {
        Pdf::PdfParser parser;
        result = parser.parse(path);
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, "PDF Parse Error",
            QString("Exception: ") + e.what());
        statusBar()->showMessage("Parse failed.", 4000);
        return;
    } catch (...) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, "PDF Parse Error",
            "An unknown error occurred while parsing the PDF.\n"
            "The file may be corrupt, encrypted, or use an unsupported feature.");
        statusBar()->showMessage("Parse failed.", 4000);
        return;
    }

    QApplication::restoreOverrideCursor();

    if (!result.ok || !result.document) {
        QMessageBox::critical(this, "PDF Parse Error", result.errorMessage);
        statusBar()->showMessage("Failed to open PDF.", 4000);
        return;
    }

    try {
        QString title = result.document->metadata.title;
        if (title.isEmpty()) title = QFileInfo(path).fileName();
        setWindowTitle("UDoc — " + title);

        size_t pageCount = result.document->totalPages();
        size_t wordCount = result.document->wordCount();

        loadDocument(std::move(result.document));

        statusBar()->showMessage(
            QString("Opened: %1 page%2, ~%3 word%4")
                .arg(pageCount)
                .arg(pageCount == 1 ? "" : "s")
                .arg(wordCount)
                .arg(wordCount == 1 ? "" : "s"),
            6000);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Warning",
            QString("PDF parsed but display failed: ") + e.what());
    } catch (...) {
        QMessageBox::warning(this, "Warning",
            "PDF parsed but an error occurred during display.");
    }
}

// ============================================================
//  Load document
// ============================================================

void MainWindow::loadDocument(std::unique_ptr<UDoc::Document> doc) {
    m_document = std::move(doc);
    m_documentView->setDocument(m_document.get());
    m_documentView->setZoom(1.0);
}

// ============================================================
//  Slots
// ============================================================

void MainWindow::onSelectionChanged(const UDoc::Selection& sel) {
    if (sel.isEmpty())
        m_statusLabel->setText("Ready");
    else if (sel.isSingle())
        m_statusLabel->setText(
            QString("Selected element ID %1").arg(sel.firstElementId()));
    else
        m_statusLabel->setText(
            QString("%1 elements selected").arg(sel.elementIds.size()));
}

void MainWindow::onZoomChanged(double zoom) {
    m_zoomLabel->setText(QString("%1%").arg((int)std::round(zoom * 100)));
}
