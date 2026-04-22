#include "platform/windows/native_screen_capture.h"

#include <memory>
#include <optional>

#include <QColorSpace>
#include <QHash>

#include <windows.h>

#include "platform/windows/windows_graphics_capture_backend.h"

namespace ais::platform::windows {

namespace {

struct MonitorDescriptor {
    HMONITOR handle = nullptr;
    QString deviceName;
    QRect monitorRect;
};

class ScreenDcHandle {
public:
    ScreenDcHandle()
        : handle_(GetDC(nullptr)) {}

    ~ScreenDcHandle() {
        if (handle_ != nullptr) {
            ReleaseDC(nullptr, handle_);
        }
    }

    [[nodiscard]] HDC get() const noexcept { return handle_; }

private:
    HDC handle_ = nullptr;
};

class CompatibleDcHandle {
public:
    explicit CompatibleDcHandle(HDC source)
        : handle_(source != nullptr ? CreateCompatibleDC(source) : nullptr) {}

    ~CompatibleDcHandle() {
        if (handle_ != nullptr) {
            DeleteDC(handle_);
        }
    }

    [[nodiscard]] HDC get() const noexcept { return handle_; }

private:
    HDC handle_ = nullptr;
};

class BitmapHandle {
public:
    explicit BitmapHandle(HBITMAP handle)
        : handle_(handle) {}

    ~BitmapHandle() {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }
    }

    [[nodiscard]] HBITMAP get() const noexcept { return handle_; }

private:
    HBITMAP handle_ = nullptr;
};

struct MonitorEnumContext {
    QList<MonitorDescriptor>* monitors = nullptr;
};

[[nodiscard]] QString normalizedDeviceName(QString name) {
    name = name.trimmed().toUpper();
    if (name.startsWith(QStringLiteral(R"(\\?\)")) ||
        name.startsWith(QStringLiteral(R"(\\.\)"))) {
        name.remove(0, 4);
    }
    return name;
}

[[nodiscard]] QImage captureMonitorImage(HDC screenDc, const RECT& monitorRect) {
    if (screenDc == nullptr) {
        return {};
    }

    const int width = monitorRect.right - monitorRect.left;
    const int height = monitorRect.bottom - monitorRect.top;
    if (width <= 0 || height <= 0) {
        return {};
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* rawPixels = nullptr;
    BitmapHandle bitmap(CreateDIBSection(screenDc,
                                         &bitmapInfo,
                                         DIB_RGB_COLORS,
                                         &rawPixels,
                                         nullptr,
                                         0));
    if (bitmap.get() == nullptr || rawPixels == nullptr) {
        return {};
    }

    CompatibleDcHandle memoryDc(screenDc);
    if (memoryDc.get() == nullptr) {
        return {};
    }

    HGDIOBJ oldObject = SelectObject(memoryDc.get(), bitmap.get());
    const bool copied = BitBlt(memoryDc.get(),
                               0,
                               0,
                               width,
                               height,
                               screenDc,
                               monitorRect.left,
                               monitorRect.top,
                               SRCCOPY | CAPTUREBLT) != FALSE;
    SelectObject(memoryDc.get(), oldObject);

    if (!copied) {
        return {};
    }

    const QImage image(static_cast<const uchar*>(rawPixels),
                       width,
                       height,
                       width * 4,
                       QImage::Format_ARGB32);
    QImage copy = image.copy();
    copy.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return copy;
}

[[nodiscard]] std::optional<NativeScreenFrame> captureMonitorWithGdi(HDC screenDc,
                                                                     const MonitorDescriptor& monitor) {
    const RECT rcMonitor{
        monitor.monitorRect.left(),
        monitor.monitorRect.top(),
        monitor.monitorRect.right(),
        monitor.monitorRect.bottom(),
    };
    const QImage image = captureMonitorImage(screenDc, rcMonitor);
    if (image.isNull()) {
        return std::nullopt;
    }

    return NativeScreenFrame{
        .deviceName = monitor.deviceName,
        .image = image,
        .monitorRect = monitor.monitorRect,
    };
}

[[nodiscard]] BOOL CALLBACK enumMonitorProc(HMONITOR monitor,
                                            HDC,
                                            LPRECT,
                                            LPARAM userData) {
    auto* context = reinterpret_cast<MonitorEnumContext*>(userData);
    if (context == nullptr || context->monitors == nullptr) {
        return TRUE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    context->monitors->append(MonitorDescriptor{
        .handle = monitor,
        .deviceName = QString::fromWCharArray(monitorInfo.szDevice),
        .monitorRect = QRect(monitorInfo.rcMonitor.left,
                             monitorInfo.rcMonitor.top,
                             monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                             monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top),
    });
    return TRUE;
}

[[nodiscard]] QList<MonitorDescriptor> enumerateMonitors() {
    QList<MonitorDescriptor> monitors;
    MonitorEnumContext context{
        .monitors = &monitors,
    };
    EnumDisplayMonitors(nullptr, nullptr, &enumMonitorProc, reinterpret_cast<LPARAM>(&context));
    return monitors;
}

[[nodiscard]] QList<NativeScreenFrame> captureGdiScreens(const QList<MonitorDescriptor>& monitors) {
    ScreenDcHandle screenDc;
    if (screenDc.get() == nullptr) {
        return {};
    }

    QList<NativeScreenFrame> frames;
    frames.reserve(monitors.size());
    for (const MonitorDescriptor& monitor : monitors) {
        const auto frame = captureMonitorWithGdi(screenDc.get(), monitor);
        if (frame.has_value()) {
            frames.append(frame.value());
        }
    }
    return frames;
}

[[nodiscard]] QList<NativeScreenFrame> mergePreferredWithGdiFallback(
    const QList<NativeScreenFrame>& preferredFrames,
    const QList<MonitorDescriptor>& monitors) {
    if (monitors.isEmpty()) {
        return preferredFrames;
    }

    QHash<QString, NativeScreenFrame> preferredFramesByName;
    for (const NativeScreenFrame& frame : preferredFrames) {
        if (!frame.image.isNull()) {
            preferredFramesByName.insert(normalizedDeviceName(frame.deviceName), frame);
        }
    }

    ScreenDcHandle screenDc;
    if (screenDc.get() == nullptr) {
        return preferredFrames;
    }

    QList<NativeScreenFrame> frames;
    frames.reserve(monitors.size());
    for (const MonitorDescriptor& monitor : monitors) {
        const auto preferredIt = preferredFramesByName.constFind(normalizedDeviceName(monitor.deviceName));
        if (preferredIt != preferredFramesByName.cend() && !preferredIt->image.isNull()) {
            frames.append(preferredIt.value());
            continue;
        }

        const auto fallbackFrame = captureMonitorWithGdi(screenDc.get(), monitor);
        if (fallbackFrame.has_value()) {
            frames.append(fallbackFrame.value());
        }
    }

    return frames.isEmpty() ? preferredFrames : frames;
}

}  // namespace

QList<NativeScreenFrame> captureNativeScreens() {
    const QList<MonitorDescriptor> monitors = enumerateMonitors();
    if (monitors.isEmpty()) {
        return captureWindowsGraphicsScreens();
    }

    const QList<NativeScreenFrame> wgcFrames =
        isWindowsGraphicsCaptureSupported() ? captureWindowsGraphicsScreens() : QList<NativeScreenFrame>{};
    if (!wgcFrames.isEmpty()) {
        return mergePreferredWithGdiFallback(wgcFrames, monitors);
    }

    return captureGdiScreens(monitors);
}

}  // namespace ais::platform::windows
