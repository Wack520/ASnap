# ApplicationController P1 Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `ApplicationController` 拆成总编排器加 4 个协作模块，在不改变现有功能行为的前提下降低耦合和单文件复杂度。

**Architecture:** 保留 `ApplicationController` 作为组合根与共享依赖容器，新增 `ConversationRuntime`、`SettingsCoordinator`、`CaptureCoordinator`、`ShellIntegration`。每个模块通过 hooks 与 controller 通信，不互相直接持有对方 UI 或平台对象。

**Tech Stack:** C++20, Qt6, Windows, CMake, QtTest

---

### Task 1: 建立协作模块骨架与 hooks 契约

**Files:**
- Create: `native/src/app/conversation_runtime.h`
- Create: `native/src/app/conversation_runtime.cpp`
- Create: `native/src/app/settings_coordinator.h`
- Create: `native/src/app/settings_coordinator.cpp`
- Create: `native/src/app/capture_coordinator.h`
- Create: `native/src/app/capture_coordinator.cpp`
- Create: `native/src/app/shell_integration.h`
- Create: `native/src/app/shell_integration.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `native/tests/test_application_controller.cpp`

- [ ] **Step 1: 写最小编译级测试，确保后续能从 controller 引入新模块**

在 `native/tests/test_application_controller.cpp` 追加一个最小 smoke 测试，保证 controller 仍可实例化：

```cpp
void ApplicationControllerTests::controllerStillConstructsAfterCoordinatorSplit() {
    ApplicationController controller;
    QVERIFY(true);
}
```

- [ ] **Step 2: 运行单测确认现有基线仍然可跑**

Run:

```powershell
cmake --build build\native --config Debug --target test_application_controller
ctest --test-dir build\native -C Debug -R test_application_controller --output-on-failure
```

Expected: `test_application_controller` 通过。

- [ ] **Step 3: 创建 4 个模块 header，先只定义最小类型与 hooks**

示例：`native/src/app/conversation_runtime.h`

```cpp
#pragma once

#include <functional>
#include <memory>

#include <QString>
#include <QStringList>

#include "ai/ai_client.h"
#include "app/request_guard.h"
#include "capture/capture_selection.h"
#include "chat/chat_session.h"
#include "config/app_config.h"

namespace ais::ui {
class FloatingChatPanel;
}

namespace ais::app {

class ConversationRuntime final {
public:
    using RequestStreamStarter = std::function<bool(const config::ProviderProfile&,
                                                    const QList<chat::ChatMessage>&,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::DeltaHandler,
                                                    ai::AiClient::CompletionHandler,
                                                    ai::AiClient::FailureHandler,
                                                    int)>;

    struct Hooks {
        std::function<void(const QString&)> syncStatus;
        std::function<void()> refreshChatBinding;
    };
};

}  // namespace ais::app
```

其他 3 个模块先只建空壳类与 hooks struct。

- [ ] **Step 4: 创建对应 cpp 空实现**

每个 cpp 仅包含 header 与空 namespace，保证可编译：

```cpp
#include "app/conversation_runtime.h"

namespace ais::app {}  // namespace ais::app
```

- [ ] **Step 5: 把新文件加入 `native/CMakeLists.txt`**

把 8 个新文件加入 `ai_screenshot_core` 源文件列表。

- [ ] **Step 6: 重新构建验证骨架接入**

Run:

```powershell
cmake --build build\native --config Debug --target ai_screenshot_core test_application_controller
```

Expected: 编译通过。

- [ ] **Step 7: 提交骨架**

```powershell
git add native/CMakeLists.txt native/src/app/conversation_runtime.* native/src/app/settings_coordinator.* native/src/app/capture_coordinator.* native/src/app/shell_integration.* native/tests/test_application_controller.cpp
git commit -m "refactor: add app coordinator scaffolding"
```

### Task 2: 抽出 ConversationRuntime

**Files:**
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/conversation_runtime.h`
- Modify: `native/src/app/conversation_runtime.cpp`
- Create: `native/tests/test_conversation_runtime.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `native/tests/test_application_controller.cpp`

- [ ] **Step 1: 先复制 controller 中的会话行为测试到新测试文件**

在 `native/tests/test_conversation_runtime.cpp` 新建测试类，先迁移以下行为：

- queued follow-up auto send
- empty response retry
- reasoning-only response does not retry
- asset upload fallback

保留与原测试一致的断言，例如：

```cpp
void ConversationRuntimeTests::emptyAssistantResponseAutomaticallyRetriesThreeTimes() {
    // 复制当前 controller 测试里的 request starter、retryAttempts、last assistant text 断言
}
```

- [ ] **Step 2: 把新测试加入 CMake**

在 `native/CMakeLists.txt` 中新增 `test_conversation_runtime`。

- [ ] **Step 3: 运行新测试，确认当前先失败或未编译**

Run:

```powershell
cmake --build build\native --config Debug --target test_conversation_runtime
ctest --test-dir build\native -C Debug -R test_conversation_runtime --output-on-failure
```

Expected: 因 `ConversationRuntime` 尚未实现而失败或编译错误。

- [ ] **Step 4: 给 `ConversationRuntime` 增加状态与接口**

把以下成员迁入：

```cpp
std::shared_ptr<chat::ChatSession> currentSession_;
QStringList queuedFollowUpTexts_;
RequestStreamStarter requestStreamStarter_;
int emptyRetryDelayOverrideMs_ = -1;
```

增加接口：

```cpp
void setRequestStreamStarterForTest(RequestStreamStarter starter);
void setEmptyRetryDelayOverrideForTest(int delayMs);
void beginSessionFromSelection(const capture::CaptureSelection& selection,
                               const config::AppConfig& config,
                               ui::FloatingChatPanel* chatPanel);
void followUpRequested(const QString& text,
                       const config::ProviderProfile& profile,
                       ui::FloatingChatPanel* chatPanel);
void cancelCurrentConversation(ai::AiClient* aiClient,
                               RequestGuard& guard,
                               ui::FloatingChatPanel* chatPanel,
                               bool clearSession = true);
```

- [ ] **Step 5: 先迁移纯逻辑辅助函数**

先把这些静态/纯逻辑搬到 `conversation_runtime.cpp`：

- `messagesContainImage`
- `defaultEmptyRetryDelayMs`
- `isAssetUploadFailure`
- `defaultFirstPrompt`
- `emptyRetryDelayMs`
- `encodePng`

- [ ] **Step 6: 再迁移请求主流程**

把 `sendCurrentSessionRequest`、`queueFollowUp`、`scheduleQueuedFollowUpSend`、`handleRequestCompleted` 的主体迁到 `ConversationRuntime`，让 `ApplicationController` 只转发调用。

- [ ] **Step 7: 迁移 controller 测试辅助接口**

将这些 test helpers 改为代理 `ConversationRuntime`：

```cpp
controller.setRequestStreamStarterForTest(...)
controller.setEmptyRetryDelayOverrideForTest(...)
controller.seedConversationForTest(...)
controller.followUpRequestedForTest(...)
controller.queuedFollowUpCountForTest()
controller.lastAssistantMessageTextForTest()
```

- [ ] **Step 8: 运行新旧测试**

Run:

```powershell
cmake --build build\native --config Debug --target test_conversation_runtime test_application_controller
ctest --test-dir build\native -C Debug -R "test_conversation_runtime|test_application_controller" --output-on-failure
```

Expected: 两组测试都通过。

- [ ] **Step 9: 提交 ConversationRuntime**

```powershell
git add native/src/app/application_controller.* native/src/app/conversation_runtime.* native/tests/test_conversation_runtime.cpp native/tests/test_application_controller.cpp native/CMakeLists.txt
git commit -m "refactor: extract conversation runtime from application controller"
```

### Task 3: 抽出 SettingsCoordinator

**Files:**
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/settings_coordinator.h`
- Modify: `native/src/app/settings_coordinator.cpp`
- Create: `native/tests/test_settings_coordinator.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `native/tests/test_application_controller.cpp`

- [ ] **Step 1: 先复制设置相关 controller 测试**

从 `test_application_controller.cpp` 迁移或复制：

- `providerTestCompletionRefreshesStatusAfterSettingsDialogCloses`

到 `test_settings_coordinator.cpp`。

- [ ] **Step 2: 新增 SettingsCoordinator 最小接口**

```cpp
class SettingsCoordinator final {
public:
    struct Hooks {
        std::function<void(const QString&)> syncStatus;
        std::function<void(const config::AppConfig&)> onConfigApplied;
        std::function<void()> applyAppearance;
        std::function<void()> rememberWindowSizes;
        std::function<bool()> saveConfigSnapshot;
        std::function<bool()> refreshHotkeys;
        std::function<bool()> applyLaunchAtLogin;
    };
};
```

- [ ] **Step 3: 迁移 settings dialog 生命周期**

先迁移：

- `ensureSettingsDialog`
- `openSettings`
- `onSettingsDialogFinished`

保持 `ApplicationController` 仅保留 slot 转发。

- [ ] **Step 4: 再迁移 provider test / fetch model 流程**

把以下方法主体迁入：

- `runProviderTest`
- `fetchProviderModels`
- `handleProviderTestSuccess`
- `handleProviderTestFailure`

- [ ] **Step 5: 最后迁移 `applySettingsFromDialog`**

要求：

- 依旧从 dialog 读取 `currentConfig()`
- 依旧调用 `applyConfigDefaults()`
- 依旧触发 hotkey / launch-at-login / appearance 更新
- 依旧保留现有状态文本

- [ ] **Step 6: 跑测试**

Run:

```powershell
cmake --build build\native --config Debug --target test_settings_coordinator test_application_controller test_ui_widgets
ctest --test-dir build\native -C Debug -R "test_settings_coordinator|test_application_controller|test_ui_widgets" --output-on-failure
```

Expected: settings 相关测试通过。

- [ ] **Step 7: 提交 SettingsCoordinator**

```powershell
git add native/src/app/application_controller.* native/src/app/settings_coordinator.* native/tests/test_settings_coordinator.cpp native/tests/test_application_controller.cpp native/CMakeLists.txt
git commit -m "refactor: extract settings coordinator from application controller"
```

### Task 4: 抽出 CaptureCoordinator

**Files:**
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/capture_coordinator.h`
- Modify: `native/src/app/capture_coordinator.cpp`
- Create: `native/tests/test_capture_coordinator.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `native/tests/test_application_controller.cpp`

- [ ] **Step 1: 新建 capture 协调器测试**

覆盖：

- idle 时可启动截图
- request in flight 时会打断旧对话
- provider test busy 时忽略截图
- plain capture 流程复制到剪贴板

- [ ] **Step 2: 给 CaptureCoordinator 增加 hooks**

```cpp
struct Hooks {
    std::function<capture::DesktopSnapshot()> captureDesktop;
    std::function<void(const capture::CaptureSelection&)> onAiSelection;
    std::function<void(const capture::CaptureSelection&)> onPlainSelection;
    std::function<void()> onCancelled;
    std::function<void(const QString&)> syncStatus;
};
```

- [ ] **Step 3: 迁移 `rebuildCaptureWorkflowController`**

让协调器自己持有：

```cpp
std::unique_ptr<CaptureWorkflowController> workflowController_;
```

- [ ] **Step 4: 迁移 `startCapture` / `startPlainCapture` 的 workflow 逻辑**

保留 controller 只处理：

- provider-test busy 短路
- request in flight 时 cancel 对话

其余截图执行交给协调器。

- [ ] **Step 5: 跑测试**

Run:

```powershell
cmake --build build\native --config Debug --target test_capture_coordinator test_application_controller test_capture_flow
ctest --test-dir build\native -C Debug -R "test_capture_coordinator|test_application_controller|test_capture_flow" --output-on-failure
```

Expected: capture 相关测试通过。

- [ ] **Step 6: 提交 CaptureCoordinator**

```powershell
git add native/src/app/application_controller.* native/src/app/capture_coordinator.* native/tests/test_capture_coordinator.cpp native/tests/test_application_controller.cpp native/CMakeLists.txt
git commit -m "refactor: extract capture coordinator from application controller"
```

### Task 5: 抽出 ShellIntegration

**Files:**
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/shell_integration.h`
- Modify: `native/src/app/shell_integration.cpp`
- Create: `native/tests/test_shell_integration.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `native/tests/test_application_controller.cpp`

- [ ] **Step 1: 新建 shell integration 测试**

覆盖：

- hotkey 注册刷新
- provider busy 时截图快捷键是否仍保持原行为
- quit 时资源释放

- [ ] **Step 2: 增加壳层 hooks**

```cpp
struct Hooks {
    std::function<void()> onAiCaptureRequested;
    std::function<void()> onPlainCaptureRequested;
    std::function<void()> onSettingsRequested;
    std::function<void()> onQuitRequested;
};
```

- [ ] **Step 3: 迁移 tray/menu/hotkey 创建与刷新**

把以下迁入：

- `createTray`
- `registerHotkeys`
- `applyLaunchAtLoginPreference`

- [ ] **Step 4: 迁移退出逻辑**

把 `quitRequested` 迁入，controller 仅保留最终 `QCoreApplication::quit()` 或经 hooks 调用。

- [ ] **Step 5: 跑测试**

Run:

```powershell
cmake --build build\native --config Debug --target test_shell_integration test_application_controller
ctest --test-dir build\native -C Debug -R "test_shell_integration|test_application_controller" --output-on-failure
```

Expected: shell 相关测试通过。

- [ ] **Step 6: 提交 ShellIntegration**

```powershell
git add native/src/app/application_controller.* native/src/app/shell_integration.* native/tests/test_shell_integration.cpp native/tests/test_application_controller.cpp native/CMakeLists.txt
git commit -m "refactor: extract shell integration from application controller"
```

### Task 6: 收口、减薄 controller、做最终验证

**Files:**
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/src/app/application_controller.h`
- Modify: `native/tests/test_application_controller.cpp`
- Test: `native/tests/test_conversation_runtime.cpp`
- Test: `native/tests/test_settings_coordinator.cpp`
- Test: `native/tests/test_capture_coordinator.cpp`
- Test: `native/tests/test_shell_integration.cpp`

- [ ] **Step 1: 删除 controller 中已迁走的重复实现**

最终 `ApplicationController` 只保留：

- 初始化
- 共享依赖创建
- config 默认值/外观应用
- 状态文本同步
- 对 4 个协作器的 glue code

- [ ] **Step 2: 压缩 controller 测试为集成 smoke tests**

保留少量跨模块集成场景：

- idle/request/provider-test 三种 busy 行为
- plain capture 到剪贴板
- settings 保存后状态刷新

- [ ] **Step 3: 跑全量原生测试**

Run:

```powershell
cmake --build build\native --config Debug
ctest --test-dir build\native -C Debug --output-on-failure
```

Expected: 全部测试通过。

- [ ] **Step 4: 记录 controller 行数与结果**

Run:

```powershell
(Get-Content native\src\app\application_controller.cpp | Measure-Object -Line).Lines
```

Expected: 明显低于当前 ~983 行，目标 300~450 行区间。

- [ ] **Step 5: 提交最终收口**

```powershell
git add native/src/app/application_controller.* native/tests/test_application_controller.cpp
git commit -m "refactor: slim application controller into coordinators"
```

