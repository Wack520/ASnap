#pragma once

#include <memory>

#include <QPointer>

#include "ai/network_transport.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace ais::ai {

class QtNetworkTransport final : public INetworkTransport {
public:
    QtNetworkTransport();
    explicit QtNetworkTransport(std::unique_ptr<QNetworkAccessManager> manager);
    ~QtNetworkTransport() override;

    void get(const RequestSpec& spec,
             SuccessHandler onSuccess,
             FailureHandler onFailure) override;
    void post(const RequestSpec& spec,
              SuccessHandler onSuccess,
              FailureHandler onFailure) override;
    void postStream(const RequestSpec& spec,
                    ChunkHandler onChunk,
                    CompletionHandler onComplete,
                    FailureHandler onFailure) override;
    void cancelActiveRequest() override;

private:
    std::unique_ptr<QNetworkAccessManager> manager_;
    QPointer<QNetworkReply> activeReply_;
};

}  // namespace ais::ai
