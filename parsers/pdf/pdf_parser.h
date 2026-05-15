#ifndef PDF_PARSER_H
#define PDF_PARSER_H

// ============================================================
// pdf_parser.h  —  Top-level PDF → UDoc converter
//
// Usage
// ─────
//   PdfParser parser;
//   auto result = parser.parse("/path/to/file.pdf");
//   if (result.ok) {
//       // result.document is a fully populated UDoc::Document
//   } else {
//       qWarning() << result.errorMessage;
//   }
//
// What it does
// ─────────────
// 1. Reads the file into memory.
// 2. Builds the xref table via PdfXref.
// 3. Walks the page tree to enumerate all pages.
// 4. For each page:
//    a. Resolves the /Resources dict (fonts, XObjects).
//    b. Concatenates all content streams.
//    c. Runs ContentStreamInterpreter.
//    d. Clusters the resulting TextSpans into line-grouped
//       TextBlockContent Elements with approximate bounds.
//    e. Converts CollectedPaths into UDoc::VectorGraphic Elements.
//    f. Converts ImageRefs into UDoc::Image Elements.
// 5. Extracts document metadata from the /Info dict.
// 6. Returns a UDoc::Document with one PageSequence tab.
//
// Text clustering strategy
// ─────────────────────────
// PDF text is a flat list of positioned spans with no
// paragraph structure.  We reconstruct paragraphs by:
//   - Sorting spans by Y (top-to-bottom), then X.
//   - Grouping spans whose Y values are within lineHeight/2
//     of each other into the same "line".
//   - Merging lines with similar X alignment and consistent
//     inter-line spacing into paragraphs.
//   - Detecting headings by font size relative to the page's
//     modal body font size.
// ============================================================

#include "pdf_xref.h"
#include "pdf_font.h"
#include "pdf_content_stream.h"

// UDoc headers — include via relative path from project root.
// CMakeLists adds the project root to include paths.
#include "document.h"
#include "element.h"
#include "page.h"
#include "tab.h"
#include "inline.h"
#include "properties.h"
#include "image.h"
#include "vector.h"

#include <QString>
#include <QFile>
#include <memory>
#include <string>
#include <vector>

namespace Pdf {

// ---- parse result ----
struct ParseResult {
    bool ok = false;
    QString errorMessage;
    std::unique_ptr<UDoc::Document> document;
};

// ---- internal: a cluster of text spans on the same visual line ----
struct TextLine {
    double y        = 0;      // representative Y (top-left, UDoc coords)
    double x        = 0;      // leftmost X
    double right    = 0;      // rightmost right edge
    double height   = 0;      // tallest fontSize on this line
    double fontSize = 12;     // dominant font size
    std::string fontFamily;
    bool bold   = false;
    bool italic = false;
    double r=0, g=0, b=0;
    QString text;             // concatenated text of all spans on the line
};

// ---- internal: a paragraph (run of lines) ----
struct TextParagraph {
    std::vector<TextLine> lines;
    double x=0, y=0, w=0, h=0;   // bounding box
    bool isHeading = false;
    int  headingLevel = 1;
};

// ---- the parser ----
class PdfParser {
public:
    PdfParser() = default;

    // Parse a PDF file from disk.
    ParseResult parse(const QString& filePath);

    // Parse from an already-loaded byte buffer (useful for testing).
    ParseResult parseBytes(const std::vector<uint8_t>& data);

private:
    PdfXref       m_xref;
    PdfFontLoader m_fontLoader;

    // ---- page tree traversal ----
    // Recursively collect all leaf page nodes (Type=Page) in order.
    void collectPages(const PdfObject& node,
                      std::vector<PdfObject>& pages,
                      const PdfDict* inheritedResources = nullptr);

    // ---- per-page processing ----
    void buildPage(const PdfDict& pageDict,
                   UDoc::Page* udocPage,
                   UDoc::Document* doc);

    // Collect all content streams for a page into one byte buffer.
    std::vector<uint8_t> collectContentStreams(const PdfDict& pageDict);

    // Resolve page resources dict (may be inherited from parent)
    const PdfDict* resolveResources(const PdfDict& pageDict);

    // ---- content → UDoc elements ----
    void buildTextElements(const PageContent& content,
                           double pageWidth,
                           double pageHeight,
                           UDoc::Page* udocPage,
                           UDoc::Document* doc);

    void buildVectorElements(const PageContent& content,
                             UDoc::Page* udocPage,
                             UDoc::Document* doc);

    void buildImageElements(const PageContent& content,
                            UDoc::Page* udocPage,
                            UDoc::Document* doc);

    // ---- text clustering ----
    std::vector<TextLine> clusterIntoLines(
        const std::vector<TextSpan>& spans);

    std::vector<TextParagraph> clusterIntoParagraphs(
        std::vector<TextLine>& lines,
        double modalFontSize);

    double computeModalFontSize(const std::vector<TextSpan>& spans);

    // ---- metadata ----
    void extractMetadata(UDoc::Document* doc);

    // PDF string → QString (handles UTF-16BE BOM and PDFDocEncoding)
    static QString pdfStringToQString(const std::string& s);

    // Scratch storage for resource dicts resolved during page processing.
    // We need to keep them alive while the interpreter runs.
    std::vector<PdfObject> m_resourcesKeepAlive;
    std::vector<PdfDict>   m_resolvedResources;
};

} // namespace Pdf

#endif // PDF_PARSER_H
