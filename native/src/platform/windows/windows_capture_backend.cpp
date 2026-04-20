#include "platform/windows/windows_capture_backend.h"

#include <utility>

#include "platform/windows/windows_gdi_capture_backend.h"
#include "platform/windows/windows_graphics_capture_backend.h"

namespace ais::platform::windows {

namespace {

void appendDiagnostic(ais::capture::CaptureDiagnostics* diagnostics,
                      const ais::capture::DisplayDescriptor& display,
                      const ais::capture::CaptureBackendKind backendKind,
                      const bool fellBack,
                      QString note) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->entries.append(ais::capture::CaptureDiagnosticsEntry{
        .deviceName = display.deviceName,
        .backendKind = backendKind,
        .hdrToneMapped = false,
        .fellBack = fellBack,
        .note = std::move(note),
    });
}

[[nodiscard]] QString successNoteForBackend(
    const ais::capture::CaptureBackendKind backendKind) {
    switch (backendKind) {
    case ais::capture::CaptureBackendKind::WgcFp16:
        return QStringLiteral("WGC FP16");
    case ais::capture::CaptureBackendKind::WgcBgra8:
        return QStringLiteral("WGC BGRA fallback");
    case ais::capture::CaptureBackendKind::Gdi:
        return QStringLiteral("GDI fallback");
    case ais::capture::CaptureBackendKind::Unknown:
        break;
    }

    return QStringLiteral("capture");
}

[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureWithWgc(
    const ais::capture::DisplayDescriptor& display,
    const ais::capture::CaptureBackendKind preferredKind,
    QString* failureNote) {
    if (detail::g_windowsCaptureBackendTestHooks != nullptr &&
        detail::g_windowsCaptureBackendTestHooks->captureWithWgc != nullptr) {
        return detail::g_windowsCaptureBackendTestHooks->captureWithWgc(display,
                                                                        preferredKind,
                                                                        failureNote);
    }

    return detail::captureDisplayWithWgc(display, preferredKind, failureNote);
}

[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureWithGdi(
    const ais::capture::DisplayDescriptor& display) {
    if (detail::g_windowsCaptureBackendTestHooks != nullptr &&
        detail::g_windowsCaptureBackendTestHooks->captureWithGdi != nullptr) {
        return detail::g_windowsCaptureBackendTestHooks->captureWithGdi(display);
    }

    return captureDisplayWithGdi(display);
}

}  // namespace

namespace detail {

const WindowsCaptureBackendTestHooks* g_windowsCaptureBackendTestHooks = nullptr;

void setWindowsCaptureBackendTestHooks(const WindowsCaptureBackendTestHooks* hooks) {
    g_windowsCaptureBackendTestHooks = hooks;
}

}  // namespace detail

QList<ais::capture::RawScreenFrame> WindowsScreenCaptureBackend::captureDisplays(
    const QList<ais::capture::DisplayDescriptor>& displays,
    ais::capture::CaptureDiagnostics* diagnostics) const {
    if (diagnostics != nullptr) {
        diagnostics->entries.clear();
    }

    QList<ais::capture::RawScreenFrame> frames;
    frames.reserve(displays.size());

    for (const ais::capture::DisplayDescriptor& display : displays) {
        QString lastFailureNote;
        const auto tryWgc = [&](const ais::capture::CaptureBackendKind preferredKind) {
            QString attemptFailureNote;
            const auto frame = captureWithWgc(display, preferredKind, &attemptFailureNote);
            if (!attemptFailureNote.isEmpty()) {
                lastFailureNote = attemptFailureNote;
            }
            return frame;
        };

        if (const auto fp16Frame = tryWgc(ais::capture::CaptureBackendKind::WgcFp16);
            fp16Frame.has_value()) {
            frames.append(fp16Frame.value());
            appendDiagnostic(diagnostics,
                             display,
                             fp16Frame->backendKind,
                             false,
                             successNoteForBackend(fp16Frame->backendKind));
            continue;
        }

        if (const auto bgraFrame = tryWgc(ais::capture::CaptureBackendKind::WgcBgra8);
            bgraFrame.has_value()) {
            frames.append(bgraFrame.value());
            appendDiagnostic(diagnostics,
                             display,
                             bgraFrame->backendKind,
                             true,
                             successNoteForBackend(bgraFrame->backendKind));
            continue;
        }

        if (const auto gdiFrame = captureWithGdi(display); gdiFrame.has_value()) {
            frames.append(gdiFrame.value());
            appendDiagnostic(diagnostics,
                             display,
                             ais::capture::CaptureBackendKind::Gdi,
                             true,
                             !lastFailureNote.isEmpty()
                                 ? lastFailureNote
                                 : QStringLiteral("WGC capture failed"));
            continue;
        }

        appendDiagnostic(diagnostics,
                         display,
                         ais::capture::CaptureBackendKind::Unknown,
                         true,
                         !lastFailureNote.isEmpty()
                             ? lastFailureNote
                             : QStringLiteral("Capture unavailable"));
    }

    return frames;
}

}  // namespace ais::platform::windows
