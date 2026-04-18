#pragma once

#include <QByteArray>

namespace ais::ai {

class SampleImageFactory {
public:
    [[nodiscard]] static QByteArray buildPng();
};

}  // namespace ais::ai
