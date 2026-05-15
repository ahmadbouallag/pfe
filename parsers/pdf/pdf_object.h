#ifndef PDF_OBJECT_H
#define PDF_OBJECT_H

// ============================================================
// pdf_object.h  —  In-memory PDF object model
//
// KEY FIX: PdfName and PdfString are distinct tagged structs,
// not bare std::string aliases. std::variant requires all
// alternative types to be unique — having two std::string
// slots causes a static_assert failure on libc++.
// ============================================================

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>

namespace Pdf {

struct PdfObject;
struct PdfDict;
struct PdfArray;
struct PdfStream;

// ---- Distinct tagged string wrappers ----
struct PdfName {
    std::string v;
    PdfName() = default;
    explicit PdfName(std::string s) : v(std::move(s)) {}
    bool operator==(const PdfName& o) const { return v == o.v; }
};

struct PdfString {
    std::string v; // raw bytes (may contain NUL)
    PdfString() = default;
    explicit PdfString(std::string s) : v(std::move(s)) {}
    bool operator==(const PdfString& o) const { return v == o.v; }
};

using PdfNull = std::monostate;
using PdfBool = bool;
using PdfInt  = int64_t;
using PdfReal = double;

// ---- Indirect reference ----
struct PdfRef {
    uint32_t obj = 0;
    uint16_t gen = 0;
    bool operator==(const PdfRef& o) const { return obj == o.obj && gen == o.gen; }
};

// ---- Array ----
struct PdfArray {
    std::vector<PdfObject> items;
    size_t size() const { return items.size(); }
    const PdfObject& at(size_t i) const { return items.at(i); }
    PdfObject& at(size_t i)             { return items.at(i); }
};

// ---- Dictionary ----
struct PdfDict {
    // Insertion-ordered entries
    std::vector<std::pair<std::string, PdfObject>> entries;
    std::unordered_map<std::string, size_t> index;

    void set(const std::string& k, PdfObject v);
    bool has(const std::string& k) const;
    const PdfObject* get(const std::string& k) const;
    PdfObject*       get(const std::string& k);

    template<typename T> const T* getAs(const std::string& k) const;
    template<typename T> T*       getAs(const std::string& k);
};

// ---- Stream ----
struct PdfStream {
    PdfDict              dict;
    std::vector<uint8_t> data; // decompressed
};

// ---- The universal variant ----
// All alternatives are distinct types — no duplicate std::string.
struct PdfObject {
    std::variant<
        PdfNull,
        PdfBool,
        PdfInt,
        PdfReal,
        PdfName,                        // /Name
        PdfString,                      // (literal) or <hex>
        std::shared_ptr<PdfArray>,
        std::shared_ptr<PdfDict>,
        std::shared_ptr<PdfStream>,
        PdfRef
    > value;

    PdfObject() : value(PdfNull{}) {}

    // ---- Construction helpers ----
    static PdfObject null()                             { return PdfObject(); }
    static PdfObject boolean(bool b)                    { PdfObject o; o.value = b;                        return o; }
    static PdfObject integer(PdfInt i)                  { PdfObject o; o.value = i;                        return o; }
    static PdfObject real(PdfReal r)                    { PdfObject o; o.value = r;                        return o; }
    static PdfObject name(std::string n)                { PdfObject o; o.value = PdfName{std::move(n)};    return o; }
    static PdfObject string(std::string s)              { PdfObject o; o.value = PdfString{std::move(s)};  return o; }
    static PdfObject array(std::shared_ptr<PdfArray> a) { PdfObject o; o.value = std::move(a);             return o; }
    static PdfObject dict(std::shared_ptr<PdfDict> d)   { PdfObject o; o.value = std::move(d);             return o; }
    static PdfObject stream(std::shared_ptr<PdfStream> s){ PdfObject o; o.value = std::move(s);            return o; }
    static PdfObject ref(uint32_t obj, uint16_t gen)    { PdfObject o; o.value = PdfRef{obj, gen};         return o; }

    // ---- Type queries ----
    bool isNull()   const { return std::holds_alternative<PdfNull>(value); }
    bool isBool()   const { return std::holds_alternative<PdfBool>(value); }
    bool isInt()    const { return std::holds_alternative<PdfInt>(value); }
    bool isReal()   const { return std::holds_alternative<PdfReal>(value); }
    bool isNumber() const { return isInt() || isReal(); }
    bool isName()   const { return std::holds_alternative<PdfName>(value); }
    bool isString() const { return std::holds_alternative<PdfString>(value); }
    bool isArray()  const { return std::holds_alternative<std::shared_ptr<PdfArray>>(value); }
    bool isStream() const { return std::holds_alternative<std::shared_ptr<PdfStream>>(value); }
    bool isDict()   const { return std::holds_alternative<std::shared_ptr<PdfDict>>(value)
                                || isStream(); }
    bool isRef()    const { return std::holds_alternative<PdfRef>(value); }

    // ---- Value accessors (caller must check type first) ----
    bool               asBool()   const { return std::get<PdfBool>(value); }
    PdfInt             asInt()    const { return std::get<PdfInt>(value); }
    PdfReal            asReal()   const { return isInt()
                                            ? static_cast<PdfReal>(std::get<PdfInt>(value))
                                            : std::get<PdfReal>(value); }
    const std::string& asName()   const { return std::get<PdfName>(value).v; }
    const std::string& asString() const { return std::get<PdfString>(value).v; }

    PdfArray* asArray() const {
        auto* p = std::get_if<std::shared_ptr<PdfArray>>(&value);
        return p ? p->get() : nullptr;
    }
    PdfDict* asDict() const {
        if (auto* p = std::get_if<std::shared_ptr<PdfDict>>(&value))   return p->get();
        if (auto* p = std::get_if<std::shared_ptr<PdfStream>>(&value)) return &(*p)->dict;
        return nullptr;
    }
    PdfStream* asStream() const {
        auto* p = std::get_if<std::shared_ptr<PdfStream>>(&value);
        return p ? p->get() : nullptr;
    }
    PdfRef asRef() const { return std::get<PdfRef>(value); }

    // Safe numeric helpers
    double numOr(double def) const { return isNumber() ? asReal() : def; }
    PdfInt intOr(PdfInt def) const { return isInt()    ? asInt()  : def; }
};

// ---- PdfDict template implementations ----
template<typename T>
const T* PdfDict::getAs(const std::string& k) const {
    const PdfObject* obj = get(k);
    if (!obj) return nullptr;
    return std::get_if<T>(&obj->value);
}
template<typename T>
T* PdfDict::getAs(const std::string& k) {
    PdfObject* obj = get(k);
    if (!obj) return nullptr;
    return std::get_if<T>(&obj->value);
}

} // namespace Pdf

#endif // PDF_OBJECT_H
