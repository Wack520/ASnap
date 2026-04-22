# PixPin-Style Capture Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the Windows screenshot path into clear PixPin-style layers while fixing the current HDR overexposure, mixed-DPI/multi-monitor distortion, and clarity regressions without changing the product’s visible capture flow.

**Architecture:** Keep `ApplicationController` as the outer orchestrator, but move screenshot interaction into `CaptureWorkflowController` and turn `DesktopCaptureService` into a thin facade over four focused subsystems: topology, backend capture, frame normalization, and snapshot composition. Windows-specific code stays in `platform/windows`, with WGC, GDI fallback, and display enumeration split into separate files and wired together through explicit capture types plus diagnostics.

**Tech Stack:** C++20, Qt 6 Widgets/Gui/Test, CMake, Windows Graphics Capture, C++/WinRT, D3D11, DXGI, Win32 monitor APIs.

---

### Task 1: Introduce shared capture pipeline types and a dedicated pipeline test target

**Files:**
- Create: `native/src/capture/capture_pipeline_types.h`
- Modify: `native/src/capture/desktop_snapshot.h`
- Modify: `native/tests/CMakeLists.txt`
- Create: `native/tests/test_capture_pipeline.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write the failing pipeline-type test**

Add a new focused test file so capture math stops accumulating inside `test_capture_flow.cpp`.

```cpp
class CapturePipelineTests final : public QObject {
    Q_OBJECT

private slots:
    void capturePipelineTypesExposeStableDefaults();
};

void CapturePipelineTests::capturePipelineTypesExposeStableDefaults() {
    using namespace ais::capture;

    const DisplayDescriptor display{
        .deviceName = QStringLiteral(R"(\\.\DISPLAY1)"),
        .monitorRect = QRect(0, 0, 1920, 1080),
        .virtualRect = QRect(0, 0, 1536, 864),
        .devicePixelRatio = 1.25,
        .isPrimary = true,
    };
    const CaptureDiagnostics diagnostics;

    QCOMPARE(display.devicePixelRatio, 1.25);
    QVERIFY(display.isPrimary);
    QVERIFY(diagnostics.entries.isEmpty());
}
```

- [ ] **Step 2: Run the new test target and confirm it fails to compile**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target test_capture_pipeline -- /m:1
```

Expected: compile failure because `DisplayDescriptor` / `CaptureDiagnostics` do not exist yet.

- [ ] **Step 3: Add the shared capture data model**

Create `native/src/capture/capture_pipeline_types.h` with the common structs/enums used across Tasks 2-7.

```cpp
namespace ais::capture {

enum class CaptureBackendKind {
    Unknown,
    WgcFp16,
    WgcBgra8,
    Gdi,
};

struct DisplayDescriptor {
    QString deviceName;
    QRect monitorRect;
    QRect virtualRect;
    qreal devicePixelRatio = 1.0;
    bool isPrimary = false;
};

struct RawScreenFrame {
    DisplayDescriptor display;
    QImage image;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    QColorSpace colorSpace;
    bool isHdrLike = false;
};

struct PreparedScreenFrame {
    DisplayDescriptor display;
    QImage normalizedImage;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    bool hdrToneMapped = false;
};

struct CaptureDiagnosticsEntry {
    QString deviceName;
    CaptureBackendKind backendKind = CaptureBackendKind::Unknown;
    bool hdrToneMapped = false;
    bool fellBack = false;
    QString note;
};

struct CaptureDiagnostics {
    QList<CaptureDiagnosticsEntry> entries;
};

}  // namespace ais::capture
```

- [ ] **Step 4: Extend `DesktopSnapshot` with diagnostics without breaking callers**

Update `desktop_snapshot.h` so the new field defaults cleanly and existing aggregate initialization still compiles after small call-site updates.

```cpp
struct DesktopSnapshot {
    QPixmap displayImage;
    QPixmap captureImage;
    QRect overlayGeometry;
    QRect virtualGeometry;
    QList<ScreenMapping> screenMappings;
    CaptureDiagnostics diagnostics;
};
```

- [ ] **Step 5: Register the new test target**

Update `native/tests/CMakeLists.txt`:

```cmake
add_ai_test(test_capture_pipeline test_capture_pipeline.cpp)
```

- [ ] **Step 6: Rebuild and make the new type test pass**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target test_capture_pipeline -- /m:1
ctest --test-dir build/native-vs2022 -C Debug -R test_capture_pipeline --output-on-failure
```

Expected: `test_capture_pipeline` builds and passes.

- [ ] **Step 7: Commit**

```powershell
git add native/src/capture/capture_pipeline_types.h native/src/capture/desktop_snapshot.h native/tests/CMakeLists.txt native/tests/test_capture_pipeline.cpp native/CMakeLists.txt
git commit -m "refactor: add shared capture pipeline types"
```

### Task 2: Extract `FrameNormalizer` and make HDR normalization single-owner

**Files:**
- Create: `native/src/capture/frame_normalizer.h`
- Create: `native/src/capture/frame_normalizer.cpp`
- Modify: `native/src/capture/desktop_capture_service.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`
- Modify: `native/tests/test_capture_flow.cpp`
- Modify: `native/tests/test_capture_pipeline.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Move normalization assertions into the dedicated pipeline test**

Port the pure image tests out of `test_capture_flow.cpp`:
- `hdrLikeImageIsNormalizedToSdrColorSpace`
- `hdrLikeImageWithoutMetadataAssumesLinearSdrConversion`
- `chromeLikeHdrWhitesAreCompressedBeforeSdrClipping`
- `clippedSdrChromeLikeHighlightsAreCompressed`

Use the new API:

```cpp
const QImage normalized = ais::capture::FrameNormalizer::normalizeToSdr(hdrLike);
```

- [ ] **Step 2: Run the focused pipeline test and confirm the symbol is missing**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target test_capture_pipeline -- /m:1
```

Expected: compile failure because `FrameNormalizer` does not exist yet.

- [ ] **Step 3: Create the normalizer module and move the existing algorithms into it**

Create `frame_normalizer.h`:

```cpp
namespace ais::capture {

class FrameNormalizer final {
public:
    [[nodiscard]] static QImage normalizeToSdr(const QImage& image);
    [[nodiscard]] static PreparedScreenFrame normalizeFrame(const RawScreenFrame& frame);
};

}  // namespace ais::capture
```

Move the current helper cluster from `desktop_capture_service.cpp` into `frame_normalizer.cpp`:
- `isHdrLikeFormat`
- `isHdrCandidate`
- `analyzeLinearImage`
- `applyHighlightCompression`
- `applyHdrToneMapping`
- `compressedClippedSdrToSdr`
- `toneMappedHdrToSdr`

- [ ] **Step 4: Keep `DesktopCaptureService` stable with a temporary forwarding wrapper**

Do not break the current callers in the same commit; forward the old static helper to the new owner:

```cpp
QImage DesktopCaptureService::normalizeForSdr(const QImage& image) {
    return FrameNormalizer::normalizeToSdr(image);
}
```

- [ ] **Step 5: Remove the migrated tests from `test_capture_flow.cpp`**

Keep `test_capture_flow.cpp` focused on overlay and end-to-end selection behavior only.

- [ ] **Step 6: Run the two affected tests**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R "test_capture_pipeline|test_capture_flow" --output-on-failure
```

Expected: both tests pass; no behavior change.

- [ ] **Step 7: Commit**

```powershell
git add native/src/capture/frame_normalizer.h native/src/capture/frame_normalizer.cpp native/src/capture/desktop_capture_service.h native/src/capture/desktop_capture_service.cpp native/tests/test_capture_flow.cpp native/tests/test_capture_pipeline.cpp native/CMakeLists.txt
git commit -m "refactor: extract capture frame normalizer"
```

### Task 3: Extract `SnapshotComposer` and isolate coordinate/cropping math

**Files:**
- Create: `native/src/capture/snapshot_composer.h`
- Create: `native/src/capture/snapshot_composer.cpp`
- Modify: `native/src/capture/desktop_capture_service.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`
- Modify: `native/tests/test_capture_flow.cpp`
- Modify: `native/tests/test_capture_pipeline.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Move pure geometry/cropping tests into `test_capture_pipeline.cpp`**

Port these pure helpers out of the UI-heavy file:
- `translatesLocalSelectionToVirtualDesktopCoordinates`
- `logicalSelectionCopiesPhysicalPixelsForHighDpiSnapshots`
- `translatesSelectionFromPhysicalOverlayToVirtualDesktopCoordinates`
- `snapshotForScreenKeepsPhysicalPixelsAndLogicalOverlayGeometry`
- `composeFramesUseOverlayGeometryWithoutMixedDpiGap`
- `composeFramesSkipsEmptyRemoteFrames`

Use the new class name in the tests:

```cpp
const DesktopSnapshot snapshot = SnapshotComposer::composeFrames(frames);
const QRect virtualRect = SnapshotComposer::translateToVirtual(snapshot, localRect);
```

- [ ] **Step 2: Run the pipeline test and confirm the missing symbol failure**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target test_capture_pipeline -- /m:1
```

Expected: compile failure because `SnapshotComposer` does not exist yet.

- [ ] **Step 3: Create the new composition module**

Create `snapshot_composer.h`:

```cpp
namespace ais::capture {

class SnapshotComposer final {
public:
    [[nodiscard]] static DesktopSnapshot composeFrames(const QList<PreparedScreenFrame>& frames,
                                                       const CaptureDiagnostics& diagnostics = {});
    [[nodiscard]] static DesktopSnapshot snapshotForScreen(const DesktopSnapshot& snapshot,
                                                           const ScreenMapping& screenMapping);
    [[nodiscard]] static QRect translateToVirtual(const QRect& localRect,
                                                  const QPoint& virtualOrigin);
    [[nodiscard]] static QRect translateToVirtual(const DesktopSnapshot& snapshot,
                                                  const QRect& localRect);
    [[nodiscard]] static QPixmap copyLogicalSelection(const QPixmap& source,
                                                      const QRect& logicalRect);
};

}  // namespace ais::capture
```

Move the existing implementations from `DesktopCaptureService` into this file without changing logic yet.

- [ ] **Step 4: Keep `DesktopCaptureService` as a thin forwarding facade**

Update `desktop_capture_service.cpp`:

```cpp
DesktopSnapshot DesktopCaptureService::composeFrames(const QList<CapturedScreenFrame>& frames) {
    const QList<PreparedScreenFrame> prepared = legacyPreparedFramesFromCaptured(frames);
    return SnapshotComposer::composeFrames(prepared);
}
```

Define `legacyPreparedFramesFromCaptured()` in the anonymous namespace of `desktop_capture_service.cpp`; it must map geometry directly and must not call `FrameNormalizer::normalizeToSdr()` again.

- [ ] **Step 5: Assert that the `DesktopSnapshot` diagnostics survive composition**

Add a small test in `test_capture_pipeline.cpp`:

```cpp
QCOMPARE(snapshot.diagnostics.entries.size(), 2);
QCOMPARE(snapshot.diagnostics.entries.constFirst().deviceName, QStringLiteral("DISPLAY1"));
```

- [ ] **Step 6: Run the pipeline and flow tests**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R "test_capture_pipeline|test_capture_flow" --output-on-failure
```

Expected: composition/cropping tests pass after extraction.

- [ ] **Step 7: Commit**

```powershell
git add native/src/capture/snapshot_composer.h native/src/capture/snapshot_composer.cpp native/src/capture/desktop_capture_service.h native/src/capture/desktop_capture_service.cpp native/tests/test_capture_flow.cpp native/tests/test_capture_pipeline.cpp native/CMakeLists.txt
git commit -m "refactor: extract snapshot composer"
```

### Task 4: Add `DisplayTopology` and inject capture dependencies into `DesktopCaptureService`

**Files:**
- Create: `native/src/capture/display_topology.h`
- Create: `native/src/platform/windows/windows_display_topology.h`
- Create: `native/src/platform/windows/windows_display_topology.cpp`
- Create: `native/src/capture/screen_capture_backend.h`
- Modify: `native/src/capture/desktop_capture_service.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`
- Modify: `native/tests/test_capture_pipeline.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write a service-level fake-topology/fake-backend test**

Add a new test proving `DesktopCaptureService` can be driven without touching the real desktop:

```cpp
class FakeDisplayTopology final : public ais::capture::DisplayTopology {
public:
    QList<DisplayDescriptor> enumerateDisplays() const override { return displays; }
    QList<DisplayDescriptor> displays;
};

class FakeScreenCaptureBackend final : public ais::capture::ScreenCaptureBackend {
public:
    QList<RawScreenFrame> captureDisplays(const QList<DisplayDescriptor>& displays) const override {
        Q_UNUSED(displays);
        return frames;
    }
    QList<RawScreenFrame> frames;
};
```

Test target:

```cpp
void CapturePipelineTests::desktopCaptureServiceUsesInjectedTopologyAndBackend();
```

- [ ] **Step 2: Run the pipeline test and confirm constructor/type failures**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target test_capture_pipeline -- /m:1
```

Expected: compile failure because `DisplayTopology` / `ScreenCaptureBackend` / injectable constructor do not exist yet.

- [ ] **Step 3: Create the abstraction headers**

Create `display_topology.h`:

```cpp
namespace ais::capture {

class DisplayTopology {
public:
    virtual ~DisplayTopology() = default;
    [[nodiscard]] virtual QList<DisplayDescriptor> enumerateDisplays() const = 0;
};

}  // namespace ais::capture
```

Create `screen_capture_backend.h`:

```cpp
namespace ais::capture {

class ScreenCaptureBackend {
public:
    virtual ~ScreenCaptureBackend() = default;
    [[nodiscard]] virtual QList<RawScreenFrame> captureDisplays(
        const QList<DisplayDescriptor>& displays,
        CaptureDiagnostics* diagnostics) const = 0;
};

}  // namespace ais::capture
```

- [ ] **Step 4: Add the injectable `DesktopCaptureService` constructor**

Update the service interface:

```cpp
class DesktopCaptureService {
public:
    DesktopCaptureService();
    DesktopCaptureService(std::unique_ptr<DisplayTopology> topology,
                          std::unique_ptr<ScreenCaptureBackend> backend);
    [[nodiscard]] DesktopSnapshot captureVirtualDesktop() const;

private:
    std::unique_ptr<DisplayTopology> topology_;
    std::unique_ptr<ScreenCaptureBackend> backend_;
};
```

- [ ] **Step 5: Implement the Windows topology provider**

Create `windows_display_topology.cpp` using `EnumDisplayMonitors`, `GetMonitorInfoW`, and `QGuiApplication::screens()` reconciliation so each `DisplayDescriptor` has:
- `deviceName`
- physical `monitorRect`
- logical `virtualRect`
- `devicePixelRatio`
- primary-screen flag

- [ ] **Step 6: Make the fake-topology service test pass**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R test_capture_pipeline --output-on-failure
```

Expected: injected topology/backend test passes; real service still compiles through the default constructor.

- [ ] **Step 7: Commit**

```powershell
git add native/src/capture/display_topology.h native/src/capture/screen_capture_backend.h native/src/platform/windows/windows_display_topology.h native/src/platform/windows/windows_display_topology.cpp native/src/capture/desktop_capture_service.h native/src/capture/desktop_capture_service.cpp native/tests/test_capture_pipeline.cpp native/CMakeLists.txt
git commit -m "refactor: inject capture topology and backend"
```

### Task 5: Split the Windows capture backend into WGC, GDI fallback, and a single orchestration facade

**Files:**
- Create: `native/src/platform/windows/windows_capture_backend.h`
- Create: `native/src/platform/windows/windows_capture_backend.cpp`
- Create: `native/src/platform/windows/windows_gdi_capture_backend.h`
- Create: `native/src/platform/windows/windows_gdi_capture_backend.cpp`
- Modify: `native/src/platform/windows/windows_graphics_capture_backend.h`
- Modify: `native/src/platform/windows/windows_graphics_capture_backend.cpp`
- Delete: `native/src/platform/windows/native_screen_capture.cpp`
- Delete: `native/src/platform/windows/native_screen_capture.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`
- Modify: `native/tests/test_capture_flow.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Preserve existing mapped-texture tests before changing Windows files**

Keep these tests green during the split:
- `bgraMappedTextureProducesArgb32Image`
- `halfFloatMappedTextureProducesLinearFp16Image`
- `shortMappedTextureRowPitchReturnsEmptyImage`

These remain in `test_capture_flow.cpp` if they continue depending on the WGC implementation detail namespace.

- [ ] **Step 2: Create the new Windows facade header**

`windows_capture_backend.h` should implement the cross-layer interface:

```cpp
namespace ais::platform::windows {

class WindowsScreenCaptureBackend final : public ais::capture::ScreenCaptureBackend {
public:
    [[nodiscard]] QList<ais::capture::RawScreenFrame> captureDisplays(
        const QList<ais::capture::DisplayDescriptor>& displays,
        ais::capture::CaptureDiagnostics* diagnostics) const override;
};

}  // namespace ais::platform::windows
```

- [ ] **Step 3: Move GDI fallback into its own file**

Take the current GDI-only helpers out of `native_screen_capture.cpp`:
- `captureMonitorImage`
- `captureMonitorWithGdi`
- `captureGdiScreens`

New API shape:

```cpp
[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithGdi(
    const ais::capture::DisplayDescriptor& display);
```

- [ ] **Step 4: Reduce `windows_graphics_capture_backend.*` to WGC-only logic**

It should stop enumerating monitors itself. Change it to accept a single `DisplayDescriptor` and return `RawScreenFrame`.

```cpp
[[nodiscard]] std::optional<ais::capture::RawScreenFrame> captureDisplayWithWgc(
    const ais::capture::DisplayDescriptor& display);
```

Keep the existing detail test hooks (`makeQImageFromMappedTexture`) intact.

- [ ] **Step 5: Implement orchestration + diagnostics**

`windows_capture_backend.cpp` decides:
- WGC FP16 first
- WGC BGRA fallback
- GDI fallback if WGC capture fails for a display
- append one diagnostics entry per display

Expected diagnostics behavior:

```cpp
diagnostics->entries.append({
    .deviceName = display.deviceName,
    .backendKind = CaptureBackendKind::Gdi,
    .hdrToneMapped = false,
    .fellBack = true,
    .note = QStringLiteral("WGC frame acquisition timed out"),
});
```

- [ ] **Step 6: Rewire the default `DesktopCaptureService` to use the Windows facade**

Use:

```cpp
DesktopCaptureService::DesktopCaptureService()
    : DesktopCaptureService(std::make_unique<platform::windows::WindowsDisplayTopology>(),
                            std::make_unique<platform::windows::WindowsScreenCaptureBackend>()) {}
```

Delete `native_screen_capture.cpp/.h` from `ai_screenshot_core` in the same task after `DesktopCaptureService` stops including them.

- [ ] **Step 7: Run build + capture tests**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target ai_screenshot_core test_capture_flow test_capture_pipeline -- /m:1
ctest --test-dir build/native-vs2022 -C Debug -R "test_capture_flow|test_capture_pipeline" --output-on-failure
```

Expected: build succeeds; existing mapped-texture tests still pass; service now gets diagnostics from the Windows facade.

- [ ] **Step 8: Commit**

```powershell
git add native/src/platform/windows/windows_capture_backend.h native/src/platform/windows/windows_capture_backend.cpp native/src/platform/windows/windows_gdi_capture_backend.h native/src/platform/windows/windows_gdi_capture_backend.cpp native/src/platform/windows/windows_graphics_capture_backend.h native/src/platform/windows/windows_graphics_capture_backend.cpp native/src/capture/desktop_capture_service.cpp native/tests/test_capture_flow.cpp native/CMakeLists.txt
git rm native/src/platform/windows/native_screen_capture.cpp native/src/platform/windows/native_screen_capture.h
git commit -m "refactor: split windows capture backend layers"
```

### Task 6: Add `CaptureWorkflowController` and remove overlay lifecycle from `ApplicationController`

**Files:**
- Create: `native/src/app/capture_workflow_controller.h`
- Create: `native/src/app/capture_workflow_controller.cpp`
- Modify: `native/src/app/application_controller.h`
- Modify: `native/src/app/application_controller.cpp`
- Modify: `native/tests/test_application_controller.cpp`
- Modify: `native/CMakeLists.txt`

- [ ] **Step 1: Write the failing controller regression test**

Add a regression that keeps current user-visible behavior intact after extraction:

```cpp
void ApplicationControllerTests::plainCaptureCopiesScreenshotToClipboard();
void ApplicationControllerTests::requestBusyStateAllowsCaptureInterrupt();
```

The existing tests should stay unchanged first; let the refactor prove it preserves behavior.

- [ ] **Step 2: Run the controller test suite before moving code**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R test_application_controller --output-on-failure
```

Expected: green baseline before extraction.

- [ ] **Step 3: Create the workflow controller**

Define a small controller that owns overlays and only manages capture interaction state:

```cpp
class CaptureWorkflowController final : public QObject {
    Q_OBJECT

public:
    enum class LaunchMode { AiAssistant, PlainScreenshot };

    struct Hooks {
        std::function<ais::capture::DesktopSnapshot()> captureDesktop;
        std::function<void(const ais::capture::CaptureSelection&)> onConfirmed;
        std::function<void()> onCancelled;
        std::function<void(const QString&)> syncStatus;
    };

    explicit CaptureWorkflowController(Hooks hooks, QObject* parent = nullptr);
    bool start(LaunchMode mode);
    void clear();
};
```

Internal responsibilities:
- hide/show overlays
- build one overlay per `ScreenMapping`
- wire `captureConfirmed` / `captureCancelled`
- restore idle status on cancel

- [ ] **Step 4: Replace the overlay fields in `ApplicationController`**

Move these members out:
- `QList<capture::CaptureOverlay*> overlays_`
- `CaptureLaunchMode activeCaptureMode_`
- `startCaptureWorkflow()`
- `clearOverlay()`
- most of `onCaptureConfirmed()` / `onCaptureCancelled()`

Replace with:

```cpp
std::unique_ptr<CaptureWorkflowController> captureWorkflowController_;
```

- [ ] **Step 5: Keep routing decisions in `ApplicationController`**

`ApplicationController` should still decide what to do with the final selection:
- AI mode -> `beginSessionFromSelection()`
- plain screenshot mode -> `handlePlainScreenshotCapture()`

The workflow controller should not know anything about chat sessions or provider state.

- [ ] **Step 6: Re-run the controller tests**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R test_application_controller --output-on-failure
```

Expected: all existing application controller tests remain green after extraction.

- [ ] **Step 7: Commit**

```powershell
git add native/src/app/capture_workflow_controller.h native/src/app/capture_workflow_controller.cpp native/src/app/application_controller.h native/src/app/application_controller.cpp native/tests/test_application_controller.cpp native/CMakeLists.txt
git commit -m "refactor: extract capture workflow controller"
```

### Task 7: Make `DesktopCaptureService` a real facade and remove duplicate normalization risk

**Files:**
- Modify: `native/src/capture/desktop_capture_service.h`
- Modify: `native/src/capture/desktop_capture_service.cpp`
- Modify: `native/src/capture/frame_normalizer.cpp`
- Modify: `native/src/capture/snapshot_composer.cpp`
- Modify: `native/tests/test_capture_pipeline.cpp`

- [ ] **Step 1: Write the failing no-double-normalize regression**

Add a new pipeline test using a fake backend frame + diagnostics to prove the service normalizes exactly once.

Test shape:

```cpp
void CapturePipelineTests::desktopCaptureServiceNormalizesEachRawFrameExactlyOnce();
```

Use a fake backend returning a single HDR-like `RawScreenFrame`; assert the resulting `DesktopSnapshot` has:
- one diagnostics entry
- a valid sRGB image
- no second-pass washout (e.g. pixel value remains within a bounded expected range)

- [ ] **Step 2: Run the pipeline test and confirm the current service still fails the new expectation**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R test_capture_pipeline --output-on-failure
```

Expected: the new regression initially fails or cannot be expressed cleanly until the service pipeline is simplified.

- [ ] **Step 3: Rewrite `captureVirtualDesktop()` as a four-stage pipeline**

Target structure:

```cpp
DesktopSnapshot DesktopCaptureService::captureVirtualDesktop() const {
    const QList<DisplayDescriptor> displays = topology_->enumerateDisplays();
    CaptureDiagnostics diagnostics;
    const QList<RawScreenFrame> rawFrames = backend_->captureDisplays(displays, &diagnostics);

    QList<PreparedScreenFrame> preparedFrames;
    for (const RawScreenFrame& frame : rawFrames) {
        preparedFrames.append(FrameNormalizer::normalizeFrame(frame));
    }

    return SnapshotComposer::composeFrames(preparedFrames, diagnostics);
}
```

- [ ] **Step 4: Delete transitional normalization or composition adapters that are no longer needed**

Once the pipeline compiles, remove legacy helper glue that allowed the extraction tasks to land incrementally. Keep only the static forwarding helpers still used by overlay/callers; do not leave dead duplicate normalization paths behind.

- [ ] **Step 5: Re-run the pipeline tests**

Run:

```powershell
ctest --test-dir build/native-vs2022 -C Debug -R "test_capture_pipeline|test_capture_flow" --output-on-failure
```

Expected: the new no-double-normalize regression passes, along with existing composition/cropping tests.

- [ ] **Step 6: Commit**

```powershell
git add native/src/capture/desktop_capture_service.h native/src/capture/desktop_capture_service.cpp native/src/capture/frame_normalizer.cpp native/src/capture/snapshot_composer.cpp native/tests/test_capture_pipeline.cpp
git commit -m "refactor: turn desktop capture service into pipeline facade"
```

### Task 8: Full verification, packaging sanity, and manual HDR/multi-monitor matrix

**Files:**
- Verify only unless fixes are required inside `native/`

- [ ] **Step 1: Run the full native test suite**

Run:

```powershell
cmake --build build/native-vs2022 --config Debug --target ALL_BUILD -- /m:1
ctest --test-dir build/native-vs2022 -C Debug --output-on-failure
```

Expected: all native tests pass.

- [ ] **Step 2: Run one Release package build**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1 -Configuration Release -RunTests -Version pixpin-capture-refactor
```

Expected: packaging completes; note any pre-existing non-blocking warnings separately.

- [ ] **Step 3: Manual verification matrix**

Verify these behaviors in the built app:
- HDR Chrome page does not wash out to full white after several captures
- HDR Edge page no longer alternates between normal and overexposed captures
- dual-monitor mixed-DPI capture no longer stretches the main screen
- selecting on the secondary monitor crops the correct physical pixels
- double-click full-screen capture uses the clicked monitor, not the wrong display
- plain screenshot still copies to clipboard
- AI screenshot still opens the floating panel and starts a conversation

- [ ] **Step 4: Capture diagnostics spot-check**

Confirm at least one successful run shows diagnostics consistent with reality, for example:
- HDR primary monitor -> `WgcFp16`, `hdrToneMapped = true`
- SDR secondary monitor -> `WgcBgra8` or `Gdi`

- [ ] **Step 5: Final cleanup commit**

```powershell
git add native
git commit -m "refactor: finish pixpin-style capture architecture"
```
