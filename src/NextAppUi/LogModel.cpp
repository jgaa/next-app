
#include "logging.h"
#include "LogModel.h"

using namespace std;

LogModel::LogModel()
{
    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::ProxyHandler>([this](const logfault::Message& msg) -> void {
            emit messageAdded(LogMessage{static_cast<int>(msg.level_), msg.when_, msg.msg_});
        }, logfault::LogLevel::DEBUGGING));

    connect(this, &LogModel::messageAdded, [this] (const LogMessage& msg) {
        auto first_deleted = messages_.size() >= queue_size_;
        if (first_deleted) {
            beginRemoveRows(QModelIndex(), 0, 0);
            messages_.pop_front();
            endRemoveRows();
        }
        beginInsertRows(QModelIndex(), messages_.size(), messages_.size());
        messages_.push_back(msg);
        endInsertRows();
    });
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return messages_.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    static const array<QString, 7> severity_names = {
        "DISABLED", "ERROR", "WARN", "NOTICE", "INFO", "DEBUGGING", "TRACE"
    };

    static const array<QString, 7> color_names = {
        "pink", "red", "orangered", "navy", "blue", "teal", "gray"
    };



    if (!index.isValid()) {
        return QVariant();
    }

    if (index.row() >= messages_.size()) {
        return QVariant();
    }

    const auto& message = messages_[index.row()];
    switch (role) {
    case SeverityRole:
        return severity_names.at(message.severity);
    case TimeRole:
        return message.time.toString(Qt::DateFormat::ISODateWithMs);
    case MessageRole:
        return message.message;
    case ColorRole:
        return color_names.at(message.severity);
    }

    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SeverityRole] = "severity";
    roles[TimeRole] = "time";
    roles[MessageRole] = "message";
    roles[ColorRole] = "eventColor";
    return roles;
}
