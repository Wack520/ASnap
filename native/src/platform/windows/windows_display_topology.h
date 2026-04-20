#pragma once

#include "capture/display_topology.h"

namespace ais::platform::windows {

class WindowsDisplayTopology final : public capture::DisplayTopology {
public:
    [[nodiscard]] QList<capture::DisplayDescriptor> enumerateDisplays() const override;
};

}  // namespace ais::platform::windows
