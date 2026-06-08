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

void setFailureNote(QString* failureNote, QString note) {
    if (failureNote != nullptr) {
        *failureNote = std::move(note);
    }
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
                                                const qsizetype rowPitch,
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
                                            const qsizetype rowPitch,
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

[[nodiscard]] std::optional<HMONITOR> findMonitorForDisplay(
    const ais::capture::DisplayDescriptor& display,
    QString* failureNote) {
    if (!display.monitorRect.isValid()) {
        setFailureNote(failureNote, QStringLiteral("WGC monitor rectangle invalid"));
        return std::nullopt;
    }

    const RECT rect = detail::makeWinRectForWgcMonitorLookup(display.monitorRect);
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
    if (monitor == nullptr) {
        setFailureNote(failureNote, QStringLiteral("WGC monitor lookup failed"));
        return std::nullopt;
    }

    return monitor;
}

[[nodiscard]] std::optional<GraphicsCaptureItem> createCaptureItemForMonitor(HMONITOR monitor,
                                                                             QString* failureNote) {
    if (monitor == nullptr) {
        setFailureNote(failureNote, QStringLiteral("WGC capture item creation failed"));
        return std::nullopt;
    }

    auto interopFactory =
        winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    const HRESULT hr = interopFactory->CreateForMonitor(
        monitor,
        winrt::guid_of<winrt::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item));
    if (FAILED(hr) || !item) {
        setFailureNote(failureNote, QStringLiteral("WGC capture item creation failed"));
        return std::nullopt;
    }
    return item;
}

[[nodiscard]] std::optional<Direct3D11CaptureFrame> waitForFirstFrame(
    const Direct3D11CaptureFramePool& framePool,
    const GraphicsCaptureSession& session,
    QString* failureNote) {
    FrameArrivalState state;
    const auto frameArrivedToken = framePool.FrameArrived(
        [&state](const Direct3D11CaptureFramePool& sender,
                 const winrt::Windows::Foundation::IInspectable&) {
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
            setFailureNote(failureNote, QStringLiteral("WGC frame acquisition timed out"));
            return std::nullopt;
        }
    }

    framePool.FrameArrived(frameArrivedToken);
    return state.frame;
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
    if (FAILED(
            access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), sourceTexture.put_void())) ||
        !sourceTexture) {
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
    if (FAILED(captureContext.immediateContext->Map(stagingTexture.get(),
                                                    0,
                                                    D3D11_MAP_READ,
                                                    0,
                                                    &mapped))) {
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

[[nodiscard]] std::optional<DirectXPixelFormat> directXPixelFormatFor(
    const ais::capture::CaptureBackendKind preferredKind) {
    switch (preferredKind) {
    case ais::capture::CaptureBackendKind::WgcFp16:
        return DirectXPixelFormat::R16G16B16A16Float;
    case ais::capture::CaptureBackendKind::WgcBgra8:
        return DirectXPixelFormat::B8G8R8A8UIntNormalized;
    case ais::capture::CaptureBackendKind::Unknown:
    case ais::capture::CaptureBackendKind::Gdi:
        break;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<QImage> captureImageWithPreferredFormat(
    const D3D11CaptureContext& captureContext,
    const GraphicsCaptureItem& captureItem,
    const ais::capture::CaptureBackendKind preferredKind,
    QString* failureNote) {
    const auto directXPixelFormat = directXPixelFormatFor(preferredKind);
    if (!directXPixelFormat.has_value()) {
        setFailureNote(failureNote, QStringLiteral("WGC pixel format unsupported"));
        return std::nullopt;
    }

    try {
        auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(captureContext.direct3dDevice,
                                                                        directXPixelFormat.value(),
                                                                        1,
                                                                        captureItem.Size());
        auto captureSession = framePool.CreateCaptureSession(captureItem);

        const auto frame = waitForFirstFrame(framePool, captureSession, failureNote);
        if (!frame.has_value()) {
            captureSession.Close();
            framePool.Close();
            return std::nullopt;
        }

        const auto image = mapFrameToQImage(captureContext, frame.value());
        captureSession.Close();
        framePool.Close();
        if (!image.has_value()) {
            setFailureNote(failureNote, QStringLiteral("WGC frame mapping failed"));
            return std::nullopt;
        }
        return image;
    } catch (const winrt::hresult_error& error) {
        qWarning() << "WGC capture failed for backend kind"
                   << static_cast<int>(preferredKind)
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
        setFailureNote(failureNote, QStringLiteral("WGC capture failed"));
        return std::nullopt;
    }
}

}  // namespace

namespace detail {

RECT makeWinRectForWgcMonitorLookup(const QRect& rect) {
    return RECT{
        .left = rect.left(),
        .top = rect.top(),
        .right = rect.x() + rect.width(),
        .bottom = rect.y() + rect.height(),
    };
}

QImage makeQImageFromMappedTexture(const MappedTextureFormat format,
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

std::optional<ais::capture::RawScreenFrame> captureDisplayWithWgc(
    const ais::capture::DisplayDescriptor& display,
    const ais::capture::CaptureBackendKind preferredKind,
    QString* failureNote) {
    if (!isWindowsGraphicsCaptureSupported()) {
        setFailureNote(failureNote, QStringLiteral("WGC unsupported"));
        return std::nullopt;
    }

    try {
        ensureWinrtApartmentInitialized();

        const auto captureContext = createD3D11CaptureContext();
        if (!captureContext.has_value()) {
            setFailureNote(failureNote, QStringLiteral("WGC D3D11 device creation failed"));
            return std::nullopt;
        }

        const auto monitor = findMonitorForDisplay(display, failureNote);
        if (!monitor.has_value()) {
            return std::nullopt;
        }

        const auto captureItem = createCaptureItemForMonitor(monitor.value(), failureNote);
        if (!captureItem.has_value()) {
            return std::nullopt;
        }

        const auto image = captureImageWithPreferredFormat(captureContext.value(),
                                                           captureItem.value(),
                                                           preferredKind,
                                                           failureNote);
        if (!image.has_value()) {
            return std::nullopt;
        }

        return ais::capture::RawScreenFrame{
            .display = display,
            .image = image.value(),
            .backendKind = preferredKind,
            .colorSpace = image->colorSpace(),
            .isHdrLike = isHdrLikeImageFormat(image.value()),
        };
    } catch (const winrt::hresult_error& error) {
        qWarning() << "WGC display capture failed"
                   << "hr="
                   << Qt::hex
                   << static_cast<quint32>(error.code());
        setFailureNote(failureNote, QStringLiteral("WGC capture failed"));
        return std::nullopt;
    }
}

}  // namespace detail

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

std::optional<ais::capture::RawScreenFrame> captureDisplayWithWgc(
    const ais::capture::DisplayDescriptor& display) {
    QString failureNote;
    if (const auto bgraFrame =
            detail::captureDisplayWithWgc(display,
                                          ais::capture::CaptureBackendKind::WgcBgra8,
                                          &failureNote);
        bgraFrame.has_value()) {
        return bgraFrame;
    }

    return detail::captureDisplayWithWgc(display,
                                         ais::capture::CaptureBackendKind::WgcFp16,
                                         &failureNote);
}

}  // namespace ais::platform::windows
