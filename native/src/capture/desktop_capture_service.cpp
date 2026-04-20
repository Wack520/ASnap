#include "capture/desktop_capture_service.h"

#include <memory>

#include <QGuiApplication>
#include <QHash>
#include <QScreen>

#include "capture/frame_normalizer.h"
#include "capture/snapshot_composer.h"
#include "platform/windows/native_screen_capture.h"
#include "platform/windows/windows_display_topology.h"

namespace ais::capture {

namespace {

[[nodiscard]] QString normalizedScreenName(QString name) {
    name = name.trimmed().toUpper();
    if (name.startsWith(QStringLiteral(R"(\\?\)"))) {
        name.remove(0, 4);
    }
    if (name.startsWith(QStringLiteral(R"(\\.\)"))) {
        name.remove(0, 4);
    }
    return name;
}

[[nodiscard]] bool isHdrLikeImageFormat(const QImage& image) {
    switch (image.format()) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

void appendDiagnostic(CaptureDiagnostics* diagnostics,
                      const DisplayDescriptor& display,
                      CaptureBackendKind backendKind,
                      bool fellBack,
                      QString note,
                      bool hdrToneMapped = false) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->entries.append(CaptureDiagnosticsEntry{
        .deviceName = display.deviceName,
        .backendKind = backendKind,
        .hdrToneMapped = hdrToneMapped,
        .fellBack = fellBack,
        .note = std::move(note),
    });
}

void markToneMappedDiagnostics(const QList<PreparedScreenFrame>& preparedFrames,
                               CaptureDiagnostics* diagnostics) {
    if (diagnostics == nullptr || diagnostics->entries.isEmpty()) {
        return;
    }

    for (const PreparedScreenFrame& frame : preparedFrames) {
        if (!frame.hdrToneMapped) {
            continue;
        }

        for (CaptureDiagnosticsEntry& entry : diagnostics->entries) {
            if (normalizedScreenName(entry.deviceName) ==
                normalizedScreenName(frame.display.deviceName)) {
                entry.hdrToneMapped = true;
                break;
            }
        }
    }
}

class NativeScreenCaptureBackend final : public ScreenCaptureBackend {
public:
    [[nodiscard]] QList<RawScreenFrame> captureDisplays(
        const QList<DisplayDescriptor>& displays,
        CaptureDiagnostics* diagnostics) const override {
        if (diagnostics != nullptr) {
            diagnostics->entries.clear();
        }

        const QList<platform::windows::NativeScreenFrame> nativeScreens =
            platform::windows::captureNativeScreens();
        QHash<QString, platform::windows::NativeScreenFrame> nativeScreensByName;
        nativeScreensByName.reserve(nativeScreens.size());
        for (const auto& nativeScreen : nativeScreens) {
            if (!nativeScreen.image.isNull()) {
                nativeScreensByName.insert(normalizedScreenName(nativeScreen.deviceName),
                                           nativeScreen);
            }
        }

        const QList<QScreen*> qtScreens = QGuiApplication::screens();
        QHash<QString, QScreen*> qtScreensByName;
        qtScreensByName.reserve(qtScreens.size());
        for (QScreen* screen : qtScreens) {
            if (screen != nullptr) {
                qtScreensByName.insert(normalizedScreenName(screen->name()), screen);
            }
        }

        QList<RawScreenFrame> frames;
        frames.reserve(displays.size());

        for (const DisplayDescriptor& display : displays) {
            const QString deviceKey = normalizedScreenName(display.deviceName);
            const auto nativeScreen = nativeScreensByName.value(deviceKey);
            if (!nativeScreen.image.isNull()) {
                const QImage image = nativeScreen.image;
                frames.append(RawScreenFrame{
                    .display = display,
                    .image = image,
                    .backendKind = CaptureBackendKind::Gdi,
                    .colorSpace = image.colorSpace(),
                    .isHdrLike = isHdrLikeImageFormat(image),
                });
                appendDiagnostic(diagnostics,
                                 display,
                                 CaptureBackendKind::Gdi,
                                 false,
                                 QStringLiteral("native-gdi"));
                continue;
            }

            QScreen* matchedScreen = qtScreensByName.value(deviceKey, nullptr);
            if (matchedScreen == nullptr && display.virtualRect.isValid()) {
                for (QScreen* screen : qtScreens) {
                    if (screen != nullptr && screen->geometry() == display.virtualRect) {
                        matchedScreen = screen;
                        break;
                    }
                }
            }

            if (matchedScreen == nullptr) {
                appendDiagnostic(diagnostics,
                                 display,
                                 CaptureBackendKind::Unknown,
                                 true,
                                 QStringLiteral("capture-unavailable"));
                continue;
            }

            const QPixmap shot = matchedScreen->grabWindow(0);
            if (shot.isNull() || shot.width() <= 0 || shot.height() <= 0) {
                appendDiagnostic(diagnostics,
                                 display,
                                 CaptureBackendKind::Unknown,
                                 true,
                                 QStringLiteral("qt-grabwindow-empty"));
                continue;
            }

            const QImage image = shot.toImage();
            frames.append(RawScreenFrame{
                .display = display,
                .image = image,
                .backendKind = CaptureBackendKind::Unknown,
                .colorSpace = image.colorSpace(),
                .isHdrLike = isHdrLikeImageFormat(image),
            });
            appendDiagnostic(diagnostics,
                             display,
                             CaptureBackendKind::Unknown,
                             true,
                             QStringLiteral("qt-grabwindow"));
        }

        return frames;
    }
};

[[nodiscard]] QList<PreparedScreenFrame> preparedFramesFromCaptured(const QList<CapturedScreenFrame>& frames) {
    QList<PreparedScreenFrame> prepared;
    prepared.reserve(frames.size());

    for (const CapturedScreenFrame& frame : frames) {
        prepared.append(PreparedScreenFrame{
            .display = DisplayDescriptor{
                .monitorRect = frame.overlayGeometry,
                .virtualRect = frame.virtualGeometry.isValid() ? frame.virtualGeometry
                                                               : frame.overlayGeometry,
                .devicePixelRatio = frame.devicePixelRatio,
            },
            .normalizedImage = frame.image,
        });
    }

    return prepared;
}

[[nodiscard]] QList<PreparedScreenFrame> preparedFramesFromRaw(const QList<RawScreenFrame>& frames) {
    QList<PreparedScreenFrame> prepared;
    prepared.reserve(frames.size());

    for (const RawScreenFrame& frame : frames) {
        if (frame.image.isNull()) {
            continue;
        }

        const bool hdrToneMapped = frame.isHdrLike || isHdrLikeImageFormat(frame.image);
        prepared.append(PreparedScreenFrame{
            .display = frame.display,
            .normalizedImage = DesktopCaptureService::normalizeForSdr(frame.image),
            .backendKind = frame.backendKind,
            .hdrToneMapped = hdrToneMapped,
        });
    }

    return prepared;
}

}  // namespace

DesktopCaptureService::DesktopCaptureService()
    : DesktopCaptureService(std::make_unique<platform::windows::WindowsDisplayTopology>(),
                            std::make_unique<NativeScreenCaptureBackend>()) {}

DesktopCaptureService::DesktopCaptureService(std::unique_ptr<DisplayTopology> topology,
                                             std::unique_ptr<ScreenCaptureBackend> backend)
    : topology_(std::move(topology)),
      backend_(std::move(backend)) {}

DesktopSnapshot DesktopCaptureService::captureVirtualDesktop() const {
    if (!topology_ || !backend_) {
        return {};
    }

    CaptureDiagnostics diagnostics;
    const QList<DisplayDescriptor> displays = topology_->enumerateDisplays();
    const QList<RawScreenFrame> rawFrames = backend_->captureDisplays(displays, &diagnostics);
    QList<PreparedScreenFrame> preparedFrames = preparedFramesFromRaw(rawFrames);
    markToneMappedDiagnostics(preparedFrames, &diagnostics);
    return SnapshotComposer::composeFrames(preparedFrames, diagnostics);
}

DesktopSnapshot DesktopCaptureService::composeFrames(const QList<CapturedScreenFrame>& frames) {
    const QList<PreparedScreenFrame> prepared = preparedFramesFromCaptured(frames);
    return SnapshotComposer::composeFrames(prepared);
}

QImage DesktopCaptureService::normalizeForSdr(const QImage& image) {
    return FrameNormalizer::normalizeToSdr(image);
}

QRect DesktopCaptureService::translateToVirtual(const QRect& localRect,
                                                const QPoint& virtualOrigin) {
    return SnapshotComposer::translateToVirtual(localRect, virtualOrigin);
}

QRect DesktopCaptureService::translateToVirtual(const DesktopSnapshot& snapshot,
                                                const QRect& localRect) {
    return SnapshotComposer::translateToVirtual(snapshot, localRect);
}

QPixmap DesktopCaptureService::copyLogicalSelection(const QPixmap& source,
                                                    const QRect& logicalRect) {
    return SnapshotComposer::copyLogicalSelection(source, logicalRect);
}

}  // namespace ais::capture
