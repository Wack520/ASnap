#pragma once

#include <QColorSpace>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QtGlobal>

namespace ais::capture {

enum class CaptureBackendKind {
    Unknown,
    WgcFp16,
    WgcBgra8,
    Gdi,
};

struct DisplayDescriptor {
    QString deviceName;
    QRect monitorRect;
    QRect virtualRect;
    qreal devicePixelRatio = 1.0;
    bool isPrimary = false;
};

struct RawScreenFrame {
    DisplayDescriptor display;
    QImage image;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    QColorSpace colorSpace;
    bool isHdrLike = false;
};

struct PreparedScreenFrame {
    DisplayDescriptor display;
    QImage normalizedImage;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    bool hdrToneMapped = false;
};

struct CaptureDiagnosticsEntry {
    QString deviceName;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    bool hdrToneMapped = false;
    bool fellBack = false;
    QString note;
};

struct CaptureDiagnostics {
    QList<CaptureDiagnosticsEntry> entries;
};

}  // namespace ais::capture
