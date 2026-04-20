#include "capture/desktop_capture_service.h"

#include <QGuiApplication>
#include <QScreen>

#include "capture/frame_normalizer.h"
#include "capture/snapshot_composer.h"
#include "platform/windows/native_screen_capture.h"

namespace ais::capture {

namespace {

[[nodiscard]] QList<CapturedScreenFrame> capturedFramesFromScreens(const QList<QScreen*>& screens) {
    QList<CapturedScreenFrame> frames;

    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const QPixmap shot = screen->grabWindow(0);
        if (shot.isNull() || shot.width() <= 0 || shot.height() <= 0) {
            continue;
        }

        frames.append(CapturedScreenFrame{
            .image = DesktopCaptureService::normalizeForSdr(shot.toImage()),
            .overlayGeometry = screen->geometry(),
            .virtualGeometry = screen->geometry(),
            .devicePixelRatio = 1.0,
        });
    }

    return frames;
}

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

[[nodiscard]] QList<CapturedScreenFrame> capturedFramesFromNativeScreens(const QList<QScreen*>& screens) {
    const QList<platform::windows::NativeScreenFrame> nativeScreens =
        platform::windows::captureNativeScreens();
    if (nativeScreens.isEmpty()) {
        return {};
    }

    QHash<QString, platform::windows::NativeScreenFrame> nativeScreensByName;
    for (const auto& nativeScreen : nativeScreens) {
        if (!nativeScreen.image.isNull()) {
            nativeScreensByName.insert(normalizedScreenName(nativeScreen.deviceName), nativeScreen);
        }
    }

    QList<CapturedScreenFrame> frames;
    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const auto nativeScreen = nativeScreensByName.value(normalizedScreenName(screen->name()));
        if (nativeScreen.image.isNull()) {
            continue;
        }

        frames.append(CapturedScreenFrame{
            .image = DesktopCaptureService::normalizeForSdr(nativeScreen.image),
            .overlayGeometry = nativeScreen.monitorRect,
            .virtualGeometry = screen->geometry(),
            .devicePixelRatio = qMax(1.0, screen->devicePixelRatio()),
        });
    }

    return frames;
}

[[nodiscard]] QList<PreparedScreenFrame> legacyPreparedFramesFromCaptured(
    const QList<CapturedScreenFrame>& frames) {
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

}  // namespace

DesktopSnapshot DesktopCaptureService::captureVirtualDesktop() const {
    const QList<QScreen*> screens = QGuiApplication::screens();
    const QList<CapturedScreenFrame> nativeFrames = capturedFramesFromNativeScreens(screens);
    if (!nativeFrames.isEmpty() && nativeFrames.size() == screens.size()) {
        return composeFrames(nativeFrames);
    }

    return composeFrames(capturedFramesFromScreens(screens));
}

DesktopSnapshot DesktopCaptureService::composeFrames(const QList<CapturedScreenFrame>& frames) {
    const QList<PreparedScreenFrame> prepared = legacyPreparedFramesFromCaptured(frames);
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
