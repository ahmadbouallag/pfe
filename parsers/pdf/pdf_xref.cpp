#include "pdf_xref.h"
#include <cstring>
#include <cassert>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <zlib.h>

namespace Pdf {

// ============================================================
//  Utility
// ============================================================

static bool isWS(uint8_t c) {
    return c == 0x00 || c == 0x09 || c == 0x0A || c == 0x0C ||
           c == 0x0D || c == 0x20;
}

static bool isDelim(uint8_t c) {
    return c == '(' || c == ')' || c == '<' || c == '>' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '/' || c == '%';
}

static int hexVal(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint64_t readBE(const uint8_t* src, int byteCount) {
    uint64_t v = 0;
    for (int i = 0; i < byteCount; ++i)
        v = (v << 8) | src[i];
    return v;
}

// ============================================================
//  Public API
// ============================================================

bool PdfXref::load(const std::vector<uint8_t>& data, std::string& errorMsg) {
    m_data = &data;

    if (data.size() < 8) { errorMsg = "File too small to be a PDF"; return false; }

    if (data[0] != '%' || data[1] != 'P' || data[2] != 'D' || data[3] != 'F') {
        errorMsg = "Not a PDF file (missing %PDF header)";
        return false;
    }

    uint64_t xrefOffset = findStartXref();
    if (xrefOffset == UINT64_MAX) {
        errorMsg = "Could not locate 'startxref' in file";
        return false;
    }

    if (xrefOffset >= data.size()) {
        errorMsg = "startxref offset is beyond end of file";
        return false;
    }

    uint64_t pos = xrefOffset;
    skipWS(pos);

    if (pos + 4 <= data.size() &&
        data[pos]=='x' && data[pos+1]=='r' && data[pos+2]=='e' && data[pos+3]=='f') {
        return parseXrefTable(xrefOffset, errorMsg);
    } else {
        return parseXrefStream(xrefOffset, errorMsg);
    }
}

// ============================================================
//  Object resolution — with re-entrancy guard
// ============================================================

PdfObject PdfXref::resolve(const PdfRef& ref) {
    return resolve(ref.obj, ref.gen);
}

PdfObject PdfXref::resolve(uint32_t objNum, uint16_t /*gen*/) {
    // Cache hit
    auto cit = m_cache.find(objNum);
    if (cit != m_cache.end()) return cit->second;

    // Re-entrancy guard — if we're already resolving this object,
    // it's a circular reference; return null rather than infinite recurse.
    if (m_resolving.count(objNum)) return PdfObject::null();

    auto eit = m_table.entries.find(objNum);
    if (eit == m_table.entries.end()) return PdfObject::null();

    const XrefEntry& entry = eit->second;
    if (entry.type == XrefEntry::Type::Free) return PdfObject::null();

    m_resolving.insert(objNum);

    PdfObject result;
    try {
        if (entry.type == XrefEntry::Type::InUse) {
            // Validate offset before parsing
            if (entry.offset < m_data->size())
                result = parseIndirectObject(entry.offset);
        } else {
            // Compressed — inside an object stream
            std::string err;
            if (loadObjectStream(entry.objStmNum, err)) {
                auto cit2 = m_cache.find(objNum);
                if (cit2 != m_cache.end()) result = cit2->second;
            }
        }
    } catch (...) {
        // Parsing threw — return null safely
        result = PdfObject::null();
    }

    m_resolving.erase(objNum);
    m_cache[objNum] = result;
    return result;
}

// Safe iterative deref — follows ref chains without recursion.
// Breaks out after maxDepth hops to prevent infinite loops.
PdfObject PdfXref::deref(const PdfObject& obj, int maxDepth) {
    PdfObject current = obj;
    for (int i = 0; i < maxDepth; ++i) {
        if (!current.isRef()) return current;
        PdfRef ref = current.asRef();
        // Prevent following a ref to an object that's currently resolving
        if (m_resolving.count(ref.obj)) return PdfObject::null();
        current = resolve(ref);
    }
    // Exceeded depth — return null rather than crash
    return PdfObject::null();
}

// ============================================================
//  startxref location — fixed unsigned wraparound
// ============================================================

uint64_t PdfXref::findStartXref() const {
    const auto& data = *m_data;
    const size_t sz = data.size();
    static const char kw[] = "startxref";
    static const size_t kwLen = 9;

    if (sz < kwLen) return UINT64_MAX;

    // Search backwards from EOF within last 1024 bytes
    size_t searchStart = (sz > 1024) ? sz - 1024 : 0;
    // Use signed arithmetic to avoid size_t wraparound on --i at 0
    for (int64_t i = (int64_t)(sz - kwLen); i >= (int64_t)searchStart; --i) {
        if (memcmp(&data[i], kw, kwLen) == 0) {
            uint64_t pos = (uint64_t)i + kwLen;
            skipWS(pos);
            std::string tok = nextToken(pos);
            if (tok.empty()) continue;
            try { return std::stoull(tok); } catch (...) {}
        }
    }
    return UINT64_MAX;
}

// ============================================================
//  Classic xref table
// ============================================================

bool PdfXref::parseXrefTable(uint64_t offset, std::string& err) {
    uint64_t pos = offset;
    skipWS(pos);

    if (!peekBytes(pos, "xref", 4)) {
        err = "Expected 'xref' keyword at offset " + std::to_string(offset);
        return false;
    }
    pos += 4;
    skipWS(pos);

    const auto& data = *m_data;

    // Track which Prev offsets we've visited to break loops
    std::unordered_set<uint64_t> visitedOffsets;
    visitedOffsets.insert(offset);

    while (pos < data.size()) {
        skipWS(pos);
        if (pos >= data.size()) break;

        if (peekBytes(pos, "trailer", 7)) {
            pos += 7;
            skipWS(pos);
            PdfObject trailerObj = parseObject(pos);
            if (trailerObj.isDict()) {
                for (auto& kv : trailerObj.asDict()->entries) {
                    if (!m_table.trailer.has(kv.first))
                        m_table.trailer.set(kv.first, kv.second);
                }
            }
            // Follow /Prev
            const PdfObject* prev = m_table.trailer.get("Prev");
            if (prev && prev->isNumber()) {
                uint64_t prevOffset = static_cast<uint64_t>(prev->asReal());
                if (prevOffset < data.size() && !visitedOffsets.count(prevOffset)) {
                    visitedOffsets.insert(prevOffset);
                    std::string prevErr;
                    uint64_t pp = prevOffset;
                    skipWS(pp);
                    if (peekBytes(pp, "xref", 4))
                        parseXrefTable(prevOffset, prevErr);
                    else
                        parseXrefStream(prevOffset, prevErr);
                }
            }
            break;
        }

        // Parse "firstObj count"
        std::string firstStr = nextToken(pos);
        skipWS(pos);
        std::string countStr = nextToken(pos);
        skipWS(pos);

        if (firstStr.empty() || countStr.empty()) break;

        uint32_t firstObj = 0, count = 0;
        try {
            firstObj = static_cast<uint32_t>(std::stoul(firstStr));
            count    = static_cast<uint32_t>(std::stoul(countStr));
        } catch (...) {
            break; // not a valid subsection header — might be "trailer"
        }

        // Sanity: cap count to avoid reading past end of file
        uint64_t maxEntries = (data.size() - pos) / 20;
        if (count > maxEntries) count = (uint32_t)maxEntries;

        for (uint32_t i = 0; i < count; ++i) {
            if (pos + 20 > data.size()) break;

            char buf[21] = {};
            memcpy(buf, &data[pos], 20);
            uint64_t entryOffset = strtoull(buf, nullptr, 10);
            uint16_t gen         = static_cast<uint16_t>(strtoul(buf + 11, nullptr, 10));
            char     type        = buf[17];

            uint32_t objNum = firstObj + i;
            if (!m_table.entries.count(objNum)) {
                XrefEntry xe;
                xe.gen    = gen;
                xe.type   = (type == 'n') ? XrefEntry::Type::InUse : XrefEntry::Type::Free;
                xe.offset = entryOffset;
                m_table.entries[objNum] = xe;
            }
            pos += 20;
        }
    }

    return true;
}

// ============================================================
//  Xref stream (PDF 1.5+)
// ============================================================

bool PdfXref::parseXrefStream(uint64_t offset, std::string& err) {
    uint64_t pos = offset;
    if (pos >= m_data->size()) { err = "xref stream offset out of bounds"; return false; }

    // Parse "objNum gen obj"
    nextToken(pos); skipWS(pos); // objNum
    nextToken(pos); skipWS(pos); // gen
    std::string kw = nextToken(pos); skipWS(pos); // "obj"

    if (kw != "obj") { err = "Expected 'obj' keyword for xref stream"; return false; }

    PdfObject obj;
    try { obj = parseObject(pos); } catch (...) { err = "Failed to parse xref stream object"; return false; }

    if (!obj.isStream()) { err = "Expected xref stream object"; return false; }

    PdfStream* stm = obj.asStream();
    PdfDict&   d   = stm->dict;

    for (auto& kv : d.entries) {
        if (kv.first != "Type" && !m_table.trailer.has(kv.first))
            m_table.trailer.set(kv.first, kv.second);
    }

    const PdfObject* wObj = d.get("W");
    if (!wObj || !wObj->isArray() || wObj->asArray()->size() < 3) {
        err = "Xref stream missing /W array"; return false;
    }
    int w1 = (int)wObj->asArray()->at(0).intOr(1);
    int w2 = (int)wObj->asArray()->at(1).intOr(4);
    int w3 = (int)wObj->asArray()->at(2).intOr(2);
    int stride = w1 + w2 + w3;
    if (stride <= 0) { err = "Invalid /W in xref stream"; return false; }

    const PdfObject* sizeObj = d.get("Size");
    uint32_t totalSize = sizeObj ? (uint32_t)sizeObj->intOr(0) : 0;

    std::vector<std::pair<uint32_t,uint32_t>> subsections;
    const PdfObject* idxObj = d.get("Index");
    if (idxObj && idxObj->isArray()) {
        PdfArray* arr = idxObj->asArray();
        for (size_t i = 0; i + 1 < arr->size(); i += 2)
            subsections.push_back({ (uint32_t)arr->at(i).intOr(0),
                                    (uint32_t)arr->at(i+1).intOr(0) });
    } else {
        subsections.push_back({0, totalSize});
    }

    const std::vector<uint8_t>& raw = stm->data;
    size_t bytePos = 0;
    bool stmDone = false;

    for (size_t si = 0; si < subsections.size() && !stmDone; ++si) {
        uint32_t first = subsections[si].first;
        uint32_t count = subsections[si].second;
        for (uint32_t i = 0; i < count; ++i) {
            if (bytePos + (size_t)stride > raw.size()) { stmDone = true; break; }
            const uint8_t* row = &raw[bytePos];

            uint64_t field1 = w1 ? readBE(row, w1)           : 1;
            uint64_t field2 = readBE(row + w1, w2);
            uint64_t field3 = w3 ? readBE(row + w1 + w2, w3) : 0;

            uint32_t objNum = first + i;
            if (!m_table.entries.count(objNum)) {
                XrefEntry xe;
                xe.gen = (uint16_t)field3;
                switch (field1) {
                case 0: xe.type = XrefEntry::Type::Free;       xe.offset    = field2; break;
                case 1: xe.type = XrefEntry::Type::InUse;      xe.offset    = field2; break;
                case 2: xe.type = XrefEntry::Type::Compressed; xe.objStmNum = (uint32_t)field2;
                        xe.indexInStm = (uint32_t)field3;      xe.gen = 0; break;
                default: xe.type = XrefEntry::Type::Free;
                }
                m_table.entries[objNum] = xe;
            }
            bytePos += stride;
        }
    }
    (void)stmDone;

    // Follow /Prev — guard against loops
    const PdfObject* prev = m_table.trailer.get("Prev");
    if (prev && prev->isNumber()) {
        uint64_t prevOffset = (uint64_t)prev->asReal();
        if (prevOffset < m_data->size() && prevOffset != offset) {
            std::string prevErr;
            uint64_t pp = prevOffset;
            skipWS(pp);
            if (peekBytes(pp, "xref", 4))
                parseXrefTable(prevOffset, prevErr);
            else
                parseXrefStream(prevOffset, prevErr);
        }
    }

    return true;
}

// ============================================================
//  Indirect object parsing
// ============================================================

PdfObject PdfXref::parseIndirectObject(uint64_t offset) {
    if (offset >= m_data->size()) return PdfObject::null();
    uint64_t pos = offset;
    skipWS(pos);
    // "objNum gen obj\n<value>\nendobj"
    std::string n1 = nextToken(pos); skipWS(pos);
    std::string n2 = nextToken(pos); skipWS(pos);
    std::string kw = nextToken(pos); skipWS(pos);
    if (kw != "obj") return PdfObject::null();
    try { return parseObject(pos); } catch (...) { return PdfObject::null(); }
}

// ============================================================
//  Object stream loading — with re-entrancy guard
// ============================================================

bool PdfXref::loadObjectStream(uint32_t stmObjNum, std::string& err) {
    // Guard against recursive loading of the same object stream
    if (m_loadingObjStm.count(stmObjNum)) {
        err = "Recursive object stream " + std::to_string(stmObjNum);
        return false;
    }
    m_loadingObjStm.insert(stmObjNum);

    // Also check if it's already been loaded (objects are in cache)
    // We detect this by checking if stmObjNum itself is cached as a stream.
    PdfObject stmObj = resolve(stmObjNum);
    m_loadingObjStm.erase(stmObjNum);

    if (!stmObj.isStream()) {
        err = "Object stream " + std::to_string(stmObjNum) + " is not a stream";
        return false;
    }

    PdfStream* stm = stmObj.asStream();
    const PdfObject* nObj_     = stm->dict.get("N");
    const PdfObject* firstObj_ = stm->dict.get("First");
    int N     = nObj_     ? (int)nObj_->intOr(0)     : 0;
    int first = firstObj_ ? (int)firstObj_->intOr(0) : 0;

    if (N <= 0 || first < 0 || (size_t)first >= stm->data.size()) return true;

    const std::vector<uint8_t>& data = stm->data;

    // Parse the header: N pairs of "objNum localOffset"
    uint64_t pos = 0;
    std::vector<std::pair<uint32_t, uint64_t>> offsets;
    offsets.reserve(N);

    for (int i = 0; i < N && pos < data.size(); ++i) {
        // Skip whitespace manually (we can't use skipWS since m_data points to file)
        while (pos < data.size() && isWS(data[pos])) ++pos;
        std::string onStr, offStr;
        while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9')
            onStr += (char)data[pos++];
        while (pos < data.size() && isWS(data[pos])) ++pos;
        while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9')
            offStr += (char)data[pos++];
        if (onStr.empty() || offStr.empty()) break;
        try {
            offsets.push_back({ (uint32_t)std::stoul(onStr), (uint64_t)std::stoul(offStr) });
        } catch (...) { break; }
    }

    // Parse each embedded object from within the stream data
    const std::vector<uint8_t>* savedData = m_data;
    m_data = &stm->data;

    for (auto& [objNum, localOff] : offsets) {
        uint64_t p = (uint64_t)first + localOff;
        if (p >= stm->data.size()) continue;
        // Don't overwrite already-cached objects
        if (m_cache.count(objNum)) continue;
        try {
            PdfObject embedded = parseObject(p);
            m_cache[objNum] = embedded;
        } catch (...) {
            // Skip corrupt embedded objects
        }
    }

    m_data = savedData;
    return true;
}

// ============================================================
//  Tokeniser
// ============================================================

void PdfXref::skipWS(uint64_t& pos) const {
    const auto& data = *m_data;
    while (pos < data.size()) {
        if (isWS(data[pos])) { ++pos; continue; }
        if (data[pos] == '%') {
            while (pos < data.size() && data[pos] != '\n' && data[pos] != '\r') ++pos;
            continue;
        }
        break;
    }
}

void PdfXref::skipLine(uint64_t& pos) const {
    const auto& data = *m_data;
    while (pos < data.size() && data[pos] != '\n' && data[pos] != '\r') ++pos;
    if (pos < data.size() && data[pos] == '\r') ++pos;
    if (pos < data.size() && data[pos] == '\n') ++pos;
}

bool PdfXref::peekBytes(uint64_t pos, const char* expect, size_t len) const {
    const auto& data = *m_data;
    if (pos + len > data.size()) return false;
    return memcmp(&data[pos], expect, len) == 0;
}

std::string PdfXref::nextToken(uint64_t& pos) const {
    const auto& data = *m_data;
    skipWS(pos);
    if (pos >= data.size()) return {};
    std::string tok;
    uint8_t c = data[pos];
    if (c == '<' && pos+1 < data.size() && data[pos+1] == '<') { pos += 2; return "<<"; }
    if (c == '>' && pos+1 < data.size() && data[pos+1] == '>') { pos += 2; return ">>"; }
    if (isDelim(c) && c != '<' && c != '>') { tok += (char)c; ++pos; return tok; }
    while (pos < data.size() && !isWS(data[pos]) && !isDelim(data[pos]))
        tok += (char)data[pos++];
    return tok;
}

// ============================================================
//  Full object parser — with bounds checks
// ============================================================

PdfObject PdfXref::parseObject(uint64_t& pos) {
    const auto& data = *m_data;
    skipWS(pos);
    if (pos >= data.size()) return PdfObject::null();

    uint8_t c = data[pos];

    if (c == '/') { ++pos; return parseName(pos); }
    if (c == '[') { ++pos; return parseArray(pos); }

    if (c == '<') {
        if (pos+1 < data.size() && data[pos+1] == '<') { pos += 2; return parseDict(pos); }
        else { ++pos; return parseHexString(pos); }
    }

    if (c == '(') { ++pos; return parseLiteralString(pos); }

    if (pos+4 <= data.size() && memcmp(&data[pos], "null", 4) == 0 &&
        (pos+4 >= data.size() || isWS(data[pos+4]) || isDelim(data[pos+4])))
        { pos += 4; return PdfObject::null(); }

    if (pos+4 <= data.size() && memcmp(&data[pos], "true", 4) == 0 &&
        (pos+4 >= data.size() || isWS(data[pos+4]) || isDelim(data[pos+4])))
        { pos += 4; return PdfObject::boolean(true); }

    if (pos+5 <= data.size() && memcmp(&data[pos], "false", 5) == 0 &&
        (pos+5 >= data.size() || isWS(data[pos+5]) || isDelim(data[pos+5])))
        { pos += 5; return PdfObject::boolean(false); }

    // Number or indirect ref
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
        uint64_t savedPos = pos;
        PdfObject num = parseNumber(pos);

        // Look ahead: is this "N G R" (indirect ref)?
        if (num.isInt() && num.asInt() >= 0) {
            uint64_t afterNum = pos;
            skipWS(afterNum);
            if (afterNum < data.size() && data[afterNum] >= '0' && data[afterNum] <= '9') {
                uint64_t p2 = afterNum;
                PdfObject gen = parseNumber(p2);
                if (gen.isInt() && gen.asInt() >= 0) {
                    skipWS(p2);
                    if (p2 < data.size() && data[p2] == 'R' &&
                        (p2+1 >= data.size() || isWS(data[p2+1]) || isDelim(data[p2+1]))) {
                        ++p2;
                        pos = p2;
                        return PdfObject::ref((uint32_t)num.asInt(), (uint16_t)gen.asInt());
                    }
                }
            }
        }
        return num;
    }

    // Unknown keyword — skip it
    std::string tok = nextToken(pos);
    // Return as a name so callers can inspect it (e.g. "endobj", "stream")
    return PdfObject::name(tok);
}

PdfObject PdfXref::parseDict(uint64_t& pos) {
    auto dict = std::make_shared<PdfDict>();
    const auto& data = *m_data;

    // Safety: cap dict parsing to avoid infinite loops on malformed data
    int safetyCounter = 0;
    const int kMaxEntries = 10000;

    while (pos < data.size() && safetyCounter++ < kMaxEntries) {
        skipWS(pos);
        if (pos >= data.size()) break;
        if (pos + 1 < data.size() && data[pos] == '>' && data[pos+1] == '>') {
            pos += 2; break;
        }
        if (data[pos] != '/') {
            // Skip one byte to recover from parse errors
            ++pos;
            continue;
        }
        ++pos;
        PdfObject keyObj = parseName(pos);
        skipWS(pos);
        if (pos >= data.size()) break;
        PdfObject val = parseObject(pos);
        if (keyObj.isName())
            dict->set(keyObj.asName(), val);
    }

    // Check for stream keyword
    uint64_t afterDict = pos;
    skipWS(afterDict);
    if (peekBytes(afterDict, "stream", 6)) {
        afterDict += 6;
        if (afterDict < data.size() && data[afterDict] == '\r') ++afterDict;
        if (afterDict < data.size() && data[afterDict] == '\n') ++afterDict;

        uint64_t posAfter = afterDict;
        std::vector<uint8_t> rawData;
        try {
            rawData = readStreamData(afterDict, *dict, posAfter);
        } catch (...) {
            // Stream read failed — return as plain dict
            return PdfObject::dict(dict);
        }

        auto stm = std::make_shared<PdfStream>();
        stm->dict = *dict;
        stm->data = std::move(rawData);
        pos = posAfter;
        return PdfObject::stream(stm);
    }

    return PdfObject::dict(dict);
}

PdfObject PdfXref::parseArray(uint64_t& pos) {
    auto arr = std::make_shared<PdfArray>();
    const auto& data = *m_data;
    int safety = 0;
    const int kMaxItems = 100000;

    while (pos < data.size() && safety++ < kMaxItems) {
        skipWS(pos);
        if (pos >= data.size()) break;
        if (data[pos] == ']') { ++pos; break; }
        arr->items.push_back(parseObject(pos));
    }
    return PdfObject::array(arr);
}

PdfObject PdfXref::parseName(uint64_t& pos) {
    const auto& data = *m_data;
    std::string name;
    while (pos < data.size() && !isWS(data[pos]) && !isDelim(data[pos])) {
        if (data[pos] == '#' && pos+2 < data.size()) {
            int hi = hexVal(data[pos+1]), lo = hexVal(data[pos+2]);
            if (hi >= 0 && lo >= 0) { name += (char)((hi<<4)|lo); pos += 3; continue; }
        }
        name += (char)data[pos++];
    }
    return PdfObject::name(name);
}

PdfObject PdfXref::parseLiteralString(uint64_t& pos) {
    const auto& data = *m_data;
    std::string result;
    int depth = 1;
    while (pos < data.size() && depth > 0) {
        uint8_t c = data[pos++];
        if (c == '\\') {
            if (pos >= data.size()) break;
            uint8_t esc = data[pos++];
            switch (esc) {
            case 'n': result += '\n'; break; case 'r': result += '\r'; break;
            case 't': result += '\t'; break; case 'b': result += '\b'; break;
            case 'f': result += '\f'; break; case '(': result += '(';  break;
            case ')': result += ')';  break; case '\\': result += '\\'; break;
            case '\r': if (pos < data.size() && data[pos]=='\n') ++pos; break;
            case '\n': break;
            default:
                if (esc >= '0' && esc <= '7') {
                    int oct = esc - '0';
                    for (int i=0; i<2 && pos<data.size() && data[pos]>='0' && data[pos]<='7'; ++i)
                        oct = oct*8 + (data[pos++]-'0');
                    result += (char)(oct & 0xFF);
                } else { result += (char)esc; }
            }
        } else if (c == '(') { depth++; result += c; }
        else if (c == ')') { if (--depth > 0) result += c; }
        else result += c;
    }
    return PdfObject::string(result);
}

PdfObject PdfXref::parseHexString(uint64_t& pos) {
    const auto& data = *m_data;
    std::string result, hexBuf;
    while (pos < data.size() && data[pos] != '>') {
        uint8_t c = data[pos++];
        if (isWS(c)) continue;
        hexBuf += (char)c;
        if (hexBuf.size() == 2) {
            int hi = hexVal(hexBuf[0]), lo = hexVal(hexBuf[1]);
            if (hi >= 0 && lo >= 0) result += (char)((hi<<4)|lo);
            hexBuf.clear();
        }
    }
    if (!hexBuf.empty()) { int hi = hexVal(hexBuf[0]); if (hi >= 0) result += (char)(hi<<4); }
    if (pos < data.size()) ++pos; // skip '>'
    return PdfObject::string(result);
}

PdfObject PdfXref::parseNumber(uint64_t& pos) {
    const auto& data = *m_data;
    std::string s;
    if (pos < data.size() && (data[pos]=='-'||data[pos]=='+')) s += (char)data[pos++];
    bool isFloat = false;
    while (pos < data.size()) {
        uint8_t c = data[pos];
        if (c>='0'&&c<='9') { s+=(char)c; ++pos; }
        else if (c=='.'&&!isFloat) { s+='.'; isFloat=true; ++pos; }
        else break;
    }
    if (s.empty()||s=="-"||s=="+") return PdfObject::null();
    try {
        if (isFloat) return PdfObject::real(std::stod(s));
        else         return PdfObject::integer(std::stoll(s));
    } catch (...) { return PdfObject::null(); }
}

// ============================================================
//  Stream reading and decompression
// ============================================================

std::vector<uint8_t> PdfXref::readStreamData(uint64_t dataStart,
                                               const PdfDict& dict,
                                               uint64_t& posAfterStream) {
    const auto& fileData = *m_data;
    if (dataStart >= fileData.size()) return {};

    int64_t length = -1;
    const PdfObject* lenObj = dict.get("Length");
    if (lenObj) {
        PdfObject resolved = deref(*lenObj);
        if (resolved.isNumber()) length = (int64_t)resolved.asReal();
    }

    std::vector<uint8_t> raw;

    if (length >= 0 && length < (int64_t)100*1024*1024 && // sanity: < 100 MB
        dataStart + (uint64_t)length <= fileData.size()) {
        raw.assign(fileData.begin() + dataStart,
                   fileData.begin() + dataStart + length);
        posAfterStream = dataStart + length;
        skipWS(posAfterStream);
        if (peekBytes(posAfterStream, "endstream", 9)) posAfterStream += 9;
        skipWS(posAfterStream);
        if (peekBytes(posAfterStream, "endobj", 6)) posAfterStream += 6;
    } else {
        // Fallback: scan for "endstream"
        uint64_t p = dataStart;
        uint64_t limit = std::min(fileData.size(), dataStart + (uint64_t)100*1024*1024);
        while (p + 9 <= limit) {
            if (memcmp(&fileData[p], "endstream", 9) == 0) {
                uint64_t end = p;
                if (end > dataStart && fileData[end-1] == '\n') --end;
                if (end > dataStart && fileData[end-1] == '\r') --end;
                raw.assign(fileData.begin() + dataStart, fileData.begin() + end);
                posAfterStream = p + 9;
                break;
            }
            ++p;
        }
    }

    // Apply filters
    const PdfObject* filterObj = dict.get("Filter");
    if (!filterObj || raw.empty()) return raw;

    std::vector<std::string> filters;
    if (filterObj->isName()) {
        filters.push_back(filterObj->asName());
    } else if (filterObj->isArray()) {
        for (size_t i = 0; i < filterObj->asArray()->size(); ++i) {
            const PdfObject& f = filterObj->asArray()->at(i);
            if (f.isName()) filters.push_back(f.asName());
        }
    }

    const PdfObject* parmsObj = dict.get("DecodeParms");

    std::vector<uint8_t> current = raw;
    for (size_t fi = 0; fi < filters.size(); ++fi) {
        const std::string& fname = filters[fi];

        int predictor = 1, columns = 1, colors = 1, bpc = 8;
        if (parmsObj) {
            const PdfObject* parm = nullptr;
            if (parmsObj->isDict()) {
                parm = parmsObj;
            } else if (parmsObj->isArray() && parmsObj->asArray()->size() > fi) {
                parm = &parmsObj->asArray()->at(fi);
            }
            if (parm && parm->isDict()) {
                const PdfObject* pv;
                if ((pv = parm->asDict()->get("Predictor"))) predictor = (int)pv->intOr(1);
                if ((pv = parm->asDict()->get("Columns")))   columns   = (int)pv->intOr(1);
                if ((pv = parm->asDict()->get("Colors")))    colors    = (int)pv->intOr(1);
                if ((pv = parm->asDict()->get("BitsPerComponent"))) bpc = (int)pv->intOr(8);
            }
        }

        if (fname == "FlateDecode" || fname == "Fl") {
            auto decompressed = inflateFlateDecode(current.data(), current.size());
            if (!decompressed.empty()) {
                current = std::move(decompressed);
                if (predictor > 1)
                    current = applyPredictor(current, predictor, columns, colors, bpc);
            }
            // If decompression failed (empty result), keep current as-is
        } else if (fname == "DCTDecode" || fname == "DCT") {
            // JPEG — pass through
        } else if (fname == "ASCIIHexDecode") {
            std::vector<uint8_t> decoded;
            decoded.reserve(current.size() / 2);
            for (size_t i = 0; i < current.size(); ) {
                if (isWS(current[i])) { ++i; continue; }
                if (current[i] == '>') break;
                int hi = hexVal(current[i]); ++i;
                while (i < current.size() && isWS(current[i])) ++i;
                int lo = (i < current.size() && current[i] != '>') ? hexVal(current[i++]) : 0;
                if (hi >= 0 && lo >= 0) decoded.push_back((uint8_t)((hi<<4)|lo));
            }
            current = decoded;
        } else if (fname == "ASCII85Decode") {
            std::vector<uint8_t> decoded;
            size_t i = 0;
            while (i < current.size()) {
                if (isWS(current[i])) { ++i; continue; }
                if (current[i] == '~') break;
                if (current[i] == 'z') {
                    decoded.insert(decoded.end(), 4, 0); ++i; continue;
                }
                uint64_t val = 0; int n = 0;
                while (n < 5 && i < current.size() && current[i] != '~') {
                    if (isWS(current[i])) { ++i; continue; }
                    val = val * 85 + (current[i] - '!'); ++i; ++n;
                }
                for (int extra = n; extra < 5; ++extra) val = val * 85;
                for (int b = 3; b >= 4 - n; --b) decoded.push_back((uint8_t)(val >> (b*8)));
            }
            current = decoded;
        }
        // LZW, RunLength — pass through
    }

    return current;
}

// ============================================================
//  FlateDecode (zlib)
// ============================================================

std::vector<uint8_t> PdfXref::inflateFlateDecode(const uint8_t* src, size_t srcLen) {
    if (!src || srcLen == 0) return {};
    std::vector<uint8_t> out;
    out.reserve(std::min(srcLen * 4, (size_t)64*1024*1024)); // cap reserve at 64 MB

    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return {};

    zs.next_in  = const_cast<Bytef*>(src);
    zs.avail_in = static_cast<uInt>(srcLen);

    uint8_t buf[65536];
    int ret = Z_OK;
    size_t totalOut = 0;
    const size_t kMaxOut = 256 * 1024 * 1024; // 256 MB safety cap

    while (ret != Z_STREAM_END && totalOut < kMaxOut) {
        zs.next_out  = buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) break;
        size_t produced = sizeof(buf) - zs.avail_out;
        out.insert(out.end(), buf, buf + produced);
        totalOut += produced;
        if (zs.avail_in == 0 && ret != Z_STREAM_END) break;
    }
    inflateEnd(&zs);
    return out;
}

// ============================================================
//  PNG predictor un-filtering
// ============================================================

std::vector<uint8_t> PdfXref::applyPredictor(
        const std::vector<uint8_t>& raw,
        int predictor, int columns, int colors, int bitsPerComponent) {

    if (columns <= 0) columns = 1;
    if (colors  <= 0) colors  = 1;
    if (bitsPerComponent <= 0) bitsPerComponent = 8;

    if (predictor < 10) {
        if (predictor == 2) {
            std::vector<uint8_t> out = raw;
            int bytesPerPixel = (colors * bitsPerComponent + 7) / 8;
            int rowBytes = ((columns * colors * bitsPerComponent) + 7) / 8;
            if (rowBytes <= 0 || raw.empty()) return raw;
            for (size_t row = 0; row < raw.size() / rowBytes; ++row) {
                for (int col = bytesPerPixel; col < rowBytes; ++col)
                    out[row * rowBytes + col] += out[row * rowBytes + col - bytesPerPixel];
            }
            return out;
        }
        return raw;
    }

    int bytesPerPixel = (colors * bitsPerComponent + 7) / 8;
    int rowStride     = ((columns * colors * bitsPerComponent) + 7) / 8;
    int srcStride     = rowStride + 1;

    if (rowStride <= 0 || srcStride <= 0) return raw;

    std::vector<uint8_t> out;
    out.reserve(raw.size());
    std::vector<uint8_t> prev(rowStride, 0);

    size_t pos = 0;
    while (pos + (size_t)srcStride <= raw.size()) {
        uint8_t filterType = raw[pos++];
        std::vector<uint8_t> row(raw.begin() + pos, raw.begin() + pos + rowStride);
        pos += rowStride;

        switch (filterType) {
        case 0: break;
        case 1:
            for (int i = bytesPerPixel; i < rowStride; ++i)
                row[i] += row[i - bytesPerPixel];
            break;
        case 2:
            for (int i = 0; i < rowStride; ++i) row[i] += prev[i];
            break;
        case 3:
            for (int i = 0; i < rowStride; ++i) {
                uint8_t a = (i >= bytesPerPixel) ? row[i-bytesPerPixel] : 0;
                row[i] += (uint8_t)(((int)a + prev[i]) / 2);
            }
            break;
        case 4: {
            auto paeth=[](int a,int b,int c)->uint8_t{
                int p=a+b-c,pa=std::abs(p-a),pb=std::abs(p-b),pc=std::abs(p-c);
                if(pa<=pb&&pa<=pc)return(uint8_t)a;if(pb<=pc)return(uint8_t)b;return(uint8_t)c;
            };
            for (int i = 0; i < rowStride; ++i) {
                int a=(i>=bytesPerPixel)?row[i-bytesPerPixel]:0;
                int c=(i>=bytesPerPixel)?prev[i-bytesPerPixel]:0;
                row[i] += paeth(a, prev[i], c);
            }
            break;
        }
        default: break;
        }

        out.insert(out.end(), row.begin(), row.end());
        prev = row;
    }
    return out;
}

} // namespace Pdf
