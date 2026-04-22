#pragma once

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBrowser>
#include <QToolButton>

#include "config/app_config.h"
#include "config/provider_profile.h"
#include "config/provider_protocol.h"

namespace ais::ui {

class SettingsDialogAppearanceSection;

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
    [[nodiscard]] QComboBox* captureModeField() const noexcept { return captureModeField_; }
    [[nodiscard]] QKeySequenceEdit* aiShortcutField() const noexcept { return aiShortcutField_; }
    [[nodiscard]] QKeySequenceEdit* screenshotShortcutField() const noexcept { return screenshotShortcutField_; }
    [[nodiscard]] QComboBox* themeField() const noexcept;
    [[nodiscard]] QDoubleSpinBox* opacityField() const noexcept;
    [[nodiscard]] QPushButton* panelColorButton() const noexcept;
    [[nodiscard]] QPushButton* panelTextColorButton() const noexcept;
    [[nodiscard]] QPushButton* panelTextAutoButton() const noexcept;
    [[nodiscard]] QPushButton* panelBorderColorButton() const noexcept;
    [[nodiscard]] QPushButton* panelBorderAutoButton() const noexcept;
    [[nodiscard]] QFrame* previewSurface() const noexcept;
    [[nodiscard]] QLabel* previewTitleLabel() const noexcept;
    [[nodiscard]] QTextBrowser* previewHistoryView() const noexcept;
    [[nodiscard]] QLineEdit* previewInputPreviewField() const noexcept;
    [[nodiscard]] QPushButton* previewSendButton() const noexcept;
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
    void refreshModelActionUi();
    [[nodiscard]] int indexForProtocol(ais::config::ProviderProtocol protocol) const;
    void preserveScrollPosition();
    void beginScrollAnchorIfNeeded();
    void enforceScrollAnchorNow();
    void restoreScrollPositionLater(bool releaseScrollAnchor = false);

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
    QComboBox* captureModeField_ = nullptr;
    QKeySequenceEdit* aiShortcutField_ = nullptr;
    QKeySequenceEdit* screenshotShortcutField_ = nullptr;
    SettingsDialogAppearanceSection* appearanceSection_ = nullptr;
    QPlainTextEdit* firstPromptField_ = nullptr;
    QCheckBox* launchAtLoginCheckBox_ = nullptr;
    QPushButton* testConnectionButton_ = nullptr;
    QPushButton* testImageButton_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    int preservedScrollValue_ = 0;
    int scrollAnchorValue_ = 0;
    bool scrollAnchorActive_ = false;
};

}  // namespace ais::ui
