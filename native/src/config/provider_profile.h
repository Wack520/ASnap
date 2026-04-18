#pragma once

#include <QString>

#include "config/provider_protocol.h"

namespace ais::config {

struct ProviderProfile {
    ProviderProtocol protocol = ProviderProtocol::OpenAiResponses;
    QString baseUrl;
    QString apiKey;
    QString model;

    bool operator==(const ProviderProfile&) const = default;
};

}  // namespace ais::config
