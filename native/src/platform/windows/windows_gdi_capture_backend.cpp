#include "platform/windows/windows_gdi_capture_backend.h"

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
    explicit CompatibleDcHandle(const HDC source)
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
    explicit BitmapHandle(const HBITMAP handle)
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

[[nodiscard]] QImage captureMonitorImage(const HDC screenDc, const RECT& monitorRect) {
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

[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureMonitorWithGdi(
    const HDC screenDc,
    const ais::capture::DisplayDescriptor& display) {
    if (!display.monitorRect.isValid()) {
        return std::nullopt;
    }

    const RECT monitorRect{
        .left = display.monitorRect.left(),
        .top = display.monitorRect.top(),
        .right = display.monitorRect.right(),
        .bottom = display.monitorRect.bottom(),
    };
    const QImage image = captureMonitorImage(screenDc, monitorRect);
    if (image.isNull()) {
        return std::nullopt;
    }

    return ais::capture::RawScreenFrame{
        .display = display,
        .image = image,
        .backendKind = ais::capture::CaptureBackendKind::Gdi,
        .colorSpace = image.colorSpace(),
        .isHdrLike = false,
    };
}

}  // namespace

std::optional<ais::capture::RawScreenFrame> captureDisplayWithGdi(
    const ais::capture::DisplayDescriptor& display) {
    ScreenDcHandle screenDc;
    if (screenDc.get() == nullptr) {
        return std::nullopt;
    }

    return captureMonitorWithGdi(screenDc.get(), display);
}

}  // namespace ais::platform::windows
