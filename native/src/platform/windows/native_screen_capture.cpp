#include "platform/windows/native_screen_capture.h"

#include <memory>

#include <QColorSpace>

#include <windows.h>

namespace ais::platform::windows {

namespace {

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

struct CaptureEnumContext {
    HDC screenDc = nullptr;
    QList<NativeScreenFrame>* frames = nullptr;
};

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

[[nodiscard]] BOOL CALLBACK captureMonitorProc(HMONITOR monitor,
                                               HDC,
                                               LPRECT,
                                               LPARAM userData) {
    auto* context = reinterpret_cast<CaptureEnumContext*>(userData);
    if (context == nullptr || context->frames == nullptr || context->screenDc == nullptr) {
        return TRUE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    const QImage image = captureMonitorImage(context->screenDc, monitorInfo.rcMonitor);
    if (image.isNull()) {
        return TRUE;
    }

    context->frames->append(NativeScreenFrame{
        .deviceName = QString::fromWCharArray(monitorInfo.szDevice),
        .image = image,
    });
    return TRUE;
}

}  // namespace

QList<NativeScreenFrame> captureNativeScreens() {
    ScreenDcHandle screenDc;
    if (screenDc.get() == nullptr) {
        return {};
    }

    QList<NativeScreenFrame> frames;
    CaptureEnumContext context{
        .screenDc = screenDc.get(),
        .frames = &frames,
    };
    EnumDisplayMonitors(nullptr, nullptr, &captureMonitorProc, reinterpret_cast<LPARAM>(&context));
    return frames;
}

}  // namespace ais::platform::windows
