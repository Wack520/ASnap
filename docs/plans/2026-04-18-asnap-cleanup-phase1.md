# ASnap Cleanup Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 清理已经确认的低风险历史残留，降低 header 噪音和无效 UI 链路，给后续继续拆 `ApplicationController` / `SettingsDialog` 腾出空间。

**Architecture:** 这一批不做大拆，只做两类收口：一是把旧默认提示词迁移逻辑限制在配置加载层，避免污染全局配置头文件；二是删除已经不再生效的聊天面板预览图与“重新截图”死链路，并同步收紧测试。这样能在不改主流程行为的前提下减掉一批无用接口。

**Tech Stack:** C++20, Qt Widgets, CMake, Qt Test

---

### Task 1: 默认提示词迁移逻辑收口到配置层

**Files:**
- Modify: `native/src/config/app_config.h`
- Modify: `native/src/config/config_store.cpp`
- Modify: `native/src/app/application_controller.cpp`
- Test: `native/tests/test_config_and_session.cpp`

- [ ] 保留当前默认提示词定义，只让 `app_config.h` 公开当前默认值
- [ ] 将旧默认提示词识别逻辑移入 `config_store.cpp` 内部匿名命名空间
- [ ] 移除 `ApplicationController` 对旧默认提示词识别函数的依赖，只保留空值兜底
- [ ] 保留并验证“老配置自动迁移到新默认提示词”的测试

### Task 2: 删除聊天面板中的失效预览图与重新截图死链路

**Files:**
- Modify: `native/src/ui/chat/floating_chat_panel.h`
- Modify: `native/src/ui/chat/floating_chat_panel.cpp`
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Test: `native/tests/test_ui_widgets.cpp`

- [ ] 删除不再显示的预览图成员、setter 和相关测试断言
- [ ] 删除未实际触发的 `recaptureRequested` signal 与对应 controller 槽/连接
- [ ] 保留仍在使用的链接测试辅助接口，不误删真实测试入口
- [ ] 确认聊天面板布局、发送、链接打开行为测试仍然通过

### Task 3: 构建与测试验证

**Files:**
- Verify only: `native/`

- [ ] 运行 `.\scripts\package-windows.ps1 -Configuration Release -RunTests -Version cleanup-phase1`
- [ ] 确认 Release 构建成功且 `7/7` 测试通过
- [ ] 记录脚本末尾现有的 CMake 平台警告属于已知噪音，不把它误判为本批失败
