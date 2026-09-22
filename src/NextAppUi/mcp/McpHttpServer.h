#pragma once

#include <QHttpServer>
#include <QTcpServer>

#include "McpGateway.h"

class RuntimeServices;

namespace nextapp::mcp {

class McpHttpServer final : public QObject {
    Q_OBJECT
public:
    explicit McpHttpServer(RuntimeServices& runtime, QObject* parent = nullptr);
    QCoro::Task<bool> start();
    void stop();
    bool running() const noexcept { return running_; }
    quint16 port() const noexcept { return port_; }
    QString credential();
    QString rotateCredential();
public slots:
    void refresh();
    void abortForOffline();
    void abortForSync();
signals:
    void listenerChanged();
private:
    HeaderMap headers(const QHttpServerRequest& request) const;
    void writeJson(QHttpServerResponder&& responder, const QJsonObject& object, int status = 200) const;
    RuntimeServices& runtime_;
    McpGateway gateway_;
    QTcpServer listener_;
    QHttpServer server_;
    bool bound_{};
    bool running_{};
    quint16 port_{};
};
} // namespace nextapp::mcp
