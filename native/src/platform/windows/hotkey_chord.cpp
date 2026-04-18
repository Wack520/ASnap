#include "platform/windows/hotkey_chord.h"

#include <QStringList>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ais::platform::windows {

namespace {

[[nodiscard]] std::optional<Qt::KeyboardModifier> parseModifier(const QString& token) {
    if (token == QStringLiteral("alt")) {
        return Qt::AltModifier;
    }
    if (token == QStringLiteral("ctrl") || token == QStringLiteral("control")) {
        return Qt::ControlModifier;
    }
    if (token == QStringLiteral("shift")) {
        return Qt::ShiftModifier;
    }
    if (token == QStringLiteral("win") || token == QStringLiteral("meta")) {
        return Qt::MetaModifier;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<Qt::Key> parseKeyToken(const QString& token) {
    if (token == QStringLiteral("space")) {
        return Qt::Key_Space;
    }
    if (token == QStringLiteral("tab")) {
        return Qt::Key_Tab;
    }
    if (token == QStringLiteral("enter") || token == QStringLiteral("return")) {
        return Qt::Key_Return;
    }
    if (token == QStringLiteral("esc") || token == QStringLiteral("escape")) {
        return Qt::Key_Escape;
    }

    if (token.size() == 1) {
        const QChar character = token.at(0).toUpper();
        if (character.isLetterOrNumber()) {
            return static_cast<Qt::Key>(character.unicode());
        }
    }

    if (token.startsWith(QLatin1Char('f'))) {
        bool ok = false;
        const int functionIndex = token.sliced(1).toInt(&ok);
        if (ok && functionIndex >= 1 && functionIndex <= 24) {
            return static_cast<Qt::Key>(Qt::Key_F1 + (functionIndex - 1));
        }
    }

    return std::nullopt;
}

}  // namespace

std::optional<HotkeyChord> HotkeyChord::parse(const QString& shortcut) {
    const QStringList tokens = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return std::nullopt;
    }

    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    std::optional<Qt::Key> parsedKey;

    for (QString token : tokens) {
        token = token.trimmed().toLower();
        if (token.isEmpty()) {
            continue;
        }

        if (const auto modifier = parseModifier(token); modifier.has_value()) {
            modifiers |= modifier.value();
            continue;
        }

        if (parsedKey.has_value()) {
            return std::nullopt;
        }

        parsedKey = parseKeyToken(token);
        if (!parsedKey.has_value()) {
            return std::nullopt;
        }
    }

    if (!parsedKey.has_value()) {
        return std::nullopt;
    }

    return HotkeyChord{
        .modifiers = modifiers,
        .key = parsedKey.value(),
    };
}

unsigned int HotkeyChord::nativeModifiers() const noexcept {
    unsigned int modifiersValue = 0;

#if defined(_WIN32)
    if (modifiers.testFlag(Qt::AltModifier)) {
        modifiersValue |= MOD_ALT;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        modifiersValue |= MOD_CONTROL;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        modifiersValue |= MOD_SHIFT;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        modifiersValue |= MOD_WIN;
    }
    modifiersValue |= MOD_NOREPEAT;
#endif

    return modifiersValue;
}

unsigned int HotkeyChord::nativeVirtualKey() const noexcept {
#if defined(_WIN32)
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<unsigned int>(key);
    }

    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<unsigned int>(key);
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<unsigned int>(VK_F1 + (key - Qt::Key_F1));
    }

    switch (key) {
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    default:
        return 0;
    }
#else
    return 0;
#endif
}

}  // namespace ais::platform::windows
