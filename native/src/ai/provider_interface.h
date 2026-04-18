#pragma once

#include <QList>
#include <QString>

#include "ai/request_spec.h"
#include "chat/chat_message.h"
#include "config/provider_profile.h"

namespace ais::ai {

class IProvider {
public:
    virtual ~IProvider() = default;

    [[nodiscard]] virtual RequestSpec buildRequest(const config::ProviderProfile& profile,
                                                   const QList<chat::ChatMessage>& messages) const = 0;
    [[nodiscard]] virtual QString parseTextResponse(const QByteArray& payload) const = 0;
};

}  // namespace ais::ai
