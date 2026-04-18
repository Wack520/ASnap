#pragma once

#include "app/app_busy_state.h"

namespace ais::app {

class RequestGuard {
public:
    [[nodiscard]] BusyState state() const noexcept { return state_; }

    [[nodiscard]] bool isBusy() const noexcept { return state_ != BusyState::Idle; }

    [[nodiscard]] bool tryEnter(BusyState next) noexcept {
        if (next == BusyState::Idle || state_ != BusyState::Idle) {
            return false;
        }

        state_ = next;
        return true;
    }

    void leave(BusyState expected) noexcept {
        if (state_ == expected) {
            state_ = BusyState::Idle;
        }
    }

private:
    BusyState state_ = BusyState::Idle;
};

}  // namespace ais::app
