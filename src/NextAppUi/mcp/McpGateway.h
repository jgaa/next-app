#pragma once

#include <QJsonObject>
#include <functional>

#include "McpProtocol.h"
#include "McpRequestStore.h"

class RuntimeServices;

namespace nextapp::mcp {

class McpGateway final {
public:
    explicit McpGateway(RuntimeServices& runtime);
    QCoro::Task<bool> initialize();
    QCoro::Task<QJsonObject> handle(const QByteArray& body, const HeaderMap& headers);
    bool enabled() const;
    QString credential();
    QString rotateCredential();
    void abortForOffline();
    void abortForSync();

private:
    QCoro::Task<QJsonObject> toolCall(const Request& request);
    QCoro::Task<QJsonObject> action(const QString& id);
    QCoro::Task<QJsonObject> actions(const QJsonObject& arguments, bool search);
    QCoro::Task<QJsonObject> createAction(const QJsonObject& arguments);
    QCoro::Task<QJsonObject> updateAction(const QJsonObject& arguments);
    QCoro::Task<QJsonObject> completeAction(const QJsonObject& arguments);
    QCoro::Task<std::optional<StoredRequest>> reserveMutation(const QString& operation, const QJsonObject& arguments);
    bool mutationAllowed(const QString& operation) const;
    QJsonObject toolResult(const QJsonObject& structured, bool is_error = false) const;
    bool authenticate(const HeaderMap& headers) const;
    bool originAllowed(const HeaderMap& headers) const;
    int boundedPageSize(const QJsonObject& arguments) const;

    RuntimeServices& runtime_;
    McpRequestStore store_;
};
} // namespace nextapp::mcp
