#include "pdf_font.h"
#include "pdf_xref.h"
#include <QChar>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace Pdf {

// ============================================================
//  PdfFont
// ============================================================

QString PdfFont::decode(const std::string& s, bool twoByteChars) const {
    QString result;
    if (twoByteChars) {
        for (size_t i = 0; i + 1 < s.size(); i += 2) {
            uint32_t code = ((uint8_t)s[i] << 8) | (uint8_t)s[i+1];
            auto it = toUnicode.find(code);
            if (it != toUnicode.end() && it->second != 0)
                result += QChar((uint)it->second);
            else
                result += QChar(QChar::ReplacementCharacter);
        }
    } else {
        for (size_t i = 0; i < s.size(); ++i) {
            uint32_t code = (uint8_t)s[i];
            auto it = toUnicode.find(code);
            if (it != toUnicode.end() && it->second != 0)
                result += QChar((uint)it->second);
            else if (code >= 0x20 && code < 0x7F)
                result += QChar((char)code);
        }
    }
    return result;
}

double PdfFont::charWidth(uint32_t charCode) const {
    auto it = widths.find(charCode);
    return (it != widths.end()) ? it->second : defaultWidth;
}

// ============================================================
//  PdfFontLoader
// ============================================================

PdfFont* PdfFontLoader::loadFont(const PdfObject& fontObj, PdfXref& xref) {
    PdfObject resolved = xref.deref(fontObj);
    if (!resolved.isDict() && !resolved.isStream()) return nullptr;

    uint32_t cacheKey = 0;
    if (fontObj.isRef()) cacheKey = fontObj.asRef().obj;

    if (cacheKey && m_cache.count(cacheKey))
        return &m_cache[cacheKey];

    PdfDict* d = resolved.asDict();
    if (!d) return nullptr;

    PdfFont font = buildFont(*d, xref);

    if (cacheKey) {
        m_cache[cacheKey] = std::move(font);
        return &m_cache[cacheKey];
    }
    static uint32_t synthKey = 0xFFFF0000u;
    m_cache[++synthKey] = std::move(font);
    return &m_cache[synthKey];
}

PdfFont PdfFontLoader::buildFont(const PdfDict& d, PdfXref& xref) {
    PdfFont font;
    font.fontMatrixScale = 0.001;

    const PdfObject* baseNameObj = d.get("BaseFont");
    if (baseNameObj && baseNameObj->isName())
        font.baseName = baseNameObj->asName();

    deriveStyle(d, font, xref);
    font.qtFamilyName = mapFontFamily(font.baseName);

    std::string subtype;
    const PdfObject* stObj = d.get("Subtype");
    if (stObj && stObj->isName()) subtype = stObj->asName();

    bool isTwoByteFont = (subtype == "Type0");

    if (!isTwoByteFont) {
        const PdfObject* encObj = d.get("Encoding");
        if (encObj) parseEncoding(*encObj, font, xref);
    }

    const PdfObject* tuObj = d.get("ToUnicode");
    if (tuObj) {
        PdfObject tuResolved = xref.deref(*tuObj);
        if (tuResolved.isStream())
            parseToUnicodeCMap(tuResolved.asStream()->data, font);
    }

    if (subtype != "Type0" && subtype != "CIDFontType0" && subtype != "CIDFontType2")
        parseWidths(d, font, xref);

    if (subtype == "Type0") {
        const PdfObject* descObj = d.get("DescendantFonts");
        if (descObj) {
            PdfObject descResolved = xref.deref(*descObj);
            if (descResolved.isArray() && descResolved.asArray()->size() > 0) {
                PdfObject cidObj = xref.deref(descResolved.asArray()->at(0));
                if (cidObj.isDict())
                    parseCIDWidths(*cidObj.asDict(), font);
            }
        }
    }

    return font;
}

// ============================================================
//  ToUnicode CMap
// ============================================================

static uint32_t hexToU32(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h.front() == '<') h = h.substr(1);
    if (!h.empty() && h.back()  == '>') h.pop_back();
    if (h.empty()) return 0;
    try { return (uint32_t)std::stoul(h, nullptr, 16); } catch (...) { return 0; }
}

void PdfFontLoader::parseToUnicodeCMap(const std::vector<uint8_t>& data, PdfFont& font) {
    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream ss(text);
    std::string tok;
    std::vector<std::string> stack;

    auto popStr = [&]() -> std::string {
        if (stack.empty()) return {};
        std::string v = stack.back(); stack.pop_back(); return v;
    };

    while (ss >> tok) {
        if (tok == "beginbfchar") {
            int n = 0;
            try { n = std::stoi(stack.empty() ? "0" : popStr()); } catch (...) {}
            for (int i = 0; i < n; ++i) {
                std::string src, dst;
                if (!(ss >> src >> dst)) break;
                font.toUnicode[hexToU32(src)] = hexToU32(dst);
            }
            ss >> tok; // endbfchar
        } else if (tok == "beginbfrange") {
            int n = 0;
            try { n = std::stoi(stack.empty() ? "0" : popStr()); } catch (...) {}
            for (int i = 0; i < n; ++i) {
                std::string startS, endS, dstS;
                if (!(ss >> startS >> endS >> dstS)) break;
                uint32_t cStart = hexToU32(startS);
                uint32_t cEnd   = hexToU32(endS);
                if (!dstS.empty() && dstS.front() == '[') {
                    std::string arr = dstS;
                    while (!arr.empty() && arr.back() != ']' && ss >> tok) arr += " " + tok;
                    std::istringstream arrss(arr);
                    std::string u; arrss >> u;
                    for (uint32_t c = cStart; c <= cEnd; ++c) {
                        if (!(arrss >> u)) break;
                        if (!u.empty() && u.back() == ']') u.pop_back();
                        font.toUnicode[c] = hexToU32(u);
                    }
                } else {
                    uint32_t uStart = hexToU32(dstS);
                    for (uint32_t c = cStart; c <= cEnd; ++c)
                        font.toUnicode[c] = uStart + (c - cStart);
                }
            }
            ss >> tok; // endbfrange
        } else {
            stack.push_back(tok);
            if (stack.size() > 64) stack.erase(stack.begin());
        }
    }
}

// ============================================================
//  Encoding
// ============================================================

void PdfFontLoader::parseEncoding(const PdfObject& encObj, PdfFont& font, PdfXref& xref) {
    PdfObject enc = xref.deref(encObj);

    auto populateLatin1 = [&]() {
        for (uint32_t i = 0x20; i < 0x100; ++i)
            font.toUnicode.emplace(i, i);
    };

    if (enc.isName()) {
        const std::string& n = enc.asName();
        if (n == "WinAnsiEncoding" || n == "MacRomanEncoding" ||
            n == "StandardEncoding" || n == "PDFDocEncoding")
            populateLatin1();
        return;
    }

    if (!enc.isDict()) { populateLatin1(); return; }
    PdfDict* d = enc.asDict();

    const PdfObject* baseObj = d->get("BaseEncoding");
    if (baseObj && baseObj->isName()) {
        PdfObject baseEnc = PdfObject::name(baseObj->asName());
        parseEncoding(baseEnc, font, xref);
    } else {
        populateLatin1();
    }

    const PdfObject* diffObj = d->get("Differences");
    if (!diffObj || !diffObj->isArray()) return;

    PdfArray* arr = diffObj->asArray();
    uint32_t code = 0;
    for (size_t i = 0; i < arr->size(); ++i) {
        const PdfObject& item = arr->at(i);
        if (item.isInt())        code = (uint32_t)item.asInt();
        else if (item.isName()) { font.toUnicode[code] = glyphNameToUnicode(item.asName()); ++code; }
    }
}

// ============================================================
//  Widths
// ============================================================

void PdfFontLoader::parseWidths(const PdfDict& d, PdfFont& font, PdfXref& xref) {
    const PdfObject* fcObj = d.get("FirstChar");
    const PdfObject* wObj  = d.get("Widths");
    if (!fcObj || !wObj) return;

    PdfObject fcR = xref.deref(*fcObj);
    PdfObject wR  = xref.deref(*wObj);
    if (!fcR.isInt() || !wR.isArray()) return;

    uint32_t firstChar = (uint32_t)fcR.asInt();
    PdfArray* arr = wR.asArray();
    for (size_t i = 0; i < arr->size(); ++i) {
        PdfObject w = xref.deref(arr->at(i));
        font.widths[firstChar + (uint32_t)i] = w.numOr(font.defaultWidth);
    }

    const PdfObject* fdObj = d.get("FontDescriptor");
    if (fdObj) {
        PdfObject fd = xref.deref(*fdObj);
        if (fd.isDict()) {
            const PdfObject* mwObj = fd.asDict()->get("MissingWidth");
            if (mwObj) font.defaultWidth = xref.deref(*mwObj).numOr(font.defaultWidth);
        }
    }
}

void PdfFontLoader::parseCIDWidths(const PdfDict& d, PdfFont& font) {
    const PdfObject* dwObj = d.get("DW");
    if (dwObj && dwObj->isNumber()) font.defaultWidth = dwObj->asReal();

    const PdfObject* wObj = d.get("W");
    if (!wObj || !wObj->isArray()) return;
    PdfArray* arr = wObj->asArray();
    size_t i = 0;
    while (i < arr->size()) {
        const PdfObject& a = arr->at(i++);
        if (!a.isInt() || i >= arr->size()) break;
        uint32_t first = (uint32_t)a.asInt();
        const PdfObject& b = arr->at(i);
        if (b.isArray()) {
            PdfArray* ws = b.asArray();
            for (size_t j = 0; j < ws->size(); ++j)
                font.widths[first + (uint32_t)j] = ws->at(j).numOr(font.defaultWidth);
            ++i;
        } else if (b.isInt()) {
            uint32_t last = (uint32_t)b.asInt(); ++i;
            if (i >= arr->size()) break;
            double w = arr->at(i++).numOr(font.defaultWidth);
            for (uint32_t c = first; c <= last; ++c) font.widths[c] = w;
        }
    }
}

// ============================================================
//  Style derivation
// ============================================================

void PdfFontLoader::deriveStyle(const PdfDict& d, PdfFont& font, PdfXref& xref) {
    const PdfObject* fdObj = d.get("FontDescriptor");
    if (fdObj) {
        PdfObject fd = xref.deref(*fdObj);
        if (fd.isDict()) {
            const PdfObject* flagsObj = fd.asDict()->get("Flags");
            if (flagsObj && flagsObj->isInt()) {
                uint32_t flags = (uint32_t)flagsObj->asInt();
                if (flags & (1 << 6)) font.italic = true;
            }
        }
    }
    std::string lower = font.baseName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("bold")    != std::string::npos) font.bold   = true;
    if (lower.find("italic")  != std::string::npos) font.italic = true;
    if (lower.find("oblique") != std::string::npos) font.italic = true;
}

// ============================================================
//  Font family mapping
// ============================================================

std::string PdfFontLoader::mapFontFamily(const std::string& pdfName) {
    std::string name = pdfName;
    if (name.size() > 7 && name[6] == '+') name = name.substr(7);

    struct { const char* pdf; const char* qt; } table[] = {
        {"Helvetica","Helvetica"},{"Arial","Arial"},
        {"Times","Times New Roman"},{"TimesNewRoman","Times New Roman"},
        {"Courier","Courier New"},{"CourierNew","Courier New"},
        {"Symbol","Symbol"},{"ZapfDingbats","Zapf Dingbats"},
        {"Palatino","Palatino"},{"Garamond","Garamond"},
        {"Georgia","Georgia"},{"Verdana","Verdana"},
        {"Tahoma","Tahoma"},{"Impact","Impact"},
    };
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto& e : table) {
        std::string pLower = e.pdf;
        std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
        if (lower.find(pLower) != std::string::npos) return e.qt;
    }
    return "Arial";
}

// ============================================================
//  Adobe Glyph List (subset)
// ============================================================

uint32_t PdfFontLoader::glyphNameToUnicode(const std::string& name) {
    static const std::unordered_map<std::string, uint32_t> agl = {
        {"space",0x20},{"exclam",0x21},{"quotedbl",0x22},{"numbersign",0x23},
        {"dollar",0x24},{"percent",0x25},{"ampersand",0x26},{"quotesingle",0x27},
        {"parenleft",0x28},{"parenright",0x29},{"asterisk",0x2A},{"plus",0x2B},
        {"comma",0x2C},{"hyphen",0x2D},{"period",0x2E},{"slash",0x2F},
        {"zero",0x30},{"one",0x31},{"two",0x32},{"three",0x33},{"four",0x34},
        {"five",0x35},{"six",0x36},{"seven",0x37},{"eight",0x38},{"nine",0x39},
        {"colon",0x3A},{"semicolon",0x3B},{"less",0x3C},{"equal",0x3D},
        {"greater",0x3E},{"question",0x3F},{"at",0x40},
        {"A",0x41},{"B",0x42},{"C",0x43},{"D",0x44},{"E",0x45},{"F",0x46},
        {"G",0x47},{"H",0x48},{"I",0x49},{"J",0x4A},{"K",0x4B},{"L",0x4C},
        {"M",0x4D},{"N",0x4E},{"O",0x4F},{"P",0x50},{"Q",0x51},{"R",0x52},
        {"S",0x53},{"T",0x54},{"U",0x55},{"V",0x56},{"W",0x57},{"X",0x58},
        {"Y",0x59},{"Z",0x5A},
        {"bracketleft",0x5B},{"backslash",0x5C},{"bracketright",0x5D},
        {"asciicircum",0x5E},{"underscore",0x5F},{"grave",0x60},
        {"a",0x61},{"b",0x62},{"c",0x63},{"d",0x64},{"e",0x65},{"f",0x66},
        {"g",0x67},{"h",0x68},{"i",0x69},{"j",0x6A},{"k",0x6B},{"l",0x6C},
        {"m",0x6D},{"n",0x6E},{"o",0x6F},{"p",0x70},{"q",0x71},{"r",0x72},
        {"s",0x73},{"t",0x74},{"u",0x75},{"v",0x76},{"w",0x77},{"x",0x78},
        {"y",0x79},{"z",0x7A},
        {"braceleft",0x7B},{"bar",0x7C},{"braceright",0x7D},{"asciitilde",0x7E},
        {"exclamdown",0xA1},{"cent",0xA2},{"sterling",0xA3},{"yen",0xA5},
        {"section",0xA7},{"copyright",0xA9},{"degree",0xB0},{"plusminus",0xB1},
        {"mu",0xB5},{"paragraph",0xB6},{"registered",0xAE},
        {"Agrave",0xC0},{"Aacute",0xC1},{"Acircumflex",0xC2},{"Atilde",0xC3},
        {"Adieresis",0xC4},{"Aring",0xC5},{"AE",0xC6},{"Ccedilla",0xC7},
        {"Egrave",0xC8},{"Eacute",0xC9},{"Ecircumflex",0xCA},{"Edieresis",0xCB},
        {"Igrave",0xCC},{"Iacute",0xCD},{"Icircumflex",0xCE},{"Idieresis",0xCF},
        {"Ntilde",0xD1},{"Ograve",0xD2},{"Oacute",0xD3},{"Ocircumflex",0xD4},
        {"Otilde",0xD5},{"Odieresis",0xD6},{"multiply",0xD7},{"Oslash",0xD8},
        {"Ugrave",0xD9},{"Uacute",0xDA},{"Ucircumflex",0xDB},{"Udieresis",0xDC},
        {"germandbls",0xDF},
        {"agrave",0xE0},{"aacute",0xE1},{"acircumflex",0xE2},{"atilde",0xE3},
        {"adieresis",0xE4},{"aring",0xE5},{"ae",0xE6},{"ccedilla",0xE7},
        {"egrave",0xE8},{"eacute",0xE9},{"ecircumflex",0xEA},{"edieresis",0xEB},
        {"igrave",0xEC},{"iacute",0xED},{"icircumflex",0xEE},{"idieresis",0xEF},
        {"ntilde",0xF1},{"ograve",0xF2},{"oacute",0xF3},{"ocircumflex",0xF4},
        {"otilde",0xF5},{"odieresis",0xF6},{"divide",0xF7},{"oslash",0xF8},
        {"ugrave",0xF9},{"uacute",0xFA},{"ucircumflex",0xFB},{"udieresis",0xFC},
        {"ydieresis",0xFF},
        {"endash",0x2013},{"emdash",0x2014},
        {"quotedblleft",0x201C},{"quotedblright",0x201D},
        {"quoteleft",0x2018},{"quoteright",0x2019},
        {"bullet",0x2022},{"ellipsis",0x2026},{"trademark",0x2122},
        {"fi",0xFB01},{"fl",0xFB02},{"Euro",0x20AC},
        {"oe",0x0153},{"OE",0x0152},{"scaron",0x0161},{"Scaron",0x0160},
        {"lslash",0x0142},{"Lslash",0x0141},
    };
    auto it = agl.find(name);
    if (it != agl.end()) return it->second;
    if (name.size() == 7 && name.substr(0,3) == "uni") {
        try { return (uint32_t)std::stoul(name.substr(3), nullptr, 16); } catch (...) {}
    }
    if (name.size() == 1) return (uint32_t)(unsigned char)name[0];
    return 0x3F; // '?'
}

} // namespace Pdf
