# AI Screenshot Tool C++ Qt V1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a fresh C++ + Qt Widgets Windows tray app that freezes the virtual desktop, lets the user drag any region, sends the screenshot to one configured AI provider, and supports follow-up chat with stable async loading locks and provider self-tests.

**Architecture:** Create an isolated native workspace under `native/` with a CMake-based Qt 6 app. Keep UI widgets under Qt parent ownership, keep non-UI models/services under `std::unique_ptr` / `std::shared_ptr`, route capture/request flow through a small controller-level state machine, and build provider requests on top of `QNetworkAccessManager` via an injected async transport.

**Tech Stack:** C++20, Qt 6 Core/Gui/Widgets/Network/Test, CMake, CTest, Win32 API, QSaveFile, QJsonDocument

> **Workspace note:** This directory is not a Git repository right now. Treat commit steps below as conditional.

---

### Task 1: Bootstrap the native CMake workspace and request-state guard

**Files:**
- Create: `native/CMakeLists.txt`
- Create: `native/tests/CMakeLists.txt`
- Create: `native/src/main.cpp`
- Create: `native/src/app/app_busy_state.h`
- Create: `native/src/app/request_guard.h`
- Test: `native/tests/test_request_guard.cpp`

- [ ] **Step 1: Write the failing guard test**

```cpp
#include <QtTest/QtTest>
#include "app/request_guard.h"
using namespace ais::app;
class RequestGuardTests final : public QObject { Q_OBJECT
private slots:
    void entersAndLeavesBusyStates();
    void rejectsSecondEntryUntilReleased();
};
void RequestGuardTests::entersAndLeavesBusyStates() {
    RequestGuard guard;
    QVERIFY(guard.tryEnter(BusyState::Capturing));
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Capturing));
    guard.leave(BusyState::Capturing);
    QCOMPARE(static_cast<int>(guard.state()), static_cast<int>(BusyState::Idle));
}
void RequestGuardTests::rejectsSecondEntryUntilReleased() {
    RequestGuard guard;
    QVERIFY(guard.tryEnter(BusyState::RequestInFlight));
    QVERIFY(!guard.tryEnter(BusyState::Capturing));
    guard.leave(BusyState::RequestInFlight);
    QVERIFY(guard.tryEnter(BusyState::TestingProvider));
}
QTEST_APPLESS_MAIN(RequestGuardTests)
#include "test_request_guard.moc"
```

- [ ] **Step 2: Verify the first configure fails before the project exists**

Run: `cmake -S native -B build/native`

Expected: FAIL because `native/CMakeLists.txt` does not exist yet.

- [ ] **Step 3: Create the CMake skeleton and minimal state code**

```cmake
# native/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(ai_screenshot_native VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Test)
qt_standard_project_setup()
enable_testing()
add_library(ai_screenshot_core)
target_include_directories(ai_screenshot_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ai_screenshot_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
add_executable(ai_screenshot WIN32 src/main.cpp)
target_link_libraries(ai_screenshot PRIVATE ai_screenshot_core)
add_subdirectory(tests)
```

```cmake
# native/tests/CMakeLists.txt
function(add_ai_test test_name source_file)
    qt_add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE ai_screenshot_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()
add_ai_test(test_request_guard test_request_guard.cpp)
```

```cpp
// native/src/app/app_busy_state.h
namespace ais::app { enum class BusyState { Idle, Capturing, RequestInFlight, TestingProvider }; }
```

```cpp
// native/src/app/request_guard.h
#pragma once
#include "app/app_busy_state.h"
namespace ais::app {
class RequestGuard {
public:
    [[nodiscard]] BusyState state() const noexcept { return state_; }
    [[nodiscard]] bool isBusy() const noexcept { return state_ != BusyState::Idle; }
    [[nodiscard]] bool tryEnter(BusyState next) noexcept { if (state_ != BusyState::Idle) return false; state_ = next; return true; }
    void leave(BusyState expected) noexcept { if (state_ == expected) state_ = BusyState::Idle; }
private:
    BusyState state_ = BusyState::Idle;
};
}
```

- [ ] **Step 4: Build and run the first test**

Run:
- `cmake -S native -B build/native`
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure`

Expected: `test_request_guard` PASS.

### Task 2: Add config, provider presets, chat session, placement, and bundled sample image

**Files:**
- Create: `native/src/config/provider_protocol.h`
- Create: `native/src/config/provider_preset.h`
- Create: `native/src/config/provider_profile.h`
- Create: `native/src/config/app_config.h`
- Create: `native/src/config/config_store.h`
- Create: `native/src/config/config_store.cpp`
- Create: `native/src/chat/chat_message.h`
- Create: `native/src/chat/chat_session.h`
- Create: `native/src/chat/chat_session.cpp`
- Create: `native/src/ui/panel_placement.h`
- Create: `native/src/ui/panel_placement.cpp`
- Create: `native/src/ai/sample_image_factory.h`
- Create: `native/src/ai/sample_image_factory.cpp`
- Modify: `native/CMakeLists.txt`, `native/tests/CMakeLists.txt`
- Test: `native/tests/test_config_and_session.cpp`

- [ ] **Step 1: Write failing tests for config round-trip, preset lookup, session reset, placement, and sample image bytes**

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "config/config_store.h"
#include "config/provider_preset.h"
#include "chat/chat_session.h"
#include "ui/panel_placement.h"
#include "ai/sample_image_factory.h"
class ConfigAndSessionTests final : public QObject { Q_OBJECT
private slots:
    void writesAndReadsSingleActiveProfile();
    void presetLookupReturnsRecommendedBaseUrl();
    void newScreenshotResetsConversation();
    void choosePanelPositionPrefersVisibleRightSide();
    void sampleImageFactoryProducesPngBytes();
};
```

- [ ] **Step 2: Run the new test target and confirm it fails because the classes are missing**

Run: `cmake --build build/native --config Debug --target test_config_and_session`

Expected: FAIL with missing `config/...`, `chat/...`, and `ui/...` headers.

- [ ] **Step 3: Implement the config/session layer using Qt JSON + QSaveFile**

Required code shape:

```cpp
// ProviderProtocol: OpenAiChat, OpenAiResponses, OpenAiCompatible, Gemini, Claude
// ProviderPreset: protocol, label, defaultBaseUrl, defaultModel, modelHint
// AppConfig: single active profile + shortcut + theme + opacity
// ConfigStore: load()/save() using QJsonDocument and QSaveFile
// ChatSession: beginWithCapture(), addUserText(), addAssistantText(), messages()
// choosePanelPosition(anchor, panelSize, screen): right -> left -> below -> above -> clamp
// SampleImageFactory::buildPng(): create a PNG with QImage/QPainter/QBuffer
```

- [ ] **Step 4: Add the exact tests to `native/tests/CMakeLists.txt`, rebuild, and make them pass**

Run:
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure -R "test_request_guard|test_config_and_session"`

Expected: PASS for both targets.

### Task 3: Implement provider builders, async AI client, and provider self-tests with loading locks

**Files:**
- Create: `native/src/ai/request_spec.h`
- Create: `native/src/ai/provider_interface.h`
- Create: `native/src/ai/provider_factory.h`
- Create: `native/src/ai/provider_factory.cpp`
- Create: `native/src/ai/network_transport.h`
- Create: `native/src/ai/qt_network_transport.h`
- Create: `native/src/ai/qt_network_transport.cpp`
- Create: `native/src/ai/ai_client.h`
- Create: `native/src/ai/ai_client.cpp`
- Create: `native/src/ai/provider_test_runner.h`
- Create: `native/src/ai/provider_test_runner.cpp`
- Modify: `native/CMakeLists.txt`, `native/tests/CMakeLists.txt`
- Test: `native/tests/test_ai_layer.cpp`

- [ ] **Step 1: Write failing tests for protocol payloads and async busy-state release**

```cpp
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include "ai/provider_factory.h"
#include "ai/ai_client.h"
#include "chat/chat_session.h"
#include "app/request_guard.h"
class FakeTransport final : public ais::ai::INetworkTransport {
public:
    ais::ai::RequestSpec lastSpec;
    std::function<void(QByteArray)> success;
    std::function<void(QString)> failure;
    void post(const ais::ai::RequestSpec &spec, std::function<void(QByteArray)> onSuccess, std::function<void(QString)> onFailure) override {
        lastSpec = spec; success = std::move(onSuccess); failure = std::move(onFailure);
    }
};
class AiLayerTests final : public QObject { Q_OBJECT
private slots:
    void openAiChatBuildsImageUrlPayload();
    void responsesBuildsInputImagePayload();
    void geminiBuildsGenerateContentRequest();
    void claudeBuildsBase64ImagePayload();
    void sendConversationLocksUntilCallbackReturns();
    void failedRequestReleasesBusyState();
};
```

- [ ] **Step 2: Run the new target and confirm it fails because the provider/client files do not exist yet**

Run: `cmake --build build/native --config Debug --target test_ai_layer`

Expected: FAIL with missing `ai/...` headers.

- [ ] **Step 3: Implement pure provider builders first**

Required file contents:

```cpp
// RequestSpec = { QUrl url; QHash<QString, QString> headers; QByteArray body; }
// IProvider::buildRequest(profile, messages) + parseTextResponse(payload)
// makeProvider(protocol) returns std::unique_ptr<IProvider>
// OpenAI Chat/OpenAI-Compatible => /chat/completions + image_url data URI
// OpenAI Responses => /responses + input_text/input_image
// Gemini => /models/<model>:generateContent?key=...
// Claude => /messages + base64 image source + x-api-key header
```

Use `QJsonDocument`/`QJsonObject` to build payloads. Parse the minimal assistant text from each protocol response shape.

- [ ] **Step 4: Implement the async transport and AI client with explicit request locking**

Required code shape:

```cpp
// INetworkTransport::post(spec, onSuccess, onFailure)
// QtNetworkTransport owns std::unique_ptr<QNetworkAccessManager>
// AiClient owns std::unique_ptr<INetworkTransport> and references RequestGuard
// AiClient::sendConversation(...) must:
//   1. guard_.tryEnter(BusyState::RequestInFlight)
//   2. build RequestSpec via makeProvider(profile.protocol)
//   3. call transport_->post(...)
//   4. release BusyState::RequestInFlight on both success and failure
// ProviderTestRunner::runTextTest() => prompt "Reply with OK only"
// ProviderTestRunner::runImageTest() => use SampleImageFactory::buildPng()
```

- [ ] **Step 5: Rebuild and run the AI-layer tests until green**

Run:
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure -R "test_request_guard|test_config_and_session|test_ai_layer"`

Expected: PASS; especially confirm `BusyState::RequestInFlight` returns to `Idle` on both success and failure.

### Task 4: Implement Windows DPI, hotkey parsing/registration, virtual-desktop capture, and the manual-only overlay

**Files:**
- Create: `native/src/platform/windows/dpi_awareness.h`
- Create: `native/src/platform/windows/dpi_awareness.cpp`
- Create: `native/src/platform/windows/hotkey_chord.h`
- Create: `native/src/platform/windows/hotkey_chord.cpp`
- Create: `native/src/platform/windows/global_hotkey_host.h`
- Create: `native/src/platform/windows/global_hotkey_host.cpp`
- Create: `native/src/capture/desktop_snapshot.h`
- Create: `native/src/capture/desktop_capture_service.h`
- Create: `native/src/capture/desktop_capture_service.cpp`
- Create: `native/src/capture/capture_selection.h`
- Create: `native/src/capture/capture_overlay.h`
- Create: `native/src/capture/capture_overlay.cpp`
- Modify: `native/CMakeLists.txt`, `native/tests/CMakeLists.txt`
- Test: `native/tests/test_capture_flow.cpp`

- [ ] **Step 1: Write failing tests for hotkey parsing, virtual-coordinate translation, drag-confirm, and Esc-cancel**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "platform/windows/hotkey_chord.h"
#include "capture/desktop_capture_service.h"
#include "capture/capture_overlay.h"
class CaptureFlowTests final : public QObject { Q_OBJECT
private slots:
    void parsesAltQHotkeyChord();
    void translatesLocalSelectionToVirtualDesktopCoordinates();
    void dragReleaseEmitsConfirmedSelection();
    void escapeEmitsCancelled();
};
```

- [ ] **Step 2: Run the target and confirm it fails because the platform/capture files are missing**

Run: `cmake --build build/native --config Debug --target test_capture_flow`

Expected: FAIL with missing `platform/windows/...` and `capture/...` headers.

- [ ] **Step 3: Implement the Windows and capture services**

Required code shape:

```cpp
// enablePerMonitorDpiV2() => SetProcessDpiAwarenessContext fallback to SetProcessDpiAwareness
// HotkeyChord::parse("Alt+Q") => Qt modifiers + key
// GlobalHotkeyHost => QObject + QAbstractNativeEventFilter, RegisterHotKey/UnregisterHotKey, emits triggered()
// DesktopCaptureService::captureVirtualDesktop() => union of QGuiApplication::screens(), stitch QScreen::grabWindow(0)
// DesktopCaptureService::translateToVirtual(localRect, virtualOrigin)
// CaptureOverlay => frozen screenshot, press/drag/release, size threshold, emit captureConfirmed or captureCancelled, no smart region logic
```

- [ ] **Step 4: Rebuild and run the capture-flow tests until green**

Run:
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure -R "test_request_guard|test_config_and_session|test_ai_layer|test_capture_flow"`

Expected: PASS for hotkey parsing, capture geometry, overlay confirm, and overlay cancel.

### Task 5: Build the settings dialog and floating chat panel with explicit loading-state APIs

**Files:**
- Create: `native/src/ui/settings/settingsdialog.h`
- Create: `native/src/ui/settings/settingsdialog.cpp`
- Create: `native/src/ui/chat/floating_chat_panel.h`
- Create: `native/src/ui/chat/floating_chat_panel.cpp`
- Modify: `native/CMakeLists.txt`, `native/tests/CMakeLists.txt`
- Test: `native/tests/test_ui_widgets.cpp`

- [ ] **Step 1: Write failing widget tests for protocol switching and busy-state locking**

```cpp
#include <QtTest/QtTest>
#include "config/app_config.h"
#include "ui/settings/settingsdialog.h"
#include "ui/chat/floating_chat_panel.h"
class UiWidgetTests final : public QObject { Q_OBJECT
private slots:
    void changingProtocolAppliesRecommendedBaseUrl();
    void busySettingsDialogDisablesTestButtons();
    void busyChatPanelDisablesFollowUpInput();
};
```

- [ ] **Step 2: Run the target and confirm it fails because the widgets do not exist yet**

Run: `cmake --build build/native --config Debug --target test_ui_widgets`

Expected: FAIL with missing `ui/settings/...` and `ui/chat/...` headers.

- [ ] **Step 3: Implement both widgets with stable loading behavior**

Required code shape:

```cpp
// SettingsDialog fields: protocol, base URL, API key, model, shortcut, theme, opacity
// SettingsDialog::applyProtocolPreset(protocol) => recommended base URL + default model, but editable
// SettingsDialog::setBusy(bool, status) => disable protocol selector + Test buttons while request runs
// FloatingChatPanel => status label, preview, history, input, send, recapture
// FloatingChatPanel::setBusy(bool, status) => disable input/send while request runs
// FloatingChatPanel::bindSession(shared_ptr<ChatSession>) renders current history
```

- [ ] **Step 4: Rebuild and run the widget tests until green**

Run:
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure -R "test_request_guard|test_config_and_session|test_ai_layer|test_capture_flow|test_ui_widgets"`

Expected: PASS; test buttons and follow-up input are disabled while busy.

### Task 6: Wire the application controller, tray lifecycle, hotkey flow, provider tests, and final verification

**Files:**
- Create: `native/src/app/application_controller.h`
- Create: `native/src/app/application_controller.cpp`
- Modify: `native/src/main.cpp`
- Modify: `native/CMakeLists.txt`, `native/tests/CMakeLists.txt`
- Create: `native/tests/test_application_controller.cpp`
- Create: `native/README.md`
- Modify: `docs/superpowers/specs/2026-04-15-ai-screenshot-cpp-qt-design.md`

- [ ] **Step 1: Write a failing controller test for hotkey suppression while busy**

```cpp
#include <QtTest/QtTest>
#include "app/application_controller.h"
class ApplicationControllerTests final : public QObject { Q_OBJECT
private slots:
    void ignoresCaptureShortcutWhileRequestIsBusy();
};
void ApplicationControllerTests::ignoresCaptureShortcutWhileRequestIsBusy() {
    ais::app::ApplicationController controller;
    controller.forceBusyStateForTest(ais::app::BusyState::RequestInFlight);
    QVERIFY(!controller.canStartCaptureForTest());
}
QTEST_APPLESS_MAIN(ApplicationControllerTests)
#include "test_application_controller.moc"
```

- [ ] **Step 2: Run the target and confirm it fails because the controller does not exist yet**

Run: `cmake --build build/native --config Debug --target test_application_controller`

Expected: FAIL with missing `app/application_controller.h`.

- [ ] **Step 3: Implement the controller that owns services via smart pointers and UI via Qt parent ownership**

Required code shape:

```cpp
// ApplicationController owns:
//   RequestGuard guard_;
//   std::unique_ptr<ConfigStore> configStore_;
//   std::unique_ptr<DesktopCaptureService> captureService_;
//   std::unique_ptr<AiClient> aiClient_;
//   std::unique_ptr<ProviderTestRunner> providerTestRunner_;
//   std::shared_ptr<ChatSession> currentSession_;
// plus Qt-owned QSystemTrayIcon, GlobalHotkeyHost, SettingsDialog, FloatingChatPanel, CaptureOverlay
// startCapture() => refuse if guard is busy, hide panel, processEvents(), capture virtual desktop, show overlay
// handleSelection() => create a new session, attach screenshot, show panel, send first request automatically
// handleFollowUp() => append user text, set panel busy, send request
// settings-page test buttons => use ProviderTestRunner::runTextTest/runImageTest and setBusy(true/false)
// while any request or provider test is active, do not start a new capture
```

- [ ] **Step 4: Rebuild the full app, run all tests, and fix any controller wiring issues**

Run:
- `cmake --build build/native --config Debug`
- `ctest --test-dir build/native -C Debug --output-on-failure`

Expected: all native test targets PASS.

- [ ] **Step 5: Run the app manually and execute the acceptance checklist**

Run: `build\\native\\Debug\\ai_screenshot.exe`

Verify manually:
- tray icon appears
- shortcut triggers only when app is idle
- overlay freezes the virtual desktop and allows manual drag only
- mouse release confirms capture
- Esc cancels cleanly
- floating panel stays topmost and on-screen
- while AI request is pending, follow-up input is disabled
- while settings tests are pending, test buttons are disabled
- repeated capture does not leak stale windows
- text test and image test both return readable success/failure messages

- [ ] **Step 6: Add the native README and final spec note after implementation matches the plan**

```md
# native/README.md

## Configure
`cmake -S native -B build/native`

## Build
`cmake --build build/native --config Debug`

## Test
`ctest --test-dir build/native -C Debug --output-on-failure`

## Run
`build\\native\\Debug\\ai_screenshot.exe`

## Notes
- Qt Widgets only; no QML
- CMake only; no qmake
- Non-UI services/models use smart pointers
- UI/network flow must honor busy-state locking
```

Spec follow-up note to append when implementation is complete:

```md
- Implementation status: completed against the native C++/Qt plan on <actual completion date>
```

- [ ] **Step 7: If Git is initialized, commit the finished native app and docs**

```bash
git add native docs/superpowers/specs/2026-04-15-ai-screenshot-cpp-qt-design.md
git commit -m "feat: deliver native ai screenshot v1"
```
