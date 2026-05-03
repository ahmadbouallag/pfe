#include <QApplication>
#include "document.h"
#include "tab.h"
#include "page.h"
#include "element.h"
#include "block.h"
#include <QDebug>

using namespace UDoc;

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Create document
    auto doc = std::make_unique<Document>();

    // Add a page sequence tab
    Tab* tab = doc->addTab(TabType::PageSequence, "Document 1");
    PageSequence* ps = tab->initPageSequence();

    // Add a page
    Page* page = ps->addPage(doc->generateId());

    // Add a text element
    auto textElem = std::make_unique<Element>(doc->generateId());
    textElem->bounds = Rect(100, 100, 400, 200);
    TextBlockContent tc;
    tc.setPlainText("Hello UDoc! This is the universal document engine.", CharacterProperties());
    textElem->content = std::move(tc);
    page->addElement(std::move(textElem));

    // Add a rectangle element
    auto rectElem = std::make_unique<Element>(doc->generateId());
    rectElem->bounds = Rect(100, 350, 200, 100);
    VectorGraphic vec;
    vec.commands.push_back({PathCommandType::MoveTo, 0, 0, 0,0,0,0});
    vec.commands.push_back({PathCommandType::LineTo, 200, 0, 0,0,0,0});
    vec.commands.push_back({PathCommandType::LineTo, 200, 100, 0,0,0,0});
    vec.commands.push_back({PathCommandType::LineTo, 0, 100, 0,0,0,0});
    vec.commands.push_back({PathCommandType::ClosePath, 0,0,0,0,0,0});
    vec.fill = Fill{Color::blue().withAlpha(0.5)};
    vec.stroke = Stroke{Color::blue(), 2};
    rectElem->content = std::move(vec);
    page->addElement(std::move(rectElem));

    // Test hit testing
    Point hitPoint(150, 150);
    Element* hit = page->hitTest(hitPoint);
    if (hit) {
        qDebug() << "Hit element:" << hit->id();
    }

    // Test story
    auto storyTab = doc->addTab(TabType::FlowDocument, "Story");
    FlowDocument* fd = storyTab->initFlowDocument();
    fd->story->appendText("This is a flowing document. ");
    fd->story->appendText("It has no pages, just content.");

    Block* heading = fd->story->appendBlock(Block::Type::Heading);
    heading->outlineLevel = 1;
    heading->setText("Chapter 1");

    qDebug() << "Document has" << doc->totalElements() << "elements";
    qDebug() << "Document has" << doc->totalPages() << "pages";
    qDebug() << "Word count:" << doc->wordCount();

    return 0;
}
