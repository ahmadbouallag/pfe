#include "pdf_object.h"

namespace Pdf {

void PdfDict::set(const std::string& k, PdfObject v) {
    auto it = index.find(k);
    if (it != index.end()) {
        entries[it->second].second = std::move(v);
    } else {
        index[k] = entries.size();
        entries.emplace_back(k, std::move(v));
    }
}

bool PdfDict::has(const std::string& k) const {
    return index.find(k) != index.end();
}

const PdfObject* PdfDict::get(const std::string& k) const {
    auto it = index.find(k);
    if (it == index.end()) return nullptr;
    return &entries[it->second].second;
}

PdfObject* PdfDict::get(const std::string& k) {
    auto it = index.find(k);
    if (it == index.end()) return nullptr;
    return &entries[it->second].second;
}

} // namespace Pdf
