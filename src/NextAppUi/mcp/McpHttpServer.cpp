#include "McpHttpServer.h"

#include <QJsonDocument>
#include <QHostAddress>
#include <QTcpServer>

#include <algorithm>
#include <memory>

#include "RuntimeServices.h"
#include "logging.h"

namespace nextapp::mcp {
namespace {
constexpr quint16 default_mcp_port = 58421;
}

McpHttpServer::McpHttpServer(RuntimeServices& runtime, QObject* parent)
    : QObject{parent}, runtime_{runtime}, gateway_{runtime}
{
    server_.route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
            const auto max_body = std::clamp(runtime_.settings().value("ai/mcp/limits/request_body", 256 * 1024).toInt(), 1024, 256 * 1024);
            if (request.body().size() > max_body) {
                writeJson(std::move(responder), errorResponse({}, {-32600, QStringLiteral("MCP request body is too large"), {}}), 413);
                return;
            }
            auto pending_responder = std::make_shared<QHttpServerResponder>(std::move(responder));
            QCoro::connect(gateway_.handle(request.body(), headers(request)), this,
                [this, pending_responder](QJsonObject result) { writeJson(std::move(*pending_responder), result); });
        });
    const auto method_not_allowed = [](QHttpServerResponder& responder) {
        responder.write(QHttpServerResponder::StatusCode::MethodNotAllowed);
    };
    server_.route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Get, method_not_allowed);
    server_.route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Delete, method_not_allowed);
}

HeaderMap McpHttpServer::headers(const QHttpServerRequest& request) const {
    HeaderMap values;
    for (const auto& name : {QByteArray{"authorization"}, QByteArray{"origin"}, QByteArray{"mcp-protocol-version"}, QByteArray{"mcp-method"}, QByteArray{"mcp-name"}})
        values.insert(name, request.value(name));
    return values;
}

void McpHttpServer::writeJson(QHttpServerResponder&& responder, const QJsonObject& object, int status) const {
    responder.write(QJsonDocument(object), static_cast<QHttpServerResponder::StatusCode>(status));
}

QCoro::Task<bool> McpHttpServer::start() {
    if (running_) co_return true;
    if (!gateway_.enabled()) {
        LOG_DEBUG_N << "MCP listener remains disabled: AI/MCP is disabled or the client is offline";
        co_return false;
    }
    if (!co_await gateway_.initialize()) {
        LOG_ERROR_N << "Cannot initialize the local MCP request store";
        co_return false;
    }
    const auto configured_address = runtime_.settings().value("ai/mcp/listen_address", QStringLiteral("127.0.0.1")).toString();
    QHostAddress address;
    if (!address.setAddress(configured_address) || !address.isLoopback()) {
        // Phase 1 only implements HTTP. A non-loopback listener requires the
        // separately configured HTTPS transport, so never widen the bind here.
        LOG_ERROR_N << "Refusing MCP listener address " << configured_address << "; Phase 1 HTTP requires a loopback address";
        co_return false;
    }
    const auto saved_port = runtime_.settings().value("ai/mcp/port", default_mcp_port).toUInt();
    const auto configured = saved_port == 0 ? default_mcp_port : saved_port;
    if (saved_port == 0) {
        // Migrate the former automatic-port setting. MCP clients need a
        // stable URL across restarts, so retain a concrete port before bind.
        runtime_.settings().setValue("ai/mcp/port", configured);
        runtime_.settings().sync();
    }
    if (!listener_.listen(address, static_cast<quint16>(configured))) {
        LOG_ERROR_N << "Cannot bind MCP listener to " << configured_address << ":" << configured;
        co_return false;
    }
    if (!bound_ && !server_.bind(&listener_)) {
        listener_.close();
        LOG_ERROR_N << "Cannot attach the MCP HTTP server to " << configured_address << ":" << configured;
        co_return false;
    }
    bound_ = true;
    port_ = listener_.serverPort();
    running_ = true;
    (void) gateway_.credential();
    LOG_INFO_N << "Local MCP listener started at http://" << configured_address << ":" << port_ << "/mcp";
    emit listenerChanged();
    co_return true;
}

void McpHttpServer::stop() {
    if (!running_) return;
    LOG_INFO_N << "Local MCP listener stopped";
    listener_.close();
    running_ = false;
    port_ = 0;
    emit listenerChanged();
}
void McpHttpServer::refresh() { if (gateway_.enabled()) QCoro::connect(start(), this, [] (bool) {}); else stop(); }
void McpHttpServer::abortForOffline() { gateway_.abortForOffline(); stop(); }
void McpHttpServer::abortForSync() { gateway_.abortForSync(); stop(); }
QString McpHttpServer::credential() { return gateway_.credential(); }
QString McpHttpServer::rotateCredential() { return gateway_.rotateCredential(); }
} // namespace nextapp::mcp
