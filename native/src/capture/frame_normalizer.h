#pragma once

#include <QImage>

#include "capture/capture_pipeline_types.h"

namespace ais::capture {

class FrameNormalizer final {
public:
    [[nodiscard]] static QImage normalizeToSdr(const QImage& image);
    [[nodiscard]] static PreparedScreenFrame normalizeFrame(const RawScreenFrame& frame);
};

}  // namespace ais::capture
