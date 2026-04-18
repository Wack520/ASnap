# AI Screenshot Tool C++ Qt Design

**Date:** 2026-04-15
**Status:** Approved for spec write; waiting for user review before implementation planning
**Scope:** Build V1 of a Windows desktop AI screenshot tool in C++ + Qt Widgets, replacing the current Python implementation direction.

## 1. Product Goal
Build a Windows desktop tool that stays in the tray, listens for a global shortcut, freezes the full screen on trigger, lets the user drag any arbitrary rectangle, and immediately opens a topmost floating chat window that can continue asking questions based on that screenshot.

V1 optimizes for a stable closed loop:
- tray resident
- global shortcut
- frozen full-screen capture
- freeform drag selection
- auto-send first AI analysis
- floating follow-up chat
- settings page with text/image connectivity tests

## 2. Explicit V1 Decisions
### Included
- C++ + Qt Widgets
- QSystemTrayIcon tray app
- Windows global hotkey via native API
- Full virtual-desktop screenshot freeze
- Manual drag selection only
- Release mouse to confirm
- Esc to cancel
- Single active provider/model at a time
- New screenshot starts a new session
- Auto-send default analysis prompt after capture
- Floating topmost chat panel
- Settings page with protocol/base URL/API key/model fields
- "Test Connection" button for text request
- "Test Image Understanding" button using a bundled sample image

### Excluded from V1
- Automatic candidate regions
- OCR workflow
- Annotation tools
- Scrolling capture
- Screenshot history library
- Parallel/multi-model compare view
- In-panel provider/model switching

## 3. Technology Direction
### 3.1 UI stack
Use **Qt Widgets**, not QML.

Reasons:
- faster V1 delivery
- simpler Windows desktop behavior control
- easier always-on-top / overlay / tray style tool implementation
- lower risk for capture overlay and window positioning

### 3.2 Build system
Use **CMake** as the only supported build system for the new C++ project.

Rules:
- do not use qmake
- structure the repo as a modern CMake project from day one
- keep third-party dependency integration CMake-friendly
- prefer a layout that works cleanly with Qt Creator, Visual Studio, and CLI builds

Reason:
- modern C++ ecosystem standard
- easier future integration of third-party libraries such as JSON parsers
- clearer target-based dependency management
- better long-term maintainability than qmake

### 3.3 Runtime model
Use a **single-process desktop app** with clear module boundaries.

Do not split into a Python sidecar or multi-process architecture in V1.

## 4. Architecture Overview
Recommended module layout:

- `src/app/`
  - app entry
  - application controller
  - lifecycle orchestration
- `src/platform/windows/`
  - hotkey registration
  - DPI awareness setup
  - virtual desktop geometry helpers
- `src/capture/`
  - full-screen snapshot service
  - capture overlay widget
  - selection result model
- `src/ai/`
  - provider abstraction
  - provider implementations
  - text/image self-test runners
- `src/chat/`
  - chat session model
  - message history
  - default prompt policy
- `src/ui/`
  - floating chat panel
  - settings dialog
  - shared theme helpers
- `src/config/`
  - config model
  - JSON persistence
- `tests/`
  - unit tests for config, providers, placement, session state, request payloads

## 5. Lifetime and Memory Management Policy
### 5.1 Qt UI objects
Qt UI objects may use the normal `QObject` parent-child ownership model.

Examples:
- windows
- dialogs
- buttons
- layouts
- tray actions
- widgets attached to parent widgets

### 5.2 Business objects
For self-defined business objects, use **modern C++ smart pointers** consistently.

Mandatory rule:
- use `std::unique_ptr` by default for single-owner objects
- use `std::shared_ptr` only when shared ownership is truly needed
- avoid raw owning pointers
- do not manually scatter `new` / `delete` across business logic

Applies to objects such as:
- session list / session manager
- capture result models
- image cache / preview cache
- provider request services
- config service
- test runner objects
- message models not owned by QObject hierarchy

Raw pointers may appear only as:
- non-owning observer references
- temporary API interop pointers
- Qt parent-owned object references

This rule is intended to prevent leaks and simplify teardown during repeated capture / panel / request cycles.

## 6. Capture Subsystem Design
### 6.1 User flow
1. App is running in tray.
2. User presses global shortcut.
3. Existing floating panel is hidden.
4. UI event queue is flushed once to avoid panel appearing in the screenshot.
5. App captures the full virtual desktop.
6. Overlay covers the virtual desktop with the frozen image.
7. User drags an arbitrary rectangle.
8. Mouse release confirms capture immediately.
9. Esc cancels capture and restores previous chat panel if one exists.
10. Confirmed capture opens a new chat session and sends the initial AI request.

### 6.2 Overlay responsibilities
The overlay widget has exactly these responsibilities:
- display the frozen screenshot
- handle press / drag / release input
- draw current selection rectangle and size tag
- emit either confirm or cancel

The overlay must not do:
- automatic candidate detection
- smart snapping
- wheel switching
- double-click alternate selection
- recognition heuristics

### 6.3 Multi-monitor and DPI
All capture coordinates are based on the **Windows virtual desktop coordinate space**.

The app should enable **Per-Monitor DPI Awareness V2** on startup so mixed-DPI monitors do not cause selection offsets.

## 7. Floating Chat Panel Design
### 7.1 Behavior
The floating chat panel must:
- be always on top
- support dark / light theme
- support configurable opacity
- appear near the captured region
- remain fully inside the target screen bounds
- support follow-up questions on the same screenshot session

### 7.2 Placement strategy
Preferred placement order:
1. right of capture
2. left of capture
3. below capture
4. above capture
5. clamp into visible screen bounds if none fit perfectly

The target screen is the screen containing the capture center.

### 7.3 Session behavior
- each new screenshot creates a new session
- follow-up text continues within that session
- V1 does not merge multiple screenshots into one conversation automatically

## 8. AI Provider Design
### 8.1 Supported protocols in V1
- OpenAI Chat
- OpenAI Responses
- OpenAI-compatible
- Gemini
- Claude

### 8.2 Unified provider abstraction
Use a common provider interface that cleanly separates:
- text request
- image request
- text connection test
- image understanding test

Suggested logical operations:
- `sendText(...)`
- `sendImage(...)`
- `testText(...)`
- `testImage(...)`

The abstraction should keep protocol differences behind implementation classes.

### 8.3 Single active profile
V1 uses one active profile at a time.

No multi-model fan-out, no compare mode, no in-panel switching.

## 9. Initial Request Policy
After a successful capture:
- create a new session
- attach the captured image to the first request
- automatically send the default prompt
- display the AI result in the floating chat panel

The default prompt should instruct the model to:
- analyze only the captured content
- ignore screenshot-tool decorations
- explicitly say when the selection is wrong / blank / unusable
- focus on concise and useful observations

## 10. Settings Page Design
### 10.1 Required fields
The settings page must clearly separate:
- protocol type
- base URL
- API key
- model

### 10.2 Additional fields
Also include:
- global shortcut
- theme
- opacity

### 10.3 Protocol switching behavior
When protocol type changes:
- fill in the recommended default base URL
- keep the field editable
- allow users to replace it with a relay / proxy / compatible endpoint

### 10.4 Validation tools
#### Test Connection
- sends a minimal text request
- verifies model/network/auth path is working
- shows success or readable error text

#### Test Image Understanding
- sends a bundled sample image
- verifies the image upload / image understanding path is working
- shows success or readable error text

These buttons are part of the real product workflow, not just developer diagnostics.

## 11. Config and Persistence
Store config locally as JSON under a Windows app data directory, for example:
- `%APPDATA%/AiScreenshotTool/config.json`

Persist at least:
- protocol type
- base URL
- API key
- model
- shortcut
- theme
- opacity

V1 stores API keys in plain local config by user choice.

Writes should be atomic to reduce config corruption risk.

## 12. Error Handling
The product should fail visibly but calmly:
- hotkey registration failure -> show tray notification / readable message
- capture cancelled -> restore prior panel when appropriate
- provider request failure -> show readable error in chat panel
- invalid image response -> show response content or parse error instead of crashing
- malformed config -> recover with defaults when reasonable

### 12.1 Async request locking and debounce
Because Qt network requests are asynchronous, UI state must be explicitly locked during in-flight operations.

Required behavior:
- when the floating panel is sending an AI request, mark the session UI as loading
- while loading, disable repeated send actions for that session
- while a settings-page test is running, disable repeated clicks on the same test buttons
- while any critical in-flight request is active, ignore or defer the global hotkey so capture cannot restart into an inconsistent state
- do not allow overlapping request pipelines from accidental double-clicks or repeated Enter presses

Recommended implementation model:
- a small controller-level state machine with explicit states such as `Idle`, `Capturing`, `RequestInFlight`, and `TestingProvider`
- a scoped or centralized busy flag owned by controller/service state, not scattered ad-hoc booleans in multiple widgets
- UI widgets reflect the lock state through disabled buttons, loading text, and predictable recovery on success/failure

Unlock rules:
- always release the loading lock on both success and failure paths
- restore interactive controls before showing the final result/error
- never leave the app stuck in a disabled state after network failure, cancellation, or timeout

The goal is to prevent state-machine corruption during asynchronous provider requests and keep repeated user input harmless.

## 13. Test Strategy
### 13.1 Automated tests
Prioritize tests for:
- config serialization / deserialization
- provider preset mapping
- text payload construction per protocol
- image payload construction per protocol
- placement calculation
- session lifecycle and message ordering

### 13.2 Manual verification
Required manual checks for V1:
- tray starts normally
- shortcut triggers reliably
- repeated capture cycles do not leak stale windows
- Esc cancels correctly
- capture rectangle is accurate on multi-monitor setups
- floating panel stays on screen
- text connection test works
- image understanding test works
- real screenshot follow-up chat works

## 14. Non-Goals
The following are intentionally out of scope for V1:
- OCR-specific understanding pipeline
- selection history management
- annotation / draw tools
- delayed capture
- scroll capture
- multi-model answer comparison
- plugin marketplace or workflow automation

## 15. Success Criteria
V1 is successful when:
- the app can stay in tray and capture repeatedly
- the overlay is simple, stable, and manual-only
- the capture does not include the floating panel itself
- the panel opens near the capture and stays visible on-screen
- the user can continue asking follow-up questions
- every supported protocol can pass both text and image validation in settings
- no business-object ownership relies on scattered manual `new` / `delete`

## 16. Implementation Guidance Summary
Recommended implementation style:
- keep UI widgets Qt-owned when that is the natural model
- keep non-UI services and models smart-pointer-owned
- keep files small and responsibility-focused
- avoid rebuilding features from the old Python version that are now out of scope
- optimize first for stability of the screenshot-to-AI loop

- Implementation status: completed against the native C++/Qt plan on 2026-04-15
