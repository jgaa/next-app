#pragma once

#include <QJsonObject>
#include <functional>

#include "McpProtocol.h"
#include "McpRequestStore.h"

class RuntimeServices;

namespace nextapp::mcp {

using PendingReply = std::function<void(const QJsonObject&)>;

class McpGateway final {
public:
    explicit McpGateway(RuntimeServices& runtime);
    QCoro::Task<bool> initialize();
    QCoro::Task<QJsonObject> handle(QByteArray body, HeaderMap headers,
                                    QString peer, PendingReply pending_reply = {});
    bool enabled() const;
    QString credential();
    QString rotateCredential();
    void abortForOffline();
    void abortForSync();

private:
    QCoro::Task<QJsonObject> toolCall(const Request& request, const QString& peer, PendingReply pending_reply);
    QCoro::Task<QJsonObject> requestStatus(const QJsonObject& arguments);
    QCoro::Task<QJsonObject> action(const QString& id);
    QCoro::Task<QJsonObject> actions(const QJsonObject& arguments, bool search);
    QCoro::Task<QJsonObject> nodes(const QJsonObject& arguments, bool search);
    QCoro::Task<QJsonObject> categories(const QJsonObject& arguments, bool search);
    QCoro::Task<QJsonObject> createNode(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply);
    QCoro::Task<QJsonObject> updateNode(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply);
    QCoro::Task<QJsonObject> createAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply);
    QCoro::Task<QJsonObject> updateAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply);
    QCoro::Task<QJsonObject> completeAction(const QJsonObject& arguments, const QString& peer, PendingReply pending_reply);
    QCoro::Task<std::optional<StoredRequest>> reserveMutation(const QString& operation, const QJsonObject& arguments);
    QCoro::Task<std::optional<QJsonObject>> approveMutation(const StoredRequest& request,
                                                            const QJsonObject& arguments, PendingReply pending_reply);
    bool mutationAllowed(const QString& operation) const;
    QString mutationGate(const QString& operation) const;
    void recordActivity(const StoredRequest& request, const QString& state, const QJsonObject& result = {}) const;
    void logMutation(const StoredRequest& request, const QJsonObject& arguments,
                     const QString& peer) const;
    QString agentDescription(const StoredRequest& request) const;
    QJsonObject toolResult(const QJsonObject& structured, bool is_error = false) const;
    bool authenticate(const HeaderMap& headers) const;
    bool originAllowed(const HeaderMap& headers) const;
    int boundedPageSize(const QJsonObject& arguments) const;

    RuntimeServices& runtime_;
    McpRequestStore store_;
};
} // namespace nextapp::mcp
