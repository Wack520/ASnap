# Text Query Shortcut Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Windows-only text-query shortcut that captures selected text, restores the clipboard, opens the chat panel, and auto-sends a concise explanation request.

**Architecture:** Add a new persisted shortcut in config/UI, wire a third global hotkey in `ApplicationController`, and introduce a Windows selected-text reader that simulates copy and restores clipboard state. Keep the feature off the screenshot pipeline by starting a text-only chat session.

**Tech Stack:** Qt 6 Widgets/Core/Gui, Win32 hotkeys/input/clipboard integration, existing `ApplicationController` + `ConversationRequestController` flow, QtTest.

---

### Task 1: Cover config and controller behavior with tests

**Files:**
- Modify: `native/tests/test_config_and_session.cpp`
- Modify: `native/tests/test_ui_widgets.cpp`
- Modify: `native/tests/test_application_controller.cpp`

- [ ] Add failing tests for persisted `textQueryShortcut`, settings dialog editing, and text-query success/failure flows.
- [ ] Run the affected tests and confirm they fail for the missing feature.

### Task 2: Add config and settings support

**Files:**
- Modify: `native/src/config/app_config.h`
- Modify: `native/src/config/config_store.cpp`
- Modify: `native/src/ui/settings/settingsdialog.h`
- Modify: `native/src/ui/settings/settingsdialog.cpp`

- [ ] Add `textQueryShortcut` to config and persistence.
- [ ] Update defaults/migration so text query gets `Ctrl+Shift+A` and AI capture falls back to `Ctrl+Shift+Q` when needed.
- [ ] Add a third shortcut editor in settings.

### Task 3: Implement Windows selected-text query flow

**Files:**
- Create: `native/src/platform/windows/selected_text_query.h`
- Create: `native/src/platform/windows/selected_text_query.cpp`
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/conversation_request_controller.h`
- Modify: `native/src/app/conversation_request_controller.cpp`
- Modify: `native/src/native/CMakeLists.txt`

- [ ] Add a Windows helper that saves clipboard content, sends Ctrl+C to the foreground app, reads text, and restores clipboard.
- [ ] Add a text-only session start path.
- [ ] Register a third hotkey and route it into auto-send text query behavior.

### Task 4: Verify end to end

**Files:**
- None

- [ ] Run targeted Debug tests.
- [ ] Run full Debug test suite.
- [ ] If green, optionally build Release package for manual verification.
