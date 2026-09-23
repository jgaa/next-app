#include "McpRequestStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>

namespace nextapp::mcp {
namespace {
QString canonicalJson(const QJsonValue& value) {
    // Qt serializes object keys deterministically, which is sufficient for the local idempotency key.
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
}
StoredRequest fromRow(const QList<QVariant>& row) {
    StoredRequest value;
    value.request_id = row.at(0).toString(); value.agent_id = row.at(1).toString();
    value.idempotency_key = row.at(2).toString(); value.request_hash = row.at(3).toByteArray();
    value.operation = row.at(4).toString();
    value.arguments = QJsonDocument::fromJson(row.at(5).toByteArray()).object();
    const auto state = row.at(6).toString();
    constexpr OperationState states[] = {OperationState::Validating, OperationState::Pending, OperationState::Approved,
        OperationState::Executing, OperationState::Executed, OperationState::Rejected, OperationState::Failed,
        OperationState::Cancelled, OperationState::Expired, OperationState::AbortedSync, OperationState::AbortedOffline,
        OperationState::OutcomeUnknown};
    for (const auto candidate : states) if (state == McpRequestStore::stateName(candidate)) value.state = candidate;
    value.result = QJsonDocument::fromJson(row.at(7).toByteArray()).object();
    return value;
}
} // namespace

QString McpRequestStore::stateName(OperationState state) {
    switch (state) {
    case OperationState::Validating: return QStringLiteral("VALIDATING"); case OperationState::Pending: return QStringLiteral("PENDING");
    case OperationState::Approved: return QStringLiteral("APPROVED"); case OperationState::Executing: return QStringLiteral("EXECUTING");
    case OperationState::Executed: return QStringLiteral("EXECUTED"); case OperationState::Rejected: return QStringLiteral("REJECTED");
    case OperationState::Failed: return QStringLiteral("FAILED"); case OperationState::Cancelled: return QStringLiteral("CANCELLED");
    case OperationState::Expired: return QStringLiteral("EXPIRED"); case OperationState::AbortedSync: return QStringLiteral("ABORTED_SYNC");
    case OperationState::AbortedOffline: return QStringLiteral("ABORTED_OFFLINE"); case OperationState::OutcomeUnknown: return QStringLiteral("OUTCOME_UNKNOWN");
    } return {};
}

QByteArray McpRequestStore::canonicalHash(const QString& operation, const QJsonObject& arguments) {
    const auto normalized = operation.toUtf8() + '\n' + canonicalJson(arguments).toUtf8();
    return QCryptographicHash::hash(normalized, QCryptographicHash::Sha256);
}

QCoro::Task<bool> McpRequestStore::initialize() {
    if (initialized_) co_return true;
    const auto query = co_await db_.query(R"(CREATE TABLE IF NOT EXISTS mcp_request (
        request_id TEXT PRIMARY KEY, agent_id TEXT NOT NULL, idempotency_key TEXT NOT NULL,
        request_hash BLOB NOT NULL, operation TEXT NOT NULL, arguments BLOB NOT NULL,
        state TEXT NOT NULL, result BLOB, created_at INTEGER NOT NULL, resolved_at INTEGER,
        UNIQUE(agent_id, idempotency_key)))");
    initialized_ = bool(query);
    co_return initialized_;
}

QCoro::Task<std::optional<StoredRequest>> McpRequestStore::reserve(const QString& agent_id, const QString& idempotency_key,
                                                                     const QString& operation, const QJsonObject& arguments) {
    if (agent_id.isEmpty() || idempotency_key.isEmpty() || idempotency_key.size() > 256) co_return std::nullopt;
    const auto hash = canonicalHash(operation, arguments);
    const auto transaction = co_await db_.beginExclusiveTransaction();
    if (!transaction) co_return std::nullopt;
    const auto token = *transaction;
    const auto existing = co_await db_.queryInTransaction(token,
        "SELECT request_id,agent_id,idempotency_key,request_hash,operation,arguments,state,COALESCE(result,'{}') FROM mcp_request WHERE agent_id=? AND idempotency_key=?",
        agent_id, idempotency_key);
    if (!existing) { (void) co_await db_.rollbackExclusiveTransaction(token); co_return std::nullopt; }
    if (!existing->rows.isEmpty()) {
        const auto stored = fromRow(existing->rows.front());
        if (stored.request_hash != hash) { (void) co_await db_.rollbackExclusiveTransaction(token); co_return std::nullopt; }
        if (!co_await db_.commitExclusiveTransaction(token)) co_return std::nullopt;
        co_return stored;
    }
    StoredRequest stored{QUuid::createUuid().toString(QUuid::WithoutBraces), agent_id, idempotency_key, hash, operation, arguments};
    const auto inserted = co_await db_.queryInTransaction(token,
        "INSERT INTO mcp_request(request_id,agent_id,idempotency_key,request_hash,operation,arguments,state,result,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
        stored.request_id, agent_id, idempotency_key, hash, operation,
        QJsonDocument(arguments).toJson(QJsonDocument::Compact), stateName(stored.state), QByteArray{"{}"}, QDateTime::currentSecsSinceEpoch());
    if (!inserted || !co_await db_.commitExclusiveTransaction(token)) { (void) co_await db_.rollbackExclusiveTransaction(token); co_return std::nullopt; }
    stored.newly_reserved = true;
    co_return stored;
}

QCoro::Task<std::optional<StoredRequest>> McpRequestStore::get(const QString& agent_id, const QString& request_id) {
    const auto result = co_await db_.query(
        "SELECT request_id,agent_id,idempotency_key,request_hash,operation,arguments,state,COALESCE(result,'{}') "
        "FROM mcp_request WHERE agent_id=? AND request_id=?", agent_id, request_id);
    if (!result || result->rows.isEmpty()) co_return std::nullopt;
    co_return fromRow(result->rows.front());
}

QCoro::Task<bool> McpRequestStore::transition(const QString& request_id, OperationState from, OperationState to, const QJsonObject& result) {
    const auto changed = co_await db_.query("UPDATE mcp_request SET state=?,result=?,resolved_at=? WHERE request_id=? AND state=?",
        stateName(to), QJsonDocument(result).toJson(QJsonDocument::Compact), QDateTime::currentSecsSinceEpoch(), request_id, stateName(from));
    co_return changed && changed->affected_rows.value_or(0) == 1;
}

QCoro::Task<void> McpRequestStore::abortNonterminal(OperationState terminal_state) {
    // Connection state can change before the UI database initialization has
    // completed. With no MCP listener there can be no request to abort, and
    // querying a table that has not yet been created only produces a noisy
    // SQLite error.
    if (!initialized_) co_return;
    const auto ignored = co_await db_.query("UPDATE mcp_request SET state=?,resolved_at=? WHERE state IN ('VALIDATING','PENDING','APPROVED','EXECUTING')",
        stateName(terminal_state), QDateTime::currentSecsSinceEpoch());
    Q_UNUSED(ignored);
}

QCoro::Task<bool> McpRequestStore::copyToStagedDatabase(DbStore& source, DbStore& staged) {
    McpRequestStore origin{source};
    if (!co_await origin.initialize()) co_return false;
    McpRequestStore destination{staged};
    if (!co_await destination.initialize()) co_return false;
    const auto rows = co_await source.query("SELECT request_id,agent_id,idempotency_key,request_hash,operation,arguments,state,result,created_at,resolved_at FROM mcp_request");
    if (!rows) co_return false;
    for (const auto& row : rows->rows) {
        const auto inserted = co_await staged.query(
            "INSERT INTO mcp_request(request_id,agent_id,idempotency_key,request_hash,operation,arguments,state,result,created_at,resolved_at) VALUES(?,?,?,?,?,?,?,?,?,?)",
            row.at(0), row.at(1), row.at(2), row.at(3), row.at(4), row.at(5), row.at(6), row.at(7), row.at(8), row.at(9));
        if (!inserted) co_return false;
    }
    co_return true;
}
} // namespace nextapp::mcp
