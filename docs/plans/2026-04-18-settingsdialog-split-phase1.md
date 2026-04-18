# SettingsDialog Split Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从 `SettingsDialog` 中抽出低耦合的外观样式与颜色预览辅助逻辑，先把最安全的一层拆走，降低文件复杂度而不改变 UI 行为。

**Architecture:** 这一刀只抽纯函数，不碰控件所有权、不碰 Windows 标题栏、不改信号槽行为。新的 helper 文件负责主题判断、颜色计算、QSS/HTML 生成；`SettingsDialog` 保留状态和控件更新调用。这样能在零行为变更的前提下把大段样式逻辑从巨型 `.cpp` 中剥离出去。

**Tech Stack:** C++20, Qt Widgets, CMake, Qt Test

---

### Task 1: 抽出 SettingsDialog 外观 helper

**Files:**
- Create: `native/src/ui/settings/settingsdialog_appearance_helpers.h`
- Create: `native/src/ui/settings/settingsdialog_appearance_helpers.cpp`
- Modify: `native/src/ui/settings/settingsdialog.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] 将主题/颜色/样式/预览 HTML 纯函数从 `settingsdialog.cpp` 挪到新 helper 文件
- [ ] 保持函数签名清晰，只暴露 `SettingsDialog` 真实调用需要的 helper
- [ ] `settingsdialog.cpp` 改为通过 helper 调用，不改变现有行为
- [ ] 不改 Windows 标题栏逻辑与控件创建逻辑

### Task 2: 回归验证

**Files:**
- Verify only: `native/`

- [ ] 运行 `.\scripts\package-windows.ps1 -Configuration Release -RunTests -Version settingsdialog-split-phase1`
- [ ] 确认 Release 构建成功且 `7/7` 测试通过
- [ ] 若只有既有 CMake 平台噪音，记录为已知问题，不算本批失败
