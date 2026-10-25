#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;
class QJsonObject;

class MobileConnectServer final : public QObject
{
    Q_OBJECT
public:
    using IdeasHandler = std::function<bool(const QJsonArray&, int*, QString*)>;

    explicit MobileConnectServer(QObject *parent = nullptr);

    void applySettings(bool enabled, const QString& listen_address, quint16 port);
    void setEnabled(bool enabled);
    void setListenAddress(const QString& listen_address);
    void setPort(quint16 port);
    void setIdeasHandler(IdeasHandler handler);
    void approvePendingTransfer(bool approved);
    void rejectPendingTransfer(const QString& error_message);

    [[nodiscard]] bool running() const;
    [[nodiscard]] QString listenAddress() const;
    [[nodiscard]] quint16 port() const;

signals:
    void listenerChanged();
    void transferConfirmationRequested(const QString& code);
    void transferError(const QString& message);
    void traffic();

private:
    enum class ConnectionState { Closed, WaitHello, WaitApproval, WaitPayload };

    void restartIfNeeded();
    void start();
    void stop();
    void closeActiveConnection();
    void handleNewConnection();
    void handleReadyRead();
    void handleDisconnected();
    void handleTimeout();
    bool processHelloMessage(const QJsonObject& message);
    bool processPayloadMessage(const QJsonObject& message);
    void sendErrorAndClose(const QString& message);
    void sendJsonMessage(const QJsonObject& message);
    void sendJsonMessageAndClose(const QJsonObject& message);

    static constexpr qsizetype max_message_size_bytes_{4 * 1024 * 1024};
    static constexpr int hello_timeout_ms_{5000};
    static constexpr int approval_timeout_ms_{3 * 60 * 1000};
    static constexpr int payload_timeout_ms_{30000};

    QTcpServer *server_{};
    QTcpSocket *active_socket_{};
    QTimer *timeout_timer_{};
    QByteArray read_buffer_{};
    ConnectionState connection_state_{ConnectionState::Closed};
    bool enabled_{};
    QString listen_address_{QStringLiteral("0.0.0.0")};
    quint16 configured_port_{45437};
    QString pending_code_{};
    IdeasHandler ideas_handler_{};
};
