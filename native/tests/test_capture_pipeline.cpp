#include <QRect>
#include <QString>
#include <QTest>

#include "capture/capture_pipeline_types.h"

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

QTEST_MAIN(CapturePipelineTests)

#include "test_capture_pipeline.moc"
