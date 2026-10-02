#pragma once

#include <QDate>
#include <QJsonObject>
#include <variant>
#include "nextapp.qpb.h"

namespace nextapp::mcp {
// Pure normalization. Destination/category lookup and mutation authorization
// belong to the gateway; no database or backend writes happen here.
std::variant<nextapp::pb::Action, QJsonObject> normalizeSimpleAction(
    const QJsonObject& arguments, const nextapp::pb::UserGlobalSettings& settings,
    const QDate& today);
QJsonObject simpleActionError(const QString& field, const QJsonValue& value,
                              const QString& message);
} // namespace nextapp::mcp
