# ApplicationController Request Flow Split Design

## Goal
把 `ApplicationController` 里最重、最容易继续膨胀的“会话请求流”从主控制器中拆出去，先降低状态机复杂度，同时保持当前截图、悬浮窗、设置页、Provider 测试等外部行为不变。

## Current Problem
`native/src/app/application_controller.cpp` 目前把以下职责混在一个类里：
- 托盘 / 热键 / 设置页 / 悬浮窗生命周期
- 截图工作流
- 聊天会话状态
- 追问排队
- AI 流式请求启动
- empty response 自动重试
- asset upload 失败后的兼容链路 fallback
- Provider 测试与模型拉取

其中最重、测试覆盖最密、最适合先拆的是 **request flow**：
- `currentSession_`
- `queuedFollowUpTexts_`
- `requestStreamStarter_`
- `emptyRetryDelayOverrideMs_`
- `sendCurrentSessionRequest()`
- `queueFollowUp()`
- `scheduleQueuedFollowUpSend()`
- `handleRequestCompleted()`
- 一部分 for-test 接口

## Options Considered

### A. 先拆 request flow 子控制器（推荐）
新增 `ConversationRequestController`，把会话、排队、重试、fallback、流式请求收口进去；`ApplicationController` 只保留外层编排。

**优点**
- 命中当前最重状态机
- 现有 `test_application_controller.cpp` 已覆盖空响应重试、排队追问、fallback 等关键行为，回归保护最强
- 不需要先碰截图 overlay / tray / settings 生命周期

**缺点**
- 仍需设计与 UI / status 同步的边界
- 第一刀不会让 `ApplicationController` 立刻变小到很极致

### B. 先拆 capture workflow
先把 overlay、虚拟桌面截图、plain screenshot / AI screenshot 分开。

**优点**
- 截图入口职责更清楚

**缺点**
- 这块和屏幕、overlay、panel 定位耦合更深
- 目前测试对截图状态机的覆盖不如 request flow 密

### C. 一次性把 ApplicationController 拆成多控制器
同时拆 request / capture / settings / tray。

**优点**
- 理论收益最大

**缺点**
- 风险最高
- 很容易把“低风险重构”变成“大规模改写”

## Recommended Design
采用 **A**：新增 `ConversationRequestController`，优先收口“聊天请求状态机”。

## New Structure

### New files
- `native/src/app/conversation_request_controller.h`
- `native/src/app/conversation_request_controller.cpp`

### ConversationRequestController responsibilities
负责：
- 持有 `std::shared_ptr<chat::ChatSession>`
- 持有 `QStringList queuedFollowUpTexts_`
- 持有 request starter / empty retry override
- 创建首轮会话
- 处理 follow-up 发送与排队
- 启动流式请求
- 处理 empty response 自动重试
- 处理 asset upload failure -> OpenAI-compatible fallback
- 提供现有 request-flow 相关测试接口

### ApplicationController responsibilities after split
继续负责：
- tray / hotkey / quit
- overlay / capture workflow / clipboard screenshot
- SettingsDialog / FloatingChatPanel 生命周期
- config load / save / applyAppearance / registerHotkeys
- Provider test / fetch models
- 把 UI 同步、status 同步和 request controller 接起来

## Boundary Design
`ConversationRequestController` 不直接拥有 UI 控件；它通过 hooks / callback 与外层同步：
- `bindSession(session)`：让 chat panel 刷新当前会话
- `syncStatus(status)`：让外层调用 `syncBusyUi(status)`
- `statusForGuardState()`：在 request 启动失败或取消时回退到当前 guard 状态文案
- `requestStarter(...)`：真正发送请求仍复用当前 stream starter 签名
- `cancelActiveRequest()`：取消请求时复用现有 `AiClient` 能力

这样可以把 request flow 状态机抽走，但不把 UI 生命周期拖进去。

## State Ownership
- `ApplicationController` 不再直接持有：
  - `currentSession_`
  - `queuedFollowUpTexts_`
  - `requestStreamStarter_`
  - `emptyRetryDelayOverrideMs_`
- 这些迁到 `ConversationRequestController`
- `ApplicationController` 只保留一个：
  - `std::unique_ptr<ConversationRequestController> requestController_`

这也符合项目里“业务对象尽量用现代 C++ 智能指针”的既有要求。

## Testing Strategy
重点保持以下现有测试全部不变且继续通过：
- `queuedFollowUpAutoSendsAfterCurrentReplyCompletes`
- `emptyAssistantResponseAutomaticallyRetriesThreeTimes`
- `imageConversationEmptyResponseWaitsBeforeRetrying`
- `assetUploadFailureFallsBackToOpenAiCompatibleOnce`
- `reasoningOnlyAssistantResponseDoesNotRetry`
- `closingChatPanelCancelsInFlightRequest`

额外补一条 request-controller 边界回归测试：
- 验证在 request flow 拆分后，`followUpRequestedForTest()` 仍会正确进入排队或直接发送

## Non-goals
这次不做：
- capture overlay 子控制器拆分
- tray / settings / provider test lane 拆分
- 跨模块状态总线
- UI 行为改版

## Implemented Notes (2026-04-18)
- 新增 `ConversationRequestController`，并将 request-flow 状态与逻辑迁入：
  - `currentSession_`
  - `queuedFollowUpTexts_`
  - `requestStreamStarter_`
  - `emptyRetryDelayOverrideMs_`
  - request 启动/失败处理
  - empty response 自动重试
  - asset upload fallback
  - request-flow 相关 for-test 查询/注入接口
- `ApplicationController` 改为持有 `std::unique_ptr<ConversationRequestController>`，并通过 hooks 连接：
  - chat panel `bindSession` 与 `scheduleSessionRefresh`
  - chat panel busy 状态更新
  - 全局 busy/status 同步（`syncBusyUi`）
  - active profile 获取、request 启动、request cancel
- 外层编排职责保持在 `ApplicationController`：
  - tray/hotkey/capture/settings/provider-test/chat panel 生命周期
