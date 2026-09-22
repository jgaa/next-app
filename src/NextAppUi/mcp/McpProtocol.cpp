#include "McpProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace nextapp::mcp {
namespace {
ProtocolError invalid(QString message) { return {-32600, std::move(message), {}}; }
ProtocolError methodNotFound(QString method) {
    return {-32601, QStringLiteral("Unsupported MCP method: %1").arg(method), {}};
}

QJsonObject schema(QJsonObject properties, QJsonArray required = {}) {
    QJsonObject value{{QStringLiteral("type"), QStringLiteral("object")},
                      {QStringLiteral("properties"), std::move(properties)},
                      {QStringLiteral("additionalProperties"), false}};
    if (!required.isEmpty()) value.insert(QStringLiteral("required"), std::move(required));
    return value;
}

QJsonObject tool(QString name, QString description, QJsonObject input) {
    return {{QStringLiteral("name"), std::move(name)},
            {QStringLiteral("description"), std::move(description)},
            {QStringLiteral("inputSchema"), std::move(input)}};
}
} // namespace

std::variant<Request, ProtocolError> parseRequest(const QByteArray& body, const HeaderMap& headers)
{
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return invalid(QStringLiteral("Request must be a JSON-RPC object"));

    const auto object = document.object();
    if (object.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0"))
        return invalid(QStringLiteral("jsonrpc must be exactly '2.0'"));
    if (!object.contains(QStringLiteral("id")) || object.value(QStringLiteral("id")).isNull()
        || (!object.value(QStringLiteral("id")).isString() && !object.value(QStringLiteral("id")).isDouble()))
        return invalid(QStringLiteral("A string or numeric JSON-RPC id is required"));
    if (!object.value(QStringLiteral("method")).isString())
        return invalid(QStringLiteral("A JSON-RPC method is required"));
    if (object.contains(QStringLiteral("params")) && !object.value(QStringLiteral("params")).isObject())
        return invalid(QStringLiteral("params must be an object"));
    const auto params = object.value(QStringLiteral("params")).toObject();
    if (!params.value(QStringLiteral("_meta")).isObject())
        return invalid(QStringLiteral("Per-request MCP _meta is required"));

    Request request{object.value(QStringLiteral("id")), object.value(QStringLiteral("method")).toString(),
                    params, params.value(QStringLiteral("_meta")).toObject(), {}};
    const auto version = request.meta.value(QStringLiteral("protocolVersion")).toString();
    if (version != QString::fromLatin1(protocol_version))
        return invalid(QStringLiteral("Unsupported or missing MCP protocol version"));
    if (!request.meta.value(QStringLiteral("clientCapabilities")).isObject())
        return invalid(QStringLiteral("_meta.clientCapabilities is required"));

    const auto header = [&headers](const QByteArray& name) { return headers.value(name).trimmed(); };
    if (header("mcp-protocol-version") != protocol_version)
        return invalid(QStringLiteral("MCP-Protocol-Version does not match request metadata"));
    if (header("mcp-method") != request.method.toUtf8())
        return invalid(QStringLiteral("Mcp-Method does not match JSON-RPC method"));

    if (request.method == QStringLiteral("tools/call")) {
        request.tool_name = request.params.value(QStringLiteral("name")).toString();
        if (request.tool_name.isEmpty() || !request.params.value(QStringLiteral("arguments")).isObject())
            return invalid(QStringLiteral("tools/call requires name and object arguments"));
        if (header("mcp-name") != request.tool_name.toUtf8())
            return invalid(QStringLiteral("Mcp-Name does not match tools/call name"));
    } else {
        if (!header("mcp-name").isEmpty())
            return invalid(QStringLiteral("Mcp-Name is only valid for tools/call"));
        if (request.method != QStringLiteral("tools/list")) return methodNotFound(request.method);
    }
    return request;
}

QJsonObject response(const QJsonValue& id, const QJsonValue& result) {
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
}

QJsonObject errorResponse(const QJsonValue& id, const ProtocolError& error) {
    QJsonObject value{{QStringLiteral("code"), error.code}, {QStringLiteral("message"), error.message}};
    if (!error.data.isEmpty()) value.insert(QStringLiteral("data"), error.data);
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), value}};
}

QJsonObject toolList()
{
    const auto string = [](int max = -1) { QJsonObject value{{QStringLiteral("type"), QStringLiteral("string")}}; if (max >= 0) value.insert(QStringLiteral("maxLength"), max); return value; };
    const auto uuid = [&string] { auto value = string(); value.insert(QStringLiteral("format"), QStringLiteral("uuid")); return value; };
    const auto integer = [](int minimum, int maximum) { return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), minimum}, {QStringLiteral("maximum"), maximum}}; };
    const auto page = schema(QJsonObject{{QStringLiteral("pageSize"), integer(1, 50)}, {QStringLiteral("cursor"), string(256)}});
    QJsonArray tools;
    tools.append(tool(QStringLiteral("get_action"), QStringLiteral("Read one synchronized action by UUID."),
                      schema(QJsonObject{{QStringLiteral("id"), uuid()}}, {QStringLiteral("id")})));
    tools.append(tool(QStringLiteral("get_list_actions"), QStringLiteral("Read a bounded page of synchronized actions."), page));
    tools.append(tool(QStringLiteral("search_actions"), QStringLiteral("Search action names in the local synchronized cache."),
                      schema(QJsonObject{{QStringLiteral("query"), string(256)}, {QStringLiteral("pageSize"), integer(1, 50)}}, {QStringLiteral("query")})));
    tools.append(tool(QStringLiteral("create_action"), QStringLiteral("Create an action after the configured local policy permits it."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), string(256)}, {QStringLiteral("nodeId"), uuid()}, {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 255}}}, {QStringLiteral("description"), string()}}, {QStringLiteral("idempotencyKey"), QStringLiteral("nodeId"), QStringLiteral("name")})));
    tools.append(tool(QStringLiteral("update_action"), QStringLiteral("Patch an action after the configured local policy permits it."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), string(256)}, {QStringLiteral("id"), uuid()}, {QStringLiteral("baseVersion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}, {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 255}}}, {QStringLiteral("description"), string()}}, {QStringLiteral("idempotencyKey"), QStringLiteral("id"), QStringLiteral("baseVersion")})));
    tools.append(tool(QStringLiteral("complete_action"), QStringLiteral("Set action completion state after the configured local policy permits it."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), string(256)}, {QStringLiteral("id"), uuid()}, {QStringLiteral("baseVersion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}, {QStringLiteral("done"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}, {QStringLiteral("idempotencyKey"), QStringLiteral("id"), QStringLiteral("baseVersion"), QStringLiteral("done")})));
    return {{QStringLiteral("tools"), tools}};
}
} // namespace nextapp::mcp
