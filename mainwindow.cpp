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

    m_documentView = new DocumentView(this);
    setCentralWidget(m_documentView);

    auto doc = std::make_unique<Document>();

    Tab* tab = doc->addTab(TabType::PageSequence, "Document 1");
    PageSequence* ps = tab->initPageSequence();

    Page* page = ps->addPage(doc->generateId());
    page->width  = 800;
    page->height = 1000;
    page->backgroundColor = Color::white();

    // --- Heading ---
    // outlineLevel=1 → font size 24pt. One line ≈ 32px tall. Give it 40px height.
    auto headingElem = std::make_unique<Element>(doc->generateId());
    headingElem->bounds = Rect(100, 50, 600, 40);
    HeadingContent hc;
    CharacterProperties headingProps;
    headingProps.fontSize  = 24.0;
    headingProps.bold      = true;
    headingProps.fontFamily = "Arial";
    hc.setPlainText("Welcome to UDoc", headingProps);
    hc.outlineLevel = 1;
    headingElem->content = std::move(hc);
    page->addElement(std::move(headingElem));

    // --- Text block ---
    // Default font 12pt. Text wraps at width=600 → about 3 lines ≈ 60px.
    auto textElem = std::make_unique<Element>(doc->generateId());
    textElem->bounds = Rect(100, 110, 600, 65);
    TextBlockContent tc;
    tc.setPlainText(
        "Hello UDoc! This is the universal document engine. "
        "You can see this text rendered on screen using the "
        "RenderEngine with QTextLayout integration.",
        CharacterProperties());
    textElem->content = std::move(tc);
    page->addElement(std::move(textElem));

    // --- List ---
    // 3 items × 22px line height = 66px
    auto listElem = std::make_unique<Element>(doc->generateId());
    listElem->bounds = Rect(100, 200, 600, 66);
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

    lc.items.push_back(makeItem("First item"));
    lc.items.push_back(makeItem("Second item"));
    lc.items.push_back(makeItem("Third item"));

    listElem->content = std::move(lc);
    page->addElement(std::move(listElem));

    m_documentView->setDocument(doc.release());
}
