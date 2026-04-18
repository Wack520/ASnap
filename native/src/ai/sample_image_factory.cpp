#include "ai/sample_image_factory.h"

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>

namespace ais::ai {

QByteArray SampleImageFactory::buildPng() {
    QImage image(QSize(96, 72), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(34, 39, 46));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(QRect(8, 8, 80, 56), QColor(74, 144, 226));
    painter.setPen(QPen(QColor(255, 255, 255), 3));
    painter.drawLine(QPoint(16, 48), QPoint(38, 28));
    painter.drawLine(QPoint(38, 28), QPoint(52, 42));
    painter.drawLine(QPoint(52, 42), QPoint(80, 18));
    painter.end();

    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }

    if (!image.save(&buffer, "PNG")) {
        return {};
    }

    return pngBytes;
}

}  // namespace ais::ai
