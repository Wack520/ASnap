#pragma once

#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QColor>
#include <QFrame>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>

#include "config/app_config.h"
#include "config/provider_profile.h"
#include "config/provider_protocol.h"

namespace ais::ui {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    enum class ActionMode {
        None,
        FetchModels,
        TestText,
        TestImage,
    };

    explicit SettingsDialog(const ais::config::AppConfig& config, QWidget* parent = nullptr);

    [[nodiscard]] ais::config::ProviderProfile currentProfile() const;
    [[nodiscard]] ais::config::AppConfig currentConfig() const;

    void applyAppearance(const QString& theme);
    void applyProtocolPreset(ais::config::ProviderProtocol protocol);
    void setPanelColor(const QColor& color);
    void setPanelTextColor(const QColor& color);
    void setPanelBorderColor(const QColor& color);
    void setAvailableModels(const QStringList& models);
    void setActionMode(ActionMode mode, const QString& status = {});
    void setBusy(bool busy, const QString& status = {});

    [[nodiscard]] QComboBox* protocolSelector() const noexcept { return protocolSelector_; }
    [[nodiscard]] QLineEdit* baseUrlField() const noexcept { return baseUrlField_; }
    [[nodiscard]] QLineEdit* apiKeyField() const noexcept { return apiKeyField_; }
    [[nodiscard]] QComboBox* modelField() const noexcept { return modelField_; }
    [[nodiscard]] QToolButton* modelPopupButton() const noexcept { return modelPopupButton_; }
    [[nodiscard]] QPushButton* fetchModelsButton() const noexcept { return fetchModelsButton_; }
    [[nodiscard]] QKeySequenceEdit* aiShortcutField() const noexcept { return aiShortcutField_; }
    [[nodiscard]] QKeySequenceEdit* screenshotShortcutField() const noexcept { return screenshotShortcutField_; }
    [[nodiscard]] QComboBox* themeField() const noexcept { return themeField_; }
    [[nodiscard]] QDoubleSpinBox* opacityField() const noexcept { return opacityField_; }
    [[nodiscard]] QPushButton* panelColorButton() const noexcept { return panelColorButton_; }
    [[nodiscard]] QPushButton* panelTextColorButton() const noexcept { return panelTextColorButton_; }
    [[nodiscard]] QPushButton* panelTextAutoButton() const noexcept { return panelTextAutoButton_; }
    [[nodiscard]] QPushButton* panelBorderColorButton() const noexcept { return panelBorderColorButton_; }
    [[nodiscard]] QPushButton* panelBorderAutoButton() const noexcept { return panelBorderAutoButton_; }
    [[nodiscard]] QFrame* previewSurface() const noexcept { return previewSurface_; }
    [[nodiscard]] QLabel* previewTitleLabel() const noexcept { return previewTitleLabel_; }
    [[nodiscard]] QTextBrowser* previewHistoryView() const noexcept { return previewHistoryView_; }
    [[nodiscard]] QLineEdit* previewInputPreviewField() const noexcept { return previewInputField_; }
    [[nodiscard]] QPushButton* previewSendButton() const noexcept { return previewSendButton_; }
    [[nodiscard]] QPlainTextEdit* firstPromptField() const noexcept { return firstPromptField_; }
    [[nodiscard]] QCheckBox* launchAtLoginCheckBox() const noexcept { return launchAtLoginCheckBox_; }
    [[nodiscard]] QPushButton* testConnectionButton() const noexcept { return testConnectionButton_; }
    [[nodiscard]] QPushButton* testImageButton() const noexcept { return testImageButton_; }
    [[nodiscard]] QLabel* statusLabel() const noexcept { return statusLabel_; }
    [[nodiscard]] QLabel* modelActionStatusLabel() const noexcept { return modelActionStatusLabel_; }

signals:
    void saveRequested();
    void fetchModelsRequested();
    void testConnectionRequested();
    void testImageUnderstandingRequested();

private:
    void handleProtocolChanged();
    void choosePanelColor();
    void choosePanelTextColor();
    void choosePanelBorderColor();
    void restoreAutomaticTextColor();
    void restoreAutomaticBorderColor();
    void refreshColorButtons();
    void refreshPreview();
    void refreshModelActionUi();
    [[nodiscard]] int indexForProtocol(ais::config::ProviderProtocol protocol) const;

    config::AppConfig initialConfig_;
    ActionMode actionMode_ = ActionMode::None;
    QLabel* headerLabel_ = nullptr;
    QLabel* subtitleLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QComboBox* protocolSelector_ = nullptr;
    QLineEdit* baseUrlField_ = nullptr;
    QLineEdit* apiKeyField_ = nullptr;
    QComboBox* modelField_ = nullptr;
    QToolButton* modelPopupButton_ = nullptr;
    QPushButton* fetchModelsButton_ = nullptr;
    QLabel* modelActionStatusLabel_ = nullptr;
    QKeySequenceEdit* aiShortcutField_ = nullptr;
    QKeySequenceEdit* screenshotShortcutField_ = nullptr;
    QComboBox* themeField_ = nullptr;
    QDoubleSpinBox* opacityField_ = nullptr;
    QPushButton* panelColorButton_ = nullptr;
    QPushButton* panelTextColorButton_ = nullptr;
    QPushButton* panelTextAutoButton_ = nullptr;
    QPushButton* panelBorderColorButton_ = nullptr;
    QPushButton* panelBorderAutoButton_ = nullptr;
    QFrame* previewSurface_ = nullptr;
    QLabel* previewTitleLabel_ = nullptr;
    QLabel* previewStatusLabel_ = nullptr;
    QToolButton* previewReasoningToggle_ = nullptr;
    QTextBrowser* previewHistoryView_ = nullptr;
    QLineEdit* previewInputField_ = nullptr;
    QPushButton* previewSendButton_ = nullptr;
    QPlainTextEdit* firstPromptField_ = nullptr;
    QCheckBox* launchAtLoginCheckBox_ = nullptr;
    QPushButton* testConnectionButton_ = nullptr;
    QPushButton* testImageButton_ = nullptr;
    QColor panelColor_;
    QColor panelTextColor_;
    bool panelTextColorCustomized_ = false;
    QColor panelBorderColor_;
    bool panelBorderColorCustomized_ = false;
};

}  // namespace ais::ui
