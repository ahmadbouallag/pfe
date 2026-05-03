#include "tab.h"
#include "element.h"

namespace UDoc {

Tab::Tab(ID id, TabType type, const QString& name)
    : UDocObject(id), type(type), name(name) {}

PageSequence* Tab::pageSequence() { return std::get_if<PageSequence>(&content); }
SlideSequence* Tab::slideSequence() { return std::get_if<SlideSequence>(&content); }
GridSheet* Tab::gridSheet() { return std::get_if<GridSheet>(&content); }
InfiniteCanvas* Tab::infiniteCanvas() { return std::get_if<InfiniteCanvas>(&content); }
FlowDocument* Tab::flowDocument() { return std::get_if<FlowDocument>(&content); }

PageSequence* Tab::initPageSequence(const PageLayout& layout) {
    content = PageSequence();
    auto* ps = pageSequence();
    ps->defaultLayout = layout;
    return ps;
}

SlideSequence* Tab::initSlideSequence(double width, double height) {
    content = SlideSequence();
    auto* ss = slideSequence();
    ss->master = std::make_unique<SlideSequence::SlideMaster>();
    ss->master->id = id();
    ss->master->width = width;
    ss->master->height = height;
    return ss;
}

GridSheet* Tab::initGridSheet() {
    content = GridSheet();
    return gridSheet();
}

InfiniteCanvas* Tab::initInfiniteCanvas() {
    content = InfiniteCanvas();
    return infiniteCanvas();
}

FlowDocument* Tab::initFlowDocument() {
    content = FlowDocument();
    auto* fd = flowDocument();
    fd->story = std::make_unique<Story>(id());
    fd->story->setParent(this);
    return fd;
}

// === PAGE SEQUENCE ===
Page* PageSequence::addPage(ID id) {
    auto page = std::make_unique<Page>(id);
    Page* raw = page.get();
    pages.push_back(std::move(page));
    return raw;
}

Page* PageSequence::insertPage(size_t index, ID id) {
    auto page = std::make_unique<Page>(id);
    Page* raw = page.get();
    if (index >= pages.size()) pages.push_back(std::move(page));
    else pages.insert(pages.begin() + index, std::move(page));
    return raw;
}

void PageSequence::deletePage(ID pageId) {
    pages.erase(std::remove_if(pages.begin(), pages.end(),
        [pageId](const auto& p) { return p->id() == pageId; }), pages.end());
}

Page* PageSequence::findPage(ID pageId) {
    for (auto& page : pages) if (page->id() == pageId) return page.get();
    return nullptr;
}

// === SLIDE SEQUENCE ===
Slide* SlideSequence::addSlide(ID id) {
    auto slide = std::make_unique<Slide>();
    slide->id = id;
    Slide* raw = slide.get();
    slides.push_back(std::move(slide));
    return raw;
}

void SlideSequence::deleteSlide(ID slideId) {
    slides.erase(std::remove_if(slides.begin(), slides.end(),
        [slideId](const auto& s) { return s->id == slideId; }), slides.end());
}

// === GRID SHEET ===
Sheet* GridSheet::addSheet(const QString& name, ID id) {
    auto sheet = std::make_unique<Sheet>();
    Sheet* raw = sheet.get();
    namedSheets[name] = std::move(sheet);
    return raw;
}

Sheet* GridSheet::findSheet(const QString& name) {
    auto it = namedSheets.find(name);
    return (it != namedSheets.end()) ? it->second.get() : nullptr;
}

// === INFINITE CANVAS ===
void InfiniteCanvas::recalculateBounds() {
    if (elements.empty()) { contentBounds = Rect(); return; }
    Rect bounds = elements[0]->transformedBounds();
    for (size_t i = 1; i < elements.size(); ++i)
        bounds.expandToInclude(elements[i]->transformedBounds());
    contentBounds = bounds;
}

} // namespace UDoc
