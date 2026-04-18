# ASnap

Windows 桌面 AI 截图助手。  
冻结屏幕、自由框选、立刻提问，并在截图旁继续追问。

<p>
  <a href="https://github.com/Wack520/ASnap/releases">
    <img src="https://img.shields.io/badge/Download-Windows%20Installer-4f8cff?style=for-the-badge" alt="Download Windows Installer" />
  </a>
  <a href="https://github.com/Wack520/ASnap/releases">
    <img src="https://img.shields.io/badge/Releases-Preview-111827?style=for-the-badge" alt="Releases" />
  </a>
  <a href="https://github.com/Wack520/ASnap/actions/workflows/windows-ci.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/Wack520/ASnap/windows-ci.yml?branch=main&style=for-the-badge&label=CI" alt="CI" />
  </a>
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-111827?style=for-the-badge" alt="MIT License" />
  </a>
</p>

## 项目概览

ASnap 面向 Windows 桌面场景，主打“截完继续问”。

- 先冻结屏幕，再自由框选
- 截图后直接在浮窗里追问
- 支持 Markdown、代码块、表格和链接
- 兼容 OpenAI、OpenAI Responses、OpenAI-compatible、Gemini、Claude
- 提供文本连接、图片理解、快捷键和外观设置

## 它能做什么

- 全局快捷键触发截图，不打断当前桌面节奏
- 截图完成后，悬浮面板贴着选区出现，直接继续问
- Markdown、代码块、链接、思考内容分区都能正常显示
- 同时支持 OpenAI、OpenAI Responses、OpenAI-compatible、Gemini、Claude
- 设置页可以测试文本连接、测试图片理解、录入快捷键、调整外观

## 核心体验

### 截图
- 托盘常驻
- AI 截图 / 普通截图双快捷键
- 全屏冻结后自由框选
- 双击直接全屏截图
- `Esc` 取消

### 对话
- 基于截图继续追问
- 回复中支持排队发送下一条消息
- Markdown / 表格 / 代码块 / 链接
- 代码块复制按钮
- 空响应自动重试

### 面板
- 永远置顶
- 跟随截图位置
- 不出屏幕边界
- 深色 / 浅色
- 可调背景色、文字色、边框色、透明度
- 可拖动、可调整宽度

## 支持的 Provider

| 类型 | 说明 |
| --- | --- |
| OpenAI Chat | 标准 Chat Completions 接口 |
| OpenAI Responses | Responses 接口 |
| OpenAI-compatible | 兼容 OpenAI 协议的中转 / 自建服务 |
| Gemini | Google Gemini |
| Claude | Anthropic Claude |

## 下载与安装

前往 Releases 页面下载 Windows 安装器：

- **Releases:** https://github.com/Wack520/ASnap/releases

当前发布默认提供：

- `ASnap-Setup-windows-x64-<version>.exe`

安装后即可直接启动，无需手动拷贝 Qt 运行库。

## 从源码构建

### 依赖

- Windows 10 / 11
- Visual Studio 2022 Build Tools（MSVC）
- CMake 3.28+
- Qt 6.6+

### 配置

```powershell
$env:CMAKE_PREFIX_PATH='C:\Qt\6.6.3\msvc2022_64'
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

### 打包安装器

```powershell
.\scripts\package-windows.ps1 -Configuration Release -RunTests -CreateInstaller -Version local
```

## 项目结构

```text
.
├─ native/       # Qt / C++ 主工程
├─ packaging/    # 安装器与发布资源
├─ scripts/      # 打包脚本
├─ docs/specs/   # 设计文档
└─ docs/plans/   # 实施计划
```

## 隐私说明

你发送给模型服务端的内容可能包括：

- 你框选到的截图
- 你输入的文字消息
- 与当前会话相关的上下文

如果你使用第三方 Base URL 或中转服务，请先确认对方的日志、存储和隐私策略。

## 路线图

- 持续优化 HDR / 多屏 / 远程桌面兼容性
- 继续打磨聊天面板与设置页体验
- 评估自动更新与更多平台支持

## License

MIT
