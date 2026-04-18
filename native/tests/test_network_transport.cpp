#include <memory>
#include <cstring>

#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtTest/QtTest>

#include "ai/qt_network_transport.h"

namespace {

using ais::ai::QtNetworkTransport;
using ais::ai::RequestSpec;

class FakeStreamReply final : public QNetworkReply {
public:
    FakeStreamReply(const QNetworkRequest& request, QByteArray payload, QObject* parent = nullptr)
        : QNetworkReply(parent),
          payload_(std::move(payload)) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/event-stream"));
    }

    void emitFinishedOnly() {
        setFinished(true);
        emit finished();
    }

    void abort() override {}

    [[nodiscard]] qint64 bytesAvailable() const override {
        return payload_.size() - offset_ + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (offset_ >= payload_.size()) {
            return -1;
        }

        const qint64 length = qMin(maxSize, payload_.size() - offset_);
        memcpy(data, payload_.constData() + offset_, static_cast<size_t>(length));
        offset_ += length;
        return length;
    }

private:
    QByteArray payload_;
    qint64 offset_ = 0;
};

class FakeAccessManager final : public QNetworkAccessManager {
public:
    QByteArray nextPayload;
    QNetworkRequest lastRequest;
    QByteArray lastBody;
    FakeStreamReply* lastReply = nullptr;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request, QIODevice* outgoingData) override {
        Q_UNUSED(op);
        lastRequest = request;
        lastBody.clear();
        if (outgoingData != nullptr) {
            lastBody = outgoingData->readAll();
        }
        lastReply = new FakeStreamReply(request, nextPayload, this);
        return lastReply;
    }
};

}  // namespace

class NetworkTransportTests final : public QObject {
    Q_OBJECT

private slots:
    void postStreamReadsRemainingPayloadOnFinishedAndSetsAcceptHeader();
};

void NetworkTransportTests::postStreamReadsRemainingPayloadOnFinishedAndSetsAcceptHeader() {
    auto manager = std::make_unique<FakeAccessManager>();
    manager->nextPayload = QByteArray(
        "event: response.completed\n"
        "data: {\"type\":\"response.completed\"}\n\n");
    FakeAccessManager* const managerPtr = manager.get();

    QtNetworkTransport transport(std::move(manager));

    QByteArray received;
    bool completed = false;
    QString failure;

    transport.postStream(
        RequestSpec{
            .url = QUrl(QStringLiteral("https://api.example.test/v1/responses")),
            .headers = {{QStringLiteral("Content-Type"), QStringLiteral("application/json")}},
            .body = QByteArray("{\"stream\":true}"),
        },
        [&](QByteArray chunk) { received += chunk; },
        [&]() { completed = true; },
        [&](QString error) { failure = std::move(error); });

    QVERIFY(managerPtr->lastReply != nullptr);
    QCOMPARE(managerPtr->lastRequest.rawHeader("Accept"), QByteArray("text/event-stream"));

    managerPtr->lastReply->emitFinishedOnly();

    QCOMPARE(received, managerPtr->nextPayload);
    QVERIFY(completed);
    QVERIFY(failure.isEmpty());
}

QTEST_MAIN(NetworkTransportTests)

#include "test_network_transport.moc"
