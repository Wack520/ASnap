# SettingsDialog Split Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `SettingsDialog` 里剩余最重的 appearance / preview 控件与交互状态拆成独立子组件，继续缩小对话框主类体积，同时保持现有 UI 行为与测试结果不变。

**Architecture:** 新增一个 `SettingsDialogAppearanceSection`，它自己持有主题、透明度、颜色按钮、预览控件和相关状态，并封装颜色选择、自动颜色恢复、按钮刷新和预览刷新。`SettingsDialog` 退回为外层编排者，只负责 provider / prompt 区、总的 busy/action 协调，以及把 appearance 数据映射到 `AppConfig`。公开给测试和上层调用的现有 `SettingsDialog` API 继续保留，但实现改为转发给新组件，避免行为漂移。

**Tech Stack:** C++20, Qt Widgets, CMake, Qt Test

---

### Task 1: 提取 appearance / preview 子组件

**Files:**
- Create: `native/src/ui/settings/settingsdialog_appearance_section.h`
- Create: `native/src/ui/settings/settingsdialog_appearance_section.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/ui/settings/settingsdialog.h`
- Modify: `native/src/ui/settings/settingsdialog.cpp`

- [ ] **Step 1: 先补一个会锁定 appearance 控件的测试锚点**

在 `native/tests/test_ui_widgets.cpp` 增加一个小测试，验证 `setBusy(true)` 时 theme / opacity / 颜色按钮 / preview 输入按钮仍然全部被禁用，确保拆分后忙态锁定没有漏项。

Run: `ctest -R test_ui_widgets --output-on-failure -C Release`
Expected: 新测试先失败，或在未接线前无法编译通过。

- [ ] **Step 2: 定义新的 appearance section 边界**

在新头文件里声明一个 `QWidget` 子类，例如：

```cpp
class SettingsDialogAppearanceSection final : public QFrame {
    Q_OBJECT
public:
    explicit SettingsDialogAppearanceSection(QWidget* parent = nullptr);

    void loadFromConfig(const ais::config::AppConfig& config);
    void applyAppearance(const QString& theme);
    void setBusy(bool busy);

    [[nodiscard]] QString theme() const;
    [[nodiscard]] double opacity() const;
    [[nodiscard]] QString panelColorText() const;
    [[nodiscard]] QString panelTextColorText() const;
    [[nodiscard]] QString panelBorderColorText() const;

    // existing widget accessors kept for SettingsDialog test forwarding
};
```

要求：把以下职责完整迁入新类：
- theme / opacity / panelColor / panelTextColor / panelBorderColor 控件创建
- preview mock 控件创建
- `choosePanelColor` / `choosePanelTextColor` / `choosePanelBorderColor`
- `restoreAutomaticTextColor` / `restoreAutomaticBorderColor`
- `refreshColorButtons` / `refreshPreview`
- panel color / text color / border color 状态

- [ ] **Step 3: 在 section `.cpp` 里落实现有外观逻辑**

把当前 `SettingsDialog` 中 appearance 相关代码迁过去，继续复用：
- `ui/settings/settingsdialog_appearance_helpers.h`
- 同样的预览 HTML / QSS helper
- 同样的自动颜色计算规则

要求：
- 预览 widget objectName 和属性名（`previewColor` / `previewBorderColor` / `previewOpacity`）保持不变
- 按钮文案和 tooltip 保持不变
- 颜色选择框仍使用 `QColorDialog::DontUseNativeDialog` + alpha channel

- [ ] **Step 4: 让 SettingsDialog 改成编排 / 转发层**

在 `SettingsDialog` 中：
- 用 `SettingsDialogAppearanceSection* appearanceSection_` 替代现有一大串 appearance 成员
- 构造函数里只创建 section 并插入原 appearance card 位置
- `currentConfig()` 改为从 section 取主题 / 透明度 / 颜色值
- 保留 `themeField()` / `opacityField()` / `panelColorButton()` / `previewHistoryView()` 等现有 public accessor，但实现改成转发到 `appearanceSection_`
- `applyAppearance()` / `setPanelColor()` / `setPanelTextColor()` / `setPanelBorderColor()` / `setBusy()` 对 appearance 相关部分改成转发

- [ ] **Step 5: 更新构建入口**

把新文件加入 `native/CMakeLists.txt` 的 `ai_screenshot_core` 源列表。

Run: `cmake --build build/native --config Release --target ai_screenshot test_ui_widgets`
Expected: 编译通过。

- [ ] **Step 6: 提交本任务**

```bash
git add native/CMakeLists.txt native/src/ui/settings/settingsdialog.h native/src/ui/settings/settingsdialog.cpp native/src/ui/settings/settingsdialog_appearance_section.h native/src/ui/settings/settingsdialog_appearance_section.cpp native/tests/test_ui_widgets.cpp
git commit -m "refactor: extract settings dialog appearance section"
```

### Task 2: 回归验证与清理

**Files:**
- Verify only: `native/`
- Modify if needed: `native/tests/test_ui_widgets.cpp`

- [ ] **Step 1: 跑 UI 测试与全量打包测试**

Run: `powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -Configuration Release -RunTests -Version settingsdialog-split-phase2`
Expected: `7/7 tests passed`，并生成新的 zip / sha256 产物。

- [ ] **Step 2: 检查是否有明显行为漂移**

重点人工核对：
- 设置页仍可调主题、透明度、背景色、字体色、边框色
- 预览仍随设置实时变化
- `setBusy(true)` 仍锁住 appearance 控件
- `currentConfig()` 仍保留现有颜色序列化行为（自动色返回空字符串，自定义色返回 hex / argb）

- [ ] **Step 3: 提交验证后的修正**

```bash
git add native/CMakeLists.txt native/src/ui/settings/settingsdialog.h native/src/ui/settings/settingsdialog.cpp native/src/ui/settings/settingsdialog_appearance_section.h native/src/ui/settings/settingsdialog_appearance_section.cpp native/tests/test_ui_widgets.cpp
git commit -m "test: cover settings dialog appearance section split"
```
