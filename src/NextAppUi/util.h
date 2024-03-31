#pragma once

#include <QString>
#include <QUuid>

[[nodiscard]] QString toValidQuid(const QString& str);
[[nodiscard]] QUuid toQuid(const QString& str);
[[nodiscard]] QString toHourMin(const int duration);
[[nodiscard]] int parseDuration(const QString& value);

