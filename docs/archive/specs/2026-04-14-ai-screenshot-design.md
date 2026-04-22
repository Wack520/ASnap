# AI 截图项目改造设计

- 日期：2026-04-14
- 范围：在现有 PyQt6 桌面应用基础上，完成多协议大模型接入、PixPin 风格截图体验、截图后二次裁剪、多轮 AI 会话、悬浮窗主题与智能跟随定位、多屏高 DPI 支持。

## 目标

1. 支持统一 Provider 抽象层，可接入：
   - OpenAI Chat Completions
   - OpenAI Responses API
   - OpenAI-compatible（DeepSeek / 国内兼容 OpenAI 的平台 / Ollama / OneAPI 等）
   - Gemini 原生
   - Claude 原生
2. 截图时提供接近 PixPin 的纯色遮罩、高亮选区、蓝色边框、尺寸提示与确认提示。
3. 首次框选后进入二次裁剪编辑态，确认后再发送到 AI。
4. 截图结果在置顶悬浮聊天窗中展示，支持同一张图的多轮追问。
5. 悬浮窗支持浅色/深色纯色主题与透明度设置。
6. 悬浮窗根据截图区域智能选择弹出位置，避免超出屏幕边界。
7. 支持多屏与不同缩放比例环境。

## 设计

### 模块

- `ai_screenshot.config`：配置、Provider Profile、主题设置、配置文件读写。
- `ai_screenshot.providers`：统一 Provider 接口与不同厂商实现。
- `ai_screenshot.session`：当前截图会话、图像与消息历史管理。
- `ai_screenshot.placement`：悬浮窗智能定位算法。
- `ai_screenshot.screen_service`：虚拟桌面、多屏截图与坐标换算。
- `ai_screenshot.ui_capture`：截图遮罩与二次裁剪。
- `ai_screenshot.ui_chat`：悬浮聊天窗、设置页、主题。
- `ai_screenshot.app`：托盘、热键、主流程编排。

### 会话流

1. 热键触发截图遮罩。
2. 用户在虚拟桌面上框出第一块区域。
3. 应用弹出二次裁剪编辑框，允许用户调整并确认。
4. 确认后创建或更新当前聊天会话。
5. 悬浮窗贴近截图区域出现，并发送默认图片分析请求。
6. 用户可继续针对同一张图追问，或重新裁剪后继续问。

### Provider 抽象

统一输入：文本消息历史 + 当前图片（可选） + 当前 Profile。
统一输出：模型文本结果。

Provider 需提供：
- `display_name`
- `list_models()`
- `complete()`
- `supports_model_listing`

### 风格原则

- 纯色主题，避免重渐变和玻璃拟态。
- 截图层强调清晰、干净、像工具软件而非演示产品。
- 悬浮窗置顶、轻量、可拖动。

### 边界

本次不实现：历史会话列表、标注工具栏、OCR 独立模式、多标签聊天。
