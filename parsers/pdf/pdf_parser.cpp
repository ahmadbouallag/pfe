#include "pdf_parser.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_map>

namespace Pdf {

// ============================================================
//  Public entry points
// ============================================================

ParseResult PdfParser::parse(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        ParseResult r;
        r.ok = false;
        r.errorMessage = "Cannot open file: " + filePath;
        return r;
    }
    QByteArray ba = f.readAll();
    std::vector<uint8_t> data(reinterpret_cast<const uint8_t*>(ba.constData()),
                               reinterpret_cast<const uint8_t*>(ba.constData()) + ba.size());
    return parseBytes(data);
}

ParseResult PdfParser::parseBytes(const std::vector<uint8_t>& data) {
    ParseResult result;

    std::string xrefErr;
    if (!m_xref.load(data, xrefErr)) {
        result.ok = false;
        result.errorMessage = QString::fromStdString(xrefErr);
        return result;
    }

    const PdfDict& trailer = m_xref.trailer();
    const PdfObject* rootObj = trailer.get("Root");
    if (!rootObj) {
        result.ok = false; result.errorMessage = "PDF trailer missing /Root"; return result;
    }
    PdfObject catalog = m_xref.deref(*rootObj);
    if (!catalog.isDict()) {
        result.ok = false; result.errorMessage = "PDF /Root is not a dictionary"; return result;
    }

    const PdfObject* pagesObj = catalog.asDict()->get("Pages");
    if (!pagesObj) {
        result.ok = false; result.errorMessage = "PDF catalog missing /Pages"; return result;
    }
    PdfObject pagesRoot = m_xref.deref(*pagesObj);

    std::vector<PdfObject> pageNodes;
    collectPages(pagesRoot, pageNodes);
    if (pageNodes.empty()) {
        result.ok = false; result.errorMessage = "PDF contains no pages"; return result;
    }

    auto doc = std::make_unique<UDoc::Document>();
    UDoc::Tab* tab = doc->addTab(UDoc::TabType::PageSequence, "PDF Document");
    UDoc::PageSequence* ps = tab->initPageSequence();

    for (auto& pageNode : pageNodes) {
        if (!pageNode.isDict()) continue;
        PdfDict* pageDict = pageNode.asDict();

        double pageW = 612.0, pageH = 792.0;
        const PdfObject* mbObj = pageDict->get("MediaBox");
        if (mbObj) {
            PdfObject mb = m_xref.deref(*mbObj);
            if (mb.isArray() && mb.asArray()->size() >= 4) {
                double x0 = mb.asArray()->at(0).numOr(0);
                double y0 = mb.asArray()->at(1).numOr(0);
                double x1 = mb.asArray()->at(2).numOr(612);
                double y1 = mb.asArray()->at(3).numOr(792);
                pageW = std::abs(x1 - x0);
                pageH = std::abs(y1 - y0);
            }
        }

        int rotate = 0;
        const PdfObject* rotObj = pageDict->get("Rotate");
        if (rotObj) rotate = (int)m_xref.deref(*rotObj).intOr(0);
        if (rotate == 90 || rotate == 270) std::swap(pageW, pageH);

        UDoc::Page* udocPage = ps->addPage(doc->generateId());
        udocPage->width  = pageW;
        udocPage->height = pageH;
        udocPage->backgroundColor = UDoc::Color::white();
        udocPage->marginTop = udocPage->marginBottom =
        udocPage->marginLeft = udocPage->marginRight = 0;

        buildPage(*pageDict, udocPage, doc.get());
    }

    extractMetadata(doc.get());

    result.ok = true;
    result.document = std::move(doc);
    return result;
}

// ============================================================
//  Page tree traversal
// ============================================================

void PdfParser::collectPages(const PdfObject& node,
                              std::vector<PdfObject>& pages,
                              const PdfDict* inheritedResources) {
    PdfObject resolved = m_xref.deref(node);
    if (!resolved.isDict()) return;

    PdfDict* d = resolved.asDict();
    const PdfObject* typeObj = d->get("Type");
    if (!typeObj) return;
    PdfObject typeR = m_xref.deref(*typeObj);
    if (!typeR.isName()) return;
    const std::string& type = typeR.asName();

    if (type == "Pages") {
        const PdfObject* kidsObj = d->get("Kids");
        if (!kidsObj) return;
        PdfObject kidsR = m_xref.deref(*kidsObj);
        if (!kidsR.isArray()) return;

        const PdfDict* childRes = inheritedResources;
        const PdfObject* resObj = d->get("Resources");
        PdfObject resR;
        if (resObj) {
            resR = m_xref.deref(*resObj);
            if (resR.isDict()) childRes = resR.asDict();
        }
        // Inherit MediaBox
        const PdfObject* mbObj = d->get("MediaBox");

        for (size_t i = 0; i < kidsR.asArray()->size(); ++i)
            collectPages(kidsR.asArray()->at(i), pages, childRes);

    } else if (type == "Page") {
        if (!d->has("Resources") && inheritedResources) {
            auto inh = std::make_shared<PdfDict>(*inheritedResources);
            d->set("Resources", PdfObject::dict(inh));
        }
        if (!d->has("MediaBox") && inheritedResources) {
            const PdfObject* mb = inheritedResources->get("MediaBox");
            if (mb) d->set("MediaBox", *mb);
        }
        pages.push_back(resolved);
    }
}

// ============================================================
//  Per-page build
// ============================================================

void PdfParser::buildPage(const PdfDict& pageDict,
                           UDoc::Page* udocPage,
                           UDoc::Document* doc) {
    const PdfDict* resources = nullptr;
    const PdfObject* resObj = pageDict.get("Resources");
    PdfObject resR;
    if (resObj) {
        resR = m_xref.deref(*resObj);
        if (resR.isDict()) resources = resR.asDict();
    }

    std::vector<uint8_t> streamData = collectContentStreams(pageDict);
    if (streamData.empty()) return;

    ContentStreamInterpreter interp(m_xref, m_fontLoader, resources, udocPage->height);
    interp.process(streamData);
    const PageContent& content = interp.content();

    buildTextElements(content, udocPage->width, udocPage->height, udocPage, doc);
    buildVectorElements(content, udocPage, doc);
    buildImageElements(content, udocPage, doc);
}

// ============================================================
//  Content stream collection
// ============================================================

std::vector<uint8_t> PdfParser::collectContentStreams(const PdfDict& pageDict) {
    std::vector<uint8_t> combined;
    const PdfObject* contObj = pageDict.get("Contents");
    if (!contObj) return combined;

    PdfObject resolved = m_xref.deref(*contObj);
    std::vector<PdfObject> streams;
    if (resolved.isStream()) {
        streams.push_back(resolved);
    } else if (resolved.isArray()) {
        for (size_t i = 0; i < resolved.asArray()->size(); ++i)
            streams.push_back(m_xref.deref(resolved.asArray()->at(i)));
    }

    for (auto& stmObj : streams) {
        if (!stmObj.isStream()) continue;
        if (!combined.empty()) combined.push_back(' ');
        const auto& d = stmObj.asStream()->data;
        combined.insert(combined.end(), d.begin(), d.end());
    }
    return combined;
}

// ============================================================
//  Text element assembly
// ============================================================

void PdfParser::buildTextElements(const PageContent& content,
                                   double pageWidth, double pageHeight,
                                   UDoc::Page* udocPage, UDoc::Document* doc) {
    if (content.textSpans.empty()) return;

    double modalSize = computeModalFontSize(content.textSpans);
    auto lines = clusterIntoLines(content.textSpans);
    auto paragraphs = clusterIntoParagraphs(lines, modalSize);

    for (auto& para : paragraphs) {
        if (para.lines.empty()) continue;

        QString fullText;
        for (size_t i = 0; i < para.lines.size(); ++i) {
            if (i > 0) fullText += ' ';
            fullText += para.lines[i].text;
        }
        fullText = fullText.trimmed();
        if (fullText.isEmpty()) continue;

        const TextLine& firstLine = para.lines[0];

        UDoc::CharacterProperties charProps;
        // fontFamily is std::string — convert to QString
        charProps.fontFamily = firstLine.fontFamily.empty()
                               ? "Arial"
                               : QString::fromStdString(firstLine.fontFamily);
        charProps.fontSize   = firstLine.fontSize > 0 ? firstLine.fontSize : modalSize;
        charProps.bold       = firstLine.bold;
        charProps.italic     = firstLine.italic;
        charProps.color      = UDoc::Color(firstLine.r, firstLine.g, firstLine.b, 1.0);

        double bx = std::max(0.0, para.x);
        double by = std::max(0.0, para.y);
        double bw = std::min(para.w, pageWidth  - bx);
        double bh = std::min(para.h, pageHeight - by);
        if (bw < 1.0 || bh < 1.0) continue;
        bh = std::max(bh, charProps.fontSize * 1.4 * (double)para.lines.size());

        auto elem = std::make_unique<UDoc::Element>(doc->generateId());
        elem->bounds = UDoc::Rect(bx, by, bw, bh);

        if (para.isHeading) {
            UDoc::HeadingContent hc;
            hc.outlineLevel = para.headingLevel;
            charProps.fontSize = std::max(charProps.fontSize,
                                          modalSize * (2.2 - (para.headingLevel-1)*0.3));
            charProps.bold = true;
            hc.setPlainText(fullText, charProps);
            elem->content = std::move(hc);
        } else {
            UDoc::TextBlockContent tc;
            tc.setPlainText(fullText, charProps);
            elem->content = std::move(tc);
        }
        udocPage->addElement(std::move(elem));
    }
}

// ============================================================
//  Text clustering — spans → lines
// ============================================================

std::vector<TextLine> PdfParser::clusterIntoLines(const std::vector<TextSpan>& spans) {
    if (spans.empty()) return {};

    std::vector<const TextSpan*> sorted;
    sorted.reserve(spans.size());
    for (auto& s : spans) sorted.push_back(&s);
    std::sort(sorted.begin(), sorted.end(), [](const TextSpan* a, const TextSpan* b){
        if (std::abs(a->y - b->y) > 2.0) return a->y < b->y;
        return a->x < b->x;
    });

    std::vector<TextLine> lines;
    for (const TextSpan* sp : sorted) {
        if (sp->text.trimmed().isEmpty()) continue;
        if (sp->fontSize < 0.5) continue;

        double tol = std::max(sp->fontSize * 0.5, 2.0);
        TextLine* target = nullptr;
        for (auto& line : lines)
            if (std::abs(line.y - sp->y) <= tol) { target = &line; break; }

        if (!target) {
            TextLine nl;
            nl.y = sp->y; nl.x = sp->x; nl.right = sp->x + sp->width;
            nl.height = sp->fontSize; nl.fontSize = sp->fontSize;
            // fontFamily is std::string in both TextSpan and TextLine
            nl.fontFamily = sp->fontFamily.empty() ? "Arial" : sp->fontFamily;
            nl.bold = sp->bold; nl.italic = sp->italic;
            nl.r = sp->r; nl.g = sp->g; nl.b = sp->b;
            nl.text = sp->text;
            lines.push_back(nl);
        } else {
            double gap = sp->x - target->right;
            if (gap > target->fontSize * 0.25 && !target->text.endsWith(' '))
                target->text += ' ';
            target->text  += sp->text;
            target->right  = std::max(target->right, sp->x + sp->width);
            target->x      = std::min(target->x, sp->x);
            if (sp->fontSize > target->fontSize) {
                target->fontSize  = sp->fontSize;
                target->fontFamily = sp->fontFamily.empty() ? "Arial" : sp->fontFamily;
                target->bold = sp->bold; target->italic = sp->italic;
                target->r = sp->r; target->g = sp->g; target->b = sp->b;
            }
            target->height = std::max(target->height, sp->fontSize);
        }
    }

    std::sort(lines.begin(), lines.end(),
              [](const TextLine& a, const TextLine& b){ return a.y < b.y; });
    return lines;
}

// ============================================================
//  Text clustering — lines → paragraphs
// ============================================================

std::vector<TextParagraph> PdfParser::clusterIntoParagraphs(
        std::vector<TextLine>& lines, double modalFontSize) {

    std::vector<TextParagraph> paragraphs;
    if (lines.empty()) return paragraphs;

    TextParagraph current;
    current.lines.push_back(lines[0]);

    auto finish = [&](){
        if (current.lines.empty()) return;
        double minX=1e18,minY=1e18,maxX=-1e18,maxY=-1e18;
        for (auto& l : current.lines) {
            minX=std::min(minX,l.x);   minY=std::min(minY,l.y);
            maxX=std::max(maxX,l.right); maxY=std::max(maxY,l.y+l.height*1.2);
        }
        current.x=minX; current.y=minY;
        current.w=maxX-minX; current.h=maxY-minY;
        double domFont = current.lines[0].fontSize;
        if (domFont >= modalFontSize * 1.3 && current.lines.size() <= 3) {
            current.isHeading = true;
            double ratio = domFont / std::max(modalFontSize, 1.0);
            if      (ratio >= 2.0) current.headingLevel = 1;
            else if (ratio >= 1.6) current.headingLevel = 2;
            else if (ratio >= 1.3) current.headingLevel = 3;
            else                   current.headingLevel = 4;
        }
        paragraphs.push_back(std::move(current));
        current = TextParagraph();
    };

    for (size_t i = 1; i < lines.size(); ++i) {
        const TextLine& prev = current.lines.back();
        const TextLine& cur  = lines[i];

        double lineH  = std::max(prev.height, 1.0);
        double gapY   = cur.y - (prev.y + prev.height);
        double xDiff  = std::abs(cur.x - current.lines[0].x);
        bool sameCol  = xDiff < std::max(modalFontSize * 2.0, 20.0);
        bool bigGap   = gapY > lineH * 2.5;
        bool fontChange = std::abs(cur.fontSize - prev.fontSize) > 2.5;

        if (bigGap || !sameCol || fontChange) { finish(); current.lines.push_back(cur); }
        else                                  { current.lines.push_back(cur); }
    }
    finish();
    return paragraphs;
}

// ============================================================
//  Modal font size
// ============================================================

double PdfParser::computeModalFontSize(const std::vector<TextSpan>& spans) {
    if (spans.empty()) return 12.0;
    std::unordered_map<int,double> buckets;
    for (auto& s : spans) {
        int key = (int)std::round(s.fontSize * 2.0);
        if (key > 0) buckets[key] += s.text.length();
    }
    int bestKey = 24; double bestW = 0;
    for (auto& kv : buckets) if (kv.second > bestW) { bestW=kv.second; bestKey=kv.first; }
    return bestKey / 2.0;
}

// ============================================================
//  Vector elements
// ============================================================

void PdfParser::buildVectorElements(const PageContent& content,
                                     UDoc::Page* udocPage, UDoc::Document* doc) {
    for (auto& cp : content.paths) {
        bool isBgRect = cp.filled && !cp.stroked &&
                        cp.fillR > 0.95 && cp.fillG > 0.95 && cp.fillB > 0.95;
        if (isBgRect) continue;
        if (cp.w < 0.5 && cp.h < 0.5) continue;

        UDoc::VectorGraphic vg;
        for (auto& seg : cp.segments) {
            UDoc::PathCommand cmd;
            switch (seg.op) {
            case PathSegment::Op::MoveTo:
                cmd.type=UDoc::PathCommandType::MoveTo; cmd.x1=seg.x1; cmd.y1=seg.y1; break;
            case PathSegment::Op::LineTo:
                cmd.type=UDoc::PathCommandType::LineTo; cmd.x1=seg.x1; cmd.y1=seg.y1; break;
            case PathSegment::Op::CubicTo:
                cmd.type=UDoc::PathCommandType::CubicTo;
                cmd.cx1=seg.cx1; cmd.cy1=seg.cy1; cmd.cx2=seg.cx2; cmd.cy2=seg.cy2;
                cmd.x1=seg.x1;   cmd.y1=seg.y1;   break;
            case PathSegment::Op::Close:
                cmd.type=UDoc::PathCommandType::ClosePath; break;
            }
            vg.commands.push_back(cmd);
        }
        if (vg.commands.empty()) continue;

        if (cp.filled) {
            UDoc::Fill fill;
            fill.content = UDoc::Color(cp.fillR,cp.fillG,cp.fillB,cp.fillA);
            fill.opacity = cp.fillA; vg.fill = fill;
        }
        if (cp.stroked) {
            UDoc::Stroke stroke;
            stroke.color = UDoc::Color(cp.strokeR,cp.strokeG,cp.strokeB,cp.strokeA);
            stroke.width = cp.lineWidth; vg.stroke = stroke;
        }

        double bx=std::max(0.0,cp.x), by=std::max(0.0,cp.y);
        double bw=cp.w>0.1?cp.w:cp.lineWidth+0.1;
        double bh=cp.h>0.1?cp.h:cp.lineWidth+0.1;

        for (auto& cmd : vg.commands) {
            cmd.x1-=bx; cmd.y1-=by; cmd.cx1-=bx; cmd.cy1-=by; cmd.cx2-=bx; cmd.cy2-=by;
        }

        auto elem = std::make_unique<UDoc::Element>(doc->generateId());
        elem->bounds = UDoc::Rect(bx, by, bw, bh);
        elem->content = std::move(vg);
        udocPage->addElement(std::move(elem));
    }
}

// ============================================================
//  Image elements
// ============================================================

void PdfParser::buildImageElements(const PageContent& content,
                                    UDoc::Page* udocPage, UDoc::Document* doc) {
    for (auto& ir : content.images) {
        if (ir.data.empty() && ir.imageWidth == 0) continue;
        if (ir.w < 1.0 || ir.h < 1.0) continue;

        auto imgData = std::make_shared<UDoc::ImageData>();
        imgData->rawBytes = ir.data;
        imgData->width    = ir.imageWidth;
        imgData->height   = ir.imageHeight;

        if (ir.isJpeg) {
            imgData->format = UDoc::ImageFormat::JPEG;
        } else if (!ir.data.empty() && ir.data.size() > 4 &&
                   ir.data[0]==0x89 && ir.data[1]=='P' &&
                   ir.data[2]=='N' && ir.data[3]=='G') {
            imgData->format = UDoc::ImageFormat::PNG;
        } else if (!ir.data.empty() && ir.data[0]==0xFF && ir.data[1]==0xD8) {
            imgData->format = UDoc::ImageFormat::JPEG;
        } else {
            imgData->format = UDoc::ImageFormat::Raw;
        }

        if      (ir.colorSpace == "DeviceGray") imgData->colorSpace = UDoc::ColorSpace::Grayscale;
        else if (ir.colorSpace == "DeviceCMYK") imgData->colorSpace = UDoc::ColorSpace::CMYK;
        else                                     imgData->colorSpace = UDoc::ColorSpace::RGB;

        UDoc::Image img;
        img.data = imgData; img.displayWidth = ir.w; img.displayHeight = ir.h;

        auto elem = std::make_unique<UDoc::Element>(doc->generateId());
        elem->bounds  = UDoc::Rect(ir.x, ir.y, ir.w, ir.h);
        elem->content = std::move(img);
        udocPage->addElement(std::move(elem));
    }
}

// ============================================================
//  Metadata
// ============================================================

void PdfParser::extractMetadata(UDoc::Document* doc) {
    const PdfDict& trailer = m_xref.trailer();
    const PdfObject* infoObj = trailer.get("Info");
    if (!infoObj) return;
    PdfObject info = m_xref.deref(*infoObj);
    if (!info.isDict()) return;
    PdfDict* d = info.asDict();

    auto getStr = [&](const std::string& key) -> QString {
        const PdfObject* v = d->get(key);
        if (!v) return {};
        PdfObject r = m_xref.deref(*v);
        if (r.isString()) return pdfStringToQString(r.asString());
        if (r.isName())   return QString::fromStdString(r.asName());
        return {};
    };

    doc->metadata.title    = getStr("Title");
    doc->metadata.author   = getStr("Author");
    doc->metadata.subject  = getStr("Subject");
    doc->metadata.keywords = getStr("Keywords");
    doc->metadata.creator  = getStr("Creator");
    doc->metadata.producer = getStr("Producer");

    // Parse PDF date string "D:YYYYMMDDHHmmSS" into QDateTime.
    // Qt 6.9 deprecated QDateTime(QDate,QTime,Qt::TimeSpec) — use QDateTime::fromString.
    auto parseDate = [](const QString& s) -> QDateTime {
        if (s.isEmpty()) return {};
        QString d = s.startsWith("D:") ? s.mid(2) : s;
        // Normalise to at least 14 digits
        while (d.length() < 14) d += "0";
        // Format: YYYYMMDDHHmmSS
        return QDateTime::fromString(d.left(14), "yyyyMMddHHmmss");
    };

    QString cd = getStr("CreationDate");
    QString md = getStr("ModDate");
    if (!cd.isEmpty()) doc->metadata.creationDate     = parseDate(cd);
    if (!md.isEmpty()) doc->metadata.modificationDate = parseDate(md);
}

// ============================================================
//  PDF string → QString
// ============================================================

QString PdfParser::pdfStringToQString(const std::string& s) {
    if (s.size() >= 2 &&
        (unsigned char)s[0]==0xFE && (unsigned char)s[1]==0xFF) {
        // UTF-16 BE
        QString r;
        for (size_t i = 2; i+1 < s.size(); i += 2)
            r += QChar((uint16_t)(((unsigned char)s[i]<<8)|(unsigned char)s[i+1]));
        return r;
    }
    if (s.size() >= 2 &&
        (unsigned char)s[0]==0xFF && (unsigned char)s[1]==0xFE) {
        // UTF-16 LE
        QString r;
        for (size_t i = 2; i+1 < s.size(); i += 2)
            r += QChar((uint16_t)((unsigned char)s[i]|((unsigned char)s[i+1]<<8)));
        return r;
    }
    return QString::fromLatin1(s.c_str(), (int)s.size());
}

} // namespace Pdf
