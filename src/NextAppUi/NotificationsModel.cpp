
#include <algorithm>
#include <QProtobufSerializer>

#include "DbStore.h"
#include "NextAppCore.h"
#include "format_wrapper.h"
#include "logging.h"

#include "NotificationsModel.h"

namespace {
    static const QString insert_query = R"(INSERT INTO notification (
            id, uuid, time, kind, updated, data
        ) VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            time = excluded.time,
            kind = excluded.kind,
            updated = excluded.updated,
            data = excluded.data
    )";

    QList<QVariant> getParams(const nextapp::pb::Notification &notification)
    {
        QList<QVariant> params;
        params.reserve(6);
        params << static_cast<quint32>(notification.id_proto());
        params << notification.uuid().uuid();
        params << QDateTime::fromSecsSinceEpoch(notification.createdTime().unixTime());
        params << static_cast<quint32>(notification.kind());
        params << static_cast<qlonglong>(notification.updated());

        QProtobufSerializer serializer;
        params << notification.serialize(&serializer);

        return params;
    }

} // namespace


QCoro::Task<bool> NotificationsModel::saveBatch(const QList<nextapp::pb::Notification> &items)
{
    auto& db = NextAppCore::instance()->db();
    static const QString delete_query = "DELETE FROM notification WHERE id = ?";
    auto isDeleted = [](const auto& notification) {
        return notification.kind() == nextapp::pb::Notification::Kind::DELETED;
    };
    auto getId = [](const auto& work) {
        return static_cast<quint32>(work.id_proto());
    };

    co_return co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId);
}

QCoro::Task<void> NotificationsModel::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    assert(update);
    if (update->hasNotifications()) {
        auto notifications = update->notifications();
        const auto& items = notifications.notifications();
        if (items.empty()) {
            co_return;
        }

        auto& db = NextAppCore::instance()->db();
        static const QString delete_query = "DELETE FROM notification WHERE id = ?";
        auto isDeleted = [](const auto& notification) {
            return notification.kind() == nextapp::pb::Notification::Kind::DELETED;
        };
        auto getId = [](const auto& work) {
            return static_cast<quint32>(work.id_proto());
        };

        co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId);
        loadFromCache();

        const auto& last_update = std::ranges::max_element(items, [](const auto& lhs, const auto& rhs) {
            return lhs.updated() < rhs.updated();
        });

        if (last_update_seen_ < last_update->updated()) {
            setLastUpdateSeen(last_update->updated());
        }
    } else if (update->hasLastReadNotificationId()) {
        SetLastReadValue(update->lastReadNotificationId());
    }

    co_return;
}

QCoro::Task<bool> NotificationsModel::save(const QProtobufMessage &item)
{
    const auto& notification = static_cast<const nextapp::pb::Notification&>(item);
    auto& db = NextAppCore::instance()->db();

    if (notification.kind() == nextapp::pb::Notification::Kind::DELETED) {
        static const QString delete_query = "DELETE FROM notification WHERE id = ?";
        QList<QVariant> params;
        params << static_cast<quint32>(notification.id_proto());
        co_return co_await db.query(delete_query, params);
    }

    auto params = getParams(notification);
    const auto rval = co_await db.query(insert_query, params);
    if (rval) {
        co_return rval.value().affected_rows.value() > 0;
    }

    LOG_WARN_N << "Failed to save notification #" << notification.id_proto() << ": " << rval.error();
    co_return false;
}

QCoro::Task<bool> NotificationsModel::loadFromCache()
{
    clear();

    auto& db = NextAppCore::instance()->db();
    static const QString query = "SELECT id, data FROM notification ORDER BY id DESC";
    enum Cols {
        ID = 0,
        DATA,
    };
    auto res = co_await db.query(query);
    if (res) {
        auto now = QDateTime::currentDateTime();
        decltype (notifications_) notifications;
        const auto& rows = res.value().rows;
        for (const auto& row : rows) {
            QProtobufSerializer serializer;
            nextapp::pb::Notification notification;
            if (!notification.deserialize(&serializer, row.at(DATA).toByteArray())) {
                LOG_ERROR_N << "Failed to parse notification #" << row.at(ID).toUInt();
                continue;
            }

            if (notification.hasValidTo()) {
                const auto until = QDateTime::fromSecsSinceEpoch(notification.validTo().unixTime());
                if (until < now) {
                    LOG_TRACE_N << "Notification #" << notification.id_proto() << " is expired";
                    continue;
                }
            }

            notifications.emplace_back(notification);
        }
        if (!notifications.empty()) {
            beginInsertRows(QModelIndex(), 0, notifications.size() - 1);
            notifications_.insert(notifications_.end(), notifications.begin(), notifications.end());
            endInsertRows();
        }
    }
    emit unreadChanged();
    co_return true;
}

std::shared_ptr<GrpcIncomingStream> NotificationsModel::openServerStream(nextapp::pb::GetNewReq req)
{
    return ServerComm::instance().synchNotifications(req);
}

void NotificationsModel::clear()
{
    beginResetModel();
    notifications_.clear();
    endResetModel();
}

int NotificationsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return notifications_.size();
}

int NotificationsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return 1; // Only one column for notifications
}

QVariant NotificationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= notifications_.size()) {
        return QVariant();
    }

    const auto& notification = notifications_.at(index.row());
    switch (role) {
        case idRole:
            return static_cast<quint32>(notification.id_proto());
        case uuidRole:
            return notification.uuid().uuid();
        case createdTimeRole: {
            auto date = QDateTime::fromSecsSinceEpoch(notification.createdTime().unixTime()).date();
            return date.toString(Qt::TextDate);
        }
        case validToRole:
            return QDateTime::fromSecsSinceEpoch(notification.validTo().unixTime());
        case subjectRole:
            return notification.subject();
        case messageRole:
            return notification.message();
        case senderTypeRole:
            return static_cast<quint32>(notification.senderType());
        case senderIdRole:
            return notification.senderId();
        case toUserRole:
            return notification.toUser().uuid();
        case toTenantRole:
            return notification.toTenant().uuid();
        case kindRole:
            return static_cast<quint32>(notification.kind());
        case dataRole:
            return notification.data();
        case updatedRole:
            return static_cast<qlonglong>(notification.updated());
        default:
            LOG_WARN_N << "Unknown role requested: " << role;
            break;
    }

    return {};
}

QHash<int, QByteArray> NotificationsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[idRole] = "id";
    roles[uuidRole] = "uuid";
    roles[createdTimeRole] = "createdTime";
    roles[validToRole] = "validTo";
    roles[subjectRole] = "subject";
    roles[messageRole] = "message";
    roles[senderTypeRole] = "senderType";
    roles[senderIdRole] = "senderId";
    roles[toUserRole] = "toUser";
    roles[toTenantRole] = "toTenant";
    roles[kindRole] = "kind";
    roles[dataRole] = "data";
    roles[updatedRole] = "updated";

    return roles;
}

NotificationsModel *NotificationsModel::instance() noexcept
{
    static NotificationsModel nm;
    return &nm;
}

void NotificationsModel::setLastRead(uint32_t last_read) {
    if (last_read_ < last_read) {
        SetLastReadValue(last_read);
        ServerComm::instance().setLastReadNotification(last_read_);
    }
}

bool NotificationsModel::SetLastReadValue(uint32_t last_read)
{
    if (last_read_ != last_read) {
        last_read_ = last_read;
        QSettings{}.setValue("notificatins/last_read", last_read_);
        emit lastReadChanged();
        emit unreadChanged();
        LOG_TRACE_N << "Last read set to " << last_read_;
        return true;
    }

    return false;
}

void NotificationsModel::setLastUpdateSeen(uint64_t last_update_seen) {
    last_update_seen_ = last_update_seen;
    QSettings{}.setValue("notificatins/last_seen", static_cast<qlonglong>(last_update_seen_));
    LOG_TRACE_N << "Last update seen set to " << last_update_seen_;
}

bool NotificationsModel::unread() const noexcept
{
    if (notifications_.empty()) {
        return false;
    }

    const auto& last_notification = notifications_.front();
    return last_notification.id_proto() > last_read_;
}

NotificationsModel::NotificationsModel() {
    connect(&ServerComm::instance(), &ServerComm::onUpdate, this,
            [this](const std::shared_ptr<nextapp::pb::Update>& update) {
                onUpdate(update);
            });

    QSettings s;
    last_read_ = s.value("notificatins/last_read", 0).toUInt();
    last_update_seen_ = s.value("notificatins/last_seen", 0).toULongLong();
}

