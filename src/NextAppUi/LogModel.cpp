
#include <QString>
#include <QRegularExpression>

#ifdef __ANDROID__
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#endif

#include "logging.h"
#include "LogModel.h"

using namespace std;

namespace {
const array<QString, 7> severity_names = {
    "DISABLED", "ERROR", "WARN", "NOTICE", "INFO", "DEBUG", "TRACE"
};

const array<QString, 7> color_names = {
    "pink", "red", "orangered", "navy", "blue", "teal", "gray"
};

static const string handler_name = "LogModel";

#include <QString>
#include <QRegularExpression>

#include <QString>
#include <QRegularExpression>

#include <QString>
#include <QRegularExpression>

#include <QString>
#include <QRegularExpression>

// Clean log message for C++ function signature if it comes before the message
QString cleanLogMessage(const QString& logMessage) {
    static const QRegularExpression logPattern(R"(
        ^                                     # Start of string
        (?:[\w:<>\*&\s]+)?                    # Optional return type (including templates/pointers)
        [\w:<>]+                              # Function/Class with namespaces (e.g., GreenMonthModel::GreenMonthModel)
        (?:\(.*?\))?                          # Optional function parameters
        (?:\s*::\(anonymous class\)::[\w\s\(\)]+)?  # Optional anonymous class with operator() (if exists)
        \s*-\s*                               # " - " separator with spaces
    )", QRegularExpression::ExtendedPatternSyntaxOption);

    QRegularExpressionMatch match = logPattern.match(logMessage);
    if (match.hasMatch()) {
        return logMessage.mid(match.capturedEnd()).trimmed();
    }

    return logMessage;  // Return original if no match (already clean)
}

#ifdef __ANDROID__
void showToast(const QString &message) {
    QJniObject javaMessage = QJniObject::fromString(message);

    QCoreApplication::instance()->nativeInterface<QNativeInterface::QAndroidApplication>()
        ->runOnAndroidMainThread([javaMessage]() {
            QJniObject context = QJniObject::callStaticObjectMethod(
                "org/qtproject/qt/android/QtNative",
                "activity",
                "()Landroid/app/Activity;"
                );

            if (!context.isValid()) {
                LOG_DEBUG_N << "Failed to get Android context!";
                return;
            }

            QJniObject toast = QJniObject::callStaticObjectMethod(
                "android/widget/Toast",
                "makeText",
                "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
                context.object(),
                javaMessage.object(),
                0 // 0 = Toast.LENGTH_SHORT
                );

            if (toast.isValid()) {
                toast.callMethod<void>("show");
            } else {
                LOG_DEBUG_N << "Failed to create Toast!";
            }
        });
}
#endif

} // anon ns

LogModel::LogModel()
{
    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::ProxyHandler>(handler_name,
            [this](const logfault::Message& msg) -> void {
            emit messageAdded(LogMessage{static_cast<int>(msg.level_), msg.when_, msg.msg_});
        }, logfault::LogLevel::DEBUGGING));

    connect(this, &LogModel::messageAdded, [this] (const LogMessage& msg) {
        QMetaObject::invokeMethod(this, [this, msg] () {
            auto first_deleted = messages_.size() >= queue_size_;
            if (first_deleted) {
                beginRemoveRows(QModelIndex(), 0, 0);
                messages_.pop_front();
                endRemoveRows();
            }
            beginInsertRows(QModelIndex(), messages_.size(), messages_.size());
            messages_.push_back(msg);
            endInsertRows();

            if (msg.severity <= LogLevels::INFO && !msg.message.startsWith("[Qt]")) {
                setMessage(msg);
            }
        }, Qt::QueuedConnection);
    });

    clearMessageTimer_.callOnTimeout([this]() {
        setMessage({});
    });
}

LogModel::~LogModel()
{
    LOG_DEBUG_N << "Removing LogModel handler";
    logfault::LogManager::Instance().RemoveHandler(handler_name);
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return messages_.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{

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

void LogModel::setMessage(const LogMessage& msg)
{
    auto cleaned = cleanLogMessage(msg.message);

    if (cleaned == shortMessage_) {
        // Dont display a series of the same error. One time is enough.
        return;
    }

    shortMessage_ = cleaned;
    shortMessageColor_ = color_names.at(msg.severity);
    emit messageChanged();
    clearMessageTimer_.setSingleShot(true);

    if (!msg.message.isEmpty()) {
        clearMessageTimer_.start(5000);
        LOG_TRACE_N << "Original message: " << msg.message;
    }

    LOG_TRACE_N << "Setting cleaned message to: " << shortMessage_;

#ifdef __ANDROID__
    if (msg.severity == LogLevels::WARN || msg.severity == LogLevels::ERROR) {
        showToast(shortMessage_);
    }

#endif
}
