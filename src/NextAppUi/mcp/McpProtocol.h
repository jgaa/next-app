#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <variant>

namespace nextapp::mcp {

inline constexpr auto protocol_version = "2026-07-28";

struct ProtocolError {
    int code{};
    QString message;
    QJsonObject data;
};

struct Request {
    QJsonValue id;
    QString method;
    QJsonObject params;
    QJsonObject meta;
    QString tool_name;
};

using HeaderMap = QHash<QByteArray, QByteArray>;

// Validates the deliberately small, stateless MCP subset supported by NextApp.
// The HTTP adapter supplies normalized lower-case header names.
std::variant<Request, ProtocolError> parseRequest(const QByteArray& body,
                                                  const HeaderMap& headers);
QJsonObject response(const QJsonValue& id, const QJsonValue& result);
QJsonObject errorResponse(const QJsonValue& id, const ProtocolError& error);
QJsonObject toolList();

} // namespace nextapp::mcp
