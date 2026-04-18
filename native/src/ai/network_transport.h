#pragma once

#include <functional>

#include <QByteArray>
#include <QString>

#include "ai/request_spec.h"

namespace ais::ai {

class INetworkTransport {
public:
    using ChunkHandler = std::function<void(QByteArray)>;
    using SuccessHandler = std::function<void(QByteArray)>;
    using CompletionHandler = std::function<void()>;
    using FailureHandler = std::function<void(QString)>;

    virtual ~INetworkTransport() = default;

    virtual void get(const RequestSpec& spec,
                     SuccessHandler onSuccess,
                     FailureHandler onFailure) {
        (void)spec;
        if (onFailure) {
            onFailure(QStringLiteral("GET is not supported by this transport"));
        }
    }
    virtual void post(const RequestSpec& spec,
                      SuccessHandler onSuccess,
                      FailureHandler onFailure) = 0;
    virtual void postStream(const RequestSpec& spec,
                            ChunkHandler onChunk,
                            CompletionHandler onComplete,
                            FailureHandler onFailure) = 0;
    virtual void cancelActiveRequest() = 0;
};

}  // namespace ais::ai
