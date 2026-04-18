#pragma once

#include <functional>
#include <memory>

#include <QList>
#include <QString>

#include "ai/network_transport.h"
#include "app/request_guard.h"
#include "chat/chat_message.h"
#include "config/provider_profile.h"

namespace ais::ai {

class AiClient {
public:
    using DeltaHandler = std::function<void(QString)>;
    using SuccessHandler = std::function<void(QString)>;
    using CompletionHandler = std::function<void()>;
    using FailureHandler = std::function<void(QString)>;

    AiClient(std::unique_ptr<INetworkTransport> transport, app::RequestGuard& guard);
    ~AiClient();

    [[nodiscard]] bool sendConversation(const config::ProviderProfile& profile,
                                        const QList<chat::ChatMessage>& messages,
                                        SuccessHandler onSuccess,
                                        FailureHandler onFailure);
    [[nodiscard]] bool sendConversationStream(const config::ProviderProfile& profile,
                                              const QList<chat::ChatMessage>& messages,
                                              DeltaHandler onTextDelta,
                                              DeltaHandler onReasoningDelta,
                                              CompletionHandler onComplete,
                                              FailureHandler onFailure,
                                              int retryAttempt = 0);
    void cancelActiveRequest();

private:
    struct RequestState {
        bool cancelled = false;
    };

    std::unique_ptr<INetworkTransport> transport_;
    app::RequestGuard& guard_;
    std::shared_ptr<RequestState> activeRequest_;
};

}  // namespace ais::ai
