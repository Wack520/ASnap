# ASnap

一个面向 Windows 桌面的 AI 截图助手，使用 **C++20 + Qt Widgets + CMake** 构建。

[![windows-ci](https://github.com/Wack520/ASnap/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/Wack520/ASnap/actions/workflows/windows-ci.yml)
[![windows-release](https://github.com/Wack520/ASnap/actions/workflows/windows-release.yml/badge.svg)](https://github.com/Wack520/ASnap/actions/workflows/windows-release.yml)

它的目标很简单：按下全局快捷键后冻结当前屏幕，用户自由框选任意区域，松开即完成截图，然后在截图附近弹出一个始终置顶的悬浮聊天窗，基于这张截图继续和多个大模型进行多轮对话。

> 当前公开仓库只保留原生桌面主线实现：
>
> - `native/`：C++ / Qt 原生版本，推荐使用

---

## 功能概览

### 截图
- 托盘常驻
- 全局快捷键
- 双快捷键模式：
  - AI 截图
  - 普通截图
- 全屏冻结后自由框选
- `Esc` 取消
- 不框选时支持双击直接全屏截图

### AI 对话
- 统一 Provider 抽象
- 当前支持：
  - OpenAI Chat Completions
  - OpenAI Responses
  - OpenAI-compatible
  - Gemini
  - Claude
- 文本请求测试
- 图片理解测试
- 基于截图继续追问
- 回复中支持排队发送下一条追问
- 空响应自动重试

### 悬浮聊天窗
- 永远置顶
- 跟随截图位置
- 不出屏幕边界
- 深色 / 浅色主题
- 可调面板背景色、字体色、边框色、透明度
- 可拖动、可调整宽度
- Markdown 渲染增强：
  - 代码块高亮
  - 复制按钮
  - 表格显示
  - 链接点击
  - 思考内容折叠显示
  - 流式输出

### 设置页
- 协议类型 / Base URL / API Key / 模型 分离配置
- 协议切换自动带出推荐 Base URL
- 允许手动改为中转地址
- 获取模型
- 测试文字连接 / 测试图片理解
- 自定义首轮中文提示词
- 快捷键录入
- Windows 当前用户登录后静默启动

---

## 技术栈

- C++20
- Qt 6 Widgets / Network / Test
- CMake
- CTest
- Win32 API（全局快捷键、DPI、开机自启等）

设计约束：

- **UI 控件**：遵循 Qt `QObject` 父子对象树管理生命周期
- **非 UI 业务对象**：统一使用现代 C++ 智能指针（`std::unique_ptr` / `std::shared_ptr`）
- **构建系统**：仅使用 CMake，不使用 qmake

---

## 当前目录结构

```text
.
├─ .github/workflows/                 # CI / Release
├─ native/                            # 当前主线：C++ / Qt 原生桌面版
│  ├─ src/
│  ├─ tests/
│  └─ README.md
├─ docs/
│  ├─ specs/                          # 设计规格
│  └─ plans/                          # 实施计划 / 阶段记录
├─ packaging/                         # 安装器与发布资源
├─ scripts/                           # 打包 / 辅助脚本
├─ README.md
└─ LICENSE
```

---

## 构建与运行（Windows）

### 依赖

- Windows 10 / 11
- Visual Studio 2022 Build Tools（MSVC）
- CMake 3.28+
- Qt 6.6+

### 配置

```powershell
$env:CMAKE_PREFIX_PATH='C:\Qt\6.6.3\msvc2022_64'   # 按你的 Qt 实际安装路径 / kit 名称修改
cmake -S native -B build/native
```

### 构建

```powershell
cmake --build build/native --config Debug -- /m:1
```

### 测试

```powershell
ctest --test-dir build/native -C Debug --output-on-failure
```

### 运行

```powershell
.\build\native\Debug\ASnap.exe
```

### 直接打包成可运行程序

仓库已经附带 **Windows 便携版打包脚本**，会自动：

- 配置 / 构建 `Release`
- 可选执行测试
- 调用 `windeployqt` 收集 Qt 运行库
- 生成一个开箱即用的 **portable ZIP**
- 可选再生成一个**单文件安装器 EXE**

本地打包命令：

```powershell
.\scripts\package-windows.ps1 -Configuration Release -RunTests -CreateInstaller -Version local
```

打包完成后会生成：

```text
dist/
├─ ASnap-windows-x64-local/
│  └─ ASnap.exe
├─ ASnap-Setup-windows-x64-local.exe
├─ ASnap-Setup-windows-x64-local.sha256.txt
├─ ASnap-windows-x64-local.zip
└─ ASnap-windows-x64-local.sha256.txt
```

解压后直接运行：

```powershell
.\ASnap.exe
```

如果你想让普通用户**下载一个 `.exe` 就安装并开始使用**，直接分发：

```text
ASnap-Setup-windows-x64-<version>.exe
```

即可。

GitHub Release 默认只发布这一项安装器资源，页面更简洁：

```text
ASnap-Setup-windows-x64-<version>.exe
```

---

## 使用流程

1. 启动应用后常驻托盘
2. 打开设置页配置：
   - Provider 协议
   - Base URL
   - API Key
   - 模型
3. 点击：
   - `测试文字连接`
   - `测试图片理解`
4. 设置快捷键、外观和首轮提示词
5. 使用 AI 截图快捷键开始截图并对话

---

## 隐私与安全

- AI 分析时，**你框选到的截图内容** 与相关文本消息可能会被发送到你配置的模型服务端
- 这个服务端可能是：
  - OpenAI 官方
  - Gemini 官方
  - Claude 官方
  - OpenAI-compatible 中转/代理接口
- 因此，**不要把你不愿上传的敏感信息直接发给不可信的 Provider 或中转地址**
- 如果你使用第三方 Base URL，请自行确认其日志、存储、合规与隐私策略

---

## 当前状态

这个项目已经适合以 **GitHub 公开预览 / 早期开源版本** 的形式发布，并继续迭代。

当前更适合这样定位：

- **Windows 优先**
- **源码可用**
- **功能持续打磨中**

GitHub：

- 仓库首页：<https://github.com/Wack520/ASnap>
- Releases：<https://github.com/Wack520/ASnap/releases>

---

## 已知限制 / 待继续优化

- 当前主要面向 Windows，macOS 仍在规划中
- 某些 **HDR + Chrome** 场景下截图曝光仍需继续优化
- 多屏 / 远程控制等复杂桌面环境还在持续兼容打磨
- 暂未提供正式安装包与自动更新流程

---

## 开发说明

- 原生桌面主工程位于 `native/`
- 精简构建说明见 `native/README.md`
- 设计与计划文档位于 `docs/specs/`、`docs/plans/`
- 测试基于 Qt Test + CTest
- GitHub Actions：
  - `windows-ci.yml`：推送 / PR 自动构建并跑测试
  - `windows-release.yml`：打 tag 后自动打包并上传正式安装器 EXE

如果你准备继续做公开维护，下一步建议补：

- 截图 / GIF 演示素材
- 更完整的英文 README 或双语说明

---

## License

MIT
