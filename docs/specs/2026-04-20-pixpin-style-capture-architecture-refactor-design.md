# PixPin-Style Capture Architecture Refactor Design

## Goal
参考 PixPin 的分层思路，重整当前 Windows 桌面 AI 截图工具的截图主链路，避免继续在 `ApplicationController`、`DesktopCaptureService`、`native_screen_capture` 上堆逻辑；同时把本轮最关键的问题一起纳入新结构：

- HDR 页面偶发曝光过高
- 多屏 / 副屏 / 混合 DPI 下主屏变形或坐标错位
- 截图清晰度下降、裁剪后发糊
- 后续继续扩展截图能力时容易继续长成“大文件 + 多状态机”屎山

本次设计目标是：**先把结构做对，再把截图/HDR/多屏主线问题落进新结构里解决。**

## Background
当前代码已经做过两轮有效演进：

1. `ConversationRequestController` 已从 `ApplicationController` 中拆出，说明“先拆状态机边界，再继续功能演进”的方向是有效的。
2. Windows 原生截图主后端已升级为 `WGC + D3D11 + DXGI` 优先、GDI fallback 的基础形态，说明当前项目已经具备继续向原生截图内核推进的基础。

但截图链路当前仍存在几个结构性问题：

- `ApplicationController` 仍同时参与托盘、热键、overlay、截图状态、聊天窗、设置页等编排
- `DesktopCaptureService` 同时承担抓图、HDR/SDR 归一化、多屏拼接、坐标映射
- `native_screen_capture.cpp` 同时混合显示器枚举、WGC、GDI fallback
- HDR 归一化存在重复处理风险，导致部分网页在 HDR 场景下偶发过曝
- 多屏映射逻辑分散，不利于继续修 mixed-DPI / 副屏 / 远程控制等复杂场景

## Options Considered

### A. 在现有结构上继续补丁式修复
只针对 HDR、多屏、清晰度问题逐个 patch。

**优点**
- 改动小，短期见效快

**缺点**
- 继续放大已有文件复杂度
- 新旧逻辑更容易重复
- 后续再加功能时维护成本更高

### B. PixPin 风格的“分层截图内核”重整，并顺手吸纳截图主线问题（推荐）
先明确层次和边界，再把 HDR、多屏、清晰度修复落进这些层。

**优点**
- 最适合长期维护
- 便于继续增加窗口级截图、录屏、更多平台支持
- 能把当前最痛的截图问题放到正确层次处理

**缺点**
- 本轮改动面比 A 更大
- 需要更严格的迁移顺序与回归测试

### C. 一次性全项目大重构
把 AI、截图、UI、设置页全部一起大拆。

**优点**
- 理论上一次性最干净

**缺点**
- 风险过高
- 与当前最关键的截图主问题不匹配

## Approved Direction
采用 **B**：按 PixPin 风格重整截图架构，并把 HDR / 多屏 / 清晰度主线问题一起落入新结构。

## Design Principles

### 1. 分层而不是堆分支
每一层只做一件事，上层编排，下层提供清晰接口，不允许继续出现“一层同时做枚举、抓图、归一化、拼接、状态管理”的混合实现。

### 2. HDR 归一化只允许发生一次
截图从原始帧到最终上传图之间，HDR/SDR 处理必须在单一层统一执行，禁止 compose、crop、UI 预览再重复压缩或重复 normalize。

### 3. 原始像素与显示预览分离
overlay 展示可以为了交互使用缩放后的显示图，但真正用于裁剪与上传的图必须保留高保真物理像素，不得因为 UI 显示而丢清晰度。

### 4. 多屏拓扑单点收口
显示器枚举、虚拟桌面坐标、logical/physical 映射、DPI 信息统一由一个拓扑层提供，业务层不直接各自推导。

### 5. Windows 原生能力作为独立 backend，而不是散落工具函数
WGC、GDI fallback、后续可能的 DXGI Duplication 或窗口级捕获，都应表现为 backend 层，而不是在同一个 cpp 里堆 if/else。

## Target Architecture

### Layer 1: App Orchestration
负责：
- 托盘与全局快捷键
- 设置页 / 聊天浮窗生命周期
- 启动截图工作流
- 把截图结果交给会话请求流

不再负责：
- 直接持有截图多阶段状态细节
- 直接拼装多屏帧
- 直接处理 logical/physical 坐标换算

核心保留类：
- `native/src/app/application_controller.h/.cpp`

本轮新增：
- `native/src/app/capture_workflow_controller.h/.cpp`

### Layer 2: Capture Workflow
`CaptureWorkflowController` 负责：
- 启动冻结屏幕 overlay
- 管理框选 / 双击全屏 / Esc 取消
- 处理 AI 截图与普通截图两种入口
- 生成面向上层的 `CaptureResult`

它是“截图交互状态机”，但不负责底层抓屏实现。

### Layer 3: Capture Pipeline Facade
保留一个面向上层的 capture facade，但只做流程编排：

- 读取显示拓扑
- 调用 backend 抓取每块屏原始帧
- 交给 normalizer 做一次性 SDR/HDR 归一化
- 交给 composer 输出 `DesktopSnapshot`

建议保留并瘦身：
- `native/src/capture/desktop_capture_service.h/.cpp`

新的 `DesktopCaptureService` 不再承载复杂算法细节，只做 facade。

### Layer 4: Capture Subsystems

#### 4.1 Display Topology
负责：
- 枚举显示器
- 统一维护 `monitorRect` / `virtualRect` / `devicePixelRatio`
- 统一 logical/physical 坐标映射
- 处理 mixed-DPI、多屏、副屏、负坐标场景

建议文件：
- `native/src/capture/display_topology.h/.cpp`
- `native/src/platform/windows/windows_display_topology.h/.cpp`

#### 4.2 Screen Capture Backend
定义统一抓图接口：
- 输入：显示拓扑中的单个 display descriptor
- 输出：该屏原始帧与诊断信息

建议文件：
- `native/src/capture/screen_capture_backend.h`
- `native/src/platform/windows/windows_capture_backend.h/.cpp`
- `native/src/platform/windows/windows_wgc_capture_backend.h/.cpp`
- `native/src/platform/windows/windows_gdi_capture_backend.h/.cpp`

职责划分：
- `windows_wgc_capture_backend`：WGC + D3D11 + DXGI 抓帧
- `windows_gdi_capture_backend`：GDI fallback
- `windows_capture_backend`：统一调度，决定优先级与单屏 fallback

#### 4.3 Frame Normalizer
负责：
- 识别原始帧颜色空间 / 像素格式
- HDR -> SDR tone mapping
- SDR clipped highlight 压制
- 输出用于后续拼接的标准化图像

建议文件：
- `native/src/capture/frame_normalizer.h/.cpp`

当前 `DesktopCaptureService::normalizeForSdr()` 中的核心算法迁到这里，并明确“只允许在这里执行一次”。

#### 4.4 Snapshot Composer
负责：
- 多屏拼接
- overlay 坐标空间与 virtual 坐标空间映射
- 裁剪逻辑
- 保证 compose 不额外触发 normalize，不引入额外缩放

建议文件：
- `native/src/capture/snapshot_composer.h/.cpp`

## Data Model Redesign

### DisplayDescriptor
表示一块屏幕的稳定拓扑信息：
- displayId / deviceName
- monitorRect（物理像素坐标）
- virtualRect（逻辑桌面坐标）
- devicePixelRatio
- isPrimary

### RawScreenFrame
表示 backend 直接抓回的原始单屏帧：
- `DisplayDescriptor display`
- `QImage image`
- `CaptureBackendKind backendKind`（`WgcFp16` / `WgcBgra8` / `Gdi`）
- `QColorSpace colorSpace`
- `bool isHdrLike`

### PreparedScreenFrame
表示已经过一次标准化处理、可安全用于拼接的单屏帧：
- `DisplayDescriptor display`
- `QImage normalizedImage`
- `CaptureBackendKind backendKind`
- `bool hdrToneMapped`

### CaptureDiagnostics
记录本次截图的关键路径：
- 每块屏实际使用的 backend
- 是否触发 HDR tone mapping
- 是否 fallback
- 若失败，记录失败原因

用于日志与后续排查“为什么这次又过曝 / 为什么这次又模糊 / 为什么这次又走了 0 输入”这类问题。

### DesktopSnapshot
继续作为上层可消费的最终结果，但语义收紧为：
- `displayImage`：overlay 展示图
- `captureImage`：高保真裁剪/上传图
- `overlayGeometry`
- `virtualGeometry`
- `screenMappings`
- `CaptureDiagnostics diagnostics`

## Data Flow

1. `ApplicationController` 或 `CaptureWorkflowController` 发起一次截图
2. `DesktopCaptureService` 请求 `DisplayTopology`
3. `DesktopCaptureService` 对每块 display 调用 `ScreenCaptureBackend`
4. backend 返回 `RawScreenFrame`
5. `FrameNormalizer` 将 `RawScreenFrame` 转成 `PreparedScreenFrame`
6. `SnapshotComposer` 将所有 `PreparedScreenFrame` 合成为 `DesktopSnapshot`
7. overlay 使用 `displayImage`
8. 用户框选 / 双击全屏后，composer/裁剪逻辑基于 `captureImage + screenMappings` 输出最终选区
9. AI 上传链路始终使用高保真 PNG，不复用低质量预览图

## HDR Handling Policy

### Primary policy
- Windows 下优先使用 WGC
- 优先请求 `R16G16B16A16_FLOAT`
- 不支持时退回 `B8G8R8A8_UNORM`
- 单屏失败允许回退 GDI

### Tone mapping policy
- HDR 检测与 tone mapping 统一在 `FrameNormalizer`
- `SnapshotComposer` 不允许再次 normalize
- crop 阶段不允许再次 normalize
- 若输入已是 SDR，只做必要的 clipped highlight 压制，不做 HDR 逻辑

### Diagnostics policy
每次截图至少记录：
- 实际走的 backend
- 输入像素格式
- 是否执行 tone mapping
- 是否回退 GDI

这样才能把“有时候曝、有时候不曝”的问题从猜测变成可追踪事实。

## Multi-Monitor / DPI Policy

### Single source of truth
logical / physical 坐标换算全部由 `DisplayTopology` 提供，其他层只消费结果，不再自己猜。

### Required scenarios
本轮结构必须支持并验证：
- 单屏 100% DPI
- 双屏 mixed-DPI
- 主屏 + 副屏负坐标
- 远程控制 / 屏幕拓扑变化后的重新枚举
- 双击当前屏全屏截图

### Selection mapping
overlay 使用逻辑坐标交互；真正裁剪时必须能稳定映射回对应屏幕的物理像素区域，保证“不变形、不偏移、不糊”。

## Quality Policy

### Do not upscale
任何阶段都不允许为了“看起来更清晰”而做放大式插值。

### Preserve physical pixels
底层保留原始物理像素；展示和上传分离；上传图始终使用高保真 PNG。

### Compose without hidden resample
拼接阶段只允许按目标 rect 贴图，不允许额外平滑缩放或重复压缩。

## Migration Plan

### Phase 1: 边界重整
- 新增 `CaptureWorkflowController`
- 抽出 `DisplayTopology`
- 抽出 `FrameNormalizer`
- 抽出 `SnapshotComposer`
- 让 `DesktopCaptureService` 退化为 facade

### Phase 2: Windows backend 归位
- 从现有 `native_screen_capture.cpp` 中拆出：
  - `windows_display_topology`
  - `windows_wgc_capture_backend`
  - `windows_gdi_capture_backend`
  - `windows_capture_backend`
- 保持现有外层行为不变

### Phase 3: 主问题落地修复
- 移除重复 normalize 风险
- 增加 capture diagnostics
- 稳定 HDR 场景曝光
- 校正 mixed-DPI / 多屏 / 副屏映射
- 回归截图清晰度

## Testing Strategy

### Unit tests
新增或增强以下纯逻辑测试：
- HDR tone mapping helper
- SDR clipped highlight compression helper
- logical/physical 坐标换算
- 多屏 compose 与 translateToVirtual

### Integration tests
保留并继续跑：
- `test_capture_flow`
- `test_application_controller`

必要时增加：
- WGC/GDI backend facade 行为测试
- 单屏 fallback 行为测试

### Manual verification matrix
至少人工验证：
- 单屏 SDR
- 单屏 HDR（Chrome / Edge）
- 双屏 mixed-DPI
- 主屏 / 副屏双击全屏
- 拖拽区域截图清晰度
- 远程控制连接后再次截图

## Non-goals
本轮不做：
- 窗口级捕获
- 持续录屏
- macOS 原生截图后端
- 新的视频/图像滤镜配置页
- 与 AI provider 层相关的结构重整

## Acceptance Criteria
完成本轮后，至少应满足：

1. 截图主链路的职责边界清晰，核心模块不再继续膨胀
2. HDR 归一化路径清晰且只执行一次
3. 多屏 / mixed-DPI / 副屏场景的坐标映射由单一模块管理
4. 截图不会因为 UI 预览链路而明显发糊
5. 当截图再次出现曝光、空白或回退异常时，可以通过 diagnostics 快速定位实际走了哪条路径

## Notes
- 本设计优先保证 Windows 主链路稳定，因为当前用户问题集中在 Windows 截图质量、HDR、多屏。
- 本设计与已有 `ConversationRequestController` 拆分方向一致：先把高复杂度状态与算法边界拆清，再继续做功能增量。
