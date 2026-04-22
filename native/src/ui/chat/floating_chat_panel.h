#pragma once

#include <functional>
#include <memory>

#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QHash>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWidget>
#include <QFrame>

#include "chat/chat_session.h"

class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QColor;

namespace ais::ui {

class FloatingChatPanel final : public QWidget {
    Q_OBJECT

public:
    explicit FloatingChatPanel(QWidget* parent = nullptr);

    void bindSession(const std::shared_ptr<ais::chat::ChatSession>& session);
    void scheduleSessionRefresh();
    void setBusy(bool busy, const QString& status = {});
    void applyAppearance(const QString& theme,
                         double opacity,
                         const QString& panelColor = {},
                         const QString& panelTextColor = {},
                         const QString& panelBorderColor = {});
    void restoreSavedSize(const QSize& size);
    void activateLinkForTest(const QUrl& url);
    void setExternalUrlOpenerForTest(std::function<bool(const QUrl&)> opener);

    [[nodiscard]] QToolButton* reasoningToggleButton() const noexcept { return reasoningToggleButton_; }
    [[nodiscard]] QTextBrowser* reasoningView() const noexcept { return reasoningView_; }
    [[nodiscard]] QTextBrowser* historyView() const noexcept { return historyView_; }
    [[nodiscard]] QLineEdit* followUpInput() const noexcept { return followUpInput_; }
    [[nodiscard]] QPushButton* sendButton() const noexcept { return sendButton_; }
    [[nodiscard]] QLabel* statusLabel() const noexcept { return statusLabel_; }
    [[nodiscard]] QToolButton* minimizeButton() const noexcept { return minimizeButton_; }
    [[nodiscard]] QToolButton* closeButton() const noexcept { return closeButton_; }

signals:
    void sendRequested(const QString& text);
    void panelDismissed();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class InteractionMode {
        None,
        Move,
        ResizeLeft,
        ResizeRight,
    };

    [[nodiscard]] InteractionMode hitTestInteraction(const QPoint& pos) const;
    void applyCursorForPosition(const QPoint& pos);
    void refreshBoundSessionViews();
    void refreshHistory();
    void refreshReasoning();
    void refreshInputState();
    void emitSendRequest();
    void handleRichTextLink(const QUrl& url);

    std::shared_ptr<ais::chat::ChatSession> session_;
    bool busy_ = false;
    bool reasoningExpanded_ = false;
    bool historyAutoFollow_ = true;
    bool suppressHistoryScrollTracking_ = false;
    QString currentTheme_ = QStringLiteral("dark");
    QString currentPanelColor_ = QStringLiteral("#ffffff");
    QString currentTextColor_ = QStringLiteral("#15181d");
    QString currentBorderColor_ = QStringLiteral("#000000");
    int currentSurfaceAlpha_ = 242;
    int copyCounter_ = 0;
    int reasoningCopyCount_ = 0;
    QHash<QString, QString> copyPayloads_;
    QHash<QString, QString> cachedReasoningCopyPayloads_;
    std::function<bool(const QUrl&)> externalUrlOpener_;
    InteractionMode interactionMode_ = InteractionMode::None;
    QPoint interactionStartGlobal_;
    QRect interactionStartGeometry_;
    QTimer* refreshTimer_ = nullptr;
    QString cachedReasoningKey_;
    QString cachedReasoningDocumentHtml_;
    QLabel* statusLabel_ = nullptr;
    QToolButton* reasoningToggleButton_ = nullptr;
    QToolButton* minimizeButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    QTextBrowser* reasoningView_ = nullptr;
    QTextBrowser* historyView_ = nullptr;
    QFrame* composerShell_ = nullptr;
    QLineEdit* followUpInput_ = nullptr;
    QPushButton* sendButton_ = nullptr;
};

}  // namespace ais::ui
