#pragma once

#include <optional>

#include <QString>
#include <QStringView>

namespace ais::capture {

enum class CaptureMode {
    Standard,
    HdrCompatible,
};

[[nodiscard]] inline QString toString(const CaptureMode mode) {
    switch (mode) {
    case CaptureMode::Standard:
        return QStringLiteral("standard");
    case CaptureMode::HdrCompatible:
        return QStringLiteral("hdr_compatible");
    }

    return QStringLiteral("standard");
}

[[nodiscard]] inline std::optional<CaptureMode> captureModeFromString(const QStringView value) {
    if (value.compare(QStringLiteral("standard"), Qt::CaseInsensitive) == 0) {
        return CaptureMode::Standard;
    }
    if (value.compare(QStringLiteral("hdr_compatible"), Qt::CaseInsensitive) == 0 ||
        value.compare(QStringLiteral("hdr-compatible"), Qt::CaseInsensitive) == 0) {
        return CaptureMode::HdrCompatible;
    }
    return std::nullopt;
}

}  // namespace ais::capture
