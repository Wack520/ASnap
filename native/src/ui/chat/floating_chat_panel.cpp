#include "ui/chat/floating_chat_panel.h"

#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QUrl>
#include <QWheelEvent>

#include "chat/chat_message.h"
#include "ui/chat/chat_markdown_renderer.h"
#include "ui/chat/floating_chat_panel_helpers.h"

namespace ais::ui {

namespace {

using ais::chat::ChatMessage;
namespace helpers = ais::ui::floating_chat_panel_helpers;

constexpr int kResizeMargin = 14;
constexpr int kHeaderDragHeight = 28;
constexpr int kMinimumPanelWidth = 360;
constexpr int kMinimumPanelHeight = 320;
constexpr int kMaximumPanelWidth = 1280;
constexpr int kPanelCornerRadius = 18;

[[nodiscard]] bool isHeaderControl(const QWidget* widget,
                                   const QToolButton* minimizeButton,
                                   const QToolButton* closeButton) {
    return widget != nullptr &&
           (widget == minimizeButton || widget == closeButton);
}

[[nodiscard]] bool isHistoryNavigationKey(const int key) {
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Home:
    case Qt::Key_End:
        return true;
    default:
        return false;
    }
}

}  // namespace

FloatingChatPanel::FloatingChatPanel(QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("ASnap"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumWidth(kMinimumPanelWidth);
    setMinimumHeight(kMinimumPanelHeight);
    resize(kMinimumPanelWidth, 560);

    statusLabel_ = new QLabel(QStringLiteral("就绪"), this);
    statusLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    statusLabel_->setFixedHeight(16);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    reasoningToggleButton_ = new QToolButton(this);
    reasoningToggleButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    reasoningToggleButton_->setCheckable(true);
    reasoningToggleButton_->setChecked(false);
    reasoningToggleButton_->setArrowType(Qt::RightArrow);
    reasoningToggleButton_->setText(QStringLiteral("展开思考"));
    reasoningToggleButton_->hide();

    reasoningView_ = new QTextBrowser(this);
    reasoningView_->setOpenLinks(false);
    reasoningView_->setOpenExternalLinks(false);
    reasoningView_->setMinimumHeight(72);
    reasoningView_->setMaximumHeight(120);
    reasoningView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    reasoningView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    reasoningView_->setFrameShape(QFrame::NoFrame);
    reasoningView_->hide();

    historyView_ = new QTextBrowser(this);
    historyView_->setOpenLinks(false);
    historyView_->setOpenExternalLinks(false);
    historyView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    historyView_->setFrameShape(QFrame::NoFrame);
    if (QScrollBar* historyScrollBar = historyView_->verticalScrollBar(); historyScrollBar != nullptr) {
        connect(historyScrollBar, &QScrollBar::valueChanged, this, [this, historyScrollBar](int value) {
            if (suppressHistoryScrollTracking_) {
                return;
            }
            historyAutoFollow_ = (historyScrollBar->maximum() - value) <= 12;
        });
    }

    followUpInput_ = new QLineEdit(this);
    followUpInput_->setObjectName(QStringLiteral("chatInput"));
    followUpInput_->setPlaceholderText(QStringLiteral("继续追问，按 Enter 发送…"));
    followUpInput_->setMinimumHeight(30);
    followUpInput_->setFrame(false);

    sendButton_ = new QPushButton(QStringLiteral("↑"), this);
    sendButton_->setObjectName(QStringLiteral("sendButton"));
    sendButton_->setCursor(Qt::PointingHandCursor);
    sendButton_->setFixedSize(26, 26);

    minimizeButton_ = new QToolButton(this);
    minimizeButton_->setObjectName(QStringLiteral("minimizeButton"));
    minimizeButton_->setText(QStringLiteral("−"));
    minimizeButton_->setAutoRaise(true);
    minimizeButton_->setCursor(Qt::PointingHandCursor);

    closeButton_ = new QToolButton(this);
    closeButton_->setObjectName(QStringLiteral("dismissButton"));
    closeButton_->setText(QStringLiteral("×"));
    closeButton_->setAutoRaise(true);
    closeButton_->setCursor(Qt::PointingHandCursor);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(24);
    connect(refreshTimer_, &QTimer::timeout, this, &FloatingChatPanel::refreshBoundSessionViews);

    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(6);
    headerRow->addWidget(statusLabel_, 1);
    headerRow->addWidget(minimizeButton_);
    headerRow->addWidget(closeButton_);

    composerShell_ = new QFrame(this);
    composerShell_->setObjectName(QStringLiteral("chatInputShell"));
    composerShell_->setMinimumHeight(38);
    auto* composerLayout = new QHBoxLayout(composerShell_);
    composerLayout->setContentsMargins(12, 5, 8, 5);
    composerLayout->setSpacing(8);
    composerLayout->addWidget(followUpInput_, 1);
    composerLayout->addWidget(sendButton_, 0, Qt::AlignRight | Qt::AlignVCenter);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(8);
    rootLayout->addLayout(headerRow);
    rootLayout->addWidget(reasoningToggleButton_);
    rootLayout->addWidget(reasoningView_);
    rootLayout->addWidget(historyView_, 1);
    rootLayout->addWidget(composerShell_);

    for (QWidget* child : findChildren<QWidget*>()) {
        child->installEventFilter(this);
        child->setMouseTracking(true);
    }

    connect(sendButton_, &QPushButton::clicked, this, &FloatingChatPanel::emitSendRequest);
    connect(followUpInput_, &QLineEdit::returnPressed, this, &FloatingChatPanel::emitSendRequest);
    connect(minimizeButton_, &QToolButton::clicked, this, &QWidget::hide);
    connect(closeButton_, &QToolButton::clicked, this, &QWidget::close);
    connect(historyView_, &QTextBrowser::anchorClicked, this, &FloatingChatPanel::handleRichTextLink);
    connect(reasoningView_, &QTextBrowser::anchorClicked, this, &FloatingChatPanel::handleRichTextLink);
    connect(reasoningToggleButton_, &QToolButton::toggled, this,
            [this](bool checked) {
                reasoningExpanded_ = checked;
                refreshReasoning();
            });

    applyAppearance(QStringLiteral("system"),
                    0.95,
                    QStringLiteral("#ffffff"),
                    QString(),
                    QStringLiteral("#000000"));
    refreshInputState();
}

void FloatingChatPanel::bindSession(const std::shared_ptr<ais::chat::ChatSession>& session) {
    if (session_ != session) {
        reasoningExpanded_ = false;
        historyAutoFollow_ = true;
    }
    session_ = session;
    refreshBoundSessionViews();
}

void FloatingChatPanel::scheduleSessionRefresh() {
    if (refreshTimer_ == nullptr) {
        refreshBoundSessionViews();
        return;
    }

    if (!refreshTimer_->isActive()) {
        refreshTimer_->start();
    }
}

void FloatingChatPanel::activateLinkForTest(const QUrl& url) {
    handleRichTextLink(url);
}

void FloatingChatPanel::setExternalUrlOpenerForTest(std::function<bool(const QUrl&)> opener) {
    externalUrlOpener_ = std::move(opener);
}

void FloatingChatPanel::closeEvent(QCloseEvent* event) {
    emit panelDismissed();
    QWidget::closeEvent(event);
}

bool FloatingChatPanel::eventFilter(QObject* watched, QEvent* event) {
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr || widget == this) {
        return QWidget::eventFilter(watched, event);
    }

    if (!suppressHistoryScrollTracking_ && historyView_ != nullptr) {
        QScrollBar* const historyScrollBar = historyView_->verticalScrollBar();
        const bool historyViewportHit =
            watched == historyView_ || watched == historyView_->viewport();
        const bool historyScrollBarHit = watched == historyScrollBar;
        if (event != nullptr && historyScrollBar != nullptr) {
            switch (event->type()) {
            case QEvent::Wheel:
                if (historyViewportHit || historyScrollBarHit) {
                    historyAutoFollow_ = false;
                }
                break;
            case QEvent::MouseButtonPress:
                if (historyScrollBarHit) {
                    historyAutoFollow_ = false;
                }
                break;
            case QEvent::KeyPress:
                if ((historyViewportHit || historyScrollBarHit) &&
                    isHistoryNavigationKey(static_cast<QKeyEvent*>(event)->key())) {
                    historyAutoFollow_ = false;
                }
                break;
            default:
                break;
            }
        }
    }

    switch (event->type()) {
    case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier) {
            close();
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint panelPos = mapFromGlobal(mouseEvent->globalPosition().toPoint());
        if (isHeaderControl(widget, minimizeButton_, closeButton_) &&
            interactionMode_ == InteractionMode::None) {
            unsetCursor();
            break;
        }
        if (event->type() == QEvent::MouseMove && interactionMode_ == InteractionMode::None) {
            applyCursorForPosition(panelPos);
        }
        if (hitTestInteraction(panelPos) == InteractionMode::None && interactionMode_ == InteractionMode::None) {
            break;
        }

        QMouseEvent translatedEvent(mouseEvent->type(),
                                    QPointF(panelPos),
                                    mouseEvent->globalPosition(),
                                    mouseEvent->button(),
                                    mouseEvent->buttons(),
                                    mouseEvent->modifiers());
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            mousePressEvent(&translatedEvent);
            break;
        case QEvent::MouseMove:
            mouseMoveEvent(&translatedEvent);
            break;
        case QEvent::MouseButtonRelease:
            mouseReleaseEvent(&translatedEvent);
            break;
        default:
            break;
        }

        if (translatedEvent.isAccepted()) {
            event->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void FloatingChatPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        close();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void FloatingChatPanel::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QColor fillColor = helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_);
    fillColor.setAlpha(currentSurfaceAlpha_);
    QColor borderColor = QColor(currentBorderColor_);
    if (!borderColor.isValid()) {
        borderColor = helpers::autoBorderColor(fillColor, currentTheme_);
    }
    if (helpers::effectiveThemeName(currentTheme_) == QStringLiteral("dark")) {
        borderColor.setAlpha(qMin(borderColor.alpha(), 118));
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(borderColor, 1.0));
    painter.setBrush(fillColor);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), kPanelCornerRadius, kPanelCornerRadius);
}

void FloatingChatPanel::refreshBoundSessionViews() {
    copyPayloads_.clear();
    copyCounter_ = 0;
    refreshReasoning();
    refreshHistory();
    refreshInputState();
}

void FloatingChatPanel::setBusy(bool busy, const QString& status) {
    busy_ = busy;
    statusLabel_->setText(helpers::statusText(busy, status));
    refreshInputState();
}

void FloatingChatPanel::applyAppearance(const QString& theme,
                                        double opacity,
                                        const QString& panelColor,
                                        const QString& panelTextColor,
                                        const QString& panelBorderColor) {
    const QString effectiveTheme = helpers::effectiveThemeName(theme);
    const QColor surfaceColor = helpers::resolveSurfaceColor(effectiveTheme, panelColor);
    const QColor textColor = helpers::resolveTextColor(effectiveTheme, surfaceColor, panelTextColor);
    const QColor mutedTextColor = helpers::mutedTextColorForSurface(surfaceColor, effectiveTheme);
    const QColor lineColor = helpers::resolveBorderColor(effectiveTheme, surfaceColor, panelBorderColor);
    const int surfaceAlpha = qBound(0, qRound(surfaceColor.alphaF() * opacity * 255.0), 255);
    currentTheme_ = effectiveTheme;
    currentPanelColor_ = helpers::serializeColor(surfaceColor);
    currentTextColor_ = textColor.name();
    currentBorderColor_ = helpers::serializeColor(lineColor);
    currentSurfaceAlpha_ = surfaceAlpha;
    setProperty("panelSurfaceColor", currentPanelColor_);
    setProperty("panelBorderColor", currentBorderColor_);
    setStyleSheet(helpers::styleSheetForTheme(effectiveTheme,
                                              surfaceColor,
                                              textColor,
                                              mutedTextColor,
                                              lineColor,
                                              surfaceAlpha));
    setWindowOpacity(1.0);
    refreshReasoning();
    refreshHistory();
}

void FloatingChatPanel::restoreSavedSize(const QSize& size) {
    if (!size.isValid()) {
        return;
    }

    const QRect reference = geometry().isValid() ? geometry() : QRect(QPoint(0, 0), size);
    const int maxWidth =
        helpers::maximumPanelWidthForRect(reference, kMinimumPanelWidth, kMaximumPanelWidth);
    resize(qBound(kMinimumPanelWidth, size.width(), maxWidth),
           qMax(kMinimumPanelHeight, size.height()));
}

void FloatingChatPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        interactionMode_ = hitTestInteraction(event->position().toPoint());
        interactionStartGlobal_ = event->globalPosition().toPoint();
        interactionStartGeometry_ = geometry();
        if (interactionMode_ != InteractionMode::None) {
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void FloatingChatPanel::mouseMoveEvent(QMouseEvent* event) {
    const QPoint globalPos = event->globalPosition().toPoint();

    if (!(event->buttons() & Qt::LeftButton) || interactionMode_ == InteractionMode::None) {
        applyCursorForPosition(event->position().toPoint());
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = globalPos - interactionStartGlobal_;
    QRect nextGeometry = interactionStartGeometry_;
    switch (interactionMode_) {
    case InteractionMode::Move:
        setGeometry(helpers::clampGeometryToScreen(QRect(interactionStartGeometry_.topLeft() + delta,
                                                        interactionStartGeometry_.size())));
        break;
    case InteractionMode::ResizeLeft: {
        const int maxWidth = helpers::maximumPanelWidthForRect(interactionStartGeometry_,
                                                               kMinimumPanelWidth,
                                                               kMaximumPanelWidth);
        const int newWidth = qBound(kMinimumPanelWidth,
                                    interactionStartGeometry_.width() - delta.x(),
                                    maxWidth);
        nextGeometry.setLeft(interactionStartGeometry_.right() - newWidth + 1);
        nextGeometry.setWidth(newWidth);
        setGeometry(helpers::clampGeometryToScreen(nextGeometry));
        break;
    }
    case InteractionMode::ResizeRight: {
        const int maxWidth = helpers::maximumPanelWidthForRect(interactionStartGeometry_,
                                                               kMinimumPanelWidth,
                                                               kMaximumPanelWidth);
        const int newWidth = qBound(kMinimumPanelWidth,
                                    interactionStartGeometry_.width() + delta.x(),
                                    maxWidth);
        nextGeometry.setWidth(newWidth);
        setGeometry(helpers::clampGeometryToScreen(nextGeometry));
        break;
    }
    case InteractionMode::None:
        break;
    }

    event->accept();
}

void FloatingChatPanel::mouseReleaseEvent(QMouseEvent* event) {
    interactionMode_ = InteractionMode::None;
    applyCursorForPosition(event->position().toPoint());
    QWidget::mouseReleaseEvent(event);
}

FloatingChatPanel::InteractionMode FloatingChatPanel::hitTestInteraction(const QPoint& pos) const {
    if ((minimizeButton_ != nullptr && minimizeButton_->geometry().contains(pos)) ||
        (closeButton_ != nullptr && closeButton_->geometry().contains(pos))) {
        return InteractionMode::None;
    }
    if (pos.x() <= kResizeMargin) {
        return InteractionMode::ResizeLeft;
    }
    if (width() - pos.x() <= kResizeMargin) {
        return InteractionMode::ResizeRight;
    }
    if (pos.y() <= kHeaderDragHeight &&
        (closeButton_ == nullptr || !closeButton_->geometry().contains(pos))) {
        return InteractionMode::Move;
    }
    return InteractionMode::None;
}

void FloatingChatPanel::applyCursorForPosition(const QPoint& pos) {
    switch (hitTestInteraction(pos)) {
    case InteractionMode::ResizeLeft:
    case InteractionMode::ResizeRight:
        setCursor(Qt::SizeHorCursor);
        break;
    default:
        unsetCursor();
        break;
    }
}

void FloatingChatPanel::refreshHistory() {
    if (!session_) {
        historyAutoFollow_ = true;
        historyView_->clear();
        return;
    }

    QScrollBar* const verticalScrollBar =
        historyView_ != nullptr ? historyView_->verticalScrollBar() : nullptr;
    const int previousScrollValue = verticalScrollBar != nullptr ? verticalScrollBar->value() : 0;
    const int previousScrollMaximum = verticalScrollBar != nullptr ? verticalScrollBar->maximum() : 0;
    const bool shouldFollowLatestMessage =
        verticalScrollBar == nullptr ||
        previousScrollMaximum <= 0 ||
        historyAutoFollow_;

    QString html = QStringLiteral(
        "<html><head><style>"
        "%1"
        "</style></head><body>").arg(helpers::historyDocumentCss(currentTheme_,
                                                                 helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                                 QColor(currentTextColor_),
                                                                 helpers::mutedTextColorForSurface(
                                                                     helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                                     currentTheme_),
                                                                 helpers::resolveBorderColor(
                                                                     currentTheme_,
                                                                     helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                                     currentBorderColor_),
                                                                 currentSurfaceAlpha_));

    for (const ChatMessage& message : session_->messages()) {
        html += helpers::htmlForMessage(message, currentTheme_, &copyCounter_, &copyPayloads_);
    }
    html += QStringLiteral("</body></html>");

    const auto restoreHistoryScrollPosition = [this,
                                               verticalScrollBar,
                                               shouldFollowLatestMessage,
                                               previousScrollValue]() {
        if (verticalScrollBar == nullptr) {
            return;
        }

        const QSignalBlocker blocker(verticalScrollBar);
        suppressHistoryScrollTracking_ = true;
        if (shouldFollowLatestMessage) {
            verticalScrollBar->setValue(verticalScrollBar->maximum());
            historyAutoFollow_ = true;
        } else {
            verticalScrollBar->setValue(qMin(previousScrollValue, verticalScrollBar->maximum()));
            historyAutoFollow_ = (verticalScrollBar->maximum() - verticalScrollBar->value()) <= 12;
        }
        suppressHistoryScrollTracking_ = false;
    };

    const bool updatesWereEnabled = historyView_->updatesEnabled();
    suppressHistoryScrollTracking_ = true;
    historyView_->setUpdatesEnabled(false);
    historyView_->setHtml(html);
    restoreHistoryScrollPosition();
    historyView_->setUpdatesEnabled(updatesWereEnabled);
    suppressHistoryScrollTracking_ = false;
    if (verticalScrollBar == nullptr) {
        return;
    }

    QTimer::singleShot(0, historyView_, restoreHistoryScrollPosition);
}

void FloatingChatPanel::refreshReasoning() {
    if (!session_) {
        reasoningExpanded_ = false;
        cachedReasoningKey_.clear();
        cachedReasoningDocumentHtml_.clear();
        cachedReasoningCopyPayloads_.clear();
        reasoningCopyCount_ = 0;
        reasoningToggleButton_->hide();
        {
            const QSignalBlocker blocker(reasoningToggleButton_);
            reasoningToggleButton_->setChecked(false);
        }
        reasoningToggleButton_->setArrowType(Qt::RightArrow);
        reasoningToggleButton_->setText(QStringLiteral("展开思考"));
        reasoningView_->hide();
        reasoningView_->clear();
        return;
    }

    const QString reasoning = session_->latestAssistantReasoning();
    if (reasoning.trimmed().isEmpty()) {
        reasoningExpanded_ = false;
        cachedReasoningKey_.clear();
        cachedReasoningDocumentHtml_.clear();
        cachedReasoningCopyPayloads_.clear();
        reasoningCopyCount_ = 0;
        reasoningToggleButton_->hide();
        {
            const QSignalBlocker blocker(reasoningToggleButton_);
            reasoningToggleButton_->setChecked(false);
        }
        reasoningToggleButton_->setArrowType(Qt::RightArrow);
        reasoningToggleButton_->setText(QStringLiteral("展开思考"));
        reasoningView_->hide();
        reasoningView_->clear();
        return;
    }

    const QString reasoningCacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(currentTheme_,
             currentPanelColor_,
             currentTextColor_,
             currentBorderColor_,
             QString::number(currentSurfaceAlpha_),
             reasoning);
    const bool reasoningChanged = cachedReasoningKey_ != reasoningCacheKey;
    if (reasoningChanged) {
        int reasoningCopyCounter = 0;
        const RenderedMarkdown rendered =
            renderMarkdownWithCodeTools(reasoning, currentTheme_, &reasoningCopyCounter);
        cachedReasoningCopyPayloads_ = rendered.copyPayloads;
        reasoningCopyCount_ = reasoningCopyCounter;
        cachedReasoningDocumentHtml_ =
            QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                .arg(helpers::historyDocumentCss(currentTheme_,
                                                 helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                 QColor(currentTextColor_),
                                                 helpers::mutedTextColorForSurface(
                                                     helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                     currentTheme_),
                                                 helpers::resolveBorderColor(
                                                     currentTheme_,
                                                     helpers::resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                     currentBorderColor_),
                                                 currentSurfaceAlpha_),
                     rendered.html);
        cachedReasoningKey_ = reasoningCacheKey;
    }
    for (auto it = cachedReasoningCopyPayloads_.cbegin(); it != cachedReasoningCopyPayloads_.cend(); ++it) {
        copyPayloads_.insert(it.key(), it.value());
    }
    copyCounter_ = reasoningCopyCount_;

    reasoningToggleButton_->show();
    {
        const QSignalBlocker blocker(reasoningToggleButton_);
        reasoningToggleButton_->setChecked(reasoningExpanded_);
    }
    reasoningToggleButton_->setArrowType(reasoningExpanded_ ? Qt::DownArrow : Qt::RightArrow);
    reasoningToggleButton_->setText(reasoningExpanded_ ? QStringLiteral("收起思考")
                                                       : QStringLiteral("展开思考"));

    if (reasoningChanged) {
        const bool updatesWereEnabled = reasoningView_->updatesEnabled();
        reasoningView_->setUpdatesEnabled(false);
        reasoningView_->setHtml(cachedReasoningDocumentHtml_);
        reasoningView_->setUpdatesEnabled(updatesWereEnabled);
    }
    reasoningView_->setVisible(reasoningExpanded_);
}

void FloatingChatPanel::refreshInputState() {
    const bool hasSession = static_cast<bool>(session_);
    followUpInput_->setEnabled(hasSession);
    if (composerShell_ != nullptr) {
        composerShell_->setEnabled(hasSession);
    }
    sendButton_->setEnabled(hasSession);
}

void FloatingChatPanel::emitSendRequest() {
    if (!session_) {
        return;
    }

    const QString text = followUpInput_->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    emit sendRequested(text);
    followUpInput_->clear();
}

void FloatingChatPanel::handleRichTextLink(const QUrl& url) {
    if (url.scheme() != QStringLiteral("copy-code")) {
        if (url.isValid() && !url.scheme().isEmpty()) {
            if (externalUrlOpener_) {
                externalUrlOpener_(url);
            } else {
                QDesktopServices::openUrl(url);
            }
            statusLabel_->setText(QStringLiteral("已打开链接"));
        }
        return;
    }

    const QString codeId = url.host().isEmpty() ? url.path().mid(1) : url.host();
    const QString code = copyPayloads_.value(codeId);
    if (code.isEmpty()) {
        return;
    }

    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(code);
        statusLabel_->setText(QStringLiteral("代码已复制"));
    }
}

}  // namespace ais::ui
