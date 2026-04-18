#pragma once

#include <functional>
#include <memory>

#include <QList>
#include <QString>
#include <QStringList>

#include "ai/network_transport.h"
#include "app/request_guard.h"
#include "chat/chat_message.h"
#include "config/provider_profile.h"

namespace ais::ai {

class ProviderTestRunner {
public:
    using SuccessHandler = std::function<void(QString)>;
    using ModelsHandler = std::function<void(QStringList)>;
    using FailureHandler = std::function<void(QString)>;

    ProviderTestRunner(std::unique_ptr<INetworkTransport> transport, app::RequestGuard& guard);
    ~ProviderTestRunner();

    [[nodiscard]] bool runTextTest(const config::ProviderProfile& profile,
                                   SuccessHandler onSuccess,
                                   FailureHandler onFailure);
    [[nodiscard]] bool runImageTest(const config::ProviderProfile& profile,
                                    SuccessHandler onSuccess,
                                    FailureHandler onFailure);
    [[nodiscard]] bool fetchModels(const config::ProviderProfile& profile,
                                   ModelsHandler onSuccess,
                                   FailureHandler onFailure);

private:
    [[nodiscard]] bool runMessages(const config::ProviderProfile& profile,
                                   const QList<chat::ChatMessage>& messages,
                                   SuccessHandler onSuccess,
                                   FailureHandler onFailure);

    std::unique_ptr<INetworkTransport> transport_;
    app::RequestGuard& guard_;
};

}  // namespace ais::ai
