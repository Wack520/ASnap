# AI Screenshot Tool Rewrite Design

**Date:** 2026-04-15
**Scope:** Rewrite the screenshot subsystem while keeping the provider/config/chat foundations that already work.

## 1. Goal
Build a stable Windows desktop AI screenshot tool that freezes the full virtual desktop on hotkey, lets the user drag any rectangle reliably across multi-monitor setups, and then opens a topmost floating AI chat panel tied to that capture.

## 2. Keep vs Rewrite
### Keep
- `ai_screenshot/providers.py`: provider abstraction and API transport
- `ai_screenshot/config.py`: config persistence
- `ai_screenshot/provider_presets.py`: protocol presets
- `ai_screenshot/ui_chat.py`: floating chat panel UI and settings UI
- `ai_screenshot/placement.py`: floating panel placement
- `ai_screenshot/session.py`: conversation state

### Rewrite
- `ai_screenshot/hotkeys.py`: replace current fragile registration flow with a minimal, dedicated global-hotkey host
- `ai_screenshot/app.py`: simplify orchestration so only one overlay/panel lifecycle exists at a time
- `ai_screenshot/ui_capture.py`: replace with a clean manual-drag overlay only
- `ai_screenshot/screen_service.py`: reduce responsibility to virtual desktop capture, geometry translation, and screen lookup only

## 3. User Experience
1. App stays in tray.
2. User presses global hotkey or tray action.
3. Existing floating panel/overlay is fully hidden and event queue flushed.
4. App captures the entire virtual desktop once.
5. Full-screen frozen overlay appears on all monitors as one virtual canvas.
6. User presses mouse, drags any rectangle, releases to confirm.
7. `Esc` cancels. Tiny accidental clicks do not exit capture mode.
8. Captured image is sent directly to AI with no second crop step.
9. Floating AI panel appears near the capture but remains inside the target screen.
10. User can continue asking follow-up questions about that image.

## 4. Screenshot Subsystem Design
### 4.1 Capture runtime
A dedicated capture runtime owns:
- virtual desktop geometry refresh
- one-shot full-desktop snapshot
- conversion between global coordinates and local overlay coordinates

It does **not** try to infer candidate windows or smart regions.

### 4.2 Overlay
The overlay is a single-responsibility widget:
- shows the frozen screenshot
- captures mouse/keyboard explicitly on show
- tracks `press -> drag -> release`
- draws only the current rectangle and dimension tag
- emits either `confirmed(rect, pixmap)` or `cancelled`

It must not include automatic region detection, wheel behavior, hierarchy switching, or double-click special cases.

### 4.3 Multi-monitor behavior
The overlay geometry is always the full virtual desktop bounds. All drag coordinates are interpreted in that same space. Final capture anchors are translated back into virtual-screen coordinates before placement logic runs.

## 5. AI Request Flow
### 5.1 Provider behavior
Providers stay unified behind the existing abstraction. Improvements in rewrite:
- add explicit request self-test hooks later if needed
- keep proxy fallback and UTF-8 event-stream handling
- keep both Responses and Chat paths available

### 5.2 Default prompt
Default prompt must explicitly tell the model:
- analyze only the user-selected screenshot content
- ignore screenshot-tool decorations
- if the selection is obviously wrong/empty, say so directly

### 5.3 Model expectation
The default active profile should use a model proven to work with image understanding in the user’s proxy environment; current evidence favors `gpt-5.2` for Responses.

## 6. Floating Panel Design
- Remains topmost and lightweight
- Supports dark/light theme and opacity
- Follows placement rules already implemented
- Opens only after successful capture
- Hidden before a new capture begins
- Never allowed to contaminate the next screenshot

## 7. Error Handling
- If hotkey registration fails, tray shows a clear message
- If capture is cancelled, previous floating panel may reappear
- If the provider returns text-only/empty/invalid image response, show that plainly in chat instead of crashing
- If connection fails because a dead local proxy is configured, transport falls back to direct connection automatically

## 8. Testing Strategy
### Automated
- hotkey parsing and registration smoke checks
- capture overlay drag-selection tests
- provider text/image request parsing tests
- placement tests
- config persistence tests

### Manual
- hotkey capture on primary monitor
- hotkey capture across secondary monitor
- tray capture entry
- cancel with `Esc`
- repeated capture cycles without stale overlay artifacts
- Responses image analysis with current proxy config

## 9. Non-Goals for Rewrite V1
- automatic smart-region detection
- DOM-like hover recognition
- OCR-specific workflows
- annotation tools
- delayed capture
- scrolling capture

## 10. File Plan
- Modify: `ai_screenshot/app.py`
- Modify: `ai_screenshot/hotkeys.py`
- Modify: `ai_screenshot/screen_service.py`
- Replace: `ai_screenshot/ui_capture.py`
- Adjust tests: `tests/test_capture_overlay.py`, `tests/test_hotkeys.py`, plus targeted provider regression tests if needed

## 11. Risks and Mitigations
- **Qt focus/input quirks on Windows:** use explicit mouse/keyboard grab and keep overlay logic minimal.
- **Multi-monitor coordinate drift:** always compute from virtual desktop bounds, not per-screen logical coordinates.
- **Old UI leaking into screenshot:** hide/close panel first and flush UI events before snapshot.
- **Provider instability:** preserve currently working request path and verify with image test before calling rewrite done.

## 12. Success Criteria
The rewrite is successful when:
- capture can be started repeatedly from hotkey and tray
- the user can freely drag any region without the overlay fighting input
- no auto-detection behavior remains
- the floating panel does not appear in the captured image
- the default configured provider can successfully analyze a test image and the user’s real screenshots
