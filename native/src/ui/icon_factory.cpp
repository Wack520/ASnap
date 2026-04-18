#include "ui/icon_factory.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>

namespace ais::ui {

namespace {

void paintFallbackIcon(QPainter* painter, const QRectF& rect) {
    if (painter == nullptr) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(QStringLiteral("#0d1014")));
    painter->drawRoundedRect(rect.adjusted(1.0, 1.0, -1.0, -1.0), rect.width() * 0.24, rect.height() * 0.24);

    const qreal stroke = qMax<qreal>(1.8, rect.width() * 0.08);
    const QColor lineColor(QStringLiteral("#f3f5f7"));
    QPen pen(lineColor, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    const QRectF cropRect = rect.adjusted(rect.width() * 0.18,
                                          rect.height() * 0.18,
                                          -rect.width() * 0.24,
                                          -rect.height() * 0.28);
    const qreal corner = cropRect.width() * 0.18;

    painter->drawLine(cropRect.topLeft(), cropRect.topLeft() + QPointF(corner, 0));
    painter->drawLine(cropRect.topLeft(), cropRect.topLeft() + QPointF(0, corner));

    painter->drawLine(cropRect.topRight(), cropRect.topRight() + QPointF(-corner, 0));
    painter->drawLine(cropRect.topRight(), cropRect.topRight() + QPointF(0, corner));

    painter->drawLine(cropRect.bottomLeft(), cropRect.bottomLeft() + QPointF(corner, 0));
    painter->drawLine(cropRect.bottomLeft(), cropRect.bottomLeft() + QPointF(0, -corner));

    painter->drawLine(cropRect.bottomRight(), cropRect.bottomRight() + QPointF(-corner, 0));
    painter->drawLine(cropRect.bottomRight(), cropRect.bottomRight() + QPointF(0, -corner));

    const QRectF bubbleRect(rect.left() + rect.width() * 0.48,
                            rect.top() + rect.height() * 0.48,
                            rect.width() * 0.28,
                            rect.height() * 0.2);
    QPainterPath bubblePath;
    bubblePath.addRoundedRect(bubbleRect, bubbleRect.height() * 0.45, bubbleRect.height() * 0.45);
    const QPointF tailBase(bubbleRect.left() + bubbleRect.width() * 0.2, bubbleRect.bottom());
    QPainterPath tail;
    tail.moveTo(tailBase);
    tail.lineTo(tailBase + QPointF(rect.width() * 0.05, rect.height() * 0.08));
    tail.lineTo(tailBase + QPointF(rect.width() * 0.1, 0));
    tail.closeSubpath();
    bubblePath.addPath(tail);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(QStringLiteral("#f3f5f7")));
    painter->drawPath(bubblePath);
}

[[nodiscard]] QIcon buildFallbackIcon() {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paintFallbackIcon(&painter, QRectF(0, 0, size, size));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

}  // namespace

QString brandDisplayName() {
    return QStringLiteral("ASnap");
}

QIcon createAppIcon() {
    static const QIcon icon = [] {
        QIcon resourceIcon(QStringLiteral(":/branding/asnap-icon.png"));
        return resourceIcon.isNull() ? buildFallbackIcon() : resourceIcon;
    }();
    return icon;
}

}  // namespace ais::ui
