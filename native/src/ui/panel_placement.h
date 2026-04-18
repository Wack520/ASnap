#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

namespace ais::ui {

[[nodiscard]] QPoint choosePanelPosition(const QRect& anchor, const QSize& panelSize, const QRect& screen);

}  // namespace ais::ui
