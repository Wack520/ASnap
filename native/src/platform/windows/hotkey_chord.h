#pragma once

#include <optional>

#include <QString>
#include <Qt>

namespace ais::platform::windows {

struct HotkeyChord {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    Qt::Key key = Qt::Key_unknown;

    [[nodiscard]] static std::optional<HotkeyChord> parse(const QString& shortcut);
    [[nodiscard]] unsigned int nativeModifiers() const noexcept;
    [[nodiscard]] unsigned int nativeVirtualKey() const noexcept;
};

}  // namespace ais::platform::windows
