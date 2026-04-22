# Windows Native Capture Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current Windows screenshot backend with a WGC/D3D11/DXGI-first backend while preserving the existing Qt overlay and conversation flow.

**Architecture:** Keep `DesktopCaptureService` and overlay contracts stable. Add a dedicated Windows Graphics Capture backend that captures one frame per monitor and falls back to the existing GDI path when unavailable.

**Tech Stack:** C++20, Qt6, Windows Graphics Capture, C++/WinRT, D3D11, DXGI, existing Qt test suite.

---

### Task 1: Add Windows Graphics Capture backend

**Files:**
- Create: `native/src/platform/windows/windows_graphics_capture_backend.h`
- Create: `native/src/platform/windows/windows_graphics_capture_backend.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] Define a backend interface returning monitor frames compatible with `NativeScreenFrame`.
- [ ] Implement monitor enumeration + `IGraphicsCaptureItemInterop::CreateForMonitor` capture flow.
- [ ] Implement GPU readback for `R16G16B16A16_FLOAT` first, fallback to `B8G8R8A8_UNORM`.
- [ ] Add required Windows libraries in CMake.

### Task 2: Wire native backend into existing capture path

**Files:**
- Modify: `native/src/platform/windows/native_screen_capture.cpp`
- Modify: `native/src/platform/windows/native_screen_capture.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`

- [ ] Make `captureNativeScreens()` prefer WGC backend.
- [ ] Preserve current GDI fallback per-screen or whole-backend fallback.
- [ ] Keep returned geometry/device-name contract compatible with `DesktopCaptureService`.
- [ ] Verify no higher-level API changes leak into overlay/controller code.

### Task 3: Stabilize behavior and cover regression points

**Files:**
- Modify: `native/tests/test_capture_flow.cpp` (if helper-level tests are added)
- Modify: any other test file only if compilation/runtime integration requires it

- [ ] Add or adjust tests for new helper logic if introduced.
- [ ] Build the project.
- [ ] Run capture-related tests and fix integration issues.

### Task 4: Final verification

**Files:**
- No new product files expected unless build fixes require them

- [ ] Run `cmake --build` for the native target.
- [ ] Run targeted Qt tests.
- [ ] Summarize remaining known limits (if any), especially HDR edge cases or unsupported systems.
