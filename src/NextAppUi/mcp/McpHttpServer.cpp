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
constexpr quint16 default_mcp_port = 3120;
}

McpHttpServer::McpHttpServer(RuntimeServices& runtime, QObject* parent)
    : QObject{parent}, runtime_{runtime}, gateway_{runtime}
{
    server_.route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
            const auto peer = QStringLiteral("%1:%2").arg(request.remoteAddress().toString()).arg(request.remotePort());
            const auto max_body = std::clamp(runtime_.settings().value("ai/mcp/limits/requestBody", 256 * 1024).toInt(), 1024, 256 * 1024);
            LOG_DEBUG_N << "MCP HTTP POST from " << peer << ", body=" << request.body().size() << " bytes";
            LOG_TRACE_N << "MCP request payload from " << peer << ": " << request.body();
            if (request.body().size() > max_body) {
                LOG_DEBUG_N << "Rejected oversized MCP request from " << peer;
                writeJson(std::move(responder), errorResponse({}, {-32600, QStringLiteral("MCP request body is too large"), {}}), 413);
                return;
            }
            const auto max_calls = std::clamp(runtime_.settings().value("ai/mcp/limits/concurrentCalls", 16).toInt(), 1, 64);
            if (active_calls_ >= max_calls) {
                LOG_DEBUG_N << "Rejected MCP request from " << peer << ": " << active_calls_ << " calls active";
                writeJson(std::move(responder), errorResponse({}, {-32000, QStringLiteral("MCP server is busy; retry later"), {}}), 429);
                return;
            }
            ++active_calls_;
            auto pending_responder = std::make_shared<QHttpServerResponder>(std::move(responder));
            auto replied = std::make_shared<bool>(false);
            const auto modern = request.value("mcp-protocol-version") == protocol_version;
            const auto send = [this, pending_responder, replied, peer, modern](const QJsonObject& result) {
                    if (*replied) return;
                    *replied = true;
                    --active_calls_;
                    if (result.isEmpty()) {
                        LOG_DEBUG_N << "MCP notification from " << peer << " accepted";
                        pending_responder->write(QHttpServerResponder::StatusCode::Accepted);
                        return;
                    }
                    LOG_DEBUG_N << "MCP HTTP request from " << peer
                                << (result.contains(QStringLiteral("error")) ? " completed with JSON-RPC error" : " completed");
                    LOG_TRACE_N << "MCP response payload to " << peer << ": "
                                << QJsonDocument(result).toJson(QJsonDocument::Compact);
                    const auto error = result.value(QStringLiteral("error")).toObject();
                    const auto code = error.value(QStringLiteral("code")).toInt();
                    const auto explicit_status = error.value(QStringLiteral("data")).toObject()
                        .value(QStringLiteral("httpStatus")).toInt();
                    const auto status = explicit_status ? explicit_status
                        : code == -32601 && modern ? 404
                        : code == -32020 || code == -32022 || code == -32600 || code == -32602 || code == -32700 ? 400 : 200;
                    writeJson(std::move(*pending_responder), result, status);
                };
            QCoro::connect(gateway_.handle(request.body(), headers(request), peer, send), this,
                [send, replied, peer](QJsonObject result) {
                    if (*replied) {
                        LOG_DEBUG_N << "Background MCP approval/mutation finished for " << peer;
                        LOG_TRACE_N << "Background MCP result: " << QJsonDocument(result).toJson(QJsonDocument::Compact);
                        return;
                    }
                    send(result);
                });
        });
    const auto method_not_allowed = [](const QHttpServerRequest& request, QHttpServerResponder& responder) {
        LOG_DEBUG_N << "Rejected unsupported MCP HTTP method from " << request.remoteAddress().toString()
                    << ":" << request.remotePort();
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
    const auto configured_address = runtime_.settings().value("ai/mcp/listenAddress", QStringLiteral("127.0.0.1")).toString();
    QHostAddress address;

    if (!address.setAddress(configured_address)) {
        LOG_ERROR_N << "Cannot parse MCP listener address " << configured_address;
        co_return false;
    }

// For testing it's useful to allow other VM's to run untrusted agents
#ifndef _DEBUG
    if (!address.isLoopback()) {
        // Phase 1 only implements HTTP. A non-loopback listener requires the
        // separately configured HTTPS transport, so never widen the bind here.
        LOG_ERROR_N << "Refusing MCP listener address " << configured_address << "; Phase 1 HTTP requires a loopback address";
        co_return false;
    }
#endif
    const auto saved_port = runtime_.settings().value("ai/mcp/port", default_mcp_port).toUInt();
    const auto configured = saved_port == 0 ? default_mcp_port : saved_port;
    if (saved_port == 0) {
        // Migrate the former automatic-port setting. MCP clients need a
        // stable URL across restarts, so retain a concrete port before bind.
        runtime_.settings().setValue("ai/mcp/port", configured);
        runtime_.settings().sync();
    }
    if (!listener_.listen(address, static_cast<quint16>(configured))) {
        LOG_ERROR_N << "Cannot bind MCP listener to " << configured_address << ":" << configured
                    << ": " << listener_.errorString();
        co_return false;
    }
    if (!bound_ && !server_.bind(&listener_)) {
        listener_.close();
        LOG_ERROR_N << "Cannot attach the MCP HTTP server to " << configured_address << ":" << configured;
        co_return false;
    }
    bound_ = true;
    port_ = listener_.serverPort();
    bound_address_ = configured_address;
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
    bound_address_.clear();
    emit listenerChanged();
}
void McpHttpServer::refresh() {
    if (!gateway_.enabled()) {
        stop();
        return;
    }
    const auto address = runtime_.settings().value("ai/mcp/listenAddress", QStringLiteral("127.0.0.1")).toString();
    const auto saved_port = runtime_.settings().value("ai/mcp/port", default_mcp_port).toUInt();
    const auto configured_port = saved_port == 0 ? default_mcp_port : static_cast<quint16>(saved_port);
    if (running_ && bound_address_ == address && port_ == configured_port) return;
    if (running_) stop();
    QCoro::connect(start(), this, [] (bool) {});
}
void McpHttpServer::abortForOffline() { gateway_.abortForOffline(); stop(); }
void McpHttpServer::abortForSync() { gateway_.abortForSync(); stop(); }
QString McpHttpServer::credential() { return gateway_.credential(); }
QString McpHttpServer::rotateCredential() { return gateway_.rotateCredential(); }
} // namespace nextapp::mcp
