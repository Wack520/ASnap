from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Rect:
    left: int
    top: int
    width: int
    height: int

    @property
    def right(self) -> int:
        return self.left + self.width

    @property
    def bottom(self) -> int:
        return self.top + self.height

    @property
    def center_x(self) -> int:
        return self.left + self.width // 2

    @property
    def center_y(self) -> int:
        return self.top + self.height // 2


@dataclass(frozen=True, slots=True)
class Size:
    width: int
    height: int


def choose_panel_position(
    anchor: Rect,
    panel_size: Size,
    screen_rect: Rect,
    margin: int = 16,
) -> tuple[int, int]:
    candidates = [
        (anchor.right + margin, anchor.top + margin),
        (anchor.right + margin, anchor.bottom - panel_size.height),
        (anchor.left - panel_size.width - margin, anchor.top + margin),
        (anchor.left - panel_size.width - margin, anchor.bottom - panel_size.height),
        (anchor.center_x - panel_size.width // 2, anchor.bottom + margin),
        (anchor.center_x - panel_size.width // 2, anchor.top - panel_size.height - margin),
    ]

    for x, y in candidates:
        if _fits(x, y, panel_size, screen_rect):
            return x, y

    preferred_x, preferred_y = candidates[0]
    return (
        _clamp(preferred_x, screen_rect.left, screen_rect.right - panel_size.width),
        _clamp(preferred_y, screen_rect.top, screen_rect.bottom - panel_size.height),
    )


def _fits(x: int, y: int, panel_size: Size, screen_rect: Rect) -> bool:
    return (
        x >= screen_rect.left
        and y >= screen_rect.top
        and x + panel_size.width <= screen_rect.right
        and y + panel_size.height <= screen_rect.bottom
    )


def _clamp(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(value, maximum))
