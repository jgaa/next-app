#include "McpGateway.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProtobufSerializer>
#include <QRandomGenerator>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QCoroFuture>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "RuntimeServices.h"
#include "ActionInfoCache.h"
#include "logging.h"

namespace nextapp::mcp {
namespace {
constexpr auto ai_enabled = "ai/enabled";
constexpr auto mcp_enabled = "ai/mcp/enabled";
constexpr auto credential_key = "ai/mcp/credential";
QString randomCredential() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (auto offset = 0; offset < bytes.size(); offset += int(sizeof(quint32))) {
        const auto word = QRandomGenerator::system()->generate();
        memcpy(bytes.data() + offset, &word, std::min<int>(sizeof(word), bytes.size() - offset));
    }
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
QJsonObject actionObject(const QList<QVariant>& row) {
    return {{QStringLiteral("id"), row.at(0).toString()}, {QStringLiteral("nodeId"), row.at(1).toString()},
            {QStringLiteral("name"), row.at(2).toString()}, {QStringLiteral("description"), row.at(3).toString()},
            {QStringLiteral("status"), row.at(4).toInt()}, {QStringLiteral("completedAt"), row.at(5).toString()},
            {QStringLiteral("version"), row.at(6).toInt()}, {QStringLiteral("updatedAt"), row.at(7).toLongLong()}};
}
QJsonObject modernResult(QJsonObject result) {
    result.insert(QStringLiteral("resultType"), QStringLiteral("complete"));
    result.insert(QStringLiteral("_meta"), QJsonObject{{QStringLiteral("io.modelcontextprotocol/serverInfo"),
        QJsonObject{{QStringLiteral("name"), QStringLiteral("NextApp")},
                    {QStringLiteral("version"), QString::fromLatin1(NEXTAPP_VERSION)}}}});
    return result;
}
bool validBaseVersion(const QJsonObject& arguments) {
    const auto value = arguments.value(QStringLiteral("baseVersion"));
    if (!value.isDouble()) return false;
    const auto number = value.toDouble();
    return std::isfinite(number) && number >= 0 && number <= std::numeric_limits<int>::max()
        && std::floor(number) == number;
}
bool validOptionalString(const QJsonObject& arguments, const QString& key) {
    return !arguments.contains(key) || arguments.value(key).isString();
}
bool onlyFields(const QJsonObject& arguments, const QSet<QString>& allowed) {
    for (auto it = arguments.begin(); it != arguments.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
QString nodeKind(nextapp::pb::Node::Kind kind) {
    using Kind = nextapp::pb::Node::Kind;
    switch (kind) {
    case Kind::FOLDER: return QStringLiteral("folder");
    case Kind::ORGANIZATION: return QStringLiteral("organization");
    case Kind::PERSON: return QStringLiteral("person");
    case Kind::PROJECT: return QStringLiteral("project");
    case Kind::TASK: return QStringLiteral("task");
    }
    return QStringLiteral("unknown");
}
std::optional<nextapp::pb::Node::Kind> parseNodeKind(const QString& value) {
    using Kind = nextapp::pb::Node::Kind;
    if (value == QStringLiteral("folder")) return Kind::FOLDER;
    if (value == QStringLiteral("organization")) return Kind::ORGANIZATION;
    if (value == QStringLiteral("person")) return Kind::PERSON;
    if (value == QStringLiteral("project")) return Kind::PROJECT;
    if (value == QStringLiteral("task")) return Kind::TASK;
    return std::nullopt;
}
QString canonicalUuid(const QString& value) {
    const QUuid uuid{value};
    return uuid.isNull() ? QString{} : uuid.toString(QUuid::WithoutBraces);
}
bool stateIsError(OperationState state) {
    return state == OperationState::Failed || state == OperationState::Rejected
        || state == OperationState::Cancelled || state == OperationState::Expired
        || state == OperationState::AbortedSync || state == OperationState::AbortedOffline
        || state == OperationState::OutcomeUnknown;
}
QJsonObject storedOutcome(const StoredRequest& request) {
    auto result = request.result;
    result.insert(QStringLiteral("requestId"), request.request_id);
    result.insert(QStringLiteral("state"), McpRequestStore::stateName(request.state));
    if (request.state == OperationState::Pending)
        result.insert(QStringLiteral("nextStep"), QStringLiteral("Call nextapp_get_request_status with this requestId. Reuse the same idempotencyKey if retrying; do not create a new one."));
    return result;
}
} // namespace

McpGateway::McpGateway(RuntimeServices& runtime) : runtime_{runtime}, store_{runtime.db()} {}

QCoro::Task<bool> McpGateway::initialize() { co_return co_await store_.initialize(); }

bool McpGateway::enabled() const {
    return runtime_.settings().value(ai_enabled, false).toBool()
        && runtime_.settings().value(mcp_enabled, false).toBool()
        && runtime_.serverComm().connected();
}

QString McpGateway::credential() {
    auto credential = runtime_.settings().value(credential_key).toString();
    if (credential.isEmpty()) {
        credential = randomCredential();
        runtime_.settings().setValue(credential_key, credential);
        runtime_.settings().sync();
    }
    return credential;
}

QString McpGateway::rotateCredential() {
    const auto new_credential = randomCredential();
    runtime_.settings().setValue(credential_key, new_credential);
    runtime_.settings().sync();
    LOG_INFO_N << "Local MCP credential rotated";
    return new_credential;
}

bool McpGateway::authenticate(const HeaderMap& headers) const {
    const auto authorization = headers.value("authorization");
    const auto expected = QByteArray{"Bearer "} + runtime_.settings().value(credential_key).toString().toUtf8();
    if (expected.size() != authorization.size()) return false;
    unsigned char different = 0;
    for (qsizetype i = 0; i < expected.size(); ++i) different |= expected.at(i) ^ authorization.at(i);
    return different == 0;
}

bool McpGateway::originAllowed(const HeaderMap& headers) const {
    const auto origin = headers.value("origin");
    if (origin.isEmpty()) return true; // Non-browser local MCP clients do not send Origin.
    const auto url = QUrl(QString::fromUtf8(origin), QUrl::StrictMode);
    return url.isValid() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
        && (url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1")
            || url.host() == QStringLiteral("::1"))
        && !url.hasQuery() && !url.hasFragment() && url.userInfo().isEmpty()
        && (url.path().isEmpty() || url.path() == QStringLiteral("/"));
}

int McpGateway::boundedPageSize(const QJsonObject& arguments) const {
    const auto configured = runtime_.settings().value("ai/mcp/limits/page_size", 50).toInt();
    const auto maximum = std::clamp(configured, 1, 50);
    return std::clamp(arguments.value(QStringLiteral("pageSize")).toInt(maximum), 1, maximum);
}

QJsonObject McpGateway::toolResult(const QJsonObject& structured, bool is_error) const {
    return {{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), QString::fromUtf8(QJsonDocument(structured).toJson(QJsonDocument::Compact))}}}},
        {QStringLiteral("structuredContent"), structured}, {QStringLiteral("isError"), is_error}};
}

QCoro::Task<QJsonObject> McpGateway::action(const QString& id) {
    const auto result = co_await runtime_.db().query("SELECT id,node,name,descr,status,completed_time,version,updated FROM action WHERE id=?", id);
    if (!result || result->rows.isEmpty()) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Action not found")}}, true);
    co_return toolResult({{QStringLiteral("action"), actionObject(result->rows.front())}, {QStringLiteral("online"), runtime_.serverComm().connected()}});
}

QCoro::Task<QJsonObject> McpGateway::actions(const QJsonObject& arguments, bool search) {
    const auto page_size = boundedPageSize(arguments);
    QString sql = "SELECT id,node,name,descr,status,completed_time,version,updated FROM action";
    QList<QVariant> values;
    if (search) {
        const auto query = arguments.value(QStringLiteral("query")).toString();
        if (query.isEmpty() || query.size() > 256) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid search query")}}, true);
        sql += " WHERE name LIKE ? ESCAPE '\\'";
        auto escaped = query; escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        values << (QStringLiteral("%") + escaped + QStringLiteral("%"));
    }
    sql += " ORDER BY updated DESC,id ASC LIMIT ?";
    values << page_size;
    const auto result = co_await runtime_.db().query(sql, values);
    if (!result) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local cache query failed")}}, true);
    QJsonArray list;
    for (const auto& row : result->rows) list.append(actionObject(row));
    co_return toolResult({{QStringLiteral("actions"), list}, {QStringLiteral("online"), runtime_.serverComm().connected()},
                           {QStringLiteral("cacheComplete"), runtime_.serverComm().connected()}, {QStringLiteral("pageSize"), page_size}});
}

QCoro::Task<QJsonObject> McpGateway::nodes(const QJsonObject& arguments, bool search) {
    const auto cursor = arguments.value(QStringLiteral("cursor")).toString();
    const auto query = arguments.value(QStringLiteral("query")).toString();
    const auto format = arguments.value(QStringLiteral("format")).toString(QStringLiteral("short"));
    if ((arguments.contains(QStringLiteral("cursor")) && !arguments.value(QStringLiteral("cursor")).isString())
        || (!cursor.isEmpty() && QUuid{cursor}.isNull())
        || (arguments.contains(QStringLiteral("query")) && !arguments.value(QStringLiteral("query")).isString())
        || (arguments.contains(QStringLiteral("format")) && !arguments.value(QStringLiteral("format")).isString())
        || query.size() > 256 || (search && query.isEmpty())
        || (format != QStringLiteral("short") && format != QStringLiteral("full")))
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid node query or cursor")}}, true);

    const auto page_size = boundedPageSize(arguments);
    QString sql = QStringLiteral("SELECT uuid,parent,name,active,updated,data FROM node WHERE active=1");
    QList<QVariant> values;
    if (!cursor.isEmpty()) {
        sql += QStringLiteral(" AND uuid > ?");
        values << QUuid{cursor}.toString(QUuid::WithoutBraces);
    }
    if (search && !query.isEmpty()) {
        sql += QStringLiteral(" AND name LIKE ? ESCAPE '\\'");
        auto escaped = query; escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        values << (QStringLiteral("%") + escaped + QStringLiteral("%"));
    }
    sql += QStringLiteral(" ORDER BY uuid LIMIT ?");
    values << (page_size + 1);
    const auto result = co_await runtime_.db().query(sql, values);
    if (!result) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local node cache query failed")}}, true);

    QJsonArray list;
    QProtobufSerializer serializer;
    const auto count = std::min<qsizetype>(result->rows.size(), page_size);
    for (qsizetype i = 0; i < count; ++i) {
        const auto& row = result->rows.at(i);
        nextapp::pb::Node node;
        if (!node.deserialize(&serializer, row.at(5).toByteArray())
            || node.uuid() != row.at(0).toString())
            co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local node cache contains an invalid record")}}, true);
        QJsonObject item{{QStringLiteral("nodeId"), row.at(0).toString()},
            {QStringLiteral("name"), row.at(2).toString()},
            {QStringLiteral("parentId"), row.at(1).toString()},
            {QStringLiteral("kind"), nodeKind(node.kind())}};
        if (node.inbox()) item.insert(QStringLiteral("inbox"), true);
        if (format == QStringLiteral("full")) {
            item.insert(QStringLiteral("description"), node.descr());
            item.insert(QStringLiteral("categoryId"), node.category());
            item.insert(QStringLiteral("version"), double(node.version()));
            item.insert(QStringLiteral("updatedAt"), double(node.updated()));
            item.insert(QStringLiteral("active"), true);
        }
        list.append(item);
    }
    QJsonObject payload{{QStringLiteral("nodes"), list},
                        {QStringLiteral("online"), runtime_.serverComm().connected()}};
    if (result->rows.size() > page_size && !list.isEmpty())
        payload.insert(QStringLiteral("nextCursor"), result->rows.at(count - 1).at(0).toString());
    co_return toolResult(payload);
}

QCoro::Task<QJsonObject> McpGateway::categories(const QJsonObject& arguments, bool search) {
    const auto cursor = arguments.value(QStringLiteral("cursor")).toString();
    const auto query = arguments.value(QStringLiteral("query")).toString();
    if ((arguments.contains(QStringLiteral("cursor")) && !arguments.value(QStringLiteral("cursor")).isString())
        || (!cursor.isEmpty() && canonicalUuid(cursor).isEmpty())
        || (arguments.contains(QStringLiteral("query")) && !arguments.value(QStringLiteral("query")).isString())
        || query.size() > 256 || (search && query.isEmpty()))
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid category query or cursor")}}, true);
    const auto page_size = boundedPageSize(arguments);
    QString sql = QStringLiteral("SELECT id,name,data FROM action_category WHERE 1=1");
    QList<QVariant> values;
    if (!cursor.isEmpty()) { sql += QStringLiteral(" AND id > ?"); values << canonicalUuid(cursor); }
    if (search) {
        sql += QStringLiteral(" AND name LIKE ? ESCAPE '\\'");
        auto escaped = query; escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        values << (QStringLiteral("%") + escaped + QStringLiteral("%"));
    }
    sql += QStringLiteral(" ORDER BY id LIMIT ?");
    values << (page_size + 1);
    const auto result = co_await runtime_.db().query(sql, values);
    if (!result) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local category cache query failed")}}, true);
    QJsonArray list;
    QProtobufSerializer serializer;
    const auto count = std::min<qsizetype>(result->rows.size(), page_size);
    for (qsizetype i = 0; i < count; ++i) {
        const auto& row = result->rows.at(i);
        nextapp::pb::ActionCategory category;
        if (!category.deserialize(&serializer, row.at(2).toByteArray()) || category.id_proto() != row.at(0).toString())
            co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local category cache contains an invalid record")}}, true);
        if (category.deleted()) continue;
        list.append(QJsonObject{{QStringLiteral("categoryId"), category.id_proto()},
            {QStringLiteral("name"), category.name()}, {QStringLiteral("description"), category.descr()},
            {QStringLiteral("color"), category.color()}, {QStringLiteral("icon"), category.icon()},
            {QStringLiteral("version"), int(category.version())}});
    }
    QJsonObject payload{{QStringLiteral("categories"), list}, {QStringLiteral("online"), runtime_.serverComm().connected()}};
    if (result->rows.size() > page_size)
        payload.insert(QStringLiteral("nextCursor"), result->rows.at(count - 1).at(0).toString());
    co_return toolResult(payload);
}

QString McpGateway::mutationGate(const QString& operation) const {
    return runtime_.settings().value(QStringLiteral("ai/mcp/gate/") + operation, QStringLiteral("Disabled")).toString();
}

bool McpGateway::mutationAllowed(const QString& operation) const {
    const auto gate = mutationGate(operation);
    return gate == QStringLiteral("Ask") || gate == QStringLiteral("Always allow");
}

QCoro::Task<std::optional<StoredRequest>> McpGateway::reserveMutation(const QString& operation, const QJsonObject& arguments) {
    if (!mutationAllowed(operation)) co_return std::nullopt;
    const auto key = arguments.value(QStringLiteral("idempotencyKey")).toString();
    const auto configured_id = runtime_.settings().value(QStringLiteral("ai/mcp/agent_id"), QStringLiteral("local-agent")).toString().trimmed();
    co_return co_await store_.reserve(configured_id.isEmpty() ? QStringLiteral("local-agent") : configured_id, key, operation, arguments);
}

void McpGateway::recordActivity(const StoredRequest& request, const QString& state, const QJsonObject& result) const {
    QVariantMap event{{QStringLiteral("requestId"), request.request_id}, {QStringLiteral("operation"), request.operation},
                      {QStringLiteral("state"), state}, {QStringLiteral("agent"), request.agent_id}};
    if (!result.isEmpty()) event.insert(QStringLiteral("result"), result.toVariantMap());
    runtime_.recordMcpActivity(event);
}

QString McpGateway::agentDescription(const StoredRequest& request) const {
    const auto configured_name = runtime_.settings().value(QStringLiteral("ai/mcp/agent_name")).toString().trimmed();
    return configured_name.isEmpty() ? request.agent_id : configured_name + QStringLiteral(" (") + request.agent_id + QStringLiteral(")");
}

void McpGateway::logMutation(const StoredRequest& request, const QJsonObject& arguments,
                             const QString& peer) const {
    const auto agent = agentDescription(request);
    if (request.operation == QStringLiteral("create_action")) {
        LOG_INFO_N << "MCP agent " << agent << " at " << peer << " created action \""
                   << arguments.value(QStringLiteral("name")).toString() << "\" in list "
                   << arguments.value(QStringLiteral("nodeId")).toString();
    } else if (request.operation == QStringLiteral("update_action")) {
        QStringList fields;
        if (arguments.contains(QStringLiteral("name"))) fields << QStringLiteral("name");
        if (arguments.contains(QStringLiteral("description"))) fields << QStringLiteral("description");
        LOG_INFO_N << "MCP agent " << agent << " at " << peer << " updated action "
                   << arguments.value(QStringLiteral("id")).toString() << " ("
                   << fields.join(QStringLiteral(", ")) << ")";
    } else if (request.operation == QStringLiteral("complete_action")) {
        LOG_INFO_N << "MCP agent " << agent << " at " << peer
                   << (arguments.value(QStringLiteral("done")).toBool() ? " marked action complete: " : " reopened action: ")
                   << arguments.value(QStringLiteral("id")).toString();
    } else if (request.operation == QStringLiteral("create_node")) {
        LOG_INFO_N << "MCP agent " << agent << " at " << peer << " created NextApp list/node \""
                   << arguments.value(QStringLiteral("name")).toString() << "\" ("
                   << arguments.value(QStringLiteral("nodeId")).toString() << ", "
                   << arguments.value(QStringLiteral("kind")).toString(QStringLiteral("folder"))
                   << ") under " << (arguments.value(QStringLiteral("parentId")).toString().isEmpty()
                       ? QStringLiteral("top level") : arguments.value(QStringLiteral("parentId")).toString());
    } else if (request.operation == QStringLiteral("update_node")) {
        QStringList fields;
        for (const auto& key : {"name", "description", "kind", "active", "categoryId"})
            if (arguments.contains(QLatin1StringView{key})) fields << QLatin1StringView{key};
        LOG_INFO_N << "MCP agent " << agent << " at " << peer << " updated NextApp list/node "
                   << arguments.value(QStringLiteral("nodeId")).toString() << " ("
                   << fields.join(QStringLiteral(", ")) << ")";
    }
}

QCoro::Task<std::optional<QJsonObject>> McpGateway::approveMutation(const StoredRequest& request, const QJsonObject& arguments,
                                                                     PendingReply pending_reply) {
    const auto gate = mutationGate(request.operation);
    QString target_name;
    if (request.operation == QStringLiteral("create_action")) {
        const auto target = arguments.value(QStringLiteral("nodeId")).toString();
        const auto current = co_await runtime_.db().query("SELECT name FROM node WHERE uuid=?", target);
        if (!current || current->rows.isEmpty()) {
            const QJsonObject result{{QStringLiteral("requestId"), request.request_id},
                {QStringLiteral("error"), QStringLiteral("Invalid NextApp nodeId. Use nextapp_list_nodes to find an existing node, or omit nodeId for Inbox.")}};
            (void) co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Failed, result);
            recordActivity(request, QStringLiteral("FAILED"), result);
            co_return result;
        }
        target_name = current->rows.front().at(0).toString();
    } else if (request.operation == QStringLiteral("create_node")) {
        const auto parent = arguments.value(QStringLiteral("parentId")).toString();
        if (!parent.isEmpty()) {
            const auto current = co_await runtime_.db().query("SELECT name FROM node WHERE uuid=?", parent);
            if (!current || current->rows.isEmpty()) {
                const QJsonObject result{{QStringLiteral("requestId"), request.request_id},
                    {QStringLiteral("error"), QStringLiteral("Invalid parentId. Use nextapp_list_nodes or omit parentId for a top-level list.")}};
                (void) co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Failed, result);
                recordActivity(request, QStringLiteral("FAILED"), result);
                co_return result;
            }
            target_name = current->rows.front().at(0).toString();
        } else {
            target_name = QStringLiteral("Top level");
        }
    } else if (request.operation == QStringLiteral("update_node")) {
        const auto target = arguments.value(QStringLiteral("nodeId")).toString();
        const auto current = co_await runtime_.db().query("SELECT name,data FROM node WHERE uuid=?", target);
        nextapp::pb::Node node;
        QProtobufSerializer serializer;
        if (!current || current->rows.isEmpty()
            || !node.deserialize(&serializer, current->rows.front().at(1).toByteArray())
            || node.version() != arguments.value(QStringLiteral("baseVersion")).toInteger()) {
            const QJsonObject result{{QStringLiteral("requestId"), request.request_id},
                {QStringLiteral("error"), QStringLiteral("NextApp list/node is missing or changed since baseVersion")}};
            (void) co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Failed, result);
            recordActivity(request, QStringLiteral("FAILED"), result);
            co_return result;
        }
        target_name = current->rows.front().at(0).toString();
    } else {
        const auto target = arguments.value(QStringLiteral("id")).toString();
        const auto current = co_await runtime_.db().query("SELECT name,version FROM action WHERE id=?", target);
        if (!current || current->rows.isEmpty() || current->rows.front().at(1).toInt() != arguments.value(QStringLiteral("baseVersion")).toInt()) {
            const QJsonObject result{{QStringLiteral("requestId"), request.request_id}, {QStringLiteral("error"), QStringLiteral("Action is missing or changed since the supplied baseVersion")}};
            (void) co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Failed, result);
            recordActivity(request, QStringLiteral("FAILED"), result);
            co_return result;
        }
        target_name = current->rows.front().at(0).toString();
    }
    if ((request.operation == QStringLiteral("create_node") || request.operation == QStringLiteral("update_node"))
        && !arguments.value(QStringLiteral("categoryId")).toString().isEmpty()) {
        const auto category_id = arguments.value(QStringLiteral("categoryId")).toString();
        const auto category = co_await runtime_.db().query("SELECT id FROM action_category WHERE id=?", category_id);
        if (!category || category->rows.isEmpty()) {
            const QJsonObject result{{QStringLiteral("requestId"), request.request_id},
                {QStringLiteral("error"), QStringLiteral("Invalid categoryId. Use nextapp_list_categories or nextapp_search_categories.")}};
            (void) co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Failed, result);
            recordActivity(request, QStringLiteral("FAILED"), result);
            co_return result;
        }
    }
    if (gate == QStringLiteral("Always allow")) {
        if (!co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Approved))
            co_return QJsonObject{{QStringLiteral("error"), QStringLiteral("Unable to approve MCP request")}};
        recordActivity(request, QStringLiteral("APPROVED"));
        co_return std::nullopt;
    }
    if (!co_await store_.transition(request.request_id, OperationState::Validating, OperationState::Pending))
        co_return QJsonObject{{QStringLiteral("error"), QStringLiteral("Unable to queue MCP approval")}};

    QVariantMap presentation{{QStringLiteral("requestId"), request.request_id}, {QStringLiteral("operation"), request.operation},
                             {QStringLiteral("agent"), runtime_.settings().value(QStringLiteral("ai/mcp/agent_name"), request.agent_id).toString()},
                             {QStringLiteral("reason"), arguments.value(QStringLiteral("reason")).toString()}};
    if (request.operation == QStringLiteral("create_action")) {
        presentation.insert(QStringLiteral("target"), arguments.value(QStringLiteral("name")).toString());
        presentation.insert(QStringLiteral("changes"), QVariantMap{{QStringLiteral("name"), arguments.value(QStringLiteral("name")).toString()},
            {QStringLiteral("description"), arguments.value(QStringLiteral("description")).toString()},
            {QStringLiteral("list"), target_name + QStringLiteral(" (") + arguments.value(QStringLiteral("nodeId")).toString() + QStringLiteral(")")}});
    } else if (request.operation == QStringLiteral("create_node")) {
        presentation.insert(QStringLiteral("target"), arguments.value(QStringLiteral("name")).toString());
        auto changes = arguments.toVariantMap();
        changes.remove(QStringLiteral("idempotencyKey"));
        changes.remove(QStringLiteral("reason"));
        changes.insert(QStringLiteral("parent"), target_name);
        presentation.insert(QStringLiteral("changes"), changes);
    } else {
        const auto target_id = request.operation == QStringLiteral("update_node")
            ? arguments.value(QStringLiteral("nodeId")).toString()
            : arguments.value(QStringLiteral("id")).toString();
        presentation.insert(QStringLiteral("target"), target_name + QStringLiteral(" (") + target_id + QStringLiteral(")"));
        auto changes = arguments.toVariantMap();
        changes.remove(QStringLiteral("idempotencyKey"));
        changes.remove(QStringLiteral("reason"));
        changes.remove(QStringLiteral("id"));
        changes.remove(QStringLiteral("nodeId"));
        presentation.insert(QStringLiteral("changes"), changes);
    }
    auto approval = runtime_.requestMcpApproval(presentation);
    recordActivity(request, QStringLiteral("PENDING"));
    if (pending_reply) {
        LOG_DEBUG_N << "MCP request " << request.request_id << " is pending local approval; responding before the approval completes";
        pending_reply(toolResult({{QStringLiteral("requestId"), request.request_id},
            {QStringLiteral("state"), QStringLiteral("PENDING")},
            {QStringLiteral("nextStep"), QStringLiteral("Call nextapp_get_request_status with this requestId. Do not create a new idempotencyKey for this operation.")}}));
    }
    const auto decision = co_await qCoro(approval).result();
    const auto state = decision == RuntimeServices::McpApprovalDecision::Approve ? OperationState::Approved
        : decision == RuntimeServices::McpApprovalDecision::Expired ? OperationState::Expired
        : decision == RuntimeServices::McpApprovalDecision::Cancelled ? OperationState::Cancelled : OperationState::Rejected;
    const QJsonObject result{{QStringLiteral("requestId"), request.request_id}, {QStringLiteral("state"), McpRequestStore::stateName(state)}};
    if (!co_await store_.transition(request.request_id, OperationState::Pending, state, result))
        co_return QJsonObject{{QStringLiteral("error"), QStringLiteral("MCP request was cancelled")}};
    recordActivity(request, McpRequestStore::stateName(state), result);
    if (state != OperationState::Approved) co_return result;
    co_return std::nullopt;
}

QCoro::Task<QJsonObject> McpGateway::createNode(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply) {
    const auto name = arguments.value(QStringLiteral("name")).toString();
    const auto kind = parseNodeKind(arguments.value(QStringLiteral("kind")).toString(QStringLiteral("folder")));
    if (!onlyFields(arguments, {QStringLiteral("idempotencyKey"), QStringLiteral("name"),
            QStringLiteral("kind"), QStringLiteral("description"), QStringLiteral("parentId"),
            QStringLiteral("categoryId"), QStringLiteral("reason")})
        || !arguments.value(QStringLiteral("name")).isString() || name.isEmpty() || name.size() > 128
        || (arguments.contains(QStringLiteral("kind")) && !arguments.value(QStringLiteral("kind")).isString())
        || !kind || !validOptionalString(arguments, QStringLiteral("description"))
        || !validOptionalString(arguments, QStringLiteral("reason"))
        || !validOptionalString(arguments, QStringLiteral("parentId"))
        || !validOptionalString(arguments, QStringLiteral("categoryId"))
        || (arguments.contains(QStringLiteral("parentId")) && canonicalUuid(arguments.value(QStringLiteral("parentId")).toString()).isEmpty())
        || (arguments.contains(QStringLiteral("categoryId")) && canonicalUuid(arguments.value(QStringLiteral("categoryId")).toString()).isEmpty())
        || arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024
        || arguments.value(QStringLiteral("reason")).toString().toUtf8().size() > 2048)
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid NextApp list/node fields")}}, true);
    auto resolved = arguments;
    if (arguments.contains(QStringLiteral("parentId")))
        resolved.insert(QStringLiteral("parentId"), canonicalUuid(arguments.value(QStringLiteral("parentId")).toString()));
    if (arguments.contains(QStringLiteral("categoryId")))
        resolved.insert(QStringLiteral("categoryId"), canonicalUuid(arguments.value(QStringLiteral("categoryId")).toString()));
    auto stored = co_await reserveMutation(QStringLiteral("create_node"), resolved);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Create lists/nodes are disabled or the idempotency key conflicts")}}, true);
    if (!stored->newly_reserved) {
        co_return toolResult(storedOutcome(*stored), stateIsError(stored->state));
    }
    if (const auto outcome = co_await approveMutation(*stored, resolved, pending_reply)) co_return toolResult(*outcome, true);
    if (resolved.contains(QStringLiteral("parentId"))) {
        const auto parent = co_await runtime_.db().query("SELECT uuid FROM node WHERE uuid=?", resolved.value(QStringLiteral("parentId")).toString());
        if (!parent || parent->rows.isEmpty()) {
            const QJsonObject result{{QStringLiteral("requestId"), stored->request_id},
                {QStringLiteral("error"), QStringLiteral("NextApp parent list/node disappeared before execution")}};
            (void) co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Failed, result);
            recordActivity(*stored, QStringLiteral("FAILED"), result);
            co_return toolResult(result, true);
        }
    }
    if (!co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Executing))
        co_return toolResult({{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("state"), QStringLiteral("already executing")}}, true);
    nextapp::pb::Node node;
    node.setUuid(QUuid::createUuid().toString(QUuid::WithoutBraces));
    node.setName(name);
    node.setKind(*kind);
    node.setActive(true);
    node.setDescr(resolved.value(QStringLiteral("description")).toString());
    node.setParent(resolved.value(QStringLiteral("parentId")).toString());
    node.setCategory(resolved.value(QStringLiteral("categoryId")).toString());
    nextapp::pb::CreateNodeReq request; request.setNode(node);
    const auto status = co_await runtime_.serverComm().createNodeDirect(request);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("nodeId"), node.uuid()},
        {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()},
        {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    recordActivity(*stored, McpRequestStore::stateName(state), result);
    if (state == OperationState::Executed) {
        auto logged = resolved;
        logged.insert(QStringLiteral("nodeId"), node.uuid());
        logMutation(*stored, logged, peer);
    }
    co_return toolResult(result, state != OperationState::Executed);
}

QCoro::Task<QJsonObject> McpGateway::updateNode(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply) {
    const auto kind = arguments.contains(QStringLiteral("kind"))
        ? parseNodeKind(arguments.value(QStringLiteral("kind")).toString()) : std::optional<nextapp::pb::Node::Kind>{};
    const auto node_id = canonicalUuid(arguments.value(QStringLiteral("nodeId")).toString());
    const auto name = arguments.value(QStringLiteral("name")).toString();
    if (!onlyFields(arguments, {QStringLiteral("idempotencyKey"), QStringLiteral("nodeId"),
            QStringLiteral("baseVersion"), QStringLiteral("name"), QStringLiteral("description"),
            QStringLiteral("kind"), QStringLiteral("active"), QStringLiteral("categoryId"),
            QStringLiteral("reason")})
        || node_id.isEmpty() || !validBaseVersion(arguments)
        || !validOptionalString(arguments, QStringLiteral("name"))
        || !validOptionalString(arguments, QStringLiteral("description"))
        || !validOptionalString(arguments, QStringLiteral("reason"))
        || !validOptionalString(arguments, QStringLiteral("categoryId"))
        || (arguments.contains(QStringLiteral("kind")) && !kind)
        || (arguments.contains(QStringLiteral("active")) && !arguments.value(QStringLiteral("active")).isBool())
        || (arguments.contains(QStringLiteral("name")) && (name.isEmpty() || name.size() > 128))
        || (arguments.contains(QStringLiteral("categoryId"))
            && !arguments.value(QStringLiteral("categoryId")).toString().isEmpty()
            && canonicalUuid(arguments.value(QStringLiteral("categoryId")).toString()).isEmpty())
        || arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024
        || arguments.value(QStringLiteral("reason")).toString().toUtf8().size() > 2048
        || (!arguments.contains(QStringLiteral("name")) && !arguments.contains(QStringLiteral("description"))
            && !arguments.contains(QStringLiteral("kind")) && !arguments.contains(QStringLiteral("active"))
            && !arguments.contains(QStringLiteral("categoryId"))))
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid NextApp list/node update patch")}}, true);
    auto resolved = arguments;
    resolved.insert(QStringLiteral("nodeId"), node_id);
    if (arguments.contains(QStringLiteral("categoryId")) && !arguments.value(QStringLiteral("categoryId")).toString().isEmpty())
        resolved.insert(QStringLiteral("categoryId"), canonicalUuid(arguments.value(QStringLiteral("categoryId")).toString()));
    auto stored = co_await reserveMutation(QStringLiteral("update_node"), resolved);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Update lists/nodes are disabled or the idempotency key conflicts")}}, true);
    if (!stored->newly_reserved) {
        co_return toolResult(storedOutcome(*stored), stateIsError(stored->state));
    }
    if (const auto outcome = co_await approveMutation(*stored, resolved, pending_reply)) co_return toolResult(*outcome, true);
    const auto current = co_await runtime_.db().query("SELECT data FROM node WHERE uuid=?", node_id);
    nextapp::pb::Node node;
    QProtobufSerializer serializer;
    if (!current || current->rows.isEmpty() || !node.deserialize(&serializer, current->rows.front().at(0).toByteArray())
        || node.version() != resolved.value(QStringLiteral("baseVersion")).toInteger()) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id},
            {QStringLiteral("error"), QStringLiteral("NextApp list/node changed before execution")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Failed, result);
        recordActivity(*stored, QStringLiteral("FAILED"), result);
        co_return toolResult(result, true);
    }
    if (resolved.contains(QStringLiteral("name"))) node.setName(name);
    if (resolved.contains(QStringLiteral("description"))) node.setDescr(resolved.value(QStringLiteral("description")).toString());
    if (kind) node.setKind(*kind);
    if (resolved.contains(QStringLiteral("active"))) node.setActive(resolved.value(QStringLiteral("active")).toBool());
    if (resolved.contains(QStringLiteral("categoryId"))) node.setCategory(resolved.value(QStringLiteral("categoryId")).toString());
    if (!co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Executing))
        co_return toolResult({{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("state"), QStringLiteral("already executing")}}, true);
    const auto status = co_await runtime_.serverComm().updateNodeDirect(node);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("nodeId"), node_id},
        {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()},
        {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    recordActivity(*stored, McpRequestStore::stateName(state), result);
    if (state == OperationState::Executed) logMutation(*stored, resolved, peer);
    co_return toolResult(result, state != OperationState::Executed);
}

QCoro::Task<QJsonObject> McpGateway::createAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply) {
    if (!arguments.value(QStringLiteral("name")).isString()
        || (arguments.contains(QStringLiteral("nodeId"))
            && (!arguments.value(QStringLiteral("nodeId")).isString()
                || QUuid{arguments.value(QStringLiteral("nodeId")).toString()}.isNull()))
        || !validOptionalString(arguments, QStringLiteral("description"))
        || !validOptionalString(arguments, QStringLiteral("reason"))
        || arguments.value(QStringLiteral("name")).toString().isEmpty()
        || arguments.value(QStringLiteral("name")).toString().size() > 255
        || arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024
        || arguments.value(QStringLiteral("reason")).toString().toUtf8().size() > 2048)
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid action fields")}}, true);

    auto resolved = arguments;
    const auto use_inbox = !resolved.contains(QStringLiteral("nodeId"));
    if (use_inbox) {
        const auto candidates = co_await runtime_.db().query("SELECT uuid,data FROM node ORDER BY uuid");
        if (!candidates)
            co_return toolResult({{QStringLiteral("error"), QStringLiteral("Cannot read local NextApp nodes to find Inbox")}}, true);
        QProtobufSerializer serializer;
        QString inbox_id;
        for (const auto& row : candidates->rows) {
            nextapp::pb::Node node;
            if (!node.deserialize(&serializer, row.at(1).toByteArray()))
                co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local NextApp node cache contains an invalid record")}}, true);
            if (!node.inbox()) continue;
            if (node.uuid() != row.at(0).toString() || !inbox_id.isEmpty())
                co_return toolResult({{QStringLiteral("error"), QStringLiteral("Local NextApp Inbox is inconsistent")}}, true);
            inbox_id = node.uuid();
        }
        if (inbox_id.isEmpty())
            co_return toolResult({{QStringLiteral("error"), QStringLiteral("No NextApp Inbox node is available; select an existing nodeId from nextapp_list_nodes")}}, true);
        resolved.insert(QStringLiteral("nodeId"), inbox_id);
        LOG_DEBUG_N << "MCP create_action from " << peer << " resolved omitted nodeId to Inbox " << inbox_id;
    } else {
        resolved.insert(QStringLiteral("nodeId"),
            QUuid{arguments.value(QStringLiteral("nodeId")).toString()}.toString(QUuid::WithoutBraces));
    }

    auto stored = co_await reserveMutation(QStringLiteral("create_action"), resolved);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Create actions are disabled or the idempotency key conflicts")}}, true);
    if (!stored->newly_reserved) {
        co_return toolResult(storedOutcome(*stored), stateIsError(stored->state));
    }
    if (const auto outcome = co_await approveMutation(*stored, resolved, pending_reply)) co_return toolResult(*outcome, true);
    const auto destination = co_await runtime_.db().query("SELECT uuid,data FROM node WHERE uuid=?", resolved.value(QStringLiteral("nodeId")).toString());
    bool destination_valid = destination && !destination->rows.isEmpty();
    if (destination_valid && use_inbox) {
        QProtobufSerializer serializer;
        nextapp::pb::Node node;
        destination_valid = node.deserialize(&serializer, destination->rows.front().at(1).toByteArray())
            && node.uuid() == destination->rows.front().at(0).toString() && node.inbox();
    }
    if (!destination_valid) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id},
            {QStringLiteral("error"), QStringLiteral("NextApp destination node disappeared or is no longer Inbox")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Failed, result);
        recordActivity(*stored, QStringLiteral("FAILED"), result);
        co_return toolResult(result, true);
    }
    if (!co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Executing))
        co_return toolResult({{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("state"), QStringLiteral("already executing")}}, true);
    nextapp::pb::Action action;
    action.setId_proto(QUuid::createUuid().toString(QUuid::WithoutBraces));
    action.setNode(resolved.value(QStringLiteral("nodeId")).toString());
    action.setName(resolved.value(QStringLiteral("name")).toString());
    action.setDescr(resolved.value(QStringLiteral("description")).toString());
    nextapp::pb::Date date; const auto today = QDate::currentDate(); date.setYear(today.year()); date.setMonth(today.month()); date.setMday(today.day()); action.setCreatedDate(date);
    const auto status = co_await runtime_.serverComm().addActionDirect(action);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), action.id_proto()}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    recordActivity(*stored, McpRequestStore::stateName(state), result);
    if (state == OperationState::Executed) logMutation(*stored, resolved, peer);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::updateAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply) {
    if (!validBaseVersion(arguments)
        || QUuid{arguments.value(QStringLiteral("id")).toString()}.isNull()
        || !validOptionalString(arguments, QStringLiteral("name"))
        || !validOptionalString(arguments, QStringLiteral("description"))
        || !validOptionalString(arguments, QStringLiteral("reason"))
        || (!arguments.contains(QStringLiteral("name")) && !arguments.contains(QStringLiteral("description")))
        || (arguments.contains(QStringLiteral("name")) && (arguments.value(QStringLiteral("name")).toString().isEmpty()
            || arguments.value(QStringLiteral("name")).toString().size() > 255))
        || (arguments.contains(QStringLiteral("description")) && arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024)
        || arguments.value(QStringLiteral("reason")).toString().toUtf8().size() > 2048)
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("An update patch must contain valid name or description fields")}}, true);
    auto stored = co_await reserveMutation(QStringLiteral("update_action"), arguments);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Action updates are disabled or the idempotency key conflicts")}}, true);
    if (!stored->newly_reserved) {
        co_return toolResult(storedOutcome(*stored), stateIsError(stored->state));
    }
    if (const auto outcome = co_await approveMutation(*stored, arguments, pending_reply)) co_return toolResult(*outcome, true);
    const auto action = co_await ActionInfoCache::instance()->getAction(QUuid{arguments.value(QStringLiteral("id")).toString()});
    if (!action || action->version() != arguments.value(QStringLiteral("baseVersion")).toInt()) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("error"), QStringLiteral("Action is missing or changed since the supplied baseVersion")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Failed, result);
        recordActivity(*stored, QStringLiteral("FAILED"), result);
        co_return toolResult(result, true);
    }
    if (arguments.contains(QStringLiteral("name"))) action->setName(arguments.value(QStringLiteral("name")).toString());
    if (arguments.contains(QStringLiteral("description"))) action->setDescr(arguments.value(QStringLiteral("description")).toString());
    if (!co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Executing)) co_return toolResult({{QStringLiteral("requestId"), stored->request_id}}, true);
    const auto status = co_await runtime_.serverComm().updateActionDirect(*action);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), action->id_proto()}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    recordActivity(*stored, McpRequestStore::stateName(state), result);
    if (state == OperationState::Executed) logMutation(*stored, arguments, peer);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::completeAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply) {
    if (QUuid{arguments.value(QStringLiteral("id")).toString()}.isNull()
        || !validBaseVersion(arguments) || !arguments.value(QStringLiteral("done")).isBool()
        || !validOptionalString(arguments, QStringLiteral("reason"))
        || arguments.value(QStringLiteral("reason")).toString().toUtf8().size() > 2048)
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("id, baseVersion, and done are required")}}, true);
    auto stored = co_await reserveMutation(QStringLiteral("complete_action"), arguments);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Action completion is disabled or the idempotency key conflicts")}}, true);
    if (!stored->newly_reserved) {
        co_return toolResult(storedOutcome(*stored), stateIsError(stored->state));
    }
    if (const auto outcome = co_await approveMutation(*stored, arguments, pending_reply)) co_return toolResult(*outcome, true);
    const auto id = arguments.value(QStringLiteral("id")).toString();
    const auto action = co_await ActionInfoCache::instance()->getAction(QUuid{id});
    if (!action || action->version() != arguments.value(QStringLiteral("baseVersion")).toInt()) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("error"), QStringLiteral("Action is missing or changed since the supplied baseVersion")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Failed, result);
        recordActivity(*stored, QStringLiteral("FAILED"), result);
        co_return toolResult(result, true);
    }
    if (!co_await store_.transition(stored->request_id, OperationState::Approved, OperationState::Executing)) co_return toolResult({{QStringLiteral("requestId"), stored->request_id}}, true);
    nextapp::pb::ActionDoneReq request; request.setUuid(id); request.setDone(arguments.value(QStringLiteral("done")).toBool());
    const auto status = co_await runtime_.serverComm().markActionDoneDirect(request);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), id}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    recordActivity(*stored, McpRequestStore::stateName(state), result);
    if (state == OperationState::Executed) logMutation(*stored, arguments, peer);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::requestStatus(const QJsonObject& arguments) {
    const auto request_id = canonicalUuid(arguments.value(QStringLiteral("requestId")).toString());
    if (request_id.isEmpty())
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("A valid requestId is required")}}, true);
    const auto configured_id = runtime_.settings().value(QStringLiteral("ai/mcp/agent_id"), QStringLiteral("local-agent")).toString().trimmed();
    const auto agent_id = configured_id.isEmpty() ? QStringLiteral("local-agent") : configured_id;
    const auto stored = co_await store_.get(agent_id, request_id);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("MCP request not found")}}, true);
    LOG_DEBUG_N << "MCP request status queried for " << request_id << ": " << McpRequestStore::stateName(stored->state);
    co_return toolResult(storedOutcome(*stored));
}

QCoro::Task<QJsonObject> McpGateway::toolCall(const Request& request, const QString& peer, PendingReply pending_reply) {
    const auto arguments = request.params.value(QStringLiteral("arguments")).toObject();
    if (request.tool_name == QStringLiteral("nextapp_get_request_status")) co_return co_await requestStatus(arguments);
    if (request.tool_name == QStringLiteral("nextapp_get_action")) co_return co_await action(arguments.value(QStringLiteral("id")).toString());
    if (request.tool_name == QStringLiteral("nextapp_list_actions")) co_return co_await actions(arguments, false);
    if (request.tool_name == QStringLiteral("nextapp_list_nodes")) co_return co_await nodes(arguments, false);
    if (request.tool_name == QStringLiteral("nextapp_search_nodes")) co_return co_await nodes(arguments, true);
    if (request.tool_name == QStringLiteral("nextapp_list_categories")) co_return co_await categories(arguments, false);
    if (request.tool_name == QStringLiteral("nextapp_search_categories")) co_return co_await categories(arguments, true);
    if (request.tool_name == QStringLiteral("nextapp_search_actions")) co_return co_await actions(arguments, true);
    if (request.tool_name == QStringLiteral("nextapp_create_action")) co_return co_await createAction(arguments, peer, pending_reply);
    if (request.tool_name == QStringLiteral("nextapp_create_node")) co_return co_await createNode(arguments, peer, pending_reply);
    if (request.tool_name == QStringLiteral("nextapp_update_node")) co_return co_await updateNode(arguments, peer, pending_reply);
    if (request.tool_name == QStringLiteral("nextapp_update_action")) co_return co_await updateAction(arguments, peer, pending_reply);
    if (request.tool_name == QStringLiteral("nextapp_complete_action")) co_return co_await completeAction(arguments, peer, pending_reply);
    co_return toolResult({{QStringLiteral("error"), QStringLiteral("Tool is not enabled")}}, true);
}

QCoro::Task<QJsonObject> McpGateway::handle(QByteArray body, HeaderMap headers, QString peer,
                                             PendingReply pending_reply) {
    if (!enabled()) {
        LOG_DEBUG_N << "Rejected MCP request from " << peer << ": interface is disabled, offline, or synchronizing";
        co_return errorResponse({}, {-32000, QStringLiteral("AI or MCP is disabled, offline, or synchronizing"), {}});
    }
    if (!originAllowed(headers)) {
        LOG_DEBUG_N << "Rejected MCP request with invalid Origin from " << peer;
        co_return errorResponse({}, {-32600, QStringLiteral("Invalid Origin"),
            {{QStringLiteral("httpStatus"), 403}}});
    }
    if (!authenticate(headers)) {
        LOG_DEBUG_N << "Rejected unauthorized MCP request from " << peer;
        co_return errorResponse({}, {-32600, QStringLiteral("Unauthorized MCP request"),
            {{QStringLiteral("httpStatus"), 401}}});
    }
    const auto parsed = parseRequest(body, headers);
    if (const auto* error = std::get_if<ProtocolError>(&parsed)) {
        LOG_DEBUG_N << "Rejected invalid MCP request from " << peer << ": " << error->message;
        // A valid JSON-RPC id must be echoed even when the method or its
        // parameters are invalid. Clients use it to resolve the pending call.
        const auto envelope = QJsonDocument::fromJson(body).object();
        const auto id = envelope.value(QStringLiteral("id"));
        co_return errorResponse(id.isString() || id.isDouble() ? id : QJsonValue{}, *error);
    }
    const auto& request = std::get<Request>(parsed);
    LOG_DEBUG_N << "Accepted MCP request from " << peer << ": " << request.method
                << (request.tool_name.isEmpty() ? QString{} : QStringLiteral(" / ") + request.tool_name);
    if (request.method == QStringLiteral("initialize")) {
        const auto client_info = request.params.value(QStringLiteral("clientInfo")).toObject();
        LOG_DEBUG_N << "MCP client " << client_info.value(QStringLiteral("name")).toString()
                    << " (" << client_info.value(QStringLiteral("version")).toString() << ") initialized from "
                    << peer << " using protocol " << request.protocol_version;
        co_return response(request.id, initializeResult(request.protocol_version));
    }
    if (request.notification) {
        LOG_DEBUG_N << "Accepted MCP notification " << request.method << " from " << peer;
        co_return {};
    }
    const auto modern = request.protocol_version == QString::fromLatin1(protocol_version);
    if (request.method == QStringLiteral("server/discover")) co_return response(request.id, discoverResult());
    if (request.method == QStringLiteral("ping")) co_return response(request.id, QJsonObject{});
    if (request.method == QStringLiteral("resources/list"))
        co_return response(request.id, modern ? modernResult({{QStringLiteral("resources"), QJsonArray{}},
            {QStringLiteral("ttlMs"), 0}, {QStringLiteral("cacheScope"), QStringLiteral("private")}})
            : QJsonObject{{QStringLiteral("resources"), QJsonArray{}}});
    if (request.method == QStringLiteral("resources/templates/list"))
        co_return response(request.id, modern
            ? modernResult({{QStringLiteral("resourceTemplates"), QJsonArray{}},
                            {QStringLiteral("ttlMs"), 0}, {QStringLiteral("cacheScope"), QStringLiteral("private")}})
            : QJsonObject{{QStringLiteral("resourceTemplates"), QJsonArray{}}});
    if (request.method == QStringLiteral("prompts/list"))
        co_return response(request.id, modern ? modernResult({{QStringLiteral("prompts"), QJsonArray{}},
            {QStringLiteral("ttlMs"), 0}, {QStringLiteral("cacheScope"), QStringLiteral("private")}})
            : QJsonObject{{QStringLiteral("prompts"), QJsonArray{}}});
    if (request.method == QStringLiteral("tools/list")) {
        auto result = toolList();
        if (modern) {
            result.insert(QStringLiteral("ttlMs"), 0);
            result.insert(QStringLiteral("cacheScope"), QStringLiteral("private"));
            result = modernResult(std::move(result));
        }
        co_return response(request.id, result);
    }
    auto on_pending = [pending_reply, id = request.id, modern](const QJsonObject& result) {
        if (!pending_reply) return;
        pending_reply(response(id, modern ? modernResult(result) : result));
    };
    auto result = co_await toolCall(request, peer, on_pending);
    if (modern) result = modernResult(std::move(result));
    co_return response(request.id, result);
}

void McpGateway::abortForOffline() {
    QCoro::connect(store_.abortNonterminal(OperationState::AbortedOffline), &runtime_.appEventSource(), [] {});
    runtime_.cancelMcpApprovals();
}
void McpGateway::abortForSync() {
    QCoro::connect(store_.abortNonterminal(OperationState::AbortedSync), &runtime_.appEventSource(), [] {});
    runtime_.cancelMcpApprovals();
}
} // namespace nextapp::mcp
