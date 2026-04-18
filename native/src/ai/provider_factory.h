#pragma once

#include <memory>

#include "ai/provider_interface.h"
#include "config/provider_protocol.h"

namespace ais::ai {

[[nodiscard]] std::unique_ptr<IProvider> makeProvider(config::ProviderProtocol protocol);

}  // namespace ais::ai
