#include "MobileConnectServer.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "logging.h"

namespace {

QString formattedCode(const QString& code)
{
    return code.size() == 8 ? code.sliced(0, 4) + u'-' + code.sliced(4, 4) : code;
}

}

MobileConnectServer::MobileConnectServer(QObject *parent)
    : QObject(parent)
    , server_{new QTcpServer(this)}
    , timeout_timer_{new QTimer(this)}
{
    timeout_timer_->setSingleShot(true);
    connect(server_, &QTcpServer::newConnection, this, &MobileConnectServer::handleNewConnection);
    connect(timeout_timer_, &QTimer::timeout, this, &MobileConnectServer::handleTimeout);
}

void MobileConnectServer::applySettings(bool enabled, const QString& listen_address, quint16 port)
{
    const auto normalized_address = listen_address.trimmed().isEmpty()
        ? QStringLiteral("0.0.0.0") : listen_address.trimmed();
    if (enabled_ == enabled && listen_address_ == normalized_address && configured_port_ == port) {
        if (!enabled || server_->isListening()) return;
        start();
        return;
    }
    listen_address_ = normalized_address;
    configured_port_ = port;
    enabled_ = enabled;
    restartIfNeeded();
}

void MobileConnectServer::setEnabled(bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    restartIfNeeded();
}

void MobileConnectServer::setListenAddress(const QString& listen_address)
{
    const auto normalized = listen_address.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : listen_address.trimmed();
    if (listen_address_ == normalized) return;
    listen_address_ = normalized;
    restartIfNeeded();
}

void MobileConnectServer::setPort(quint16 port)
{
    if (configured_port_ == port) return;
    configured_port_ = port;
    restartIfNeeded();
}

void MobileConnectServer::setIdeasHandler(IdeasHandler handler)
{
    ideas_handler_ = std::move(handler);
}

void MobileConnectServer::approvePendingTransfer(bool approved)
{
    if (active_socket_ == nullptr || connection_state_ != ConnectionState::WaitApproval) return;

    if (!approved) {
        sendJsonMessageAndClose(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("continue")},
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("Transfer rejected by user")},
        });
        return;
    }

    timeout_timer_->stop();
    sendJsonMessage(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("continue")},
        {QStringLiteral("ok"), true},
    });
    connection_state_ = ConnectionState::WaitPayload;
    timeout_timer_->start(payload_timeout_ms_);
}

void MobileConnectServer::rejectPendingTransfer(const QString& error_message)
{
    if (active_socket_ == nullptr || connection_state_ != ConnectionState::WaitApproval) return;
    sendJsonMessageAndClose(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("continue")},
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), error_message},
    });
}

bool MobileConnectServer::running() const
{
    return server_->isListening();
}

QString MobileConnectServer::listenAddress() const
{
    return listen_address_;
}

quint16 MobileConnectServer::port() const
{
    return server_->isListening() ? server_->serverPort() : configured_port_;
}

void MobileConnectServer::restartIfNeeded()
{
    if (!enabled_) {
        stop();
        return;
    }
    start();
}

void MobileConnectServer::start()
{
    stop();
    QHostAddress address;
    if (!address.setAddress(listen_address_)) {
        const auto message = QStringLiteral("Mobile Connect listen address is invalid: %1").arg(listen_address_);
        LOG_ERROR_N << message;
        emit transferError(message);
        return;
    }
    if (!server_->listen(address, configured_port_)) {
        const auto message = QStringLiteral("Mobile Connect could not listen on %1:%2: %3")
            .arg(listen_address_).arg(configured_port_).arg(server_->errorString());
        LOG_ERROR_N << message;
        emit transferError(message);
        emit listenerChanged();
        return;
    }
    LOG_INFO_N << "Mobile Connect listening on " << server_->serverAddress().toString()
               << ':' << server_->serverPort();
    emit listenerChanged();
}

void MobileConnectServer::stop()
{
    closeActiveConnection();
    if (server_->isListening()) {
        server_->close();
        LOG_INFO_N << "Mobile Connect listener stopped";
        emit listenerChanged();
    }
}

void MobileConnectServer::closeActiveConnection()
{
    timeout_timer_->stop();
    read_buffer_.clear();
    pending_code_.clear();
    connection_state_ = ConnectionState::Closed;
    if (active_socket_ == nullptr) return;
    disconnect(active_socket_, nullptr, this, nullptr);
    active_socket_->abort();
    active_socket_->deleteLater();
    active_socket_ = nullptr;
}

void MobileConnectServer::handleNewConnection()
{
    while (server_->hasPendingConnections()) {
        auto *socket = server_->nextPendingConnection();
        if (active_socket_ != nullptr) {
            socket->write(QJsonDocument{QJsonObject{
                {QStringLiteral("type"), QStringLiteral("error")},
                {QStringLiteral("message"), QStringLiteral("Server busy")},
            }}.toJson(QJsonDocument::Compact) + '\n');
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        active_socket_ = socket;
        read_buffer_.clear();
        pending_code_.clear();
        connection_state_ = ConnectionState::WaitHello;
        active_socket_->setReadBufferSize(max_message_size_bytes_ + 1);
        connect(active_socket_, &QTcpSocket::readyRead, this, &MobileConnectServer::handleReadyRead);
        connect(active_socket_, &QTcpSocket::disconnected, this, &MobileConnectServer::handleDisconnected);
        timeout_timer_->start(hello_timeout_ms_);
    }
}

void MobileConnectServer::handleReadyRead()
{
    if (active_socket_ == nullptr) return;
    read_buffer_.append(active_socket_->readAll());
    if (read_buffer_.size() > max_message_size_bytes_ + 1024) {
        sendErrorAndClose(QStringLiteral("Message too large"));
        return;
    }

    while (active_socket_ != nullptr) {
        const auto newline = read_buffer_.indexOf('\n');
        if (newline < 0) return;
        const auto line = read_buffer_.first(newline);
        read_buffer_.remove(0, newline + 1);
        if (line.size() > max_message_size_bytes_) {
            sendErrorAndClose(QStringLiteral("Message too large"));
            return;
        }
        QJsonParseError parse_error{};
        const auto document = QJsonDocument::fromJson(line, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
            sendErrorAndClose(QStringLiteral("Invalid JSON"));
            return;
        }
        const auto message = document.object();
        if (connection_state_ == ConnectionState::WaitHello) {
            if (!processHelloMessage(message)) return;
            continue;
        }
        if (connection_state_ == ConnectionState::WaitPayload) {
            processPayloadMessage(message);
            return;
        }
        sendErrorAndClose(QStringLiteral("Invalid state transition"));
        return;
    }
}

void MobileConnectServer::handleDisconnected()
{
    timeout_timer_->stop();
    read_buffer_.clear();
    pending_code_.clear();
    connection_state_ = ConnectionState::Closed;
    if (active_socket_ == nullptr) return;
    active_socket_->deleteLater();
    active_socket_ = nullptr;
}

void MobileConnectServer::handleTimeout()
{
    if (connection_state_ == ConnectionState::WaitHello) sendErrorAndClose(QStringLiteral("Hello timeout"));
    else if (connection_state_ == ConnectionState::WaitApproval) {
        sendJsonMessageAndClose(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("continue")},
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("Transfer approval timed out")},
        });
    } else if (connection_state_ == ConnectionState::WaitPayload) {
        sendErrorAndClose(QStringLiteral("Payload timeout"));
    }
}

bool MobileConnectServer::processHelloMessage(const QJsonObject& message)
{
    static const QRegularExpression code_pattern(QStringLiteral(R"(^\d{8}$)"));
    const auto code = message.value(QStringLiteral("code")).toString().trimmed();
    if (message.value(QStringLiteral("type")).toString() != QStringLiteral("hello")
        || message.value(QStringLiteral("app")).toString() != QStringLiteral("smtool-transfer")
        || message.value(QStringLiteral("version")).toInt(-1) != 1
        || !code_pattern.match(code).hasMatch()) {
        sendErrorAndClose(QStringLiteral("Invalid transfer hello message"));
        return false;
    }
    timeout_timer_->stop();
    pending_code_ = code;
    connection_state_ = ConnectionState::WaitApproval;
    timeout_timer_->start(approval_timeout_ms_);
    emit traffic();
    emit transferConfirmationRequested(formattedCode(code));
    return true;
}

bool MobileConnectServer::processPayloadMessage(const QJsonObject& message)
{
    if (message.value(QStringLiteral("type")).toString() != QStringLiteral("ideas")) {
        sendErrorAndClose(QStringLiteral("Unexpected message type"));
        return false;
    }
    if (message.value(QStringLiteral("version")).toInt(-1) != 1) {
        sendErrorAndClose(QStringLiteral("Unsupported version"));
        return false;
    }
    if (!message.value(QStringLiteral("items")).isArray()) {
        sendErrorAndClose(QStringLiteral("Ideas payload must contain an items array"));
        return false;
    }
    const auto items = message.value(QStringLiteral("items")).toArray();
    if (QJsonDocument{items}.toJson(QJsonDocument::Compact).size() > max_message_size_bytes_) {
        sendErrorAndClose(QStringLiteral("Payload too large"));
        return false;
    }

    int imported_count{};
    QString error_message;
    if (!ideas_handler_ || !ideas_handler_(items, &imported_count, &error_message)) {
        sendJsonMessageAndClose(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("result")},
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), error_message.isEmpty() ? QStringLiteral("No valid ideas found") : error_message},
        });
        return false;
    }
    emit traffic();
    sendJsonMessageAndClose(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("result")},
        {QStringLiteral("ok"), true},
        {QStringLiteral("imported"), imported_count},
    });
    return true;
}

void MobileConnectServer::sendErrorAndClose(const QString& message)
{
    sendJsonMessageAndClose(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("error")},
        {QStringLiteral("message"), message},
    });
}

void MobileConnectServer::sendJsonMessage(const QJsonObject& message)
{
    if (active_socket_ == nullptr) return;
    active_socket_->write(QJsonDocument{message}.toJson(QJsonDocument::Compact) + '\n');
    active_socket_->flush();
}

void MobileConnectServer::sendJsonMessageAndClose(const QJsonObject& message)
{
    sendJsonMessage(message);
    if (active_socket_ != nullptr) {
        active_socket_->disconnectFromHost();
    }
}
