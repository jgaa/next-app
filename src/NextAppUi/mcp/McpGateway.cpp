#include "McpGateway.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <cstring>

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
    const auto url = QUrl(QString::fromUtf8(origin));
    return url.isValid() && (url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("::1"));
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
        sql += " WHERE name LIKE ? ESCAPE '\\\\'";
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

bool McpGateway::mutationAllowed(const QString& operation) const {
    return runtime_.settings().value(QStringLiteral("ai/mcp/gate/") + operation, QStringLiteral("Disabled")).toString() == QStringLiteral("Always allow");
}

QCoro::Task<std::optional<StoredRequest>> McpGateway::reserveMutation(const QString& operation, const QJsonObject& arguments) {
    if (!mutationAllowed(operation)) co_return std::nullopt;
    const auto key = arguments.value(QStringLiteral("idempotencyKey")).toString();
    co_return co_await store_.reserve(runtime_.settings().value("ai/mcp/agent_id", QStringLiteral("local-agent")).toString(), key, operation, arguments);
}

QCoro::Task<QJsonObject> McpGateway::createAction(const QJsonObject& arguments) {
    if (arguments.value(QStringLiteral("name")).toString().isEmpty()
        || arguments.value(QStringLiteral("name")).toString().size() > 255
        || QUuid{arguments.value(QStringLiteral("nodeId")).toString()}.isNull()
        || arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024)
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("Invalid action fields")}}, true);
    auto stored = co_await reserveMutation(QStringLiteral("create_action"), arguments);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Create actions require the Always allow gate in this experimental build")}}, true);
    if (stored->state != OperationState::Validating) co_return toolResult(stored->result, stored->state != OperationState::Executed);
    if (!co_await store_.transition(stored->request_id, OperationState::Validating, OperationState::Executing))
        co_return toolResult({{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("state"), QStringLiteral("already executing")}}, true);
    nextapp::pb::Action action;
    action.setId_proto(QUuid::createUuid().toString(QUuid::WithoutBraces));
    action.setNode(arguments.value(QStringLiteral("nodeId")).toString());
    action.setName(arguments.value(QStringLiteral("name")).toString());
    action.setDescr(arguments.value(QStringLiteral("description")).toString());
    nextapp::pb::Date date; const auto today = QDate::currentDate(); date.setYear(today.year()); date.setMonth(today.month()); date.setMday(today.day()); action.setCreatedDate(date);
    const auto status = co_await runtime_.serverComm().addActionDirect(action);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), action.id_proto()}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::updateAction(const QJsonObject& arguments) {
    if ((!arguments.contains(QStringLiteral("name")) && !arguments.contains(QStringLiteral("description")))
        || (arguments.contains(QStringLiteral("name")) && (arguments.value(QStringLiteral("name")).toString().isEmpty()
            || arguments.value(QStringLiteral("name")).toString().size() > 255))
        || (arguments.contains(QStringLiteral("description")) && arguments.value(QStringLiteral("description")).toString().toUtf8().size() > 64 * 1024))
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("An update patch must contain valid name or description fields")}}, true);
    auto stored = co_await reserveMutation(QStringLiteral("update_action"), arguments);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Action updates require the Always allow gate in this experimental build")}}, true);
    if (stored->state != OperationState::Validating) co_return toolResult(stored->result, stored->state != OperationState::Executed);
    const auto action = co_await ActionInfoCache::instance()->getAction(QUuid{arguments.value(QStringLiteral("id")).toString()});
    if (!action || action->version() != arguments.value(QStringLiteral("baseVersion")).toInt()) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("error"), QStringLiteral("Action is missing or changed since the supplied baseVersion")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Validating, OperationState::Failed, result);
        co_return toolResult(result, true);
    }
    if (arguments.contains(QStringLiteral("name"))) action->setName(arguments.value(QStringLiteral("name")).toString());
    if (arguments.contains(QStringLiteral("description"))) action->setDescr(arguments.value(QStringLiteral("description")).toString());
    if (!co_await store_.transition(stored->request_id, OperationState::Validating, OperationState::Executing)) co_return toolResult({{QStringLiteral("requestId"), stored->request_id}}, true);
    const auto status = co_await runtime_.serverComm().updateActionDirect(*action);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), action->id_proto()}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::completeAction(const QJsonObject& arguments) {
    if (QUuid{arguments.value(QStringLiteral("id")).toString()}.isNull() || !arguments.contains(QStringLiteral("baseVersion")) || !arguments.contains(QStringLiteral("done")))
        co_return toolResult({{QStringLiteral("error"), QStringLiteral("id, baseVersion, and done are required")}}, true);
    auto stored = co_await reserveMutation(QStringLiteral("complete_action"), arguments);
    if (!stored) co_return toolResult({{QStringLiteral("error"), QStringLiteral("Action completion requires the Always allow gate in this experimental build")}}, true);
    if (stored->state != OperationState::Validating) co_return toolResult(stored->result, stored->state != OperationState::Executed);
    const auto id = arguments.value(QStringLiteral("id")).toString();
    const auto action = co_await ActionInfoCache::instance()->getAction(QUuid{id});
    if (!action || action->version() != arguments.value(QStringLiteral("baseVersion")).toInt()) {
        const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("error"), QStringLiteral("Action is missing or changed since the supplied baseVersion")}};
        (void) co_await store_.transition(stored->request_id, OperationState::Validating, OperationState::Failed, result);
        co_return toolResult(result, true);
    }
    if (!co_await store_.transition(stored->request_id, OperationState::Validating, OperationState::Executing)) co_return toolResult({{QStringLiteral("requestId"), stored->request_id}}, true);
    nextapp::pb::ActionDoneReq request; request.setUuid(id); request.setDone(arguments.value(QStringLiteral("done")).toBool());
    const auto status = co_await runtime_.serverComm().markActionDoneDirect(request);
    const QJsonObject result{{QStringLiteral("requestId"), stored->request_id}, {QStringLiteral("actionId"), id}, {QStringLiteral("serverError"), int(status.error())}, {QStringLiteral("message"), status.message()}, {QStringLiteral("manualReconciliationRequired"), status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR}};
    const auto state = status.error() == nextapp::pb::ErrorGadget::Error::OK ? OperationState::Executed
        : (status.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR ? OperationState::OutcomeUnknown : OperationState::Failed);
    (void) co_await store_.transition(stored->request_id, OperationState::Executing, state, result);
    co_return toolResult(result, status.error() != nextapp::pb::ErrorGadget::Error::OK);
}

QCoro::Task<QJsonObject> McpGateway::toolCall(const Request& request) {
    const auto arguments = request.params.value(QStringLiteral("arguments")).toObject();
    if (request.tool_name == QStringLiteral("get_action")) co_return co_await action(arguments.value(QStringLiteral("id")).toString());
    if (request.tool_name == QStringLiteral("get_list_actions")) co_return co_await actions(arguments, false);
    if (request.tool_name == QStringLiteral("search_actions")) co_return co_await actions(arguments, true);
    if (request.tool_name == QStringLiteral("create_action")) co_return co_await createAction(arguments);
    if (request.tool_name == QStringLiteral("update_action")) co_return co_await updateAction(arguments);
    if (request.tool_name == QStringLiteral("complete_action")) co_return co_await completeAction(arguments);
    co_return toolResult({{QStringLiteral("error"), QStringLiteral("Tool is not enabled")}}, true);
}

QCoro::Task<QJsonObject> McpGateway::handle(const QByteArray& body, const HeaderMap& headers) {
    if (!enabled()) co_return errorResponse({}, {-32000, QStringLiteral("AI or MCP is disabled, offline, or synchronizing"), {}});
    if (!originAllowed(headers) || !authenticate(headers)) co_return errorResponse({}, {-32001, QStringLiteral("Unauthorized MCP request"), {}});
    const auto parsed = parseRequest(body, headers);
    if (const auto* error = std::get_if<ProtocolError>(&parsed)) co_return errorResponse({}, *error);
    const auto& request = std::get<Request>(parsed);
    if (request.method == QStringLiteral("tools/list")) co_return response(request.id, toolList());
    co_return response(request.id, co_await toolCall(request));
}

void McpGateway::abortForOffline() { QCoro::connect(store_.abortNonterminal(OperationState::AbortedOffline), &runtime_.appEventSource(), [] {}); }
void McpGateway::abortForSync() { QCoro::connect(store_.abortNonterminal(OperationState::AbortedSync), &runtime_.appEventSource(), [] {}); }
} // namespace nextapp::mcp
