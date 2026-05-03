#ifndef IMAGE_H
#define IMAGE_H

#include "udoc_types.h"
#include <vector>
#include <memory>

namespace UDoc {

enum class ImageFormat { PNG, JPEG, GIF, BMP, TIFF, WebP, SVG, Raw, Unknown };
enum class ColorSpace { RGB, RGBA, CMYK, Grayscale, Indexed, Unknown };

struct ImageData {
    std::vector<uint8_t> rawBytes;
    ImageFormat format = ImageFormat::Unknown;
    mutable int32_t width = 0;
    mutable int32_t height = 0;
    ColorSpace colorSpace = ColorSpace::RGB;
    double dpiX = 72;
    double dpiY = 72;
    mutable QImage decoded;
    mutable bool decodedValid = false;

    QImage getDecoded() const;
    bool isVector() const { return format == ImageFormat::SVG; }
};

struct Image {
    std::shared_ptr<ImageData> data;
    double displayWidth = 0;
    double displayHeight = 0;
    Rect cropRect = Rect(0, 0, 1, 1);
    double rotation = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
    enum Interpolation { Nearest, Bilinear, Bicubic, Lanczos } interpolation = Bilinear;
    QString altText;
    std::optional<ID> captionBlockId;
};

} // namespace UDoc

#endif // IMAGE_H
