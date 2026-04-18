#pragma once

#include <QByteArray>
#include <QObject>
#include <QAbstractNativeEventFilter>

#include "platform/windows/hotkey_chord.h"

namespace ais::platform::windows {

class GlobalHotkeyHost final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalHotkeyHost(int hotkeyId = 1, QObject* parent = nullptr);
    ~GlobalHotkeyHost() override;

    [[nodiscard]] bool registerHotkey(const HotkeyChord& chord);
    [[nodiscard]] bool registerHotkey(const QString& shortcut);
    void unregisterHotkey();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
signals:
    void triggered();

private:
    int hotkeyId_ = 1;
    bool registered_ = false;
};

}  // namespace ais::platform::windows
