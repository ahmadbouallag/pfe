#ifndef PDF_XREF_H
#define PDF_XREF_H

// ============================================================
// pdf_xref.h  —  Cross-reference table + object resolver
// ============================================================

#include "pdf_object.h"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>

namespace Pdf {

struct XrefEntry {
    enum class Type : uint8_t { Free = 0, InUse = 1, Compressed = 2 };
    Type     type       = Type::Free;
    uint64_t offset     = 0;
    uint32_t objStmNum  = 0;
    uint32_t indexInStm = 0;
    uint16_t gen        = 0;
};

struct XrefTable {
    std::unordered_map<uint32_t, XrefEntry> entries;
    PdfDict trailer;
};

class PdfXref {
public:
    bool load(const std::vector<uint8_t>& data, std::string& errorMsg);

    PdfObject resolve(const PdfRef& ref);
    PdfObject resolve(uint32_t objNum, uint16_t gen = 0);
    PdfObject deref(const PdfObject& obj);

    const PdfDict& trailer() const { return m_table.trailer; }
    const std::vector<uint8_t>& raw() const { return *m_data; }

private:
    const std::vector<uint8_t>* m_data = nullptr;
    XrefTable m_table;
    std::unordered_map<uint32_t, PdfObject> m_cache;

    uint64_t findStartXref() const;
    bool parseXrefTable(uint64_t offset, std::string& err);
    bool parseXrefStream(uint64_t offset, std::string& err);
    PdfObject parseIndirectObject(uint64_t offset);
    bool loadObjectStream(uint32_t stmObjNum, std::string& err);

    // Tokeniser — all take byte-index into *m_data
    void skipWS(uint64_t& pos) const;
    void skipLine(uint64_t& pos) const;
    bool peekBytes(uint64_t pos, const char* expect, size_t len) const;
    std::string nextToken(uint64_t& pos) const;

    PdfObject parseObject(uint64_t& pos);
    PdfObject parseDict(uint64_t& pos);
    PdfObject parseArray(uint64_t& pos);
    PdfObject parseName(uint64_t& pos);
    PdfObject parseLiteralString(uint64_t& pos);
    PdfObject parseHexString(uint64_t& pos);
    PdfObject parseNumber(uint64_t& pos);

    std::vector<uint8_t> readStreamData(uint64_t dataStart,
                                         const PdfDict& dict,
                                         uint64_t& posAfterStream);

    static std::vector<uint8_t> inflateFlateDecode(const uint8_t* src, size_t srcLen);
    static std::vector<uint8_t> applyPredictor(const std::vector<uint8_t>& raw,
                                                int predictor, int columns,
                                                int colors, int bitsPerComponent);
};

} // namespace Pdf

#endif // PDF_XREF_H
