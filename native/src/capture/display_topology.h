#pragma once

#include <QList>

#include "capture/capture_pipeline_types.h"

namespace ais::capture {

class DisplayTopology {
public:
    virtual ~DisplayTopology() = default;

    [[nodiscard]] virtual QList<DisplayDescriptor> enumerateDisplays() const = 0;
};

}  // namespace ais::capture
