#ifndef MEDIA_H
#define MEDIA_H

#include "udoc_types.h"
#include <QString>

namespace UDoc {

struct Media {
    enum class Type { Video, Audio, Embedded } type = Type::Video;
    QString sourceURI;
    std::optional<std::vector<uint8_t>> embeddedData;
    bool autoPlay = false;
    bool loop = false;
    bool muted = false;
    double startTime = 0;
    double endTime = -1;
    double displayWidth = 0;
    double displayHeight = 0;
    std::optional<struct ImageData> posterFrame;
    std::optional<QString> captionTrackURI;
};

} // namespace UDoc

#endif // MEDIA_H
