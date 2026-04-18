#include "ui/panel_placement.h"

#include <QList>

namespace ais::ui {
namespace {

[[nodiscard]] bool fitsOnScreen(const QPoint& position, const QSize& panelSize, const QRect& screen) {
    return screen.contains(QRect(position, panelSize));
}

[[nodiscard]] QPoint clampToScreen(const QPoint& position, const QSize& panelSize, const QRect& screen) {
    const int maxX = screen.x() + qMax(0, screen.width() - panelSize.width());
    const int maxY = screen.y() + qMax(0, screen.height() - panelSize.height());

    return {
        qBound(screen.x(), position.x(), maxX),
        qBound(screen.y(), position.y(), maxY),
    };
}

}  // namespace

QPoint choosePanelPosition(const QRect& anchor, const QSize& panelSize, const QRect& screen) {
    const QList<QPoint> candidates = {
        QPoint(anchor.right() + 1, anchor.top()),
        QPoint(anchor.left() - panelSize.width(), anchor.top()),
        QPoint(anchor.left(), anchor.bottom() + 1),
        QPoint(anchor.left(), anchor.top() - panelSize.height()),
    };

    for (const QPoint& candidate : candidates) {
        if (fitsOnScreen(candidate, panelSize, screen)) {
            return candidate;
        }
    }

    return clampToScreen(candidates.constFirst(), panelSize, screen);
}

}  // namespace ais::ui
