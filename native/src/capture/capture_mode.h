#pragma once

#include <optional>

#include <QString>

namespace ais::capture {

enum class CaptureMode {
    Standard,
    HdrCompatible,
};

[[nodiscard]] inline QString toString(const CaptureMode mode) {
    switch (mode) {
    case CaptureMode::Standard:
        return QStringLiteral("Standard");
    case CaptureMode::HdrCompatible:
        return QStringLiteral("HdrCompatible");
    }

    return QStringLiteral("Standard");
}

[[nodiscard]] inline std::optional<CaptureMode> captureModeFromString(const QString& value) {
    const QString normalized = value.trimmed();
    if (normalized.compare(QStringLiteral("Standard"), Qt::CaseInsensitive) == 0) {
        return CaptureMode::Standard;
    }
    if (normalized.compare(QStringLiteral("HdrCompatible"), Qt::CaseInsensitive) == 0 ||
        normalized.compare(QStringLiteral("HDRCompatible"), Qt::CaseInsensitive) == 0) {
        return CaptureMode::HdrCompatible;
    }

    return std::nullopt;
}

}  // namespace ais::capture
