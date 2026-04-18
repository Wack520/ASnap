# Floating Chat Panel Helper Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 抽出 `FloatingChatPanel` 中最重的纯 helper 逻辑，缩小 `floating_chat_panel.cpp` 体积，同时保持现有交互、样式和测试行为不变。

**Architecture:** 新增 `floating_chat_panel_helpers.*` 承接纯函数：文本状态、消息 HTML、主题/颜色解析、样式与文档 CSS、屏幕几何约束。`FloatingChatPanel` 保留 QWidget 生命周期、事件过滤、拖拽缩放状态机和控件刷新调度。现有 public API 与 widget objectName 不变。

**Tech Stack:** C++20, Qt Widgets, Qt Test, CMake

---

### Task 1: 抽出 FloatingChatPanel helper 层

**Files:**
- Create: `native/src/ui/chat/floating_chat_panel_helpers.h`
- Create: `native/src/ui/chat/floating_chat_panel_helpers.cpp`
- Modify: `native/src/ui/chat/floating_chat_panel.cpp`
- Modify: `native/src/ui/chat/floating_chat_panel.h`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/test_ui_widgets.cpp`

- [x] 先补一个小回归测试，覆盖 `applyAppearance()` 后 `panelSurfaceColor` / `panelBorderColor` 属性仍正确存在。
- [x] 新建 helper 文件，迁出 `statusText`、`htmlForMessage`、颜色解析、样式表/文档 CSS、屏幕几何约束等纯函数。
- [x] `floating_chat_panel.cpp` 改为调用 helper，不改变现有 public API 与事件逻辑。
- [x] 更新 `native/CMakeLists.txt`。
- [x] 跑 `test_ui_widgets` 验证回归。

### Task 2: 全量验证与收尾

**Files:**
- Verify only: `native/`

- [x] 跑 `powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -Configuration Release -RunTests -Version floating-chat-panel-helper-split`
- [x] 确认 `7/7 tests passed`
- [x] 若只剩既有 CMake / VCINSTALLDIR 噪音，记录为已知非阻塞项

## Execution Notes
- 2026-04-18：完成 helper 抽离并接回 `FloatingChatPanel`。
- 回归结果：`ctest` 显示 `100% tests passed, 0 tests failed out of 7`。
- 打包脚本尾部仍有既有噪音：
  - `CMake Error: generator platform: x64 does not match the platform used previously`
  - `Warning: Cannot find Visual Studio installation directory, VCINSTALLDIR is not set.`
  已记录为非阻塞（不影响本次编译/测试通过与产物生成）。
