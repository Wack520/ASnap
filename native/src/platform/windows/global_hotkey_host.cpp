#include "platform/windows/global_hotkey_host.h"

#include <QCoreApplication>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ais::platform::windows {

GlobalHotkeyHost::GlobalHotkeyHost(const int hotkeyId, QObject* parent)
    : QObject(parent),
      hotkeyId_(hotkeyId) {
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

GlobalHotkeyHost::~GlobalHotkeyHost() {
    unregisterHotkey();
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

bool GlobalHotkeyHost::registerHotkey(const HotkeyChord& chord) {
    unregisterHotkey();

#if defined(_WIN32)
    if (chord.nativeVirtualKey() == 0) {
        return false;
    }

    registered_ = RegisterHotKey(
        nullptr,
        hotkeyId_,
        chord.nativeModifiers(),
        chord.nativeVirtualKey());
    return registered_;
#else
    Q_UNUSED(chord);
    return false;
#endif
}

bool GlobalHotkeyHost::registerHotkey(const QString& shortcut) {
    const auto chord = HotkeyChord::parse(shortcut);
    if (!chord.has_value()) {
        return false;
    }

    return registerHotkey(chord.value());
}

void GlobalHotkeyHost::unregisterHotkey() {
#if defined(_WIN32)
    if (registered_) {
        UnregisterHotKey(nullptr, hotkeyId_);
    }
#endif
    registered_ = false;
}

bool GlobalHotkeyHost::nativeEventFilter(const QByteArray& eventType,
                                         void* message,
                                         qintptr* result) {
#if defined(_WIN32)
    if (!registered_) {
        return false;
    }

    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG")) {
        return false;
    }

    MSG* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage == nullptr || nativeMessage->message != WM_HOTKEY ||
        nativeMessage->wParam != static_cast<WPARAM>(hotkeyId_)) {
        return false;
    }

    emit triggered();
    if (result != nullptr) {
        *result = 0;
    }
    return true;
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

}  // namespace ais::platform::windows
