#include "platform/windows/dpi_awareness.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>
#endif

namespace ais::platform::windows {

bool enablePerMonitorDpiV2() noexcept {
#if defined(_WIN32)
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

    const HMODULE user32Module = GetModuleHandleW(L"user32.dll");
    if (user32Module != nullptr) {
        const auto setProcessDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32Module, "SetProcessDpiAwarenessContext"));
        if (setProcessDpiAwarenessContext != nullptr) {
            if (setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return true;
            }

            if (GetLastError() == ERROR_ACCESS_DENIED) {
                return true;
            }
        }
    }

    const HRESULT result = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    return result == S_OK || result == E_ACCESSDENIED;
#else
    return false;
#endif
}

}  // namespace ais::platform::windows
