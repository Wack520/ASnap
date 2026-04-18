#include "ui/chat/floating_chat_panel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QUrl>

#include "chat/chat_message.h"
#include "ui/chat/chat_markdown_renderer.h"

namespace ais::ui {

namespace {

using ais::chat::ChatMessage;
using ais::chat::ChatRole;

constexpr int kResizeMargin = 14;
constexpr int kHeaderDragHeight = 28;
constexpr int kMinimumPanelWidth = 360;
constexpr int kMinimumPanelHeight = 320;
constexpr int kMaximumPanelWidth = 1280;
constexpr int kPanelCornerRadius = 18;

[[nodiscard]] QString effectiveThemeName(const QString& theme) {
    if (theme == QStringLiteral("light") || theme == QStringLiteral("dark")) {
        return theme;
    }

    const QColor windowColor = qApp != nullptr
        ? qApp->palette().color(QPalette::Window)
        : QColor(Qt::white);
    return windowColor.lightness() < 128 ? QStringLiteral("dark") : QStringLiteral("light");
}

[[nodiscard]] QString statusText(bool busy, const QString& status) {
    if (!status.isEmpty()) {
        return status;
    }
    return busy ? QStringLiteral("处理中…") : QStringLiteral("就绪");
}

[[nodiscard]] QString htmlForMessage(const ChatMessage& message,
                                     const QString& theme,
                                     int* copyCounter,
                                     QHash<QString, QString>* copyPayloads) {
    const QString role = message.role == ChatRole::Assistant ? QStringLiteral("AI") : QStringLiteral("你");

    RenderedMarkdown rendered = renderMarkdownWithCodeTools(message.text, theme, copyCounter);
    QString body = rendered.html;
    if (copyPayloads != nullptr) {
        for (auto it = rendered.copyPayloads.cbegin(); it != rendered.copyPayloads.cend(); ++it) {
            copyPayloads->insert(it.key(), it.value());
        }
    }
    if (message.hasImage()) {
        body.append(QStringLiteral("<div class=\"attachment-note\">已附带截图</div>"));
    }
    if (body.isEmpty()) {
        body = QStringLiteral("&nbsp;");
    }

    const QString bubbleClass =
        message.role == ChatRole::Assistant ? QStringLiteral("assistant") : QStringLiteral("user");
    const bool hasVisibleAssistantText =
        message.role == ChatRole::Assistant && !message.text.trimmed().isEmpty();
    const QString streamingBadge = message.streaming && hasVisibleAssistantText
        ? QStringLiteral("<span class=\"streaming-badge\">流式输出中…</span>")
        : QString();

    return QStringLiteral("<div class=\"bubble %1\"><div class=\"role\">%2%3</div><div class=\"body\">%4</div></div>")
        .arg(bubbleClass, role, streamingBadge, body);
}

[[nodiscard]] QColor fallbackSurfaceColor(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#f6f8fa"))
        : QColor(QStringLiteral("#101214"));
}

[[nodiscard]] QColor resolveSurfaceColor(const QString& theme, const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return fallbackSurfaceColor(theme);
}

[[nodiscard]] QColor autoTextColor(const QColor& surfaceColor, const QString& theme) {
    if (surfaceColor.isValid()) {
        return surfaceColor.lightnessF() < 0.52
            ? QColor(QStringLiteral("#f7f8fa"))
            : QColor(QStringLiteral("#15181d"));
    }

    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#15181d"))
        : QColor(QStringLiteral("#f7f8fa"));
}

[[nodiscard]] QColor resolveTextColor(const QString& theme,
                                      const QColor& surfaceColor,
                                      const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return autoTextColor(surfaceColor, theme);
}

[[nodiscard]] QColor mutedTextColorForTheme(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#5f6b7a"))
        : QColor(QStringLiteral("#9ea7b3"));
}

[[nodiscard]] QColor defaultBorderColorForTheme(const QString& theme) {
    return effectiveThemeName(theme) == QStringLiteral("light")
        ? QColor(QStringLiteral("#d0d7de"))
        : QColor(QStringLiteral("#22262b"));
}

[[nodiscard]] QColor autoBorderColor(const QColor& surfaceColor, const QString& theme) {
    if (!surfaceColor.isValid()) {
        return defaultBorderColorForTheme(theme);
    }

    QColor border = effectiveThemeName(theme) == QStringLiteral("light")
        ? surfaceColor.darker(114)
        : surfaceColor.lighter(126);
    if (qAbs(border.lightness() - surfaceColor.lightness()) < 14) {
        border = defaultBorderColorForTheme(theme);
    }
    border.setAlpha(255);
    return border;
}

[[nodiscard]] QColor resolveBorderColor(const QString& theme,
                                        const QColor& surfaceColor,
                                        const QString& requestedColor) {
    const QColor parsed(requestedColor.trimmed());
    if (parsed.isValid()) {
        return parsed;
    }
    return autoBorderColor(surfaceColor, theme);
}

[[nodiscard]] QString cssColor(const QColor& color, int alphaOverride = -1) {
    QColor actual = color;
    if (alphaOverride >= 0) {
        actual.setAlpha(qBound(0, alphaOverride, 255));
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(actual.red())
        .arg(actual.green())
        .arg(actual.blue())
        .arg(actual.alpha());
}

[[nodiscard]] QString serializeColor(const QColor& color) {
    if (!color.isValid()) {
        return {};
    }
    return color.alpha() < 255
        ? color.name(QColor::HexArgb)
        : color.name(QColor::HexRgb);
}

[[nodiscard]] QString styleSheetForTheme(const QString& theme,
                                         const QColor& surfaceColor,
                                         const QColor& textColor,
                                         const QColor& mutedTextColor,
                                         const QColor& lineColor,
                                         const int surfaceAlpha) {
    const QString subtleSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.74), 255));
    const QString strongerSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.82) + 2, 255));
    const QString borderColor = cssColor(lineColor, effectiveThemeName(theme) == QStringLiteral("light") ? 230 : 132);
    const QString selectionColor = effectiveThemeName(theme) == QStringLiteral("light")
        ? QStringLiteral("#0969da")
        : QStringLiteral("#34404d");

    return QStringLiteral(
        "FloatingChatPanel { background: transparent; color: %1; border: none; font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI'; }"
        "QTextBrowser { background: transparent; color: %1; border: none; border-radius: 14px; selection-background-color: %6; selection-color: #ffffff; }"
        "QLineEdit { color: %1; selection-background-color: %6; selection-color: #ffffff; }"
        "QFrame#chatInputShell { background: %4; border: 1px solid %5; border-radius: 18px; }"
        "QFrame#chatInputShell:disabled { background: %3; }"
        "QToolButton { color: %2; border: none; padding: 0; text-align: left; background: transparent; }"
        "QLabel { color: %1; background: transparent; }"
        "QLabel#statusBarLabel { color: %2; font-size: 11px; font-weight: 500; min-height: 16px; max-height: 16px; }"
        "QLineEdit#chatInput { background: transparent; border: none; padding: 0 2px 0 0; min-height: 18px; }"
        "QLineEdit#chatInput:focus { border: none; }"
        "QPushButton#sendButton { background: transparent; color: %1; border: none; border-radius: 13px; padding: 0; min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; font-size: 15px; font-weight: 700; }"
        "QPushButton#sendButton:hover { background: %3; }"
        "QPushButton#sendButton:disabled { color: %2; background: transparent; }"
        "QToolButton#dismissButton { background: transparent; color: %2; border: 1px solid transparent; border-radius: 9px; padding: 0; min-width: 18px; min-height: 18px; }"
        "QToolButton#dismissButton:hover { color: %1; border-color: %5; background: %4; }"
        "QToolButton#dismissButton:pressed { background: %3; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 0 4px 0; }"
        "QScrollBar::handle:vertical { background: %5; min-height: 24px; border-radius: 5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; border: none; }")
        .arg(textColor.name(),
             mutedTextColor.name(),
             strongerSurface,
             subtleSurface,
             borderColor,
             selectionColor);
}

[[nodiscard]] QString historyDocumentCss(const QString& theme,
                                         const QColor& surfaceColor,
                                         const QColor& textColor,
                                         const QColor& mutedTextColor,
                                         const QColor& lineColor,
                                         const int surfaceAlpha) {
    const QString codeSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.78) + 4, 255));
    const QString toolbarSurface = cssColor(surfaceColor, qBound(0, qRound(surfaceAlpha * 0.82) + 6, 255));
    const QString subtleBorder = cssColor(lineColor, effectiveThemeName(theme) == QStringLiteral("light") ? 120 : 64);

    return QStringLiteral(
        "body { font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Microsoft YaHei UI', sans-serif; color: %1; background: transparent; }"
        ".bubble { margin: 12px 0; padding: 0; border: none; background: transparent; }"
        ".role { font-weight: 700; margin-bottom: 8px; }"
        ".body p { margin: 0 0 8px 0; line-height: 1.65; }"
        ".body h1, .body h2, .body h3 { margin: 12px 0 8px 0; }"
        ".body ul, .body ol { margin: 0 0 8px 18px; }"
        ".body blockquote { margin: 8px 0; padding: 2px 0 2px 12px; border-left: 3px solid %3; color: %2; }"
        ".body pre { margin: 0; padding: 10px 12px; border-radius: 0 0 8px 8px; border: 0; background: %4; color: %1; font-family: 'Cascadia Code', 'Consolas', monospace; white-space: pre-wrap; }"
        ".body code { padding: 1px 4px; border-radius: 4px; background: %4; color: %1; font-family: 'Cascadia Code', 'Consolas', monospace; }"
        ".body pre code { padding: 0; background: transparent; color: inherit; }"
        ".body table { margin: 8px 0; border-collapse: collapse; }"
        ".body th, .body td { border: 1px solid %3; padding: 6px 8px; }"
        ".body a { color: %1; text-decoration: underline; }"
        ".code-card { margin: 8px 0; border: 1px solid %6; border-radius: 10px; overflow: hidden; background: %4; }"
        ".code-toolbar { display: flex; justify-content: space-between; align-items: center; padding: 6px 10px; background: %5; border-bottom: 1px solid %6; }"
        ".code-language { font-family: 'Segoe UI'; font-size: 11px; text-transform: uppercase; color: %2; }"
        ".code-copy { font-family: 'Segoe UI'; font-size: 12px; color: %1; text-decoration: none; }"
        ".streaming-badge { margin-left: 8px; font-size: 11px; color: %2; font-weight: 400; }"
        ".attachment-note { margin-top: 8px; color: %2; font-style: italic; }")
        .arg(textColor.name(),
             mutedTextColor.name(),
             lineColor.name(),
             codeSurface,
             toolbarSurface,
             subtleBorder);
}

[[nodiscard]] QRect availableScreenGeometryForRect(const QRect& rect) {
    if (QScreen* screen = QGuiApplication::screenAt(rect.center()); screen != nullptr) {
        return screen->availableGeometry();
    }

    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        return screen->availableGeometry();
    }

    return rect;
}

[[nodiscard]] QRect clampGeometryToScreen(const QRect& geometry) {
    const QRect screen = availableScreenGeometryForRect(geometry);
    const int maxAllowedWidth = qMax(280, screen.width() - 12);
    const int width = qMin(geometry.width(), maxAllowedWidth);
    const int clampedLeft = screen.left() + qMax(0, screen.width() - width);
    const int clampedTop = screen.top() + qMax(0, screen.height() - geometry.height());
    return QRect(QPoint(qBound(screen.left(), geometry.left(), clampedLeft),
                        qBound(screen.top(), geometry.top(), clampedTop)),
                 QSize(width, geometry.height()));
}

[[nodiscard]] int maximumPanelWidthForRect(const QRect& geometry) {
    const QRect screen = availableScreenGeometryForRect(geometry);
    return qMax(kMinimumPanelWidth, qMin(kMaximumPanelWidth, screen.width() - 12));
}

}  // namespace

FloatingChatPanel::FloatingChatPanel(QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("ASnap"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumWidth(kMinimumPanelWidth);
    setMinimumHeight(kMinimumPanelHeight);
    resize(560, 560);

    statusLabel_ = new QLabel(QStringLiteral("就绪"), this);
    statusLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    statusLabel_->setFixedHeight(16);
    statusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    previewLabel_ = new QLabel(QStringLiteral("暂无预览"), this);
    previewLabel_->hide();

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

    followUpInput_ = new QLineEdit(this);
    followUpInput_->setObjectName(QStringLiteral("chatInput"));
    followUpInput_->setPlaceholderText(QStringLiteral("继续追问，按 Enter 发送…"));
    followUpInput_->setMinimumHeight(30);
    followUpInput_->setFrame(false);

    sendButton_ = new QPushButton(QStringLiteral("↑"), this);
    sendButton_->setObjectName(QStringLiteral("sendButton"));
    sendButton_->setCursor(Qt::PointingHandCursor);
    sendButton_->setFixedSize(26, 26);

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
    connect(closeButton_, &QToolButton::clicked, this, &QWidget::close);
    connect(historyView_, &QTextBrowser::anchorClicked, this, &FloatingChatPanel::handleRichTextLink);
    connect(reasoningView_, &QTextBrowser::anchorClicked, this, &FloatingChatPanel::handleRichTextLink);
    connect(reasoningToggleButton_, &QToolButton::toggled, this,
            [this](bool checked) {
                reasoningExpanded_ = checked;
                refreshReasoning();
            });

    applyAppearance(QStringLiteral("system"), 0.92);
    refreshInputState();
}

void FloatingChatPanel::bindSession(const std::shared_ptr<ais::chat::ChatSession>& session) {
    if (session_ != session) {
        reasoningExpanded_ = false;
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
        if (widget == closeButton_ && interactionMode_ == InteractionMode::None) {
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

    QColor fillColor = resolveSurfaceColor(currentTheme_, currentPanelColor_);
    fillColor.setAlpha(currentSurfaceAlpha_);
    QColor borderColor = QColor(currentBorderColor_);
    if (!borderColor.isValid()) {
        borderColor = autoBorderColor(fillColor, currentTheme_);
    }
    if (effectiveThemeName(currentTheme_) == QStringLiteral("dark")) {
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

void FloatingChatPanel::setPreviewPixmap(const QPixmap& pixmap) {
    previewPixmap_ = pixmap;
    refreshPreview();
}

void FloatingChatPanel::setBusy(bool busy, const QString& status) {
    busy_ = busy;
    statusLabel_->setText(statusText(busy, status));
    refreshInputState();
}

void FloatingChatPanel::applyAppearance(const QString& theme,
                                        double opacity,
                                        const QString& panelColor,
                                        const QString& panelTextColor,
                                        const QString& panelBorderColor) {
    const QString effectiveTheme = effectiveThemeName(theme);
    const QColor surfaceColor = resolveSurfaceColor(effectiveTheme, panelColor);
    const QColor textColor = resolveTextColor(effectiveTheme, surfaceColor, panelTextColor);
    const QColor mutedTextColor = mutedTextColorForTheme(effectiveTheme);
    const QColor lineColor = resolveBorderColor(effectiveTheme, surfaceColor, panelBorderColor);
    const int surfaceAlpha = qBound(0, qRound(surfaceColor.alphaF() * opacity * 255.0), 255);
    currentTheme_ = effectiveTheme;
    currentPanelColor_ = serializeColor(surfaceColor);
    currentTextColor_ = textColor.name();
    currentBorderColor_ = serializeColor(lineColor);
    currentSurfaceAlpha_ = surfaceAlpha;
    setProperty("panelSurfaceColor", currentPanelColor_);
    setProperty("panelBorderColor", currentBorderColor_);
    setStyleSheet(styleSheetForTheme(effectiveTheme,
                                     surfaceColor,
                                     textColor,
                                     mutedTextColor,
                                     lineColor,
                                     surfaceAlpha));
    setWindowOpacity(1.0);
    refreshPreview();
    refreshReasoning();
    refreshHistory();
}

void FloatingChatPanel::restoreSavedSize(const QSize& size) {
    if (!size.isValid()) {
        return;
    }

    const QRect reference = geometry().isValid() ? geometry() : QRect(QPoint(0, 0), size);
    const int maxWidth = maximumPanelWidthForRect(reference);
    resize(qBound(kMinimumPanelWidth, size.width(), maxWidth),
           qMax(kMinimumPanelHeight, size.height()));
}

void FloatingChatPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    refreshPreview();
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
        setGeometry(clampGeometryToScreen(QRect(interactionStartGeometry_.topLeft() + delta,
                                               interactionStartGeometry_.size())));
        break;
    case InteractionMode::ResizeLeft: {
        const int maxWidth = maximumPanelWidthForRect(interactionStartGeometry_);
        const int newWidth = qBound(kMinimumPanelWidth,
                                    interactionStartGeometry_.width() - delta.x(),
                                    maxWidth);
        nextGeometry.setLeft(interactionStartGeometry_.right() - newWidth + 1);
        nextGeometry.setWidth(newWidth);
        setGeometry(clampGeometryToScreen(nextGeometry));
        break;
    }
    case InteractionMode::ResizeRight: {
        const int maxWidth = maximumPanelWidthForRect(interactionStartGeometry_);
        const int newWidth = qBound(kMinimumPanelWidth,
                                    interactionStartGeometry_.width() + delta.x(),
                                    maxWidth);
        nextGeometry.setWidth(newWidth);
        setGeometry(clampGeometryToScreen(nextGeometry));
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
    if (closeButton_ != nullptr && closeButton_->geometry().contains(pos)) {
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
        historyView_->clear();
        return;
    }

    QString html = QStringLiteral(
        "<html><head><style>"
        "%1"
        "</style></head><body>").arg(historyDocumentCss(currentTheme_,
                                                        resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                        QColor(currentTextColor_),
                                                        mutedTextColorForTheme(currentTheme_),
                                                        resolveBorderColor(currentTheme_,
                                                                           resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                                           currentBorderColor_),
                                                        currentSurfaceAlpha_));

    for (const ChatMessage& message : session_->messages()) {
        html += htmlForMessage(message, currentTheme_, &copyCounter_, &copyPayloads_);
    }
    html += QStringLiteral("</body></html>");

    historyView_->setHtml(html);
    historyView_->moveCursor(QTextCursor::End);
}

void FloatingChatPanel::refreshReasoning() {
    if (!session_) {
        reasoningExpanded_ = false;
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

    RenderedMarkdown rendered = renderMarkdownWithCodeTools(reasoning, currentTheme_, &copyCounter_);
    for (auto it = rendered.copyPayloads.cbegin(); it != rendered.copyPayloads.cend(); ++it) {
        copyPayloads_.insert(it.key(), it.value());
    }

    reasoningToggleButton_->show();
    {
        const QSignalBlocker blocker(reasoningToggleButton_);
        reasoningToggleButton_->setChecked(reasoningExpanded_);
    }
    reasoningToggleButton_->setArrowType(reasoningExpanded_ ? Qt::DownArrow : Qt::RightArrow);
    reasoningToggleButton_->setText(reasoningExpanded_ ? QStringLiteral("收起思考")
                                                       : QStringLiteral("展开思考"));

    reasoningView_->setHtml(QStringLiteral("<html><head><style>%1</style></head><body>%2</body></html>")
                                .arg(historyDocumentCss(currentTheme_,
                                                        resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                        QColor(currentTextColor_),
                                                        mutedTextColorForTheme(currentTheme_),
                                                        resolveBorderColor(currentTheme_,
                                                                           resolveSurfaceColor(currentTheme_, currentPanelColor_),
                                                                           currentBorderColor_),
                                                        currentSurfaceAlpha_),
                                     rendered.html));
    reasoningView_->setVisible(reasoningExpanded_);
}

void FloatingChatPanel::refreshPreview() {
    previewLabel_->hide();
    previewLabel_->clear();
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
