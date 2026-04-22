# ApplicationController Request Flow Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `ApplicationController` 中的聊天请求状态机抽到独立 `ConversationRequestController`，减少主控制器职责混杂，同时保持现有测试与交互行为不变。

**Architecture:** 新增 `conversation_request_controller.*`，承接会话状态、排队追问、请求启动、empty response 自动重试、asset upload fallback 和相关测试接口。`ApplicationController` 退回成外层编排者，通过 callback/hook 与 request controller 同步 chat panel 和 busy status。

**Tech Stack:** C++20, Qt Widgets, Qt Test, CMake

---

### Task 1: 提取 request flow 控制器

**Files:**
- Create: `native/src/app/conversation_request_controller.h`
- Create: `native/src/app/conversation_request_controller.cpp`
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/tests/test_application_controller.cpp`

- [x] 新建 `ConversationRequestController`，迁入：session、queued follow-up、request starter、retry override 及 request-flow 方法。
- [x] `ApplicationController` 改为持有 `std::unique_ptr<ConversationRequestController>`。
- [x] 保持现有 for-test 接口语义不变，必要时改为转发到 request controller。
- [x] 通过 callback/hook 保留 chat panel bind / status sync / ai cancel / request start 行为。
- [x] 新增一条 request-flow 拆分后的回归测试（`closingChatPanelClearsQueuedFollowUpsAfterRequestFlowSplit`）。

### Task 2: 回归验证与收尾

**Files:**
- Verify only: `native/`

- [x] 跑 `powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -Configuration Release -RunTests -Version application-controller-request-flow-split`
- [x] 确认全部测试通过（当前为 `7/7 tests passed`）
- [x] 记录已有非阻塞噪音：打包末尾仍出现既有 `generator platform: x64` / `VCINSTALLDIR is not set` 提示（命令退出码仍为 0，产物已生成）
