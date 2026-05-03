#include "image.h"

namespace UDoc {

QImage ImageData::getDecoded() const {
    if (!decodedValid) {
        decoded = QImage::fromData(rawBytes.data(), static_cast<int>(rawBytes.size()));
        decodedValid = !decoded.isNull();
        if (decodedValid) {
            width = decoded.width();
            height = decoded.height();
        }
    }
    return decoded;
}

} // namespace UDoc
