#pragma once

#include <mutex>
#include <chrono>
#include <boost/circular_buffer.hpp>

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QDateTime>
#include <QSettings>
#include <QTimer>
#include "qcorotask.h"

#include "nextapp.qpb.h"

class LogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString message MEMBER shortMessage_ NOTIFY messageChanged)
    Q_PROPERTY(QString messageColor MEMBER shortMessageColor_ NOTIFY messageChanged)

public:
    enum Roles {
        SeverityRole = Qt::UserRole + 1,
        TimeRole,
        MessageRole,
        ColorRole
    };

    // Mirrors logfault::LogLevel
    enum LogLevels {
        DISABLED, ERROR, WARN, NOTICE, INFO, DEBUGGING, TRACE
    };

    Q_ENUM(LogLevels)

    struct LogMessage {

        LogMessage() = default;
        LogMessage(int level, std::chrono::system_clock::time_point time, const std::string& message)
            : severity(static_cast<LogLevels>(level)),
            time{QDateTime::fromMSecsSinceEpoch(std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count())},
            message{QString::fromStdString(message)}  {
            assert(level >= 0 && level <= static_cast<int>(LogLevels::TRACE));
        }

        LogLevels severity{};
        QDateTime time;
        QString message;
    };

    LogModel();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void messageAdded(const LogMessage& message);
    void messageChanged();

private:
    void setMessage(const LogMessage& msg);

    static constexpr size_t queue_size_ = 1000;
    boost::circular_buffer<LogMessage> messages_{queue_size_};
    QString shortMessage_;
    QString shortMessageColor_;
    QTimer clearMessageTimer_;
    alignas(64) std::mutex mutex_;
    // read/write mutex
};
