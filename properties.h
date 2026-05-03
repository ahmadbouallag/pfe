#ifndef PROPERTIES_H
#define PROPERTIES_H

#include "udoc_types.h"
#include <vector>
#include <optional>

namespace UDoc {

// Forward declarations
class Story;

// === CHARACTER PROPERTIES ===
struct CharacterProperties {
    QString fontFamily = "Arial";
    double fontSize = 12.0;
    Color color = Color::black();
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool subscript = false;
    bool superscript = false;
    bool allCaps = false;
    bool smallCaps = false;
    double letterSpacing = 0;
    double wordSpacing = 0;
    double kerning = 0;
    std::optional<Color> highlightColor;
    std::optional<Color> backgroundColor;
    std::optional<BorderStyle> borderStyle;
    std::optional<Color> borderColor;
    std::optional<double> borderWidth;
    bool shadow = false;
    bool emboss = false;
    bool engrave = false;
    QString language = "en-US";

    bool operator==(const CharacterProperties& o) const {
        return fontFamily == o.fontFamily && fontSize == o.fontSize &&
               color == o.color && bold == o.bold && italic == o.italic &&
               underline == o.underline && strikethrough == o.strikethrough;
    }
    bool isDefault() const { return *this == CharacterProperties(); }
};

// === TAB STOP ===
struct TabStop {
    double position = 0;
    Alignment alignment = Alignment::Left;
    std::optional<QChar> decimalChar;
};

// === PARAGRAPH BORDER ===
struct ParagraphBorder {
    BorderStyle style = BorderStyle::None;
    Color color = Color::black();
    double width = 0.5;
    double space = 0;
};

// === SHADING ===
struct Shading {
    Color fillColor = Color::transparent();
    std::optional<Color> patternColor;
    enum Pattern {
        None, Solid, Percent5, Percent10, Percent20, Percent25,
        Percent30, Percent40, Percent50, Percent60, Percent70,
        Percent75, Percent80, Percent90, Percent95,
        DarkHorizontal, DarkVertical, DarkDownDiagonal,
        DarkUpDiagonal, SmallGrid, DottedGrid
    } pattern = None;
};

// === LIST PROPERTIES ===
struct ListProperties {
    ListType type = ListType::Bullet;
    int level = 0;
    int startNumber = 1;
    QString numberFormat = "%1.";
    QString bulletChar = "•";
    double indent = 0;
    double hangingIndent = 0;
};

// === PARAGRAPH PROPERTIES ===
struct ParagraphProperties {
    double spaceBefore = 0;
    double spaceAfter = 0;
    LineSpacingType lineSpacingType = LineSpacingType::Multiple;
    double lineSpacing = 1.15;
    double firstLineIndent = 0;
    double leftIndent = 0;
    double rightIndent = 0;
    Alignment alignment = Alignment::Left;
    std::vector<TabStop> tabStops;
    bool widowControl = true;
    bool orphanControl = true;
    bool keepWithNext = false;
    bool keepLinesTogether = false;
    bool pageBreakBefore = false;
    bool pageBreakAfter = false;
    std::optional<ParagraphBorder> topBorder;
    std::optional<ParagraphBorder> bottomBorder;
    std::optional<ParagraphBorder> leftBorder;
    std::optional<ParagraphBorder> rightBorder;
    std::optional<Shading> shading;
    std::optional<ListProperties> listProps;
    std::optional<QString> styleId;
    std::optional<QString> styleName;

    bool operator==(const ParagraphProperties& o) const {
        return spaceBefore == o.spaceBefore && spaceAfter == o.spaceAfter &&
               lineSpacing == o.lineSpacing && alignment == o.alignment;
    }
};

// === CELL PROPERTIES ===
struct CellProperties {
    std::optional<Color> fillColor;
    std::optional<ParagraphBorder> topBorder;
    std::optional<ParagraphBorder> bottomBorder;
    std::optional<ParagraphBorder> leftBorder;
    std::optional<ParagraphBorder> rightBorder;
    VerticalAlignment vAlign = VerticalAlignment::Top;
    double cellMargin = 0;
};

// === TABLE PROPERTIES ===
struct TableProperties {
    double borderWidth = 0.5;
    Color borderColor = Color::black();
    BorderStyle borderStyle = BorderStyle::Solid;
    uint32_t headerRows = 0;
    uint32_t headerCols = 0;
    Alignment tableAlignment = Alignment::Left;
    enum AutoFit { Auto, Fixed, FitToWindow, FitToContent } autoFit = Auto;
    enum Layout { AutoLayout, FixedLayout } layout = AutoLayout;
    std::optional<Rect> floatingPosition;
};

// === PAGE LAYOUT ===
struct PageLayout {
    double width = 612;
    double height = 792;
    PageOrientation orientation = PageOrientation::Portrait;
    double marginTop = 72;
    double marginBottom = 72;
    double marginLeft = 72;
    double marginRight = 72;
    double headerDistance = 36;
    double footerDistance = 36;
    int columnCount = 1;
    double columnSpacing = 36;
    bool equalColumnWidth = true;
    std::vector<double> columnWidths;
    QString paperSize = "Letter";
};

// === ANCHOR PROPERTIES ===
struct AnchorProperties {
    AnchorType type = AnchorType::Inline;
    AnchorRelativeTo relativeTo = AnchorRelativeTo::Paragraph;
    Point offset;
    bool lockAnchor = false;
    bool allowOverlap = true;
};

} // namespace UDoc

#endif // PROPERTIES_H
