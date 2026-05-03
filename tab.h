#ifndef TAB_H
#define TAB_H

#include "udoc_types.h"
#include "udoc_object.h"
#include "properties.h"
#include "story.h"
#include "image.h"
#include "header_footer.h"
#include <variant>
#include <memory>
#include <vector>

namespace UDoc {

// === STUB TYPES for future phases ===
struct Slide {
    ID id = NULL_ID;
    double width = 720, height = 540;
    Color backgroundColor = Color::white();
    std::vector<std::unique_ptr<class Element>> elements;
};

struct Cell {
    std::variant<double, QString, bool> value;
};

struct CellRange {
    uint32_t startRow, startCol, endRow, endCol;
};

struct ColumnProps {
    double width = -1;
    bool hidden = false;
};

struct RowProps {
    double height = -1;
    bool hidden = false;
};

struct Sheet {
    std::unordered_map<uint64_t, Cell> cells;
    std::unordered_map<uint32_t, ColumnProps> columns;
    std::unordered_map<uint32_t, RowProps> rows;
    std::unordered_map<QString, CellRange> namedRanges;
    struct { uint32_t frozenRows = 0, frozenCols = 0; double zoom = 1.0;
        bool showGridLines = true, showHeaders = true; } viewState;
};

struct CanvasViewport {
    Point offset;
    double zoom = 1.0;
};

struct InfiniteCanvas {
    Rect contentBounds;
    std::vector<std::unique_ptr<class Element>> elements;
    CanvasViewport viewport;
    bool showGrid = true;
    double gridSize = 20.0;
    bool snapToGrid = false;
    bool snapToObjects = false;
    void recalculateBounds();
};

struct FlowDocument {
    std::unique_ptr<Story> story;
    double viewportWidth = 612;
    double scrollY = 0;
    std::unordered_map<QString, ParagraphProperties> paragraphStyles;
    std::unordered_map<QString, CharacterProperties> characterStyles;
};

// Forward declaration
class Page;

// === PAGE SEQUENCE ===
class PageSequence {
public:
    std::vector<std::unique_ptr<Page>> pages;
    PageLayout defaultLayout;
    std::unordered_map<ID, std::unique_ptr<Page>> masterPages;
    std::optional<HeaderFooter> headerFooter;
    bool needsRepagination = true;

    Page* addPage(ID id);
    Page* insertPage(size_t index, ID id);
    void deletePage(ID pageId);
    Page* findPage(ID pageId);
    Page* pageAt(size_t index) { return (index < pages.size()) ? pages[index].get() : nullptr; }
};

// === SLIDE SEQUENCE ===
struct SlideTransition {
    enum Type { None, Fade, Push, Wipe, Split, Reveal, Random } type = None;
    int durationMs = 500;
    bool advanceOnClick = true;
    std::optional<int> advanceAfterMs;
};

struct SlideSequence {
    struct SlideMaster {
        ID id;
        double width = 720, height = 540;
        Color backgroundColor = Color::white();
        std::optional<Image> backgroundImage;
        std::vector<std::unique_ptr<class Element>> placeholders;
    };
    std::vector<std::unique_ptr<Slide>> slides;
    std::unique_ptr<SlideMaster> master;
    std::vector<SlideTransition> transitions;

    Slide* addSlide(ID id);
    void deleteSlide(ID slideId);
};

// === GRID SHEET ===
struct GridSheet {
    std::vector<std::unique_ptr<Sheet>> sheets;
    std::unordered_map<QString, std::unique_ptr<Sheet>> namedSheets;
    Sheet* addSheet(const QString& name, ID id);
    Sheet* findSheet(const QString& name);
};

// === TAB CLASS ===
class Tab : public UDocObject {
public:
    QString name;
    TabType type;
    Color tabColor = Color::transparent();

    std::variant<
        PageSequence,
        SlideSequence,
        GridSheet,
        InfiniteCanvas,
        FlowDocument
        > content;

    struct {
        double zoom = 1.0;
        Point scrollOffset;
        ID activePageId = NULL_ID;
    } viewState;

    explicit Tab(ID id, TabType type, const QString& name);
    ObjectType objectType() const override { return ObjectType::Tab; }

    PageSequence* pageSequence();
    SlideSequence* slideSequence();
    GridSheet* gridSheet();
    InfiniteCanvas* infiniteCanvas();
    FlowDocument* flowDocument();

    PageSequence* initPageSequence(const PageLayout& layout = PageLayout());
    SlideSequence* initSlideSequence(double width = 720, double height = 540);
    GridSheet* initGridSheet();
    InfiniteCanvas* initInfiniteCanvas();
    FlowDocument* initFlowDocument();
};

} // namespace UDoc

// Include page.h at the end to avoid circular dependency
#include "page.h"

#endif // TAB_H
