# ASnap HDR / Window State / Branding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix HDR screenshot overexposure in Chrome-heavy desktop scenes, persist dialog/panel sizes across restarts, make empty-response retries reissue real requests, simplify the chat composer, and brand the app as ASnap with usable icon assets.

**Architecture:** Keep the existing Qt Widgets app structure, but move screenshot capture to a Windows-native path with Qt fallback, store lightweight UI state in `AppConfig`, thread retry attempt metadata through AI request building, and keep chat composer changes local to the floating panel widget. Branding is added as static assets plus a tiny icon factory helper so the tray and app windows share one source.

**Tech Stack:** C++20, Qt 6 Core/Gui/Widgets/Network/Test, Win32 GDI, CMake, CTest

---

### Task 1: Native desktop capture for HDR-safe screenshots
- Files:
  - Create: `native/src/platform/windows/native_screen_capture.h`
  - Create: `native/src/platform/windows/native_screen_capture.cpp`
  - Modify: `native/src/capture/desktop_capture_service.cpp`
  - Modify: `native/src/capture/desktop_capture_service.h`
  - Modify: `native/CMakeLists.txt`
  - Test: `native/tests/test_capture_flow.cpp`
- Intent:
  - Add a Windows-native virtual desktop capture path
  - Prefer native capture, fallback to current Qt capture when unavailable
  - Keep current `normalizeForSdr()` only as a fallback cleanup layer

### Task 2: Persist settings/chat window sizes
- Files:
  - Modify: `native/src/config/app_config.h`
  - Modify: `native/src/config/config_store.cpp`
  - Modify: `native/src/ui/settings/settingsdialog.h`
  - Modify: `native/src/ui/settings/settingsdialog.cpp`
  - Modify: `native/src/ui/chat/floating_chat_panel.h`
  - Modify: `native/src/ui/chat/floating_chat_panel.cpp`
  - Modify: `native/src/app/application_controller.h`
  - Modify: `native/src/app/application_controller.cpp`
  - Test: `native/tests/test_config_and_session.cpp`
  - Test: `native/tests/test_ui_widgets.cpp`
  - Test: `native/tests/test_application_controller.cpp`
- Intent:
  - Persist only width/height for settings dialog and floating chat panel
  - Do not persist last position
  - Save resized dimensions without accidentally saving unconfirmed settings edits

### Task 3: Real empty-response retries
- Files:
  - Modify: `native/src/ai/request_spec.h`
  - Modify: `native/src/ai/provider_factory.cpp`
  - Modify: `native/src/ai/ai_client.h`
  - Modify: `native/src/ai/ai_client.cpp`
  - Modify: `native/src/app/application_controller.h`
  - Modify: `native/src/app/application_controller.cpp`
  - Test: `native/tests/test_ai_layer.cpp`
  - Test: `native/tests/test_application_controller.cpp`
- Intent:
  - Carry retry attempt metadata into requests
  - Add no-cache + retry headers so retries become distinct HTTP requests
  - On empty assistant content, cancel any previous stream state and issue a fresh request up to 3 retries

### Task 4: Inline send arrow inside chat composer
- Files:
  - Modify: `native/src/ui/chat/floating_chat_panel.h`
  - Modify: `native/src/ui/chat/floating_chat_panel.cpp`
  - Test: `native/tests/test_ui_widgets.cpp`
- Intent:
  - Replace the outer send button with a compact arrow button inside the input shell
  - Keep Enter-to-send and queued follow-up behavior

### Task 5: ASnap naming + icon assets
- Files:
  - Create: `native/assets/branding/asnap-icon.svg`
  - Create: `native/assets/branding/asnap-icon.png`
  - Create: `native/assets/branding/asnap-icon.ico`
  - Create: `native/src/ui/icon_factory.h`
  - Create: `native/src/ui/icon_factory.cpp`
  - Modify: `native/CMakeLists.txt`
  - Modify: `native/src/main.cpp`
  - Modify: `native/src/app/application_controller.cpp`
  - Modify: `README.md`
- Intent:
  - Brand app as ASnap
  - Use one shared icon for tray + app windows
  - Keep icon dark, minimal, and legible at small sizes

### Task 6: Verify everything end-to-end
- Files:
  - Modify as needed after test feedback
  - Test: full `native/tests/`
- Run:
  - `cmake --build build/native --config Debug -- /m:1`
  - `ctest --test-dir build/native -C Debug --output-on-failure`
- Expected:
  - Build succeeds
  - All CTest targets pass
  - No regression in chat panel or settings behavior
