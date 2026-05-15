# UDoc Parsers

This directory contains all format import/export adapters for UDoc.
Each format lives in its own subdirectory and is compiled directly
into the main `udoc1` target (no separate shared libraries needed
at this stage).

---

## Directory layout

```
parsers/
  pdf/          Phase 5  — PDF import (complete)
  docx/         Phase 7  — DOCX import (planned)
  html/         Phase 11 — HTML/EPUB import (planned)
  xlsx/         Phase 12 — XLSX import (planned)
  pptx/         Phase 13 — PPTX import (planned)
```

---

## parsers/pdf — PDF Parser

A from-scratch PDF 1.0–1.7 parser.  No third-party PDF library.
Only external dependency: **zlib** (already available via Qt).

### Files

| File | Responsibility |
|---|---|
| `pdf_object.h/cpp` | In-memory PDF object model (`PdfObject` variant: null, bool, int, real, name, string, array, dict, stream, ref) |
| `pdf_xref.h/cpp` | Cross-reference table (classic § 7.5.4 and compressed xref stream § 7.5.8), indirect object resolver, stream decompressor (FlateDecode/PNG predictor, DCT pass-through, ASCII85, ASCIIHex) |
| `pdf_font.h/cpp` | Font loading: ToUnicode CMap parser, Encoding dict (Differences array, standard names → Unicode via AGL subset), glyph width tables (Type1 /Widths, CIDFont /W), bold/italic derivation, Qt font family mapping |
| `pdf_content_stream.h/cpp` | Content stream interpreter: full text operator set (BT/ET, Tf, Tm, Td, TD, T*, Tj, TJ, ', "), path operators (m, l, c, v, y, re, h, S, f, B, n), color operators (g, G, rg, RG, k, K), graphics state (q, Q, cm), XObject dispatcher (Image + Form recursion), inline image skeleton |
| `pdf_parser.h/cpp` | Top-level converter: page tree walk, content stream concatenation, text span → line → paragraph clustering, heading detection, UDoc element assembly (TextBlockContent, HeadingContent, VectorGraphic, Image), metadata extraction |

### What is parsed

- ✅ Text: positioned, styled, Unicode-decoded
- ✅ Vector paths: fill + stroke, converted to `UDoc::VectorGraphic`
- ✅ Raster images: JPEG (DCTDecode) and raw/PNG pixel data
- ✅ Form XObjects (nested content streams, recursive)
- ✅ Compressed xref streams (PDF 1.5+)
- ✅ Object streams (compressed indirect objects, PDF 1.5+)
- ✅ Multi-section xref tables (/Prev chains)
- ✅ Document metadata (/Info dict)
- ✅ FlateDecode + PNG predictor (Sub, Up, Average, Paeth)
- ✅ ASCII85Decode, ASCIIHexDecode

### Known limitations (future phases)

- ❌ Encrypted PDFs (no decryption)
- ❌ Full CJK CMap tables (Type0 fonts with external CMaps)
- ❌ LZWDecode (rare in modern PDFs)
- ❌ Inline images: raw binary data captured (tokeniser limitation)
- ❌ Annotations, AcroForms, Tagged PDF
- ❌ Type3 (bitmap) fonts

### Text clustering algorithm

PDF content streams contain a flat list of positioned text spans
with no paragraph structure.  The parser reconstructs paragraphs:

1. **Sort** all spans by Y (top-to-bottom) then X (left-to-right).
2. **Line grouping**: spans within `fontSize/2` of each other in Y
   are merged into one `TextLine`.  Horizontal gaps insert spaces.
3. **Paragraph grouping**: consecutive lines are merged when:
   - Vertical gap ≤ 1.8 × line height (normal leading), AND
   - X alignment within 20pt (same column / indent), AND
   - Font size is consistent.
4. **Heading detection**: paragraphs whose dominant font size is
   ≥ 1.3× the modal body font size and that span ≤ 3 lines become
   `HeadingContent` elements (H1–H4 by size ratio).

### Usage

```cpp
#include "parsers/pdf/pdf_parser.h"

Pdf::PdfParser parser;
Pdf::ParseResult result = parser.parse("/path/to/file.pdf");
if (result.ok) {
    // result.document is a UDoc::Document with one PageSequence tab
    myView->setDocument(result.document.release());
} else {
    qWarning() << result.errorMessage;
}
```

### CMake

`parsers/pdf` files are listed directly in `PARSER_SOURCES` in the
root `CMakeLists.txt`.  The project root is on the include path so
parser files can `#include "document.h"` etc. without path prefixes.
`find_package(ZLIB REQUIRED)` + `ZLIB::ZLIB` in `target_link_libraries`
provides the zlib headers and import library.

---

## Adding a new parser (template)

1. Create `parsers/<format>/` directory.
2. Add a `<format>_parser.h` exposing a `ParseResult parse(QString path)`.
3. List all `.h/.cpp` files in `PARSER_SOURCES` in `CMakeLists.txt`.
4. Add a menu action in `mainwindow.cpp` calling the new parser.
5. Document it in this README.
