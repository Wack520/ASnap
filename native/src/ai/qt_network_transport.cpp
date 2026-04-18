#include "ai/qt_network_transport.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>

namespace ais::ai {

namespace {

[[nodiscard]] QString trimmedResponseBody(const QByteArray& payload) {
    const QString body = QString::fromUtf8(payload).trimmed();
    if (body.isEmpty()) {
        return {};
    }

    constexpr int kMaxBodyLength = 1200;
    if (body.size() <= kMaxBodyLength) {
        return body;
    }

    return body.left(kMaxBodyLength) + QStringLiteral("...");
}

[[nodiscard]] QString formatFailureMessage(QNetworkReply* reply, const QByteArray& payload) {
    const QString body = trimmedResponseBody(payload);
    const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString().trimmed();

    if (statusCode.isValid()) {
        QString message = QStringLiteral("HTTP %1").arg(statusCode.toInt());
        if (!reason.isEmpty()) {
            message += QStringLiteral(" %1").arg(reason);
        }
        if (!body.isEmpty()) {
            message += QStringLiteral(": %1").arg(body);
        }
        return message;
    }

    QString message = reply->errorString();
    if (!body.isEmpty()) {
        message += QStringLiteral(": %1").arg(body);
    }
    return message;
}

}  // namespace

QtNetworkTransport::QtNetworkTransport()
    : manager_(std::make_unique<QNetworkAccessManager>()) {}

QtNetworkTransport::QtNetworkTransport(std::unique_ptr<QNetworkAccessManager> manager)
    : manager_(manager ? std::move(manager) : std::make_unique<QNetworkAccessManager>()) {}

QtNetworkTransport::~QtNetworkTransport() = default;

void QtNetworkTransport::get(const RequestSpec& spec,
                             SuccessHandler onSuccess,
                             FailureHandler onFailure) {
    QNetworkRequest request(spec.url);
    for (auto it = spec.headers.cbegin(); it != spec.headers.cend(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply* const reply = manager_->get(request);
    activeReply_ = reply;
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, reply, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)]() mutable {
                         const QByteArray payload = reply->readAll();
                         if (activeReply_ == reply) {
                             activeReply_.clear();
                         }
                         if (reply->error() == QNetworkReply::NoError) {
                             if (onSuccess) {
                                 onSuccess(payload);
                             }
                         } else if (onFailure) {
                             onFailure(formatFailureMessage(reply, payload));
                         }

                         reply->deleteLater();
                     });
}

void QtNetworkTransport::post(const RequestSpec& spec,
                              SuccessHandler onSuccess,
                              FailureHandler onFailure) {
    QNetworkRequest request(spec.url);
    for (auto it = spec.headers.cbegin(); it != spec.headers.cend(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (!spec.headers.contains(QStringLiteral("Content-Type"))) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }

    QNetworkReply* const reply = manager_->post(request, spec.body);
    activeReply_ = reply;
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, reply, onSuccess = std::move(onSuccess), onFailure = std::move(onFailure)]() mutable {
                         const QByteArray payload = reply->readAll();
                         if (activeReply_ == reply) {
                             activeReply_.clear();
                         }
                         if (reply->error() == QNetworkReply::NoError) {
                             if (onSuccess) {
                                 onSuccess(payload);
                             }
                         } else if (onFailure) {
                             onFailure(formatFailureMessage(reply, payload));
                         }

                         reply->deleteLater();
                     });
}

void QtNetworkTransport::postStream(const RequestSpec& spec,
                                    ChunkHandler onChunk,
                                    CompletionHandler onComplete,
                                    FailureHandler onFailure) {
    QNetworkRequest request(spec.url);
    for (auto it = spec.headers.cbegin(); it != spec.headers.cend(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    if (!spec.headers.contains(QStringLiteral("Content-Type"))) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    if (!spec.headers.contains(QStringLiteral("Accept"))) {
        request.setRawHeader("Accept", "text/event-stream");
    }

    QNetworkReply* const reply = manager_->post(request, spec.body);
    activeReply_ = reply;
    auto accumulated = std::make_shared<QByteArray>();
    auto deliverChunk = std::make_shared<ChunkHandler>(std::move(onChunk));

    QObject::connect(reply, &QIODevice::readyRead, reply,
                     [reply, accumulated, deliverChunk]() mutable {
                         const QByteArray chunk = reply->readAll();
                         if (chunk.isEmpty()) {
                             return;
                         }

                         accumulated->append(chunk);
                         if (*deliverChunk) {
                             (*deliverChunk)(chunk);
                         }
                     });

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, reply, accumulated, deliverChunk, onComplete = std::move(onComplete), onFailure = std::move(onFailure)]() mutable {
                         const QByteArray tail = reply->readAll();
                         if (!tail.isEmpty()) {
                             accumulated->append(tail);
                             if (*deliverChunk) {
                                 (*deliverChunk)(tail);
                             }
                         }
                         if (activeReply_ == reply) {
                             activeReply_.clear();
                         }
                         if (reply->error() == QNetworkReply::NoError) {
                             if (onComplete) {
                                 onComplete();
                             }
                         } else if (onFailure) {
                             onFailure(formatFailureMessage(reply, *accumulated));
                         }

                         reply->deleteLater();
                     });
}

void QtNetworkTransport::cancelActiveRequest() {
    if (activeReply_.isNull()) {
        return;
    }

    activeReply_->setProperty("ais_cancelled", true);
    activeReply_->abort();
    activeReply_.clear();
}

}  // namespace ais::ai
