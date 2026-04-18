#pragma once

#include <QColor>
#include <QFrame>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QToolButton;

namespace ais::ui {

class SettingsDialogAppearanceSection final : public QFrame {
public:
    explicit SettingsDialogAppearanceSection(QWidget* parent = nullptr);

    void setPanelColor(const QColor& color);
    void setPanelTextColor(const QColor& color);
    void setPanelBorderColor(const QColor& color);
    void restoreAutomaticTextColor();
    void restoreAutomaticBorderColor();
    void refreshColorButtons();
    void refreshPreview();
    void setBusy(bool busy);

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
    [[nodiscard]] QLineEdit* previewInputField() const noexcept { return previewInputField_; }
    [[nodiscard]] QPushButton* previewSendButton() const noexcept { return previewSendButton_; }

    [[nodiscard]] QColor panelColor() const noexcept { return panelColor_; }
    [[nodiscard]] QColor panelTextColor() const noexcept { return panelTextColor_; }
    [[nodiscard]] QColor panelBorderColor() const noexcept { return panelBorderColor_; }
    [[nodiscard]] bool isPanelTextColorCustomized() const noexcept { return panelTextColorCustomized_; }
    [[nodiscard]] bool isPanelBorderColorCustomized() const noexcept { return panelBorderColorCustomized_; }

private:
    [[nodiscard]] QString currentTheme() const;
    void choosePanelColor();
    void choosePanelTextColor();
    void choosePanelBorderColor();

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
    QColor panelColor_;
    QColor panelTextColor_;
    bool panelTextColorCustomized_ = false;
    QColor panelBorderColor_;
    bool panelBorderColorCustomized_ = false;
};

}  // namespace ais::ui
