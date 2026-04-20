#include "platform/windows/windows_display_topology.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QtGlobal>

#include <windows.h>

namespace ais::platform::windows {

namespace {

struct NativeMonitorDescriptor {
    QString deviceName;
    QRect monitorRect;
    bool isPrimary = false;
};

[[nodiscard]] QString normalizedDisplayName(QString name) {
    name = name.trimmed().toUpper();
    if (name.startsWith(QStringLiteral(R"(\\?\)"))) {
        name.remove(0, 4);
    }
    if (name.startsWith(QStringLiteral(R"(\\.\)"))) {
        name.remove(0, 4);
    }
    return name;
}

[[nodiscard]] QRect rectFromNative(const RECT& rect) {
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

[[nodiscard]] BOOL CALLBACK enumerateMonitorsProc(HMONITOR monitor,
                                                  HDC,
                                                  LPRECT,
                                                  LPARAM userData) {
    auto* monitors = reinterpret_cast<QList<NativeMonitorDescriptor>*>(userData);
    if (monitors == nullptr) {
        return TRUE;
    }

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    monitors->append(NativeMonitorDescriptor{
        .deviceName = QString::fromWCharArray(monitorInfo.szDevice),
        .monitorRect = rectFromNative(monitorInfo.rcMonitor),
        .isPrimary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
    });
    return TRUE;
}

[[nodiscard]] QList<NativeMonitorDescriptor> enumerateNativeMonitors() {
    QList<NativeMonitorDescriptor> monitors;
    EnumDisplayMonitors(nullptr,
                        nullptr,
                        &enumerateMonitorsProc,
                        reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

[[nodiscard]] bool physicalSizeMatches(QScreen* screen, const QRect& monitorRect) {
    if (screen == nullptr || !monitorRect.isValid()) {
        return false;
    }

    const QRect logicalRect = screen->geometry();
    const qreal devicePixelRatio = qMax(1.0, screen->devicePixelRatio());
    const QSize expectedPhysicalSize(qRound(logicalRect.width() * devicePixelRatio),
                                     qRound(logicalRect.height() * devicePixelRatio));
    return qAbs(expectedPhysicalSize.width() - monitorRect.width()) <= 1 &&
           qAbs(expectedPhysicalSize.height() - monitorRect.height()) <= 1;
}

[[nodiscard]] int findNamedScreenIndex(const NativeMonitorDescriptor& monitor,
                                       const QList<QScreen*>& screens,
                                       const QSet<int>& usedIndices) {
    const QString targetName = normalizedDisplayName(monitor.deviceName);
    for (int index = 0; index < screens.size(); ++index) {
        if (usedIndices.contains(index)) {
            continue;
        }

        QScreen* screen = screens.at(index);
        if (screen != nullptr && normalizedDisplayName(screen->name()) == targetName) {
            return index;
        }
    }

    return -1;
}

[[nodiscard]] int findPhysicalSizeScreenIndex(const NativeMonitorDescriptor& monitor,
                                              const QList<QScreen*>& screens,
                                              const QSet<int>& usedIndices) {
    int matchedIndex = -1;
    for (int index = 0; index < screens.size(); ++index) {
        if (usedIndices.contains(index)) {
            continue;
        }

        QScreen* screen = screens.at(index);
        if (!physicalSizeMatches(screen, monitor.monitorRect)) {
            continue;
        }

        if (matchedIndex != -1) {
            return -1;
        }

        matchedIndex = index;
    }

    return matchedIndex;
}

[[nodiscard]] int findSingleRemainingScreenIndex(const QList<QScreen*>& screens,
                                                 const QSet<int>& usedIndices) {
    int matchedIndex = -1;
    for (int index = 0; index < screens.size(); ++index) {
        if (usedIndices.contains(index)) {
            continue;
        }

        if (screens.at(index) == nullptr) {
            continue;
        }

        if (matchedIndex != -1) {
            return -1;
        }

        matchedIndex = index;
    }

    return matchedIndex;
}

[[nodiscard]] QList<capture::DisplayDescriptor> fallbackQtDisplays() {
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    QList<capture::DisplayDescriptor> displays;
    displays.reserve(screens.size());

    for (QScreen* screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const QRect geometry = screen->geometry();
        displays.append(capture::DisplayDescriptor{
            .deviceName = screen->name(),
            .monitorRect = geometry,
            .virtualRect = geometry,
            .devicePixelRatio = qMax(1.0, screen->devicePixelRatio()),
            .isPrimary = screen == primaryScreen,
        });
    }

    return displays;
}

}  // namespace

QList<capture::DisplayDescriptor> WindowsDisplayTopology::enumerateDisplays() const {
    const QList<NativeMonitorDescriptor> nativeMonitors = enumerateNativeMonitors();
    if (nativeMonitors.isEmpty()) {
        return fallbackQtDisplays();
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    QSet<int> usedScreenIndices;
    QList<capture::DisplayDescriptor> displays;
    displays.reserve(nativeMonitors.size());

    for (const NativeMonitorDescriptor& monitor : nativeMonitors) {
        int screenIndex = findNamedScreenIndex(monitor, screens, usedScreenIndices);
        if (screenIndex < 0) {
            screenIndex = findPhysicalSizeScreenIndex(monitor, screens, usedScreenIndices);
        }
        if (screenIndex < 0) {
            screenIndex = findSingleRemainingScreenIndex(screens, usedScreenIndices);
        }

        QScreen* screen = nullptr;
        if (screenIndex >= 0 && screenIndex < screens.size()) {
            screen = screens.at(screenIndex);
            usedScreenIndices.insert(screenIndex);
        }

        const QRect virtualRect = screen != nullptr ? screen->geometry() : monitor.monitorRect;
        const qreal devicePixelRatio = screen != nullptr ? qMax(1.0, screen->devicePixelRatio()) : 1.0;
        displays.append(capture::DisplayDescriptor{
            .deviceName = !monitor.deviceName.isEmpty()
                              ? monitor.deviceName
                              : (screen != nullptr ? screen->name() : QString()),
            .monitorRect = monitor.monitorRect,
            .virtualRect = virtualRect,
            .devicePixelRatio = devicePixelRatio,
            .isPrimary = monitor.isPrimary || screen == primaryScreen,
        });
    }

    return displays;
}

}  // namespace ais::platform::windows
