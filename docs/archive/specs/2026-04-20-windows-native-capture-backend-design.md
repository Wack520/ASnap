# Windows Native Capture Backend Design

## Goal
将当前以 Qt/GDI 为主的桌面截图链路升级为以 Windows 原生捕获为主、Qt 仅负责 UI/拼接展示的结构，优先解决多屏混合 DPI 变形、截图清晰度下降、HDR 页面曝光过高等问题。

## Approved Direction
用户已明确批准采用 `WGC (Windows.Graphics.Capture) + D3D11 + DXGI` 路线作为主截图后端。

## Architecture

### 1. Capture backend split
- `native_screen_capture.*` 保持对上层的稳定接口：`QList<NativeScreenFrame> captureNativeScreens()`。
- 新增 Windows 原生后端实现，负责：
  - 枚举监视器
  - 为每块屏创建 `GraphicsCaptureItem`
  - 同步等待首帧
  - 将 GPU 纹理读回 `QImage`
- 原有 GDI `BitBlt` 路径保留为 fallback。

### 2. One-monitor-per-frame capture
- 每个监视器独立抓一帧，不走 Qt 的整张虚拟桌面截图。
- `NativeScreenFrame` 继续携带：
  - `deviceName`
  - `image`
  - `monitorRect`
- `DesktopCaptureService` 继续根据 `QScreen` 名称和几何信息完成上层屏幕映射与拼接。

### 3. HDR handling
- WGC 首选 `R16G16B16A16_FLOAT` 帧池；若系统/设备不支持，则退回 `B8G8R8A8_UNORM`。
- 读回 half-float 数据时使用 `QImage::Format_RGBA16FPx4` + `SRgbLinear` 色彩空间，让现有 `normalizeForSdr()` 继续做 HDR->SDR 压缩。
- 非 HDR / 回退路径仍使用现有 SDR 归一化逻辑。

### 4. Failure model
- 若单屏 WGC 初始化或取帧失败，则该屏回退到 GDI。
- 若 WGC 全量失败，则整体回退到现有 GDI/Qt 路径。
- 不改变上层截图状态机、Overlay、多窗口分屏选择逻辑。

## File Changes
- 新增：`native/src/platform/windows/windows_graphics_capture_backend.h`
- 新增：`native/src/platform/windows/windows_graphics_capture_backend.cpp`
- 修改：`native/src/platform/windows/native_screen_capture.cpp`
- 修改：`native/src/platform/windows/native_screen_capture.h`
- 修改：`native/CMakeLists.txt`
- 如有必要，小幅修改：`native/src/capture/desktop_capture_service.cpp`

## Testing
- 保留现有 capture flow / application controller 测试不变。
- 新增尽量纯函数化的单元测试（若实现中有可测试 helper）。
- 至少执行：
  - `test_capture_flow`
  - `test_application_controller`
  - 一次完整 CMake build

## Non-goals for this round
- 不做窗口级捕获
- 不做录屏/持续流式截图
- 不重写 Overlay UI
- 不在本轮引入复杂 HDR 参数配置页
