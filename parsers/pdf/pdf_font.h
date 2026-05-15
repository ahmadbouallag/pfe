#ifndef PDF_FONT_H
#define PDF_FONT_H

// ============================================================
// pdf_font.h  —  Font & encoding support
// ============================================================

#include "pdf_object.h"
#include <QString>          // must come after pdf_object.h (which has std includes)
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Pdf {

class PdfXref;

struct PdfFont {
    std::string  baseName;
    std::string  qtFamilyName;
    bool         bold   = false;
    bool         italic = false;

    double defaultWidth = 1000.0;
    std::unordered_map<uint32_t, double> widths;

    // char code → Unicode codepoint
    std::unordered_map<uint32_t, uint32_t> toUnicode;

    double fontMatrixScale = 0.001;

    // Decode a PDF byte-string to a Qt string.
    // twoByteChars = true for Type0 CID fonts.
    QString decode(const std::string& pdfString, bool twoByteChars = false) const;

    // Advance width of charCode in text-space units (before fontSize scaling).
    double charWidth(uint32_t charCode) const;
};

class PdfFontLoader {
public:
    PdfFont* loadFont(const PdfObject& fontObj, PdfXref& xref);

private:
    std::unordered_map<uint32_t, PdfFont> m_cache;

    PdfFont buildFont(const PdfDict& fontDict, PdfXref& xref);

    void parseToUnicodeCMap(const std::vector<uint8_t>& cmapData, PdfFont& font);
    void parseEncoding(const PdfObject& encObj, PdfFont& font, PdfXref& xref);
    void parseWidths(const PdfDict& fontDict, PdfFont& font, PdfXref& xref);
    void parseCIDWidths(const PdfDict& cidDict, PdfFont& font);
    void deriveStyle(const PdfDict& fontDict, PdfFont& font, PdfXref& xref);

    static std::string mapFontFamily(const std::string& pdfName);
    static uint32_t    glyphNameToUnicode(const std::string& name);
};

} // namespace Pdf

#endif // PDF_FONT_H
