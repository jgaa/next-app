#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>

#include "DbStore.h"

namespace nextapp::mcp {

enum class OperationState { Validating, Pending, Approved, Executing, Executed, Rejected, Failed,
                            Cancelled, Expired, AbortedSync, AbortedOffline, OutcomeUnknown };

struct StoredRequest {
    QString request_id;
    QString agent_id;
    QString idempotency_key;
    QByteArray request_hash;
    QString operation;
    QJsonObject arguments;
    OperationState state{OperationState::Validating};
    QJsonObject result;
    bool newly_reserved{};
};

class McpRequestStore {
public:
    explicit McpRequestStore(DbStore& db) : db_{db} {}

    QCoro::Task<bool> initialize();
    // Atomically reserves an idempotency key. A conflicting payload is returned as nullopt.
    QCoro::Task<std::optional<StoredRequest>> reserve(const QString& agent_id,
                                                      const QString& idempotency_key,
                                                      const QString& operation,
                                                      const QJsonObject& arguments);
    QCoro::Task<std::optional<StoredRequest>> get(const QString& agent_id, const QString& request_id);
    QCoro::Task<bool> transition(const QString& request_id, OperationState from,
                                 OperationState to, const QJsonObject& result = {});
    QCoro::Task<void> abortNonterminal(OperationState terminal_state);
    // Called while a staged full-sync database transaction is open. Local-only
    // idempotency/audit state must survive the staged database swap.
    static QCoro::Task<bool> copyToStagedDatabase(DbStore& source, DbStore& staged);

    static QByteArray canonicalHash(const QString& operation, const QJsonObject& arguments);
    static QString stateName(OperationState state);
private:
    DbStore& db_;
    bool initialized_{};
};
} // namespace nextapp::mcp
