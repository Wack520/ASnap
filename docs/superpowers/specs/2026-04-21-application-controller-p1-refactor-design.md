# ApplicationController P1 Refactor Design

## Goal

在**不改变现有功能行为**的前提下，把 `native/src/app/application_controller.cpp` 从“巨型总控文件”重构为“总编排器 + 4 个职责清晰的协作模块”，为后续继续拆 AI、Windows 截图后端、聊天窗体和设置页打基础。

## Why Now

当前 `ApplicationController` 已同时承担：

- tray / hotkey / 开机自启动
- capture workflow 生命周期
- settings dialog 生命周期
- provider 测试与模型获取
- AI 会话启动、继续追问、自动重试
- chat panel 生命周期与状态同步

文件已经接近 1000 行，后续每次改动都会增加：

- 回归范围
- QObject 生命周期复杂度
- 测试定位难度
- 空响应重试 / 截图打断 / 设置保存等逻辑互相污染的风险

## Scope

### In Scope

- 拆分 `ApplicationController` 的职责
- 新增 4 个 app 层协作模块
- 保持现有 UI、Provider、Capture 行为不变
- 把现有测试按职责迁移到更小的测试文件

### Out of Scope

- 不重构 provider 协议实现
- 不重构 WGC/GDI 后端
- 不新增功能
- 不改动聊天面板和设置页 UI 结构
- 不在本轮补 manifest / DPI 基础设施

## Chosen Approach

采用**渐进式抽协作器**方案：

- `ApplicationController` 保留为总入口与依赖容器
- 把超载职责拆到 4 个协作类
- 通过 hooks / callbacks 通信
- 这一轮优先迁出“行为”和“状态”，不强行重构所有 Qt ownership

这是风险最低、最适合当前稳定项目状态的方案。

## Target Architecture

### 1. ApplicationController

保留职责：

- 初始化顺序
- 全局依赖持有
- 配置读取与默认值应用
- 外观应用
- 状态文字同步
- 作为 4 个协作器的组合根

### 2. ConversationRuntime

负责 AI 会话运行时：

- 从截图开始新会话
- 继续追问
- 空响应自动重试
- reasoning / text streaming 回调
- 队列追问
- 取消当前对话

迁出的主要方法：

- `beginSessionFromSelection`
- `sendCurrentSessionRequest`
- `queueFollowUp`
- `scheduleQueuedFollowUpSend`
- `handleRequestCompleted`
- `cancelCurrentConversation`
- `defaultFirstPrompt`
- `emptyRetryDelayMs`
- `encodePng`

### 3. SettingsCoordinator

负责设置窗口与 provider test：

- 创建/展示 settings dialog
- 保存设置
- 测试文字连接
- 测试图片理解
- 获取模型列表
- 处理设置页 busy 状态

迁出的主要方法：

- `ensureSettingsDialog`
- `openSettings`
- `runProviderTest`
- `fetchProviderModels`
- `handleProviderTestSuccess`
- `handleProviderTestFailure`
- `applySettingsFromDialog`
- 对应 settings 的 slot

### 4. CaptureCoordinator

负责截图工作流：

- 启动 AI 截图
- 启动普通截图
- 重建/持有 `CaptureWorkflowController`
- 处理截图确认/取消

迁出的主要方法：

- `rebuildCaptureWorkflowController`
- `startCapture` 的截图部分
- `startPlainCapture` 的截图部分

### 5. ShellIntegration

负责壳层集成：

- tray icon
- tray menu
- hotkey 注册/刷新
- 开机启动配置
- 退出逻辑

迁出的主要方法：

- `createTray`
- `registerHotkeys`
- `applyLaunchAtLoginPreference`
- `quitRequested`

## Communication Pattern

4 个协作模块之间不直接互相持有 UI 或平台对象，而是通过 hooks 与 `ApplicationController` 交互。

例如：

- `CaptureCoordinator` 只发出 “capture confirmed / cancelled”
- `ConversationRuntime` 只发出 “session changed / status changed / request started / request finished”
- `SettingsCoordinator` 只发出 “config changed / provider test result / model list fetched”
- `ShellIntegration` 只发出 “AI hotkey triggered / plain hotkey triggered / settings clicked / quit clicked”

## State Ownership

### ApplicationController 保留

- `config_`
- `guard_`
- `configStore_`
- `captureService_`
- `aiClient_`
- `providerTestRunner_`
- `startupRegistry_`
- `chatPanel_`

### ConversationRuntime 持有

- `currentSession_`
- `queuedFollowUpTexts_`
- `requestStreamStarter_`
- `emptyRetryDelayOverrideMs_`

### SettingsCoordinator 持有

- `settingsDialog_`

### CaptureCoordinator 持有

- `captureWorkflowController_`

### ShellIntegration 持有

- `trayIcon_`
- `trayMenu_`
- `captureAction_`
- `screenshotAction_`
- `settingsAction_`
- `quitAction_`
- `aiHotkeyHost_`
- `screenshotHotkeyHost_`

## Testing Strategy

现有 `test_application_controller.cpp` 先保留为集成测试，再逐步把职责拆到：

- `test_conversation_runtime.cpp`
- `test_settings_coordinator.cpp`
- `test_capture_coordinator.cpp`
- `test_shell_integration.cpp`

目标不是马上减少总测试量，而是让每类行为都有更明确的失败定位。

## Risks

### 1. QObject 生命周期错乱

缓解：

- 协作器优先持有逻辑状态与 workflow 对象
- UI ownership 仍通过当前父对象体系维护
- 拆分时不同时改 UI 父子关系和行为逻辑

### 2. 回调链变复杂

缓解：

- 所有 hooks 统一在 header 中定义成小型结构体
- `ApplicationController` 负责把 hooks 接起来

### 3. 测试大量失效

缓解：

- 先复制现有测试覆盖到新测试文件
- 再迁移实现
- 保留少量 controller 级 smoke test

## Success Criteria

- `application_controller.cpp` 降到约 300~450 行
- 现有行为不变
- Debug 构建通过
- 原有测试通过
- 新增 4 个更聚焦的模块测试

