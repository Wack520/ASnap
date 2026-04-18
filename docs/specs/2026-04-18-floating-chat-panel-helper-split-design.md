# Floating Chat Panel Helper Split Design

## Goal
在不改变当前聊天悬浮窗 UI 行为的前提下，把 `floating_chat_panel.cpp` 中最重的纯 helper 逻辑抽离出去，先降低文件复杂度，再为后续继续拆分 `FloatingChatPanel` 本体打基础。

## Current Problem
`native/src/ui/chat/floating_chat_panel.cpp` 目前同时承载了：
- 窗口交互（拖动、左右缩放、命中测试）
- 会话绑定与刷新
- Markdown / HTML 拼接
- 样式表与文档 CSS 生成
- 主题 / 颜色解析
- 屏幕边界与宽度限制计算

其中最适合先拆的是“纯 helper 层”：
- `statusText`
- `htmlForMessage`
- 主题/颜色解析函数
- 样式表与文档 CSS 生成
- 面板几何约束函数

这些函数与控件生命周期无关，行为边界清晰，适合低风险抽离。

## Options Considered

### A. 抽出专用 `floating_chat_panel_helpers.*`（推荐）
新增专用 helper 文件，把 `FloatingChatPanel` 依赖的纯函数迁过去；`FloatingChatPanel` 保留状态和交互逻辑。

**优点**
- 改动集中，风险最低
- 可以明显缩短 `floating_chat_panel.cpp`
- 不强行改变现有 public API
- 为下一刀拆 composer / session rendering / interaction state 留出空间

**缺点**
- 与 `SettingsDialog` 之间仍会保留一部分“同类 helper 分散在不同文件”的现象
- 只是第一步，还没把 `FloatingChatPanel` 本体拆成多个子组件

### B. 现在就做跨 UI 的共享 appearance core
直接把 `FloatingChatPanel` 与 `SettingsDialog` 共用的主题/颜色 helper 合并到新的 shared helper 模块。

**优点**
- 能更彻底去重
- 后续两个 UI 的主题规则更统一

**缺点**
- 牵涉 `SettingsDialog` 已稳定代码，回归面更大
- 这一步容易从“纯抽离”升级成“共享接口设计”，风险更高

### C. 直接拆 `FloatingChatPanel` 为多组件
一次性拆成 window chrome / renderer / composer / interaction controller 等多个类。

**优点**
- 理论收益最大

**缺点**
- 超过当前这轮“低风险第三刀”的范围
- 容易碰到状态同步、事件过滤和测试回归

## Recommended Design
采用 **A**：新增 `floating_chat_panel_helpers.h/.cpp`，把 `floating_chat_panel.cpp` 中无副作用、无 QObject 生命周期依赖的纯 helper 迁走。

### New helper responsibilities
新 helper 文件负责：
- `statusText`
- `htmlForMessage`
- `resolveSurfaceColor`
- `resolveTextColor`
- `resolveBorderColor`
- `styleSheetForTheme`
- `historyDocumentCss`
- `availableScreenGeometryForRect`
- `clampGeometryToScreen`
- `maximumPanelWidthForRect`

### FloatingChatPanel keeps responsibility for
`FloatingChatPanel` 自己继续负责：
- QWidget 生命周期
- 鼠标/键盘事件处理
- 拖动与缩放状态机
- session 绑定与 refresh 调度
- 组装 helper 输出并驱动控件更新

## Boundary Rules
1. 新 helper 不持有 QWidget / QObject 状态。
2. 新 helper 只接受值类型、Qt 基础类型或 `ChatMessage`/渲染输入。
3. `FloatingChatPanel` 的 public API、测试入口、widget objectName 不变。
4. 现有测试必须继续通过，不新增视觉漂移。

## Error Handling / Regression Guard
- 对外行为保持不变：`applyAppearance()`、`refreshHistory()`、`refreshReasoning()`、拖动/缩放逻辑不改语义。
- 加一条针对外观 helper 抽离后的回归测试，确保 `applyAppearance()` 后面板 surface/border 属性仍正确写回。
- 全量回归依旧用 `scripts/package-windows.ps1 -RunTests`。

## Testing Strategy
- 现有 `test_ui_widgets` 中关于：
  - appearance
  - reasoning 区
  - 输入区
  - streaming badge
  - 链接打开
  - 拖动 / 缩放
  的测试都作为回归网。
- 额外补一个面向 helper 抽离后的属性回归测试，避免未来 helper 迁移时丢掉 `panelSurfaceColor` / `panelBorderColor`。

## Non-goals
这次不做：
- `FloatingChatPanel` 多组件拆分
- `ApplicationController` 状态机治理
- Settings / Chat 跨模块 shared theme core 合并
- Markdown renderer 重写

## Implementation Snapshot (2026-04-18)
- 已新增 `native/src/ui/chat/floating_chat_panel_helpers.h/.cpp`，承接：
  - `statusText`、`htmlForMessage`
  - 主题/颜色解析（surface/text/border）
  - `styleSheetForTheme` 与 `historyDocumentCss`
  - 屏幕几何约束（`availableScreenGeometryForRect` / `clampGeometryToScreen` / `maximumPanelWidthForRect`）
- `FloatingChatPanel` 本体保留 QWidget 生命周期、eventFilter、拖拽/缩放、session 绑定与 refresh 调度。
- `native/tests/test_ui_widgets.cpp` 增补回归 `floatingPanelAppearanceKeepsSurfaceAndBorderProperties`，覆盖 `applyAppearance()` 后 `panelSurfaceColor` / `panelBorderColor` 属性保持可用。
