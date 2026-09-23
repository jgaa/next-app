#include "McpProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace nextapp::mcp {
namespace {
ProtocolError invalid(QString message) { return {-32600, std::move(message), {}}; }
ProtocolError methodNotFound(QString method) {
    return {-32601, QStringLiteral("Unsupported MCP method: %1").arg(method), {}};
}
QByteArray decodedNameHeader(const QByteArray& value) {
    if (!value.startsWith("=?base64?") || !value.endsWith("?=")) return value;
    const auto encoded = value.mid(9, value.size() - 11);
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    return decoded ? decoded.decoded : QByteArray{};
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

QJsonObject serverInfo() {
    return {{QStringLiteral("name"), QStringLiteral("NextApp")},
            {QStringLiteral("version"), QString::fromLatin1(NEXTAPP_VERSION)}};
}

QString serverInstructions() {
    return QStringLiteral("NextApp is a personal organizer for task and project management. "
                          "An action in NextApp is a task or action item. "
                          "A list is a NextApp node; lists, folders, and projects are nodes. "
                          "When creating an action, omit nodeId for Inbox; only supply a nodeId returned by nextapp_list_nodes. "
                          "If a mutation returns PENDING, poll nextapp_get_request_status with requestId. Never retry it with a new idempotencyKey.");
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
    const auto method = object.value(QStringLiteral("method")).toString();
    const auto notification = method.startsWith(QStringLiteral("notifications/"))
        && !object.contains(QStringLiteral("id"));
    if ((!notification && (!object.contains(QStringLiteral("id")) || object.value(QStringLiteral("id")).isNull()
        || (!object.value(QStringLiteral("id")).isString() && !object.value(QStringLiteral("id")).isDouble()))))
        return invalid(QStringLiteral("A string or numeric JSON-RPC id is required"));
    if (!object.value(QStringLiteral("method")).isString())
        return invalid(QStringLiteral("A JSON-RPC method is required"));
    if (object.contains(QStringLiteral("params")) && !object.value(QStringLiteral("params")).isObject())
        return invalid(QStringLiteral("params must be an object"));
    const auto params = object.value(QStringLiteral("params")).toObject();
    const auto header = [&headers](const QByteArray& name) { return headers.value(name).trimmed(); };
    Request request{object.value(QStringLiteral("id")), method, params, {}, {}, {}, notification};

    if (request.method == QStringLiteral("initialize")) {
        const auto version = params.value(QStringLiteral("protocolVersion")).toString();
        if (version.isEmpty() || !params.value(QStringLiteral("capabilities")).isObject())
            return ProtocolError{-32602, QStringLiteral("initialize requires protocolVersion and capabilities"), {}};
        // A legacy client can accept the version offered by the server or
        // reject it. Never claim to implement a revision we have not checked.
        request.protocol_version = QString::fromLatin1(legacy_protocol_version);
        return request;
    }

    if (params.contains(QStringLiteral("_meta")) && !params.value(QStringLiteral("_meta")).isObject())
        return ProtocolError{-32602, QStringLiteral("_meta must be an object"), {}};
    request.meta = params.value(QStringLiteral("_meta")).toObject();
    const auto has_current_protocol_metadata = request.meta.contains(QString::fromLatin1(protocol_version_key))
        || request.meta.contains(QString::fromLatin1(client_capabilities_key))
        || request.meta.contains(QStringLiteral("protocolVersion"))
        || request.meta.contains(QStringLiteral("clientCapabilities"));
    if (has_current_protocol_metadata) {
        // Accept the earlier experimental unqualified spelling as input.
        // All modern responses use the published namespaced spelling.
        const auto version = request.meta.value(request.meta.contains(QString::fromLatin1(protocol_version_key))
            ? QString::fromLatin1(protocol_version_key) : QStringLiteral("protocolVersion")).toString();
        const auto capabilities = request.meta.value(request.meta.contains(QString::fromLatin1(client_capabilities_key))
            ? QString::fromLatin1(client_capabilities_key) : QStringLiteral("clientCapabilities"));
        if (version.isEmpty() || !capabilities.isObject())
            return ProtocolError{-32602, QStringLiteral("Missing required per-request MCP metadata"), {}};
        if (version != QString::fromLatin1(protocol_version))
            return ProtocolError{-32022, QStringLiteral("Unsupported protocol version"),
                {{QStringLiteral("supported"), QJsonArray{QString::fromLatin1(protocol_version),
                    QString::fromLatin1(legacy_protocol_version)}},
                 {QStringLiteral("requested"), version}}};
        if (header("mcp-protocol-version") != version.toUtf8()
            || header("mcp-method") != request.method.toUtf8())
            return ProtocolError{-32020, QStringLiteral("MCP request headers do not match the body"), {}};
        request.protocol_version = version;
    } else {
        // The 2025-03-26 Streamable HTTP revision used by Jan negotiates the
        // protocol in initialize and does not require a version header on
        // later stateless POSTs. Accept the header when a client sends it,
        // but reject a conflicting revision.
        const auto legacy_header = header("mcp-protocol-version");
        if (legacy_header == protocol_version)
            return ProtocolError{-32602, QStringLiteral("Missing required per-request MCP metadata"), {}};
        if (!legacy_header.isEmpty() && legacy_header != legacy_protocol_version)
            return ProtocolError{-32022, QStringLiteral("Unsupported protocol version"),
                {{QStringLiteral("supported"), QJsonArray{QString::fromLatin1(protocol_version),
                    QString::fromLatin1(legacy_protocol_version)}},
                 {QStringLiteral("requested"), QString::fromLatin1(legacy_header)}}};
        request.protocol_version = legacy_header.isEmpty() ? QString::fromLatin1(legacy_protocol_version)
            : QString::fromLatin1(legacy_header);
        if (!header("mcp-method").isEmpty() && header("mcp-method") != request.method.toUtf8())
            return ProtocolError{-32020, QStringLiteral("Mcp-Method does not match the body"), {}};
    }

    if (request.notification) return request;

    if (request.method == QStringLiteral("server/discover") && !has_current_protocol_metadata)
        return methodNotFound(request.method);
    if (request.method == QStringLiteral("ping") && has_current_protocol_metadata)
        return methodNotFound(request.method);

    if (request.method == QStringLiteral("tools/call")) {
        request.tool_name = request.params.value(QStringLiteral("name")).toString();
        if (request.tool_name.isEmpty() || (request.params.contains(QStringLiteral("arguments"))
            && !request.params.value(QStringLiteral("arguments")).isObject()))
            return ProtocolError{-32602, QStringLiteral("tools/call requires a name and object arguments"), {}};
        if ((has_current_protocol_metadata || !header("mcp-name").isEmpty())
            && decodedNameHeader(header("mcp-name")) != request.tool_name.toUtf8())
            return ProtocolError{-32020, QStringLiteral("Mcp-Name does not match tools/call name"), {}};
    } else {
        if (has_current_protocol_metadata && !header("mcp-name").isEmpty())
            return ProtocolError{-32020, QStringLiteral("Unexpected Mcp-Name header"), {}};
        if (request.method != QStringLiteral("tools/list")
            && request.method != QStringLiteral("server/discover")
            && request.method != QStringLiteral("ping")
            && request.method != QStringLiteral("resources/list")
            && request.method != QStringLiteral("resources/templates/list")
            && request.method != QStringLiteral("prompts/list")) return methodNotFound(request.method);
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

QJsonObject initializeResult(const QString& negotiated_protocol_version) {
    return {{QStringLiteral("protocolVersion"), negotiated_protocol_version},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("serverInfo"), serverInfo()},
            {QStringLiteral("instructions"), serverInstructions()}};
}

QJsonObject discoverResult() {
    return {{QStringLiteral("resultType"), QStringLiteral("complete")},
            {QStringLiteral("supportedVersions"), QJsonArray{QString::fromLatin1(protocol_version),
                QString::fromLatin1(legacy_protocol_version)}},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("_meta"), QJsonObject{{QStringLiteral("io.modelcontextprotocol/serverInfo"),
                serverInfo()}}},
            {QStringLiteral("instructions"), serverInstructions()},
            {QStringLiteral("ttlMs"), 0}, {QStringLiteral("cacheScope"), QStringLiteral("private")}};
}

QJsonObject toolList()
{
    const auto string = [](int max = -1) { QJsonObject value{{QStringLiteral("type"), QStringLiteral("string")}}; if (max >= 0) value.insert(QStringLiteral("maxLength"), max); return value; };
    const auto uuid = [&string] { auto value = string(); value.insert(QStringLiteral("format"), QStringLiteral("uuid")); return value; };
    const auto integer = [](int minimum, int maximum) { return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), minimum}, {QStringLiteral("maximum"), maximum}}; };
    const auto page = schema(QJsonObject{{QStringLiteral("pageSize"), integer(1, 50)}, {QStringLiteral("cursor"), string(256)}});
    const auto node_format = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("short"), QStringLiteral("full")}}};
    QJsonArray tools;
    tools.append(tool(QStringLiteral("nextapp_get_action"), QStringLiteral("Get a NextApp action (task/action item) by ID, including its status and version."),
                      schema(QJsonObject{{QStringLiteral("id"), uuid()}}, {QStringLiteral("id")})));
    tools.append(tool(QStringLiteral("nextapp_get_request_status"), QStringLiteral("Check a pending NextApp MCP mutation by requestId; returns its current state and final result. Do not retry with a new idempotencyKey."),
                      schema(QJsonObject{{QStringLiteral("requestId"), uuid()}}, {QStringLiteral("requestId")})));
    tools.append(tool(QStringLiteral("nextapp_list_actions"), QStringLiteral("List recent NextApp actions (tasks/action items) from the local cache."), page));
    tools.append(tool(QStringLiteral("nextapp_list_nodes"), QStringLiteral("List active NextApp lists/nodes (folders or projects). Short form is default; use returned nodeId for actions or child lists."),
                      schema(QJsonObject{{QStringLiteral("pageSize"), integer(1, 50)},
                          {QStringLiteral("cursor"), uuid()}, {QStringLiteral("format"), node_format}})));
    tools.append(tool(QStringLiteral("nextapp_search_nodes"), QStringLiteral("Search active NextApp lists/nodes by name; short form is default. Use a returned nodeId, never invent one."),
                      schema(QJsonObject{{QStringLiteral("query"), string(256)}, {QStringLiteral("pageSize"), integer(1, 50)},
                          {QStringLiteral("cursor"), uuid()}, {QStringLiteral("format"), node_format}}, {QStringLiteral("query")})));
    tools.append(tool(QStringLiteral("nextapp_list_categories"), QStringLiteral("List NextApp action categories (labels used to organize actions and lists/nodes)."),
                      schema(QJsonObject{{QStringLiteral("pageSize"), integer(1, 50)}, {QStringLiteral("cursor"), uuid()}})));
    tools.append(tool(QStringLiteral("nextapp_search_categories"), QStringLiteral("Search NextApp action categories by name; use a returned categoryId for a list/node."),
                      schema(QJsonObject{{QStringLiteral("query"), string(256)}, {QStringLiteral("pageSize"), integer(1, 50)},
                          {QStringLiteral("cursor"), uuid()}}, {QStringLiteral("query")})));
    tools.append(tool(QStringLiteral("nextapp_search_actions"), QStringLiteral("Search NextApp actions (tasks/action items) by name in the local cache."),
                      schema(QJsonObject{{QStringLiteral("query"), string(256)}, {QStringLiteral("pageSize"), integer(1, 50)}}, {QStringLiteral("query")})));
    const auto reason = string(2048);
    const auto idempotency_key = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("maxLength"), 256},
        {QStringLiteral("description"), QStringLiteral("Stable key for one intended mutation; reuse on retry. Never use a new key while its request is PENDING.")}};
    tools.append(tool(QStringLiteral("nextapp_create_action"), QStringLiteral("Create a NextApp action (task/action item). Omit nodeId for Inbox, or use an existing node from nextapp_list_nodes; approval may be required."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), idempotency_key},
                          {QStringLiteral("nodeId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("format"), QStringLiteral("uuid")},
                              {QStringLiteral("description"), QStringLiteral("Optional existing destination UUID from nextapp_list_nodes. Omit to use Inbox; never invent a UUID.")}}},
                          {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 255}}}, {QStringLiteral("description"), string()}, {QStringLiteral("reason"), reason}}, {QStringLiteral("idempotencyKey"), QStringLiteral("name")})));
    const auto kind = QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("folder"), QStringLiteral("organization"),
            QStringLiteral("person"), QStringLiteral("project"), QStringLiteral("task")}}};
    tools.append(tool(QStringLiteral("nextapp_create_node"), QStringLiteral("Create a NextApp list/node (folder or project); kind defaults to folder. Omit parentId for a top-level list. Approval may be required."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), idempotency_key}, {QStringLiteral("name"), string(128)},
                          {QStringLiteral("description"), string()}, {QStringLiteral("kind"), kind},
                          {QStringLiteral("parentId"), uuid()}, {QStringLiteral("categoryId"), uuid()},
                          {QStringLiteral("reason"), reason}}, {QStringLiteral("idempotencyKey"), QStringLiteral("name")})));
    tools.append(tool(QStringLiteral("nextapp_update_node"), QStringLiteral("Patch a NextApp list/node name, description, kind, active state, or category. Get baseVersion with nextapp_list_nodes format=full. Cannot move or delete it; approval may be required."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), idempotency_key}, {QStringLiteral("nodeId"), uuid()},
                          {QStringLiteral("baseVersion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                          {QStringLiteral("name"), string(128)}, {QStringLiteral("description"), string()},
                          {QStringLiteral("kind"), kind}, {QStringLiteral("active"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                          {QStringLiteral("categoryId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("description"), QStringLiteral("Category UUID from nextapp_list_categories; empty string clears it.")}}},
                          {QStringLiteral("reason"), reason}},
                          {QStringLiteral("idempotencyKey"), QStringLiteral("nodeId"), QStringLiteral("baseVersion")})));
    tools.append(tool(QStringLiteral("nextapp_update_action"), QStringLiteral("Change specified name or description fields of a NextApp action (task/action item); approval may be required."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), idempotency_key}, {QStringLiteral("id"), uuid()}, {QStringLiteral("baseVersion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}, {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), 255}}}, {QStringLiteral("description"), string()}, {QStringLiteral("reason"), reason}}, {QStringLiteral("idempotencyKey"), QStringLiteral("id"), QStringLiteral("baseVersion")})));
    tools.append(tool(QStringLiteral("nextapp_complete_action"), QStringLiteral("Mark a NextApp action (task/action item) done or reopen it; approval may be required."),
                      schema(QJsonObject{{QStringLiteral("idempotencyKey"), idempotency_key}, {QStringLiteral("id"), uuid()}, {QStringLiteral("baseVersion"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}, {QStringLiteral("done"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}, {QStringLiteral("reason"), reason}}, {QStringLiteral("idempotencyKey"), QStringLiteral("id"), QStringLiteral("baseVersion"), QStringLiteral("done")})));
    return {{QStringLiteral("tools"), tools}};
}
} // namespace nextapp::mcp
