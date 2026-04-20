#pragma once

#include <optional>

#include <QRect>
#include <QSize>
#include <QString>

#include <windows.h>

#include "capture/capture_pipeline_types.h"

namespace ais::platform::windows {

namespace detail {

enum class MappedTextureFormat {
    Bgra8Unorm,
    Rgba16Float,
};

[[nodiscard]] QImage makeQImageFromMappedTexture(MappedTextureFormat format,
                                                 const QSize& size,
                                                 qsizetype rowPitch,
                                                 const uchar* data);
[[nodiscard]] RECT makeWinRectForWgcMonitorLookup(const QRect& rect);
[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithWgc(
    const ais::capture::DisplayDescriptor& display,
    ais::capture::CaptureBackendKind preferredKind,
    QString* failureNote = nullptr);

}  // namespace detail

[[nodiscard]] bool isWindowsGraphicsCaptureSupported();
[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithWgc(
    const ais::capture::DisplayDescriptor& display);

}  // namespace ais::platform::windows
