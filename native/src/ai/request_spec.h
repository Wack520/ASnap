#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUrl>

namespace ais::ai {

struct RequestSpec {
    QUrl url;
    QHash<QString, QString> headers;
    QByteArray body;
};

}  // namespace ais::ai
