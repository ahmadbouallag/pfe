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
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QKeySequence>
#include <QColorDialog>
#include <QPixmap>
#include <QPainter>
#include <QFontComboBox>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
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
    buildFormatToolBar();
    buildViewToolBar();
    buildStatusBar();

    connect(m_documentView, &DocumentView::selectionChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_documentView, &DocumentView::zoomChanged,
            this, &MainWindow::onZoomChanged);
    connect(m_documentView, &DocumentView::textFormatChanged,
            this, &MainWindow::onTextFormatChanged);

    // ---- Demo document ----
    auto doc = std::make_unique<Document>();
    Tab* tab = doc->addTab(TabType::PageSequence, "Document 1");
    PageSequence* ps = tab->initPageSequence();

    Page* page = ps->addPage(doc->generateId());
    page->width  = 800;
    page->height = 1000;
    page->backgroundColor = Color::white();

    // Heading
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(60, 60, 680, 44);
        HeadingContent hc;
        CharacterProperties p;
        p.fontSize = 28.0; p.bold = true; p.fontFamily = "Arial";
        p.color = Color(0.1, 0.1, 0.4, 1.0);
        hc.setPlainText("Welcome to UDoc", p);
        hc.outlineLevel = 1;
        elem->content = std::move(hc);
        page->addElement(std::move(elem));
    }
    // Subheading
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(60, 118, 680, 28);
        HeadingContent hc;
        CharacterProperties p;
        p.fontSize = 16.0; p.bold = true; p.fontFamily = "Arial";
        p.color = Color(0.3, 0.3, 0.5, 1.0);
        hc.setPlainText("Universal Document Engine — Phase 5 complete", p);
        hc.outlineLevel = 2;
        elem->content = std::move(hc);
        page->addElement(std::move(elem));
    }
    // Body text
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(60, 165, 680, 80);
        TextBlockContent tc;
        CharacterProperties p;
        p.fontSize = 12.0; p.fontFamily = "Arial";
        tc.setPlainText(
            "Double-click any text element to edit it. "
            "Use the toolbar to change font, size, bold, italic and colour. "
            "Open a PDF via File → Open PDF to import and view real documents.",
            p);
        elem->content = std::move(tc);
        page->addElement(std::move(elem));
    }
    // List
    {
        auto elem = std::make_unique<Element>(doc->generateId());
        elem->bounds = Rect(60, 265, 680, 90);
        ListContent lc;
        lc.type = ListType::Bullet;
        auto makeItem = [&](const QString& text) {
            ListContent::Item item;
            item.content = std::make_unique<Element>(doc->generateId());
            TextBlockContent tc;
            CharacterProperties p; p.fontSize = 12.0; p.fontFamily = "Arial";
            tc.setPlainText(text, p);
            item.content->content = std::move(tc);
            return item;
        };
        lc.items.push_back(makeItem("Double-click text to enter edit mode"));
        lc.items.push_back(makeItem("Arrow keys, Home/End, Ctrl+A/C/V/X all work"));
        lc.items.push_back(makeItem("PDF parser: text, vectors, images — no Poppler"));
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

    QAction* openPdfAct = fileMenu->addAction("&Open PDF…");
    openPdfAct->setShortcut(QKeySequence::Open);
    connect(openPdfAct, &QAction::triggered, this, &MainWindow::openPdf);

    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QMainWindow::close);

    QMenu* editMenu = menuBar()->addMenu("&Edit");
    QAction* undoAct = editMenu->addAction("&Undo");
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, m_documentView, &DocumentView::undo);

    QAction* redoAct = editMenu->addAction("&Redo");
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, m_documentView, &DocumentView::redo);
}

// ============================================================
//  Format toolbar
// ============================================================
void MainWindow::buildFormatToolBar() {
    m_formatBar = addToolBar("Format");
    m_formatBar->setIconSize(QSize(18, 18));
    m_formatBar->setMovable(false);

    // ---- Font family ----
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setCurrentFont(QFont("Arial"));
    m_fontCombo->setMaximumWidth(180);
    m_fontCombo->setToolTip("Font family");
    m_formatBar->addWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged,
            this, &MainWindow::onFontFamilyChanged);

    // ---- Font size ----
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setRange(6, 288);
    m_sizeSpin->setValue(12);
    m_sizeSpin->setSuffix(" pt");
    m_sizeSpin->setMaximumWidth(72);
    m_sizeSpin->setToolTip("Font size");
    m_formatBar->addWidget(m_sizeSpin);
    connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onFontSizeChanged);

    m_formatBar->addSeparator();

    // ---- Bold ----
    m_boldAct = m_formatBar->addAction("B");
    m_boldAct->setCheckable(true);
    m_boldAct->setToolTip("Bold (Ctrl+B)");
    m_boldAct->setShortcut(QKeySequence("Ctrl+B"));
    QFont boldF = m_boldAct->font(); boldF.setBold(true); boldF.setPointSize(11);
    m_boldAct->setFont(boldF);
    connect(m_boldAct, &QAction::toggled, this, &MainWindow::onBoldToggled);

    // ---- Italic ----
    m_italicAct = m_formatBar->addAction("I");
    m_italicAct->setCheckable(true);
    m_italicAct->setToolTip("Italic (Ctrl+I)");
    m_italicAct->setShortcut(QKeySequence("Ctrl+I"));
    QFont italicF = m_italicAct->font(); italicF.setItalic(true); italicF.setPointSize(11);
    m_italicAct->setFont(italicF);
    connect(m_italicAct, &QAction::toggled, this, &MainWindow::onItalicToggled);

    // ---- Underline ----
    m_underlineAct = m_formatBar->addAction("U");
    m_underlineAct->setCheckable(true);
    m_underlineAct->setToolTip("Underline (Ctrl+U)");
    m_underlineAct->setShortcut(QKeySequence("Ctrl+U"));
    QFont uF = m_underlineAct->font(); uF.setUnderline(true); uF.setPointSize(11);
    m_underlineAct->setFont(uF);
    connect(m_underlineAct, &QAction::toggled, this, &MainWindow::onUnderlineToggled);

    m_formatBar->addSeparator();

    // ---- Text colour ----
    m_colorAct = m_formatBar->addAction("A");
    m_colorAct->setToolTip("Text colour");
    {
        // Underline-style colour swatch under the letter "A"
        QFont cf = m_colorAct->font(); cf.setBold(true); cf.setPointSize(11);
        m_colorAct->setFont(cf);
    }
    connect(m_colorAct, &QAction::triggered, this, &MainWindow::onColorPicker);

    m_formatBar->addSeparator();

    // ---- Alignment ----
    auto* alignGroup = new QActionGroup(this);
    alignGroup->setExclusive(true);

    m_alignLeftAct   = m_formatBar->addAction("≡L");
    m_alignCenterAct = m_formatBar->addAction("≡C");
    m_alignRightAct  = m_formatBar->addAction("≡R");
    m_alignJustAct   = m_formatBar->addAction("≡J");

    for (auto* a : {m_alignLeftAct, m_alignCenterAct,
                    m_alignRightAct, m_alignJustAct}) {
        a->setCheckable(true);
        alignGroup->addAction(a);
    }
    m_alignLeftAct->setChecked(true);
    m_alignLeftAct->setToolTip("Align left");
    m_alignCenterAct->setToolTip("Centre");
    m_alignRightAct->setToolTip("Align right");
    m_alignJustAct->setToolTip("Justify");

    connect(m_alignLeftAct,   &QAction::triggered, this, &MainWindow::onAlignLeft);
    connect(m_alignCenterAct, &QAction::triggered, this, &MainWindow::onAlignCenter);
    connect(m_alignRightAct,  &QAction::triggered, this, &MainWindow::onAlignRight);
    connect(m_alignJustAct,   &QAction::triggered, this, &MainWindow::onAlignJustify);
}

// ============================================================
//  View toolbar
// ============================================================
void MainWindow::buildViewToolBar() {
    QToolBar* viewBar = addToolBar("View");
    viewBar->setMovable(false);

    QAction* zoomIn  = viewBar->addAction("＋ Zoom In");
    QAction* zoomOut = viewBar->addAction("－ Zoom Out");
    QAction* zoom100 = viewBar->addAction("100%");
    QAction* zoomFit = viewBar->addAction("Fit");
    QAction* selTool = viewBar->addAction("↖ Select");
    QAction* panTool = viewBar->addAction("✋ Pan");

    zoomIn ->setShortcut(QKeySequence::ZoomIn);
    zoomOut->setShortcut(QKeySequence::ZoomOut);

    connect(zoomIn,  &QAction::triggered, this, [this]{ m_documentView->setZoom(m_documentView->zoom() * 1.25); });
    connect(zoomOut, &QAction::triggered, this, [this]{ m_documentView->setZoom(m_documentView->zoom() / 1.25); });
    connect(zoom100, &QAction::triggered, this, [this]{ m_documentView->setZoom(1.0); });
    connect(zoomFit, &QAction::triggered, this, [this]{
        // Fit the first page width to the viewport
        if (!m_document) return;
        Tab* tab = m_document->activeTab();
        if (!tab) return;
        if (auto* ps = tab->pageSequence()) {
            if (!ps->pages.empty()) {
                double vpW = m_documentView->width() - 60.0;
                double pageW = ps->pages[0]->width;
                if (pageW > 0) m_documentView->setZoom(vpW / pageW);
            }
        }
    });
    connect(selTool, &QAction::triggered, m_documentView, &DocumentView::startSelectTool);
    connect(panTool, &QAction::triggered, m_documentView, &DocumentView::startPanTool);
}

// ============================================================
//  Status bar
// ============================================================
void MainWindow::buildStatusBar() {
    m_statusLabel = new QLabel("Ready  |  Double-click text to edit");
    m_zoomLabel   = new QLabel("100%");
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

// ============================================================
//  Toolbar helper — block signals while updating controls
// ============================================================
void MainWindow::blockToolbarSignals(bool block) {
    m_boldAct->blockSignals(block);
    m_italicAct->blockSignals(block);
    m_underlineAct->blockSignals(block);
    m_fontCombo->blockSignals(block);
    m_sizeSpin->blockSignals(block);
}

// ============================================================
//  Build CharacterProperties from toolbar state
// ============================================================
CharacterProperties MainWindow::currentCharProps() const {
    CharacterProperties p;
    p.fontFamily  = m_fontCombo->currentFont().family();
    p.fontSize    = (double)m_sizeSpin->value();
    p.bold        = m_boldAct->isChecked();
    p.italic      = m_italicAct->isChecked();
    p.underline   = m_underlineAct->isChecked();
    p.color       = Color(m_textColor.redF(), m_textColor.greenF(),
                          m_textColor.blueF(), 1.0);
    return p;
}

// ============================================================
//  Toolbar slots — apply format changes
// ============================================================
void MainWindow::onBoldToggled(bool) {
    m_documentView->applyCharFormat(currentCharProps());
}
void MainWindow::onItalicToggled(bool) {
    m_documentView->applyCharFormat(currentCharProps());
}
void MainWindow::onUnderlineToggled(bool) {
    m_documentView->applyCharFormat(currentCharProps());
}
void MainWindow::onFontFamilyChanged(const QFont&) {
    m_documentView->applyCharFormat(currentCharProps());
}
void MainWindow::onFontSizeChanged(int) {
    m_documentView->applyCharFormat(currentCharProps());
}

void MainWindow::onColorPicker() {
    QColor c = QColorDialog::getColor(m_textColor, this, "Text Colour");
    if (!c.isValid()) return;
    m_textColor = c;
    // Update the button icon — draw a coloured bar under "A"
    QPixmap px(16, 4);
    px.fill(c);
    m_colorAct->setIcon(QIcon(px));
    m_documentView->applyCharFormat(currentCharProps());
}

void MainWindow::onAlignLeft()    {
    CharacterProperties p = currentCharProps();
    // TODO Phase 9: apply paragraph alignment via ParagraphProperties
    // For now just refresh display
    (void)p;
}
void MainWindow::onAlignCenter()  { onAlignLeft(); }
void MainWindow::onAlignRight()   { onAlignLeft(); }
void MainWindow::onAlignJustify() { onAlignLeft(); }

// ============================================================
//  Open PDF
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

        size_t pages = result.document->totalPages();
        size_t words = result.document->wordCount();
        loadDocument(std::move(result.document));

        statusBar()->showMessage(
            QString("Opened: %1 page%2, ~%3 word%4")
                .arg(pages).arg(pages==1?"":"s")
                .arg(words).arg(words==1?"":"s"), 6000);
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
    if (sel.isEmpty()) {
        m_statusLabel->setText("Ready  |  Double-click text to edit");
    } else if (sel.isSingle()) {
        // Try to show format of the selected element in the toolbar
        if (m_document) {
            Tab* tab = m_document->activeTab();
            if (tab) {
                if (auto* ps = tab->pageSequence()) {
                    for (auto& page : ps->pages) {
                        if (Element* elem = page->findElement(sel.firstElementId())) {
                            TextBlockContent* tc = nullptr;
                            if (elem->isTextBlock())    tc = elem->textContent();
                            else if (elem->isHeading()) tc = elem->headingContent();
                            if (tc && !tc->runs.empty()) {
                                onTextFormatChanged(tc->runs[0].props);
                            }
                            break;
                        }
                    }
                }
            }
        }
        m_statusLabel->setText(QString("Element %1 selected  |  Double-click to edit text")
                                .arg(sel.firstElementId()));
    } else {
        m_statusLabel->setText(QString("%1 elements selected")
                                .arg(sel.elementIds.size()));
    }
}

void MainWindow::onZoomChanged(double zoom) {
    m_zoomLabel->setText(QString("%1%").arg((int)std::round(zoom * 100)));
}

// Update toolbar controls to reflect the current text format
void MainWindow::onTextFormatChanged(const UDoc::CharacterProperties& props) {
    blockToolbarSignals(true);

    if (!props.fontFamily.isEmpty())
        m_fontCombo->setCurrentFont(QFont(props.fontFamily));
    if (props.fontSize > 0)
        m_sizeSpin->setValue((int)std::round(props.fontSize));
    m_boldAct->setChecked(props.bold);
    m_italicAct->setChecked(props.italic);
    m_underlineAct->setChecked(props.underline);

    m_textColor = QColor::fromRgbF(
        std::clamp(props.color.r, 0.0, 1.0),
        std::clamp(props.color.g, 0.0, 1.0),
        std::clamp(props.color.b, 0.0, 1.0));

    QPixmap px(16, 4);
    px.fill(m_textColor);
    m_colorAct->setIcon(QIcon(px));

    blockToolbarSignals(false);
}
