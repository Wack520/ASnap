#pragma once

namespace ais::app {

enum class BusyState {
    Idle,
    Capturing,
    RequestInFlight,
    TestingProvider,
};

}  // namespace ais::app
