#include "platform/windows/windows_graphics_capture_backend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>

#include <QColorSpace>
#include <QDebug>
#include <QImage>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/base.h>

namespace ais::platform::windows {

namespace {

using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

constexpr auto kCaptureTimeout = std::chrono::seconds(2);

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

struct D3D11CaptureContext {
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> immediateContext;
    IDirect3DDevice direct3dDevice{nullptr};
};

struct FrameArrivalState {
    std::mutex mutex;
    std::condition_variable condition;
    Direct3D11CaptureFrame frame{nullptr};
    bool hasFrame = false;
};

class CaptureEnumContext {
public:
    explicit CaptureEnumContext(QList<MonitorDescriptor>* output)
        : output_(output) {}

    [[nodiscard]] QList<MonitorDescriptor>* output() const noexcept { return output_; }

private:
    QList<MonitorDescriptor>* output_ = nullptr;
};

void ensureWinrtApartmentInitialized() {
    thread_local const bool initialized = [] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error& error) {
            if (error.code() != RPC_E_CHANGED_MODE) {
                throw;
            }
        }
        return true;
    }();
    (void)initialized;
}

[[nodiscard]] float halfToFloat(std::uint16_t half) {
    const std::uint32_t sign = (half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x03FFu;

    std::uint32_t full = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            full = sign;
        } else {
            std::uint32_t shifted = mantissa;
            std::int32_t exp = -1;
            while ((shifted & 0x0400u) == 0) {
                shifted <<= 1;
                --exp;
            }
            shifted &= 0x03FFu;
            const std::uint32_t fullExponent = static_cast<std::uint32_t>(127 - 15 + 1 + exp);
            full = sign | (fullExponent << 23) | (shifted << 13);
        }
    } else if (exponent == 0x1Fu) {
        full = sign | 0x7F800000u | (mantissa << 13);
    } else {
        const std::uint32_t fullExponent = exponent + (127 - 15);
        full = sign | (fullExponent << 23) | (mantissa << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &full, sizeof(result));
    return result;
}

[[nodiscard]] std::uint8_t to8Bit(float normalized) {
    if (!(normalized > 0.0f)) {
        return 0;
    }
    if (normalized >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(normalized * 255.0f + 0.5f);
}

[[nodiscard]] QImage copyHalfFloatImageFromRows(const QSize& size,
                                                qsizetype rowPitch,
                                                const uchar* data) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    QImage image(size, QImage::Format_RGBA16FPx4);
    if (image.isNull()) {
        return {};
    }

    constexpr int bytesPerPixel = 8;
    const qsizetype bytesPerLine = size.width() * bytesPerPixel;
    for (int y = 0; y < size.height(); ++y) {
        const auto* src = data + static_cast<std::size_t>(y) * rowPitch;
        std::memcpy(image.scanLine(y), src, static_cast<std::size_t>(bytesPerLine));
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    return image;
#else
    QImage image(size, QImage::Format_ARGB32);
    if (image.isNull()) {
        return {};
    }

    for (int y = 0; y < size.height(); ++y) {
        const auto* src =
            reinterpret_cast<const std::uint16_t*>(data + static_cast<std::size_t>(y) * rowPitch);
        auto* dst = reinterpret_cast<std::uint32_t*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const float r = halfToFloat(src[x * 4 + 0]);
            const float g = halfToFloat(src[x * 4 + 1]);
            const float b = halfToFloat(src[x * 4 + 2]);
            const float a = halfToFloat(src[x * 4 + 3]);
            dst[x] = (static_cast<std::uint32_t>(to8Bit(a)) << 24) |
                     (static_cast<std::uint32_t>(to8Bit(r)) << 16) |
                     (static_cast<std::uint32_t>(to8Bit(g)) << 8) |
                     static_cast<std::uint32_t>(to8Bit(b));
        }
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    return image;
#endif
}

[[nodiscard]] QImage copyBgra8ImageFromRows(const QSize& size,
                                            qsizetype rowPitch,
                                            const uchar* data) {
    QImage image(size, QImage::Format_ARGB32);
    if (image.isNull()) {
        return {};
    }

    constexpr int bytesPerPixel = 4;
    const qsizetype bytesPerLine = size.width() * bytesPerPixel;
    for (int y = 0; y < size.height(); ++y) {
        const auto* src = data + static_cast<std::size_t>(y) * rowPitch;
        std::memcpy(image.scanLine(y), src, static_cast<std::size_t>(bytesPerLine));
    }

    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

[[nodiscard]] QImage captureMonitorImageWithGdi(HDC screenDc, const QRect& monitorRect) {
    if (screenDc == nullptr || !monitorRect.isValid() || monitorRect.isEmpty()) {
        return {};
    }

    const int width = monitorRect.width();
    const int height = monitorRect.height();

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
                               monitorRect.left(),
                               monitorRect.top(),
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
    const QImage image = captureMonitorImageWithGdi(screenDc, monitor.monitorRect);
    if (image.isNull()) {
        return std::nullopt;
    }

    return NativeScreenFrame{
        .deviceName = monitor.deviceName,
        .image = image,
        .monitorRect = monitor.monitorRect,
    };
}

[[nodiscard]] std::optional<D3D11CaptureContext> createD3D11CaptureContext() {
    D3D11CaptureContext captureContext;

    constexpr UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    constexpr std::array<D3D_FEATURE_LEVEL, 4> featureLevels = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = E_FAIL;
    const std::array<D3D_DRIVER_TYPE, 2> driverTypes = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
    };

    for (const D3D_DRIVER_TYPE driverType : driverTypes) {
        hr = D3D11CreateDevice(nullptr,
                               driverType,
                               nullptr,
                               deviceFlags,
                               featureLevels.data(),
                               static_cast<UINT>(featureLevels.size()),
                               D3D11_SDK_VERSION,
                               captureContext.device.put(),
                               nullptr,
                               captureContext.immediateContext.put());
        if (SUCCEEDED(hr)) {
            break;
        }
    }

    if (FAILED(hr) || !captureContext.device || !captureContext.immediateContext) {
        return std::nullopt;
    }

    winrt::com_ptr<IDXGIDevice> dxgiDevice = captureContext.device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectableDevice;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectableDevice.put()))) {
        return std::nullopt;
    }

    captureContext.direct3dDevice = inspectableDevice.as<IDirect3DDevice>();
    if (!captureContext.direct3dDevice) {
        return std::nullopt;
    }

    return captureContext;
}

[[nodiscard]] std::optional<GraphicsCaptureItem> createCaptureItemForMonitor(HMONITOR monitor) {
    if (monitor == nullptr) {
        return std::nullopt;
    }

    auto interopFactory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    const HRESULT hr = interopFactory->CreateForMonitor(
        monitor,
        winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item));
    if (FAILED(hr) || !item) {
        return std::nullopt;
    }
    return item;
}

[[nodiscard]] BOOL CALLBACK enumMonitorProc(HMONITOR monitor,
                                            HDC,
                                            LPRECT,
                                            LPARAM userData) {
    auto* context = reinterpret_cast<CaptureEnumContext*>(userData);
    if (context == nullptr || context->output() == nullptr) {
        return TRUE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    const RECT& rc = monitorInfo.rcMonitor;
    context->output()->append(MonitorDescriptor{
        .handle = monitor,
        .deviceName = QString::fromWCharArray(monitorInfo.szDevice),
        .monitorRect = QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top),
    });

    return TRUE;
}

[[nodiscard]] QList<MonitorDescriptor> enumerateMonitors() {
    QList<MonitorDescriptor> monitors;
    CaptureEnumContext context(&monitors);
    EnumDisplayMonitors(nullptr, nullptr, &enumMonitorProc, reinterpret_cast<LPARAM>(&context));
    return monitors;
}

[[nodiscard]] std::optional<Direct3D11CaptureFrame> waitForFirstFrame(
    const Direct3D11CaptureFramePool& framePool,
    const GraphicsCaptureSession& session) {
    FrameArrivalState state;
    const auto frameArrivedToken = framePool.FrameArrived(
        [&state](const Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&) {
            const auto frame = sender.TryGetNextFrame();
            if (!frame) {
                return;
            }

            {
                std::lock_guard lock(state.mutex);
                if (state.hasFrame) {
                    return;
                }
                state.frame = frame;
                state.hasFrame = true;
            }
            state.condition.notify_one();
        });

    session.StartCapture();

    {
        std::unique_lock lock(state.mutex);
        if (!state.condition.wait_for(lock, kCaptureTimeout, [&state] { return state.hasFrame; })) {
            framePool.FrameArrived(frameArrivedToken);
            return std::nullopt;
        }
    }

    framePool.FrameArrived(frameArrivedToken);
    return state.frame;
}

void configureStillCaptureSession(const GraphicsCaptureSession& session) {
    if (!session) {
        return;
    }

    const detail::StillCaptureSessionOptions options = detail::stillCaptureSessionOptions();

    try {
        if (const auto session3 =
                session.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession3>()) {
            session3.IsBorderRequired(options.borderRequired);
        }
    } catch (const winrt::hresult_error& error) {
        qWarning() << "Failed to disable WGC border"
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
    }

    try {
        if (const auto session2 =
                session.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession2>()) {
            session2.IsCursorCaptureEnabled(options.cursorCaptureEnabled);
        }
    } catch (const winrt::hresult_error& error) {
        qWarning() << "Failed to disable WGC cursor capture"
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
    }
}

[[nodiscard]] std::optional<QImage> mapFrameToQImage(const D3D11CaptureContext& captureContext,
                                                     const Direct3D11CaptureFrame& frame) {
    if (!captureContext.device || !captureContext.immediateContext || !frame) {
        return std::nullopt;
    }

    const auto surface = frame.Surface();
    if (!surface) {
        return std::nullopt;
    }

    auto access =
        surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> sourceTexture;
    if (FAILED(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), sourceTexture.put_void())) || !sourceTexture) {
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC sourceDesc{};
    sourceTexture->GetDesc(&sourceDesc);

    if (sourceDesc.Width == 0 || sourceDesc.Height == 0) {
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.SampleDesc.Quality = 0;

    winrt::com_ptr<ID3D11Texture2D> stagingTexture;
    if (FAILED(captureContext.device->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.put()))) {
        return std::nullopt;
    }

    captureContext.immediateContext->CopyResource(stagingTexture.get(), sourceTexture.get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(captureContext.immediateContext->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return std::nullopt;
    }

    QImage image;
    if (stagingDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        image = detail::makeQImageFromMappedTexture(
            detail::MappedTextureFormat::Rgba16Float,
            QSize(static_cast<int>(sourceDesc.Width), static_cast<int>(sourceDesc.Height)),
            mapped.RowPitch,
            static_cast<const uchar*>(mapped.pData));
    } else if (stagingDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        image = detail::makeQImageFromMappedTexture(
            detail::MappedTextureFormat::Bgra8Unorm,
            QSize(static_cast<int>(sourceDesc.Width), static_cast<int>(sourceDesc.Height)),
            mapped.RowPitch,
            static_cast<const uchar*>(mapped.pData));
    }

    captureContext.immediateContext->Unmap(stagingTexture.get(), 0);

    if (image.isNull()) {
        return std::nullopt;
    }

    return image;
}

[[nodiscard]] std::optional<QImage> captureMonitorImageWithFormat(
    const D3D11CaptureContext& captureContext,
    const GraphicsCaptureItem& captureItem,
    DirectXPixelFormat pixelFormat) {
    try {
        auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            captureContext.direct3dDevice,
            pixelFormat,
            1,
            captureItem.Size());
        auto captureSession = framePool.CreateCaptureSession(captureItem);
        configureStillCaptureSession(captureSession);

        const auto frame = waitForFirstFrame(framePool, captureSession);
        if (!frame.has_value()) {
            captureSession.Close();
            framePool.Close();
            return std::nullopt;
        }

        const auto image = mapFrameToQImage(captureContext, frame.value());
        captureSession.Close();
        framePool.Close();
        return image;
    } catch (const winrt::hresult_error& error) {
        qWarning() << "WGC capture failed for pixel format"
                   << static_cast<int>(pixelFormat)
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
        return std::nullopt;
    }
}

[[nodiscard]] bool orderContains(const QList<capture::ScreenCaptureBackend>& order,
                                 const capture::ScreenCaptureBackend backend) {
    return std::find(order.cbegin(), order.cend(), backend) != order.cend();
}

[[nodiscard]] std::optional<DirectXPixelFormat> pixelFormatForBackend(
    const capture::ScreenCaptureBackend backend) {
    switch (backend) {
    case capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra:
        return DirectXPixelFormat::B8G8R8A8UIntNormalized;
    case capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16:
        return DirectXPixelFormat::R16G16B16A16Float;
    case capture::ScreenCaptureBackend::Gdi:
        break;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<NativeScreenFrame> captureSingleMonitor(
    const MonitorDescriptor& monitor,
    const QList<capture::ScreenCaptureBackend>& order,
    const std::optional<D3D11CaptureContext>& captureContext,
    HDC screenDc) {
    std::optional<GraphicsCaptureItem> captureItem;
    bool captureItemAttempted = false;

    for (const capture::ScreenCaptureBackend backend : order) {
        if (backend == capture::ScreenCaptureBackend::Gdi) {
            const auto gdiFrame = captureMonitorWithGdi(screenDc, monitor);
            if (gdiFrame.has_value()) {
                return gdiFrame;
            }
            continue;
        }

        if (!captureContext.has_value()) {
            continue;
        }

        if (!captureItemAttempted) {
            captureItem = createCaptureItemForMonitor(monitor.handle);
            captureItemAttempted = true;
        }
        if (!captureItem.has_value()) {
            continue;
        }

        const auto pixelFormat = pixelFormatForBackend(backend);
        if (!pixelFormat.has_value()) {
            continue;
        }

        const auto image = captureMonitorImageWithFormat(captureContext.value(),
                                                         captureItem.value(),
                                                         pixelFormat.value());
        if (!image.has_value()) {
            continue;
        }

        return NativeScreenFrame{
            .deviceName = monitor.deviceName,
            .image = image.value(),
            .monitorRect = monitor.monitorRect,
        };
    }

    return std::nullopt;
}

[[nodiscard]] QList<NativeScreenFrame> captureMonitorsWithBackends(
    const QList<MonitorDescriptor>& monitors,
    const QList<capture::ScreenCaptureBackend>& order) {
    QList<NativeScreenFrame> frames;
    if (monitors.isEmpty() || order.isEmpty()) {
        return frames;
    }

    std::optional<D3D11CaptureContext> captureContext;
    if (orderContains(order, capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra) ||
        orderContains(order, capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16)) {
        try {
            ensureWinrtApartmentInitialized();
            if (isWindowsGraphicsCaptureSupported()) {
                captureContext = createD3D11CaptureContext();
            }
        } catch (const winrt::hresult_error& error) {
            qWarning() << "WGC backend initialization failed"
                       << "hr="
                       << Qt::hex
                       << static_cast<quint32>(error.code());
        }
    }

    ScreenDcHandle screenDc;
    const HDC gdiDc =
        orderContains(order, capture::ScreenCaptureBackend::Gdi) ? screenDc.get() : nullptr;

    frames.reserve(monitors.size());
    for (const MonitorDescriptor& monitor : monitors) {
        const auto frame = captureSingleMonitor(monitor, order, captureContext, gdiDc);
        if (frame.has_value()) {
            frames.append(frame.value());
        }
    }

    return frames;
}

}  // namespace

detail::StillCaptureSessionOptions detail::stillCaptureSessionOptions() {
    return StillCaptureSessionOptions{
        .borderRequired = false,
        .cursorCaptureEnabled = false,
    };
}

QImage detail::makeQImageFromMappedTexture(const MappedTextureFormat format,
                                           const QSize& size,
                                           const qsizetype rowPitch,
                                           const uchar* data) {
    if (data == nullptr || !size.isValid() || size.isEmpty() || rowPitch <= 0) {
        return {};
    }

    const qsizetype minimumRowPitch = [size, format]() -> qsizetype {
        switch (format) {
        case MappedTextureFormat::Bgra8Unorm:
            return static_cast<qsizetype>(size.width()) * 4;
        case MappedTextureFormat::Rgba16Float:
            return static_cast<qsizetype>(size.width()) * 8;
        }
        return 0;
    }();
    if (minimumRowPitch <= 0 || rowPitch < minimumRowPitch) {
        return {};
    }

    switch (format) {
    case MappedTextureFormat::Bgra8Unorm:
        return copyBgra8ImageFromRows(size, rowPitch, data);
    case MappedTextureFormat::Rgba16Float:
        return copyHalfFloatImageFromRows(size, rowPitch, data);
    }

    return {};
}

bool isWindowsGraphicsCaptureSupported() {
    try {
        ensureWinrtApartmentInitialized();
        return GraphicsCaptureSession::IsSupported();
    } catch (const winrt::hresult_error& error) {
        qWarning() << "WGC support check failed"
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
        return false;
    }
}

QList<capture::ScreenCaptureBackend> backendOrderForCaptureMode(const capture::CaptureMode mode) {
    switch (mode) {
    case capture::CaptureMode::HdrCompatible:
        return {
            capture::ScreenCaptureBackend::Gdi,
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra,
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16,
        };
    case capture::CaptureMode::Standard:
        return {
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra,
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16,
            capture::ScreenCaptureBackend::Gdi,
        };
    }

    return {
        capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra,
        capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16,
        capture::ScreenCaptureBackend::Gdi,
    };
}

QList<NativeScreenFrame> captureWindowsGraphicsScreens() {
    return captureMonitorsWithBackends(
        enumerateMonitors(),
        {
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureBgra,
            capture::ScreenCaptureBackend::WindowsGraphicsCaptureFp16,
        });
}

QList<NativeScreenFrame> captureWindowsScreens(const capture::CaptureMode mode) {
    return captureMonitorsWithBackends(enumerateMonitors(), backendOrderForCaptureMode(mode));
}

}  // namespace ais::platform::windows
