#include <memory>

#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QColor>
#include <QCheckBox>
#include <QImage>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QRegularExpression>
#include <QToolButton>
#include <QtTest/QtTest>
#include <QUrl>

#include "chat/chat_session.h"
#include "capture/capture_mode.h"
#include "config/app_config.h"
#include "config/provider_preset.h"
#include "config/provider_protocol.h"
#include "ui/chat/floating_chat_panel.h"
#include "ui/chat/floating_chat_panel_helpers.h"
#include "ui/settings/settingsdialog.h"

using ais::chat::ChatSession;
using ais::config::AppConfig;
using ais::config::ProviderProtocol;
using ais::config::presetFor;
using ais::ui::FloatingChatPanel;
using ais::ui::SettingsDialog;

class UiWidgetTests final : public QObject {
    Q_OBJECT

private slots:
    void changingProtocolAppliesRecommendedBaseUrl();
    void busySettingsDialogDisablesTestButtons();
    void settingsDialogCurrentConfigIncludesCaptureMode();
    void settingsDialogBusyStateLocksCaptureModeSelector();
    void settingsDialogBusyStateLocksAllEditableControls();
    void settingsDialogBusyStateLocksAppearanceControls();
    void settingsDialogAllowsEditingFirstPrompt();
    void settingsDialogShortcutFieldsCapturePressedKeys();
    void settingsDialogAllowsChoosingPanelAndTextColors();
    void settingsDialogAllowsChoosingPanelBorderColor();
    void settingsDialogCanRestoreAutomaticTextColor();
    void settingsDialogCanRestoreAutomaticBorderColor();
    void settingsDialogPersistsTransparentPanelColor();
    void settingsDialogPreviewReflectsLatestAppearanceChoices();
    void settingsDialogUsesScrollableCompactLayout();
    void settingsDialogHasMinimumSizeToPreventOcclusion();
    void settingsDialogShortcutFieldsShareSingleRow();
    void settingsDialogCanToggleLaunchAtLogin();
    void settingsDialogOpacityFieldUsesPercentForTransparency();
    void settingsDialogOpacityFieldHidesStepperButtons();
    void settingsDialogUsesRequestedDefaultAppearance();
    void darkSettingsDialogUsesConsistentDarkSurface();
    void settingsDialogModelRowIncludesFetchAndTestActions();
    void settingsDialogDefaultWidthIsNarrower();
    void settingsDialogShowsDedicatedModelPopupButton();
    void settingsDialogKeepsModelActionsOnSingleVisibleRowForLongModelNames();
    void settingsDialogBusyStatePreservesScrollPosition();
    void settingsDialogFetchClickKeepsScrollPositionThroughAsyncUpdates();
    void settingsDialogBusyStateClearsFocusedModelEditorWithoutScrollJump();
    void settingsDialogFetchActionKeepsOriginalScrollAnchorAfterMidFlightJump();
    void settingsDialogActionButtonsKeepScrollFixedDuringAsyncWork();
    void settingsDialogActionStartKeepsVisibleSectionAtSameViewportOffset();
    void settingsDialogLongCompletionStatusDoesNotShiftVisibleSection();
    void settingsDialogLongCompletionStatusDoesNotPushScrollViewportDown();
    void settingsDialogProviderActionsRemainPinnedWhenSettingsContentScrolls();
    void settingsDialogProviderFieldsStillScrollBeneathPinnedActionBar();
    void settingsDialogHidesDuplicateProviderStatusRow();
    void settingsDialogActionStatusUsesSingleCompactLine();
    void settingsDialogShowsExplicitModelActionFeedback();
    void settingsDialogCanApplyFetchedModelChoices();
    void settingsDialogPreviewUsesChatLikeMock();
    void settingsDialogPreviewWidgetsDoNotTakeFocus();
    void settingsDialogRestoresSavedSize();
    void settingsDialogRestoresSavedPosition();
    void settingsDialogPreviewComposerMatchesFloatingChatLayout();
    void settingsDialogSaveKeepsDialogOpenAndEmitsSignal();
    void bindSessionRendersCurrentHistory();
    void defaultPanelWindowSizeIsCompact();
    void floatingPanelCanRestoreSavedSize();
    void floatingPanelUsesFramelessTransparentWindow();
    void floatingPanelAppearanceUsesCustomPanelColor();
    void floatingPanelAppearanceKeepsSurfaceAndBorderProperties();
    void escapeShortcutClosesFloatingPanel();
    void closeButtonHoverDoesNotShowResizeCursor();
    void floatingPanelCanBeDraggedByHeader();
    void floatingPanelCanResizeHorizontallyFromLeftEdge();
    void floatingPanelCanResizeHorizontallyFromRightEdge();
    void floatingPanelResizeHotZoneWorksFromChildViewport();
    void statusBarUsesCompactHeight();
    void chatInputRowUsesChatLikeControls();
    void chatPanelRemovesFooterActionButtons();
    void chatPanelShowsMinimizeButton();
    void minimizeButtonHidesPanelWithoutEndingSession();
    void reasoningPanelStartsCollapsedAndCanExpand();
    void reasoningPanelUsesScrollableCompactHeight();
    void assistantMarkdownRendersWithoutLiteralFenceMarkers();
    void historyCssUsesDedicatedCodeBodyContainer();
    void assistantMarkdownLinksOpenThroughDesktopServices();
    void streamingBadgeRequiresVisibleAssistantText();
    void streamingBadgeAppearsAfterAssistantTextStarts();
    void floatingPanelKeepsManualScrollPositionWhileStreaming();
    void floatingPanelScrollbarTintTracksPanelSurface();
    void busyChatPanelKeepsInputEditableWhileRequestIsRunning();
};

void UiWidgetTests::changingProtocolAppliesRecommendedBaseUrl() {
    AppConfig config;
    config.activeProfile.protocol = ProviderProtocol::OpenAiResponses;
    config.activeProfile.baseUrl = QStringLiteral("https://custom.example.test/v1");
    config.activeProfile.model = QStringLiteral("custom-model");
    config.aiShortcut = QStringLiteral("Alt+Q");
    config.screenshotShortcut = QStringLiteral("Alt+S");
    config.theme = QStringLiteral("dark");
    config.opacity = 0.88;

    SettingsDialog dialog(config);

    const int geminiIndex =
        dialog.protocolSelector()->findData(static_cast<int>(ProviderProtocol::Gemini));
    QVERIFY(geminiIndex >= 0);

    dialog.protocolSelector()->setCurrentIndex(geminiIndex);

    const AppConfig current = dialog.currentConfig();
    const auto preset = presetFor(ProviderProtocol::Gemini);

    QCOMPARE(static_cast<int>(current.activeProfile.protocol),
             static_cast<int>(ProviderProtocol::Gemini));
    QCOMPARE(current.activeProfile.baseUrl, preset.defaultBaseUrl);
    QCOMPARE(current.activeProfile.model, preset.defaultModel);
    QVERIFY(!dialog.baseUrlField()->isReadOnly());
    QVERIFY(dialog.modelField()->isEditable());
}

void UiWidgetTests::busySettingsDialogDisablesTestButtons() {
    SettingsDialog dialog(AppConfig{});

    dialog.setBusy(true, QStringLiteral("Testing connection..."));

    QVERIFY(!dialog.protocolSelector()->isEnabled());
    QVERIFY(!dialog.testConnectionButton()->isEnabled());
    QVERIFY(!dialog.testImageButton()->isEnabled());
    QCOMPARE(dialog.statusLabel()->text(), QStringLiteral("Testing connection..."));

    dialog.setBusy(false, QStringLiteral("Ready"));

    QVERIFY(dialog.protocolSelector()->isEnabled());
    QVERIFY(dialog.testConnectionButton()->isEnabled());
    QVERIFY(dialog.testImageButton()->isEnabled());
    QCOMPARE(dialog.statusLabel()->text(), QStringLiteral("Ready"));
}

void UiWidgetTests::settingsDialogCurrentConfigIncludesCaptureMode() {
    AppConfig config;
    config.captureMode = ais::capture::CaptureMode::Standard;

    SettingsDialog dialog(config);
    QVERIFY(dialog.captureModeField() != nullptr);

    const int hdrCompatibleIndex =
        dialog.captureModeField()->findData(static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
    QVERIFY(hdrCompatibleIndex >= 0);

    dialog.captureModeField()->setCurrentIndex(hdrCompatibleIndex);

    const AppConfig current = dialog.currentConfig();
    QCOMPARE(static_cast<int>(current.captureMode),
             static_cast<int>(ais::capture::CaptureMode::HdrCompatible));
}

void UiWidgetTests::settingsDialogBusyStateLocksCaptureModeSelector() {
    SettingsDialog dialog(AppConfig{});
    QVERIFY(dialog.captureModeField() != nullptr);
    QVERIFY(dialog.captureModeField()->isEnabled());

    dialog.setBusy(true, QStringLiteral("Busy"));
    QVERIFY(!dialog.captureModeField()->isEnabled());

    dialog.setBusy(false, QStringLiteral("Ready"));
    QVERIFY(dialog.captureModeField()->isEnabled());
}

void UiWidgetTests::settingsDialogBusyStateLocksAllEditableControls() {
    SettingsDialog dialog(AppConfig{});

    dialog.setBusy(true, QStringLiteral("Busy"));

    QVERIFY(!dialog.baseUrlField()->isEnabled());
    QVERIFY(!dialog.apiKeyField()->isEnabled());
    QVERIFY(!dialog.modelField()->isEnabled());
    QVERIFY(!dialog.captureModeField()->isEnabled());
    QVERIFY(!dialog.themeField()->isEnabled());
    QVERIFY(!dialog.opacityField()->isEnabled());
    QVERIFY(!dialog.panelColorButton()->isEnabled());
    QVERIFY(!dialog.panelTextColorButton()->isEnabled());
    QVERIFY(!dialog.panelTextAutoButton()->isEnabled());
    QVERIFY(!dialog.panelBorderColorButton()->isEnabled());
    QVERIFY(!dialog.panelBorderAutoButton()->isEnabled());
    QVERIFY(!dialog.firstPromptField()->isEnabled());

    dialog.setBusy(false, QStringLiteral("Ready"));

    QVERIFY(dialog.baseUrlField()->isEnabled());
    QVERIFY(dialog.apiKeyField()->isEnabled());
    QVERIFY(dialog.modelField()->isEnabled());
    QVERIFY(dialog.captureModeField()->isEnabled());
    QVERIFY(dialog.themeField()->isEnabled());
    QVERIFY(dialog.opacityField()->isEnabled());
    QVERIFY(dialog.panelColorButton()->isEnabled());
    QVERIFY(dialog.panelTextColorButton()->isEnabled());
    QVERIFY(dialog.panelTextAutoButton()->isEnabled());
    QVERIFY(dialog.panelBorderColorButton()->isEnabled());
    QVERIFY(dialog.panelBorderAutoButton()->isEnabled());
    QVERIFY(dialog.firstPromptField()->isEnabled());
}

void UiWidgetTests::settingsDialogBusyStateLocksAppearanceControls() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.themeField()->isEnabled());
    QVERIFY(dialog.opacityField()->isEnabled());
    QVERIFY(dialog.panelColorButton()->isEnabled());
    QVERIFY(dialog.panelTextColorButton()->isEnabled());
    QVERIFY(dialog.panelTextAutoButton()->isEnabled());
    QVERIFY(dialog.panelBorderColorButton()->isEnabled());
    QVERIFY(dialog.panelBorderAutoButton()->isEnabled());

    dialog.setBusy(true, QStringLiteral("Busy"));

    QVERIFY(!dialog.themeField()->isEnabled());
    QVERIFY(!dialog.opacityField()->isEnabled());
    QVERIFY(!dialog.panelColorButton()->isEnabled());
    QVERIFY(!dialog.panelTextColorButton()->isEnabled());
    QVERIFY(!dialog.panelTextAutoButton()->isEnabled());
    QVERIFY(!dialog.panelBorderColorButton()->isEnabled());
    QVERIFY(!dialog.panelBorderAutoButton()->isEnabled());

    dialog.setBusy(false, QStringLiteral("Ready"));

    QVERIFY(dialog.themeField()->isEnabled());
    QVERIFY(dialog.opacityField()->isEnabled());
    QVERIFY(dialog.panelColorButton()->isEnabled());
    QVERIFY(dialog.panelTextColorButton()->isEnabled());
    QVERIFY(dialog.panelTextAutoButton()->isEnabled());
    QVERIFY(dialog.panelBorderColorButton()->isEnabled());
    QVERIFY(dialog.panelBorderAutoButton()->isEnabled());
}

void UiWidgetTests::settingsDialogAllowsEditingFirstPrompt() {
    AppConfig config;
    config.firstPrompt = QStringLiteral("请只看截图内容。");

    SettingsDialog dialog(config);

    QCOMPARE(dialog.firstPromptField()->toPlainText(), config.firstPrompt);

    dialog.firstPromptField()->setPlainText(QStringLiteral("请先总结，再给出建议。"));

    const AppConfig current = dialog.currentConfig();
    QCOMPARE(current.firstPrompt, QStringLiteral("请先总结，再给出建议。"));
}

void UiWidgetTests::settingsDialogShortcutFieldsCapturePressedKeys() {
    SettingsDialog dialog(AppConfig{});
    dialog.aiShortcutField()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+Q")));
    dialog.screenshotShortcutField()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+S")));

    QCOMPARE(dialog.currentConfig().aiShortcut, QStringLiteral("Ctrl+Alt+Q"));
    QCOMPARE(dialog.currentConfig().screenshotShortcut, QStringLiteral("Ctrl+Alt+S"));
}

void UiWidgetTests::settingsDialogAllowsChoosingPanelAndTextColors() {
    SettingsDialog dialog(AppConfig{});
    dialog.setPanelColor(QColor(QStringLiteral("#2a3340")));
    dialog.setPanelTextColor(QColor(QStringLiteral("#f8fafc")));

    QCOMPARE(dialog.currentConfig().panelColor, QStringLiteral("#2a3340"));
    QCOMPARE(dialog.currentConfig().panelTextColor, QStringLiteral("#f8fafc"));
    QVERIFY(dialog.panelColorButton()->text().contains(QStringLiteral("#2A3340"), Qt::CaseInsensitive));
    QVERIFY(dialog.panelTextColorButton()->text().contains(QStringLiteral("#F8FAFC"), Qt::CaseInsensitive));
}

void UiWidgetTests::settingsDialogAllowsChoosingPanelBorderColor() {
    SettingsDialog dialog(AppConfig{});
    dialog.setPanelBorderColor(QColor(QStringLiteral("#6b7280")));

    QCOMPARE(dialog.currentConfig().panelBorderColor, QStringLiteral("#6b7280"));
    QVERIFY(dialog.panelBorderColorButton()->text().contains(QStringLiteral("#6B7280"), Qt::CaseInsensitive));
}

void UiWidgetTests::settingsDialogCanRestoreAutomaticTextColor() {
    SettingsDialog dialog(AppConfig{});
    dialog.setPanelColor(QColor(QStringLiteral("#151a20")));
    dialog.setPanelTextColor(QColor(QStringLiteral("#ffd166")));

    QTest::mouseClick(dialog.panelTextAutoButton(), Qt::LeftButton);

    QCOMPARE(dialog.currentConfig().panelTextColor, QString());
    QVERIFY(dialog.panelTextColorButton()->text().contains(QStringLiteral("自动")));
}

void UiWidgetTests::settingsDialogCanRestoreAutomaticBorderColor() {
    SettingsDialog dialog(AppConfig{});
    dialog.setPanelColor(QColor(QStringLiteral("#151a20")));
    dialog.setPanelBorderColor(QColor(QStringLiteral("#ffd166")));

    QTest::mouseClick(dialog.panelBorderAutoButton(), Qt::LeftButton);

    QCOMPARE(dialog.currentConfig().panelBorderColor, QString());
    QVERIFY(dialog.panelBorderColorButton()->text().contains(QStringLiteral("自动")));
}

void UiWidgetTests::settingsDialogPersistsTransparentPanelColor() {
    SettingsDialog dialog(AppConfig{});
    QColor translucent(QStringLiteral("#80335577"));
    QVERIFY(translucent.isValid());

    dialog.setPanelColor(translucent);

    QCOMPARE(dialog.currentConfig().panelColor, QStringLiteral("#80335577"));
    QVERIFY(dialog.panelColorButton()->text().contains(QStringLiteral("#80335577"), Qt::CaseInsensitive));
}

void UiWidgetTests::settingsDialogPreviewReflectsLatestAppearanceChoices() {
    SettingsDialog dialog(AppConfig{});
    dialog.setPanelColor(QColor(QStringLiteral("#66223344")));
    dialog.setPanelTextColor(QColor(QStringLiteral("#f9fafb")));
    dialog.setPanelBorderColor(QColor(QStringLiteral("#6b7280")));
    dialog.opacityField()->setValue(55.0);
    dialog.applyAppearance(QStringLiteral("dark"));

    QVERIFY(dialog.previewSurface()->property("previewColor").toString().contains(QStringLiteral("#66223344"), Qt::CaseInsensitive));
    QVERIFY(dialog.previewSurface()->property("previewBorderColor").toString().contains(QStringLiteral("#6b7280"), Qt::CaseInsensitive));
    QVERIFY(dialog.previewSurface()->property("previewOpacity").toDouble() > 0.5);
    QVERIFY(dialog.previewTitleLabel()->styleSheet().contains(QStringLiteral("#f9fafb"), Qt::CaseInsensitive));
}

void UiWidgetTests::settingsDialogUsesScrollableCompactLayout() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.findChild<QScrollArea*>() != nullptr);
    QVERIFY(dialog.height() <= 560);
}

void UiWidgetTests::settingsDialogHasMinimumSizeToPreventOcclusion() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.minimumWidth() >= 520);
    QVERIFY(dialog.minimumWidth() <= 540);
    QVERIFY(dialog.minimumHeight() >= 560);
}

void UiWidgetTests::settingsDialogShortcutFieldsShareSingleRow() {
    SettingsDialog dialog(AppConfig{});
    dialog.show();
    QCoreApplication::processEvents();

    const int aiY = dialog.aiShortcutField()->mapTo(&dialog, QPoint(0, 0)).y();
    const int screenshotY = dialog.screenshotShortcutField()->mapTo(&dialog, QPoint(0, 0)).y();

    QVERIFY(qAbs(aiY - screenshotY) <= 4);
}

void UiWidgetTests::settingsDialogCanToggleLaunchAtLogin() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.launchAtLoginCheckBox() != nullptr);
    QVERIFY(!dialog.launchAtLoginCheckBox()->isChecked());

    dialog.launchAtLoginCheckBox()->setChecked(true);

    QVERIFY(dialog.currentConfig().launchAtLogin);
}

void UiWidgetTests::settingsDialogOpacityFieldUsesPercentForTransparency() {
    AppConfig config;
    config.opacity = 0.35;

    SettingsDialog dialog(config);

    QCOMPARE(dialog.opacityField()->suffix(), QStringLiteral("%"));
    QCOMPARE(qRound(dialog.opacityField()->value()), 35);

    dialog.opacityField()->setValue(20.0);
    QCOMPARE(dialog.currentConfig().opacity, 0.20);
}

void UiWidgetTests::settingsDialogOpacityFieldHidesStepperButtons() {
    SettingsDialog dialog(AppConfig{});

    QCOMPARE(dialog.opacityField()->buttonSymbols(), QAbstractSpinBox::NoButtons);
}

void UiWidgetTests::settingsDialogUsesRequestedDefaultAppearance() {
    SettingsDialog dialog(AppConfig{});
    const AppConfig current = dialog.currentConfig();

    QCOMPARE(qRound(dialog.opacityField()->value()), 95);
    QCOMPARE(current.opacity, 0.95);
    QCOMPARE(current.panelColor, QStringLiteral("#ffffff"));
    QCOMPARE(current.panelBorderColor, QStringLiteral("#000000"));
    QCOMPARE(current.panelTextColor, QString());
}

void UiWidgetTests::darkSettingsDialogUsesConsistentDarkSurface() {
    AppConfig config;
    config.theme = QStringLiteral("dark");

    SettingsDialog dialog(config);
    dialog.show();
    QCoreApplication::processEvents();

    QImage rendered(dialog.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    dialog.render(&rendered);

    const QColor corner = rendered.pixelColor(10, 10);
    QVERIFY2(corner.lightness() < 80,
             qPrintable(QStringLiteral("expected dark dialog background, got rgba(%1,%2,%3,%4)")
                            .arg(corner.red())
                            .arg(corner.green())
                            .arg(corner.blue())
                            .arg(corner.alpha())));
    QVERIFY(dialog.styleSheet().contains(QStringLiteral("QFrame#settingsCard")));
    QVERIFY(dialog.styleSheet().contains(QStringLiteral("QScrollArea")));
}

void UiWidgetTests::settingsDialogModelRowIncludesFetchAndTestActions() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.modelPopupButton() != nullptr);
    QVERIFY(dialog.fetchModelsButton() != nullptr);
    QVERIFY(dialog.testConnectionButton() != nullptr);
    QVERIFY(dialog.testImageButton() != nullptr);

    QSignalSpy fetchSpy(&dialog, &SettingsDialog::fetchModelsRequested);
    QSignalSpy textSpy(&dialog, &SettingsDialog::testConnectionRequested);
    QSignalSpy imageSpy(&dialog, &SettingsDialog::testImageUnderstandingRequested);

    QTest::mouseClick(dialog.fetchModelsButton(), Qt::LeftButton);
    QTest::mouseClick(dialog.testConnectionButton(), Qt::LeftButton);
    QTest::mouseClick(dialog.testImageButton(), Qt::LeftButton);

    QCOMPARE(fetchSpy.count(), 1);
    QCOMPARE(textSpy.count(), 1);
    QCOMPARE(imageSpy.count(), 1);
}

void UiWidgetTests::settingsDialogDefaultWidthIsNarrower() {
    SettingsDialog dialog(AppConfig{});

    QCOMPARE(dialog.width(), dialog.minimumWidth());
    QCOMPARE(dialog.width(), 520);
}

void UiWidgetTests::settingsDialogShowsDedicatedModelPopupButton() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.modelPopupButton() != nullptr);
    QVERIFY(dialog.modelPopupButton()->isVisibleTo(&dialog));
    QCOMPARE(dialog.modelPopupButton()->arrowType(), Qt::DownArrow);
    QVERIFY(dialog.modelPopupButton()->text().trimmed().isEmpty());
}

void UiWidgetTests::settingsDialogKeepsModelActionsOnSingleVisibleRowForLongModelNames() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QCoreApplication::processEvents();

    const int modelY = dialog.modelField()->mapTo(&dialog, QPoint(0, 0)).y();
    const int fetchY = dialog.fetchModelsButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int textY = dialog.testConnectionButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int imageY = dialog.testImageButton()->mapTo(&dialog, QPoint(0, 0)).y();

    QVERIFY(qAbs(modelY - fetchY) <= 6);
    QVERIFY(qAbs(fetchY - textY) <= 6);
    QVERIFY(qAbs(textY - imageY) <= 6);

    const int imageRight =
        dialog.testImageButton()->mapTo(&dialog, QPoint(dialog.testImageButton()->width(), 0)).x();
    QVERIFY2(imageRight <= dialog.width() - 8,
             qPrintable(QStringLiteral("model action row overflowed dialog width: %1 > %2")
                            .arg(imageRight)
                            .arg(dialog.width() - 8)));
}

void UiWidgetTests::settingsDialogBusyStatePreservesScrollPosition() {
    SettingsDialog dialog(AppConfig{});
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);

    scrollArea->verticalScrollBar()->setValue(0);
    dialog.fetchModelsButton()->setFocus();
    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);

    dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                         QStringLiteral("正在获取模型列表…"));
    dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));
    QCoreApplication::processEvents();

    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);
}

void UiWidgetTests::settingsDialogFetchClickKeepsScrollPositionThroughAsyncUpdates() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);

    scrollArea->verticalScrollBar()->setValue(0);
    QCoreApplication::processEvents();
    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);

    connect(&dialog,
            &SettingsDialog::fetchModelsRequested,
            &dialog,
            [&dialog]() {
                dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                                     QStringLiteral("正在获取模型列表…"));
                dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));
                QTimer::singleShot(0, &dialog, [&dialog]() {
                    dialog.setAvailableModels({QStringLiteral("gpt-4.1-mini"),
                                               QStringLiteral("gpt-4o")});
                    dialog.setBusy(false, QStringLiteral("已获取 2 个模型，可直接选择。"));
                });
            });

    QTest::mouseClick(dialog.fetchModelsButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QTest::qWait(60);

    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);
}

void UiWidgetTests::settingsDialogBusyStateClearsFocusedModelEditorWithoutScrollJump() {
    SettingsDialog dialog(AppConfig{});
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);
    QVERIFY(dialog.modelField() != nullptr);
    QVERIFY(dialog.modelField()->lineEdit() != nullptr);

    scrollArea->verticalScrollBar()->setValue(0);
    dialog.modelField()->lineEdit()->setFocus();
    QCoreApplication::processEvents();
    QVERIFY(dialog.modelField()->lineEdit()->hasFocus());

    dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                         QStringLiteral("正在获取模型列表…"));
    dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));
    QCoreApplication::processEvents();
    QTest::qWait(30);

    QVERIFY(!dialog.modelField()->lineEdit()->hasFocus());
    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);
}

void UiWidgetTests::settingsDialogFetchActionKeepsOriginalScrollAnchorAfterMidFlightJump() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);

    scrollArea->verticalScrollBar()->setValue(0);
    QCoreApplication::processEvents();

    connect(&dialog,
            &SettingsDialog::fetchModelsRequested,
            &dialog,
            [&dialog, scrollArea]() {
                dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                                     QStringLiteral("正在获取模型列表…"));
                dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));

                QTimer::singleShot(20, &dialog, [scrollArea]() {
                    scrollArea->verticalScrollBar()->setValue(260);
                });

                QTimer::singleShot(40, &dialog, [&dialog]() {
                    dialog.setAvailableModels({QStringLiteral("gpt-4.1-mini"),
                                               QStringLiteral("gpt-4o")});
                    dialog.setBusy(false, QStringLiteral("已获取 2 个模型，可直接选择。"));
                });
            });

    QTest::mouseClick(dialog.fetchModelsButton(), Qt::LeftButton);
    QTest::qWait(140);

    QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);
}

void UiWidgetTests::settingsDialogActionButtonsKeepScrollFixedDuringAsyncWork() {
    struct ActionCase {
        QString name;
        SettingsDialog::ActionMode mode;
        std::function<QPushButton*(SettingsDialog&)> button;
        std::function<void(SettingsDialog&, std::function<void()>)> connectAction;
        QString runningStatus;
        QString completedStatus;
    };

    const QList<ActionCase> actionCases{
        ActionCase{
            .name = QStringLiteral("fetch"),
            .mode = SettingsDialog::ActionMode::FetchModels,
            .button = [](SettingsDialog& dialog) { return dialog.fetchModelsButton(); },
            .connectAction =
                [](SettingsDialog& dialog, std::function<void()> handler) {
                    QObject::connect(&dialog, &SettingsDialog::fetchModelsRequested, &dialog, std::move(handler));
                },
            .runningStatus = QStringLiteral("正在获取模型列表…"),
            .completedStatus = QStringLiteral("已获取 2 个模型，可直接选择。"),
        },
        ActionCase{
            .name = QStringLiteral("text"),
            .mode = SettingsDialog::ActionMode::TestText,
            .button = [](SettingsDialog& dialog) { return dialog.testConnectionButton(); },
            .connectAction =
                [](SettingsDialog& dialog, std::function<void()> handler) {
                    QObject::connect(&dialog, &SettingsDialog::testConnectionRequested, &dialog, std::move(handler));
                },
            .runningStatus = QStringLiteral("正在测试文字连接…"),
            .completedStatus = QStringLiteral("文字测试通过。"),
        },
        ActionCase{
            .name = QStringLiteral("image"),
            .mode = SettingsDialog::ActionMode::TestImage,
            .button = [](SettingsDialog& dialog) { return dialog.testImageButton(); },
            .connectAction =
                [](SettingsDialog& dialog, std::function<void()> handler) {
                    QObject::connect(&dialog,
                                     &SettingsDialog::testImageUnderstandingRequested,
                                     &dialog,
                                     std::move(handler));
                },
            .runningStatus = QStringLiteral("正在测试图片理解…"),
            .completedStatus = QStringLiteral("图片测试通过。"),
        },
    };

    for (const ActionCase& actionCase : actionCases) {
        AppConfig config;
        config.activeProfile.model =
            QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

        SettingsDialog dialog(config);
        dialog.resize(540, 560);
        dialog.show();
        QVERIFY2(QTest::qWaitForWindowExposed(&dialog), qPrintable(actionCase.name));

        auto* scrollArea = dialog.findChild<QScrollArea*>();
        QVERIFY(scrollArea != nullptr);
        QVERIFY(scrollArea->verticalScrollBar() != nullptr);

        int valueDuringAction = -1;
        scrollArea->verticalScrollBar()->setValue(0);
        QCoreApplication::processEvents();

        actionCase.connectAction(
            dialog,
            [&dialog, scrollArea, &valueDuringAction, actionCase]() {
                dialog.setActionMode(actionCase.mode, actionCase.runningStatus);
                dialog.setBusy(true, actionCase.runningStatus);

                QTimer::singleShot(20, &dialog, [scrollArea, &valueDuringAction]() {
                    scrollArea->verticalScrollBar()->setValue(260);
                    valueDuringAction = scrollArea->verticalScrollBar()->value();
                });

                QTimer::singleShot(40, &dialog, [&dialog, actionCase]() {
                    if (actionCase.mode == SettingsDialog::ActionMode::FetchModels) {
                        dialog.setAvailableModels({QStringLiteral("gpt-4.1-mini"),
                                                   QStringLiteral("gpt-4o")});
                    }
                    dialog.setBusy(false, actionCase.completedStatus);
                });
            });

        QTest::mouseClick(actionCase.button(dialog), Qt::LeftButton);
        QTest::qWait(80);

        QCOMPARE(valueDuringAction, 0);
        QCOMPARE(scrollArea->verticalScrollBar()->value(), 0);
    }
}

void UiWidgetTests::settingsDialogActionStartKeepsVisibleSectionAtSameViewportOffset() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->viewport() != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);
    QVERIFY(dialog.previewSurface() != nullptr);

    scrollArea->verticalScrollBar()->setValue(120);
    QCoreApplication::processEvents();

    const int previewTopBefore =
        dialog.previewSurface()->mapTo(scrollArea->viewport(), QPoint(0, 0)).y();

    connect(&dialog,
            &SettingsDialog::fetchModelsRequested,
            &dialog,
            [&dialog]() {
                dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                                     QStringLiteral("正在获取模型列表…"));
                dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));
            });

    QTest::mouseClick(dialog.fetchModelsButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QTest::qWait(30);

    const int previewTopAfter =
        dialog.previewSurface()->mapTo(scrollArea->viewport(), QPoint(0, 0)).y();

    QCOMPARE(scrollArea->verticalScrollBar()->value(), 120);
    QCOMPARE(previewTopAfter, previewTopBefore);
}

void UiWidgetTests::settingsDialogLongCompletionStatusDoesNotShiftVisibleSection() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->viewport() != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);
    QVERIFY(dialog.previewSurface() != nullptr);

    scrollArea->verticalScrollBar()->setValue(120);
    QCoreApplication::processEvents();

    const int previewTopBefore =
        dialog.previewSurface()->mapTo(scrollArea->viewport(), QPoint(0, 0)).y();

    connect(&dialog,
            &SettingsDialog::fetchModelsRequested,
            &dialog,
            [&dialog]() {
                dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                                     QStringLiteral("正在获取模型列表…"));
                dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));
                QTimer::singleShot(0, &dialog, [&dialog]() {
                    dialog.setBusy(false,
                                   QStringLiteral("测试失败：服务当前限流（HTTP 429），请稍后再试，或更换模型 / 线路。"));
                });
            });

    QTest::mouseClick(dialog.fetchModelsButton(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QTest::qWait(140);

    const int previewTopAfter =
        dialog.previewSurface()->mapTo(scrollArea->viewport(), QPoint(0, 0)).y();

    QCOMPARE(scrollArea->verticalScrollBar()->value(), 120);
    QCOMPARE(previewTopAfter, previewTopBefore);
}

void UiWidgetTests::settingsDialogLongCompletionStatusDoesNotPushScrollViewportDown() {
    AppConfig config;
    config.activeProfile.model =
        QStringLiteral("qwen3.5-omni-plus-thinking-search-super-long-model-name-for-layout-regression-check");

    SettingsDialog dialog(config);
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->viewport() != nullptr);
    QVERIFY(dialog.previewSurface() != nullptr);

    const int viewportTopBefore = scrollArea->mapTo(&dialog, QPoint(0, 0)).y();
    const int previewTopBefore = dialog.previewSurface()->mapTo(&dialog, QPoint(0, 0)).y();

    dialog.setBusy(false,
                   QStringLiteral("测试失败：服务当前限流（HTTP 429），请稍后再试，或更换模型 / 线路。"));
    QCoreApplication::processEvents();
    QTest::qWait(60);

    const int viewportTopAfter = scrollArea->mapTo(&dialog, QPoint(0, 0)).y();
    const int previewTopAfter = dialog.previewSurface()->mapTo(&dialog, QPoint(0, 0)).y();

    QCOMPARE(viewportTopAfter, viewportTopBefore);
    QCOMPARE(previewTopAfter, previewTopBefore);
}

void UiWidgetTests::settingsDialogProviderActionsRemainPinnedWhenSettingsContentScrolls() {
    SettingsDialog dialog(AppConfig{});
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);
    QVERIFY(dialog.fetchModelsButton() != nullptr);
    QVERIFY(dialog.testConnectionButton() != nullptr);
    QVERIFY(dialog.testImageButton() != nullptr);

    const int fetchTopBefore = dialog.fetchModelsButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int textTopBefore = dialog.testConnectionButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int imageTopBefore = dialog.testImageButton()->mapTo(&dialog, QPoint(0, 0)).y();

    scrollArea->verticalScrollBar()->setValue(140);
    QCoreApplication::processEvents();

    const int fetchTopAfter = dialog.fetchModelsButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int textTopAfter = dialog.testConnectionButton()->mapTo(&dialog, QPoint(0, 0)).y();
    const int imageTopAfter = dialog.testImageButton()->mapTo(&dialog, QPoint(0, 0)).y();

    QCOMPARE(fetchTopAfter, fetchTopBefore);
    QCOMPARE(textTopAfter, textTopBefore);
    QCOMPARE(imageTopAfter, imageTopBefore);
}

void UiWidgetTests::settingsDialogProviderFieldsStillScrollBeneathPinnedActionBar() {
    SettingsDialog dialog(AppConfig{});
    dialog.resize(540, 560);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* scrollArea = dialog.findChild<QScrollArea*>();
    QVERIFY(scrollArea != nullptr);
    QVERIFY(scrollArea->verticalScrollBar() != nullptr);
    QVERIFY(dialog.protocolSelector() != nullptr);
    QVERIFY(dialog.fetchModelsButton() != nullptr);

    const int protocolTopBefore = dialog.protocolSelector()->mapTo(&dialog, QPoint(0, 0)).y();
    const int fetchTopBefore = dialog.fetchModelsButton()->mapTo(&dialog, QPoint(0, 0)).y();

    scrollArea->verticalScrollBar()->setValue(140);
    QCoreApplication::processEvents();

    const int protocolTopAfter = dialog.protocolSelector()->mapTo(&dialog, QPoint(0, 0)).y();
    const int fetchTopAfter = dialog.fetchModelsButton()->mapTo(&dialog, QPoint(0, 0)).y();

    QVERIFY(protocolTopAfter < protocolTopBefore);
    QCOMPARE(fetchTopAfter, fetchTopBefore);
}

void UiWidgetTests::settingsDialogHidesDuplicateProviderStatusRow() {
    SettingsDialog dialog(AppConfig{});
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QVERIFY(dialog.statusLabel() != nullptr);
    QVERIFY(dialog.modelActionStatusLabel() != nullptr);

    QVERIFY(!dialog.statusLabel()->isVisibleTo(&dialog));
    QVERIFY(dialog.modelActionStatusLabel()->isVisibleTo(&dialog));
}

void UiWidgetTests::settingsDialogActionStatusUsesSingleCompactLine() {
    SettingsDialog dialog(AppConfig{});
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QVERIFY(dialog.modelActionStatusLabel() != nullptr);
    QVERIFY(!dialog.modelActionStatusLabel()->wordWrap());
    QVERIFY(dialog.modelActionStatusLabel()->maximumHeight() <=
            dialog.modelActionStatusLabel()->fontMetrics().lineSpacing() + 8);
}

void UiWidgetTests::settingsDialogShowsExplicitModelActionFeedback() {
    SettingsDialog dialog(AppConfig{});

    dialog.setActionMode(SettingsDialog::ActionMode::FetchModels,
                         QStringLiteral("正在获取模型列表…"));
    dialog.setBusy(true, QStringLiteral("正在获取模型列表…"));

    QCOMPARE(dialog.fetchModelsButton()->text(), QStringLiteral("获取中…"));
    QCOMPARE(dialog.modelActionStatusLabel()->text(), QStringLiteral("正在获取模型列表…"));

    dialog.setBusy(false, QStringLiteral("已获取 3 个模型"));

    QCOMPARE(dialog.fetchModelsButton()->text(), QStringLiteral("获取模型"));
    QCOMPARE(dialog.modelActionStatusLabel()->text(), QStringLiteral("已获取 3 个模型"));
    QVERIFY(dialog.fetchModelsButton()->isEnabled());
}

void UiWidgetTests::settingsDialogCanApplyFetchedModelChoices() {
    AppConfig config;
    config.activeProfile.model = QStringLiteral("custom-model");

    SettingsDialog dialog(config);
    dialog.setAvailableModels({QStringLiteral("gpt-4.1-mini"),
                               QStringLiteral("gpt-4o"),
                               QStringLiteral("gpt-4.1-mini")});

    QCOMPARE(dialog.modelField()->count(), 2);
    QCOMPARE(dialog.modelField()->itemText(0), QStringLiteral("gpt-4.1-mini"));
    QCOMPARE(dialog.modelField()->itemText(1), QStringLiteral("gpt-4o"));
    QCOMPARE(dialog.modelField()->currentText(), QStringLiteral("custom-model"));
}

void UiWidgetTests::settingsDialogPreviewUsesChatLikeMock() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.previewHistoryView() != nullptr);
    QVERIFY(dialog.previewInputPreviewField() != nullptr);
    QVERIFY(dialog.previewSendButton() != nullptr);
    QCOMPARE(dialog.previewSendButton()->text(), QStringLiteral("↑"));
    QVERIFY(dialog.previewHistoryView()->toPlainText().contains(QStringLiteral("示例回答")));
    QVERIFY(dialog.previewHistoryView()->toPlainText().contains(QStringLiteral("const int answer = 42;")));
    QVERIFY(dialog.previewInputPreviewField()->placeholderText().contains(QStringLiteral("继续追问")));
}

void UiWidgetTests::settingsDialogPreviewWidgetsDoNotTakeFocus() {
    SettingsDialog dialog(AppConfig{});

    QVERIFY(dialog.previewHistoryView() != nullptr);
    QVERIFY(dialog.previewInputPreviewField() != nullptr);
    QVERIFY(dialog.previewSendButton() != nullptr);

    QCOMPARE(dialog.previewHistoryView()->focusPolicy(), Qt::NoFocus);
    QCOMPARE(dialog.previewInputPreviewField()->focusPolicy(), Qt::NoFocus);
    QCOMPARE(dialog.previewSendButton()->focusPolicy(), Qt::NoFocus);
}

void UiWidgetTests::settingsDialogRestoresSavedSize() {
    AppConfig config;
    config.settingsDialogSize = QSize(660, 620);

    SettingsDialog dialog(config);

    QCOMPARE(dialog.size(), QSize(660, 620));
}

void UiWidgetTests::settingsDialogRestoresSavedPosition() {
    AppConfig config;
    config.settingsDialogPosition = QPoint(180, 140);

    SettingsDialog dialog(config);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QCoreApplication::processEvents();

    QCOMPARE(dialog.pos(), QPoint(180, 140));
}

void UiWidgetTests::settingsDialogPreviewComposerMatchesFloatingChatLayout() {
    SettingsDialog dialog(AppConfig{});
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QCoreApplication::processEvents();

    QVERIFY(dialog.previewComposerShell() != nullptr);
    QVERIFY(dialog.previewInputPreviewField() != nullptr);
    QVERIFY(dialog.previewSendButton() != nullptr);
    QCOMPARE(dialog.previewInputPreviewField()->parentWidget(), dialog.previewComposerShell());
    QCOMPARE(dialog.previewSendButton()->parentWidget(), dialog.previewComposerShell());
    QVERIFY(dialog.previewInputPreviewField()->styleSheet().contains(QStringLiteral("background: transparent")));
    QVERIFY(dialog.previewSendButton()->styleSheet().contains(QStringLiteral("border: none")));
}

void UiWidgetTests::settingsDialogSaveKeepsDialogOpenAndEmitsSignal() {
    SettingsDialog dialog(AppConfig{});
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QSignalSpy saveSpy(&dialog, SIGNAL(saveRequested()));
    auto* buttonBox = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    QPushButton* const saveButton = buttonBox->button(QDialogButtonBox::Save);
    QVERIFY(saveButton != nullptr);

    QTest::mouseClick(saveButton, Qt::LeftButton);

    QCOMPARE(saveSpy.count(), 1);
    QVERIFY(dialog.isVisible());
    QCOMPARE(dialog.result(), 0);
}

void UiWidgetTests::bindSessionRendersCurrentHistory() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->addUserText(QStringLiteral("What is this?"));
    session->addAssistantText(QStringLiteral("A widget test."));

    FloatingChatPanel panel;
    panel.bindSession(session);

    const QString history = panel.historyView()->toPlainText();
    QVERIFY(history.contains(QStringLiteral("What is this?")));
    QVERIFY(history.contains(QStringLiteral("A widget test.")));
}

void UiWidgetTests::defaultPanelWindowSizeIsCompact() {
    FloatingChatPanel panel;

    QCOMPARE(panel.width(), panel.minimumWidth());
    QCOMPARE(panel.width(), 360);
    QVERIFY2(panel.height() <= 660, qPrintable(QStringLiteral("unexpected height: %1").arg(panel.height())));
}

void UiWidgetTests::floatingPanelCanRestoreSavedSize() {
    FloatingChatPanel panel;

    panel.restoreSavedSize(QSize(620, 540));

    QCOMPARE(panel.size(), QSize(620, 540));
}

void UiWidgetTests::floatingPanelUsesFramelessTransparentWindow() {
    FloatingChatPanel panel;

    QVERIFY(panel.windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(panel.testAttribute(Qt::WA_TranslucentBackground));
}

void UiWidgetTests::floatingPanelAppearanceUsesCustomPanelColor() {
    FloatingChatPanel panel;
    panel.applyAppearance(QStringLiteral("dark"),
                          0.66,
                          QStringLiteral("#223344"),
                          QStringLiteral("#f8fafc"),
                          QStringLiteral("#6b7280"));
    panel.show();
    QCoreApplication::processEvents();

    QCOMPARE(panel.property("panelSurfaceColor").toString(), QStringLiteral("#223344"));
    QVERIFY(panel.styleSheet().contains(QStringLiteral("#f8fafc"), Qt::CaseInsensitive));
    QVERIFY(panel.property("panelBorderColor").toString().contains(QStringLiteral("#6b7280"), Qt::CaseInsensitive));

    QImage rendered(panel.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    panel.render(&rendered);

    const QColor sample = rendered.pixelColor(40, 40);
    QVERIFY(sample.alpha() > 0);
    QVERIFY(qAbs(sample.red() - 34) <= 20);
    QVERIFY(qAbs(sample.green() - 51) <= 20);
    QVERIFY(qAbs(sample.blue() - 68) <= 20);
}

void UiWidgetTests::floatingPanelAppearanceKeepsSurfaceAndBorderProperties() {
    FloatingChatPanel panel;

    panel.applyAppearance(QStringLiteral("dark"),
                          0.72,
                          QStringLiteral("#80335577"),
                          QString(),
                          QString());

    const QString firstSurface = panel.property("panelSurfaceColor").toString();
    const QString firstBorder = panel.property("panelBorderColor").toString();
    QCOMPARE(firstSurface, QStringLiteral("#80335577"));
    QVERIFY(!firstBorder.trimmed().isEmpty());

    panel.applyAppearance(QStringLiteral("light"),
                          0.66,
                          QStringLiteral("#223344"),
                          QStringLiteral("#f8fafc"),
                          QStringLiteral("#6b7280"));

    QCOMPARE(panel.property("panelSurfaceColor").toString(), QStringLiteral("#223344"));
    QCOMPARE(panel.property("panelBorderColor").toString(), QStringLiteral("#6b7280"));
}

void UiWidgetTests::escapeShortcutClosesFloatingPanel() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->addAssistantText(QStringLiteral("Done"));

    FloatingChatPanel panel;
    panel.bindSession(session);
    panel.show();
    panel.followUpInput()->setFocus();
    QCoreApplication::processEvents();

    QVERIFY(panel.isVisible());

    QTest::keyClick(panel.followUpInput(), Qt::Key_Escape);
    QCoreApplication::processEvents();

    QVERIFY(!panel.isVisible());
}

void UiWidgetTests::closeButtonHoverDoesNotShowResizeCursor() {
    FloatingChatPanel panel;
    panel.resize(560, 560);
    panel.show();
    QCoreApplication::processEvents();

    panel.setCursor(Qt::SizeHorCursor);

    QVERIFY(panel.closeButton() != nullptr);
    QTest::mouseMove(panel.closeButton(), panel.closeButton()->rect().center());
    QCoreApplication::processEvents();

    QCOMPARE(panel.closeButton()->cursor().shape(), Qt::PointingHandCursor);
}

void UiWidgetTests::floatingPanelCanBeDraggedByHeader() {
    FloatingChatPanel panel;
    panel.move(120, 100);
    panel.show();
    QCoreApplication::processEvents();

    const QPoint originalPos = panel.pos();
    const QPoint localPress(24, 8);
    const QPoint globalPress = panel.mapToGlobal(localPress);
    const QPoint globalMove = globalPress + QPoint(80, 36);

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           QPointF(localPress),
                           QPointF(globalPress),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&panel, &pressEvent);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(panel.mapFromGlobal(globalMove)),
                          QPointF(globalMove),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(&panel, &moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(panel.mapFromGlobal(globalMove)),
                             QPointF(globalMove),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QApplication::sendEvent(&panel, &releaseEvent);

    QVERIFY(panel.pos() != originalPos);
}

void UiWidgetTests::floatingPanelCanResizeHorizontallyFromLeftEdge() {
    FloatingChatPanel panel;
    panel.resize(560, 560);
    panel.move(240, 100);
    panel.show();
    QCoreApplication::processEvents();

    const int originalWidth = panel.width();
    const QPoint localPress(2, panel.height() / 2);
    const QPoint globalPress = panel.mapToGlobal(localPress);
    const QPoint globalMove = globalPress + QPoint(120, 0);

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           QPointF(localPress),
                           QPointF(globalPress),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&panel, &pressEvent);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(panel.mapFromGlobal(globalMove)),
                          QPointF(globalMove),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(&panel, &moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(panel.mapFromGlobal(globalMove)),
                             QPointF(globalMove),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QApplication::sendEvent(&panel, &releaseEvent);

    QVERIFY(panel.width() < originalWidth);
    QVERIFY(panel.width() >= 360);
}

void UiWidgetTests::floatingPanelCanResizeHorizontallyFromRightEdge() {
    FloatingChatPanel panel;
    panel.resize(560, 560);
    panel.move(120, 100);
    panel.show();
    QCoreApplication::processEvents();

    const int originalWidth = panel.width();
    const QPoint localPress(panel.width() - 3, panel.height() / 2);
    const QPoint globalPress = panel.mapToGlobal(localPress);
    const QPoint globalMove = globalPress + QPoint(120, 0);

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           QPointF(localPress),
                           QPointF(globalPress),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&panel, &pressEvent);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(panel.mapFromGlobal(globalMove)),
                          QPointF(globalMove),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(&panel, &moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(panel.mapFromGlobal(globalMove)),
                             QPointF(globalMove),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QApplication::sendEvent(&panel, &releaseEvent);

    QVERIFY(panel.width() > originalWidth);
    QVERIFY(panel.width() <= 1280);
}

void UiWidgetTests::floatingPanelResizeHotZoneWorksFromChildViewport() {
    FloatingChatPanel panel;
    panel.resize(560, 560);
    panel.move(120, 100);
    panel.show();
    QCoreApplication::processEvents();

    QWidget* viewport = panel.historyView()->viewport();
    QVERIFY(viewport != nullptr);

    const int originalWidth = panel.width();
    const QPoint localPress(viewport->width() - 2, viewport->height() / 2);
    const QPoint globalPress = viewport->mapToGlobal(localPress);
    const QPoint globalMove = globalPress + QPoint(100, 0);

    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           QPointF(localPress),
                           QPointF(globalPress),
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QApplication::sendEvent(viewport, &pressEvent);

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(viewport->mapFromGlobal(globalMove)),
                          QPointF(globalMove),
                          Qt::NoButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(viewport, &moveEvent);

    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             QPointF(viewport->mapFromGlobal(globalMove)),
                             QPointF(globalMove),
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QApplication::sendEvent(viewport, &releaseEvent);

    QVERIFY(panel.width() > originalWidth);
}

void UiWidgetTests::statusBarUsesCompactHeight() {
    FloatingChatPanel panel;

    QVERIFY(panel.statusLabel()->maximumHeight() <= 18);
    QVERIFY(panel.statusLabel()->minimumHeight() <= 18);
}

void UiWidgetTests::chatInputRowUsesChatLikeControls() {
    FloatingChatPanel panel;

    QVERIFY(panel.followUpInput()->minimumHeight() >= 30);
    QVERIFY(panel.sendButton()->maximumWidth() <= 30);
    QCOMPARE(panel.sendButton()->text(), QStringLiteral("↑"));
}

void UiWidgetTests::chatPanelRemovesFooterActionButtons() {
    FloatingChatPanel panel;
    const QList<QPushButton*> buttons = panel.findChildren<QPushButton*>();

    bool foundRecapture = false;
    bool foundBottomClose = false;
    for (QPushButton* button : buttons) {
        if (button == nullptr) {
            continue;
        }
        foundRecapture = foundRecapture || button->text() == QStringLiteral("重新截图");
        foundBottomClose = foundBottomClose || button->text() == QStringLiteral("关闭");
    }

    QVERIFY(!foundRecapture);
    QVERIFY(!foundBottomClose);
}

void UiWidgetTests::chatPanelShowsMinimizeButton() {
    FloatingChatPanel panel;

    QVERIFY(panel.minimizeButton() != nullptr);
    QCOMPARE(panel.minimizeButton()->text(), QStringLiteral("−"));
}

void UiWidgetTests::minimizeButtonHidesPanelWithoutEndingSession() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->addAssistantText(QStringLiteral("Done"));

    FloatingChatPanel panel;
    panel.bindSession(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    QSignalSpy dismissedSpy(&panel, &FloatingChatPanel::panelDismissed);

    QTest::mouseClick(panel.minimizeButton(), Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(!panel.isVisible());
    QCOMPARE(dismissedSpy.count(), 0);
}

void UiWidgetTests::reasoningPanelStartsCollapsedAndCanExpand() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->beginAssistantResponse();
    session->appendAssistantReasoningDelta(QStringLiteral("Thinking about the screenshot"));
    session->appendAssistantTextDelta(QStringLiteral("Final answer"));
    session->finalizeAssistantResponse();

    FloatingChatPanel panel;
    panel.bindSession(session);
    panel.show();
    QCoreApplication::processEvents();

    QVERIFY(panel.reasoningToggleButton()->isVisible());
    QVERIFY(!panel.reasoningView()->isVisible());
    QCOMPARE(panel.reasoningToggleButton()->text(), QStringLiteral("展开思考"));

    QTest::mouseClick(panel.reasoningToggleButton(), Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(panel.reasoningView()->isVisible());
    QVERIFY(panel.reasoningView()->toPlainText().contains(QStringLiteral("Thinking about the screenshot")));
    QCOMPARE(panel.reasoningToggleButton()->text(), QStringLiteral("收起思考"));
}

void UiWidgetTests::reasoningPanelUsesScrollableCompactHeight() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->beginAssistantResponse();
    session->appendAssistantReasoningDelta(QStringLiteral(
        "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10"));
    session->appendAssistantTextDelta(QStringLiteral("done"));
    session->finalizeAssistantResponse();

    FloatingChatPanel panel;
    panel.resize(860, 720);
    panel.bindSession(session);
    panel.show();
    QCoreApplication::processEvents();

    QTest::mouseClick(panel.reasoningToggleButton(), Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(panel.reasoningView()->maximumHeight() <= 160);
    QVERIFY(panel.reasoningView()->height() <= 160);
}

void UiWidgetTests::assistantMarkdownRendersWithoutLiteralFenceMarkers() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->addAssistantText(QStringLiteral(
        "### Example\n\n"
        "```cpp\n"
        "const int answer = 42;\n"
        "```\n"
        "\nUse `answer` directly."));

    FloatingChatPanel panel;
    panel.bindSession(session);

    const QString historyText = panel.historyView()->toPlainText();

    QVERIFY(historyText.contains(QStringLiteral("Example")));
    QVERIFY(historyText.contains(QStringLiteral("const int answer = 42;")));
    QVERIFY(historyText.contains(QStringLiteral("Use answer directly.")));
    QVERIFY(!historyText.contains(QStringLiteral("```")));
    QVERIFY(historyText.contains(QStringLiteral("Copy")));
}

void UiWidgetTests::historyCssUsesDedicatedCodeBodyContainer() {
    const QString css = ais::ui::floating_chat_panel_helpers::historyDocumentCss(
        QStringLiteral("light"),
        QColor(QStringLiteral("#ffffff")),
        QColor(QStringLiteral("#15181d")),
        QColor(QStringLiteral("#5f6b7a")),
        QColor(QStringLiteral("#000000")),
        242);

    QVERIFY(css.contains(QStringLiteral(".code-body")));
    QVERIFY(css.contains(QStringLiteral("border-top")));
}

void UiWidgetTests::assistantMarkdownLinksOpenThroughDesktopServices() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->addAssistantText(QStringLiteral("来源参考 [1](https://example.com/report)"));

    FloatingChatPanel panel;
    panel.resize(640, 520);
    panel.bindSession(session);
    panel.show();
    QCoreApplication::processEvents();

    QVERIFY(panel.historyView()->toHtml().contains(QStringLiteral("https://example.com/report")));
    QUrl openedUrl;
    int openCount = 0;
    panel.setExternalUrlOpenerForTest([&](const QUrl& url) {
        openedUrl = url;
        openCount += 1;
        return true;
    });
    panel.activateLinkForTest(QUrl(QStringLiteral("https://example.com/report")));

    QCOMPARE(openCount, 1);
    QCOMPARE(openedUrl, QUrl(QStringLiteral("https://example.com/report")));
    QCOMPARE(panel.statusLabel()->text(), QStringLiteral("已打开链接"));
}

void UiWidgetTests::streamingBadgeRequiresVisibleAssistantText() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->beginAssistantResponse();
    session->appendAssistantReasoningDelta(QStringLiteral("正在思考截图内容"));

    FloatingChatPanel panel;
    panel.bindSession(session);

    const QString historyHtml = panel.historyView()->toHtml();
    QVERIFY(!historyHtml.contains(QStringLiteral("流式输出中")));
}

void UiWidgetTests::streamingBadgeAppearsAfterAssistantTextStarts() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    session->beginAssistantResponse();
    session->appendAssistantReasoningDelta(QStringLiteral("正在思考截图内容"));
    session->appendAssistantTextDelta(QStringLiteral("这是可见回答"));

    FloatingChatPanel panel;
    panel.bindSession(session);

    const QString historyHtml = panel.historyView()->toHtml();
    QVERIFY(historyHtml.contains(QStringLiteral("流式输出中")));
}

void UiWidgetTests::floatingPanelKeepsManualScrollPositionWhileStreaming() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));
    for (int index = 0; index < 18; ++index) {
        session->addUserText(QStringLiteral("Question %1").arg(index));
        session->addAssistantText(QStringLiteral("Answer %1\nline a\nline b\nline c\nline d").arg(index));
    }

    FloatingChatPanel panel;
    panel.resize(360, 360);
    panel.bindSession(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QCoreApplication::processEvents();

    QScrollBar* const scrollBar = panel.historyView()->verticalScrollBar();
    QVERIFY(scrollBar != nullptr);
    QTRY_VERIFY(scrollBar->maximum() > 0);

    scrollBar->setValue(scrollBar->maximum() / 2);
    const int preservedValue = scrollBar->value();
    QVERIFY(preservedValue < scrollBar->maximum());

    session->beginAssistantResponse();
    session->appendAssistantTextDelta(QStringLiteral("流式新增内容"));
    panel.scheduleSessionRefresh();

    QTRY_VERIFY(scrollBar->value() <= preservedValue + 8);
    QVERIFY(scrollBar->value() < scrollBar->maximum());
}

void UiWidgetTests::floatingPanelScrollbarTintTracksPanelSurface() {
    FloatingChatPanel panel;
    panel.applyAppearance(QStringLiteral("light"),
                          0.95,
                          QStringLiteral("#ffffff"),
                          QString(),
                          QStringLiteral("#000000"));

    const QRegularExpression handlePattern(
        QStringLiteral(R"(QScrollBar::handle:vertical\s*\{\s*background:\s*([^;]+);)"));
    const QRegularExpressionMatch match = handlePattern.match(panel.styleSheet());
    QVERIFY(match.hasMatch());

    const QString handleColor = match.captured(1);
    QVERIFY(handleColor.startsWith(QStringLiteral("rgba(")));
    QVERIFY(!handleColor.contains(QStringLiteral("0,0,0")));
}

void UiWidgetTests::busyChatPanelKeepsInputEditableWhileRequestIsRunning() {
    auto session = std::make_shared<ChatSession>();
    session->beginWithCapture(QByteArray("png-image"));

    FloatingChatPanel panel;
    panel.bindSession(session);

    QVERIFY(panel.followUpInput()->isEnabled());
    QVERIFY(panel.sendButton()->isEnabled());

    panel.setBusy(true, QStringLiteral("Waiting for AI..."));

    QVERIFY(panel.followUpInput()->isEnabled());
    QVERIFY(panel.sendButton()->isEnabled());
    QCOMPARE(panel.statusLabel()->text(), QStringLiteral("Waiting for AI..."));

    panel.setBusy(false, QStringLiteral("Ready"));

    QVERIFY(panel.followUpInput()->isEnabled());
    QVERIFY(panel.sendButton()->isEnabled());
    QCOMPARE(panel.statusLabel()->text(), QStringLiteral("Ready"));
}

QTEST_MAIN(UiWidgetTests)

#include "test_ui_widgets.moc"
