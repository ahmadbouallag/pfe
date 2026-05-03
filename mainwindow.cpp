#include "mainwindow.h"
#include "document.h"
#include "tab.h"
#include "page.h"
#include "element.h"
#include "block.h"

using namespace UDoc;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("UDoc — Universal Document Engine");
    resize(1200, 800);
    
    // Create document view widget
    m_documentView = new DocumentView(this);
    setCentralWidget(m_documentView);
    
    // Create a test document
    auto doc = std::make_unique<Document>();
    
    // Add a page sequence tab
    Tab* tab = doc->addTab(TabType::PageSequence, "Document 1");
    PageSequence* ps = tab->initPageSequence();
    
    // Add a page
    Page* page = ps->addPage(doc->generateId());
    page->width = 800;
    page->height = 600;
    page->backgroundColor = Color::white();
    
    // Add a text element
    auto textElem = std::make_unique<Element>(doc->generateId());
    textElem->bounds = Rect(100, 100, 400, 200);
    TextBlockContent tc;
    tc.setPlainText("Hello UDoc! This is the universal document engine.\n\nYou can see this text rendered on screen using the RenderEngine with QTextLayout integration.", CharacterProperties());
    textElem->content = std::move(tc);
    page->addElement(std::move(textElem));
    
    // Add a heading element
    auto headingElem = std::make_unique<Element>(doc->generateId());
    headingElem->bounds = Rect(100, 50, 400, 50);
    HeadingContent hc;
    hc.text = "Welcome to UDoc";
    hc.outlineLevel = 1;
    headingElem->content = std::move(hc);
    page->addElement(std::move(headingElem));
    
    // Add a list element
    auto listElem = std::make_unique<Element>(doc->generateId());
    listElem->bounds = Rect(100, 350, 400, 150);
    ListContent lc;
    lc.type = ListType::Bullet;
    ListContent::Item item1;
    item1.content = std::make_unique<Element>(doc->generateId());
    TextBlockContent tc1;
    tc1.setPlainText("First item", CharacterProperties());
    item1.content->content = std::move(tc1);
    lc.items.push_back(std::move(item1));
    
    ListContent::Item item2;
    item2.content = std::make_unique<Element>(doc->generateId());
    TextBlockContent tc2;
    tc2.setPlainText("Second item", CharacterProperties());
    item2.content->content = std::move(tc2);
    lc.items.push_back(std::move(item2));
    
    ListContent::Item item3;
    item3.content = std::make_unique<Element>(doc->generateId());
    TextBlockContent tc3;
    tc3.setPlainText("Third item", CharacterProperties());
    item3.content->content = std::move(tc3);
    lc.items.push_back(std::move(item3));
    
    listElem->content = std::move(lc);
    page->addElement(std::move(listElem));
    
    // Set the document to the view
    m_documentView->setDocument(doc.release());
}
