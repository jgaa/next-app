#pragma once

#include <deque>

#include <QObject>
#include <QQmlEngine>

#include "ServerSynchedCahce.h"

class NotificationsModel : public QAbstractTableModel
    , public ServerSynchedCahce<nextapp::pb::Notification, NotificationsModel>
{
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles {
        idRole = Qt::UserRole + 1,
        uuidRole,
        createdTimeRole,
        validToRole,
        subjectRole,
        messageRole,
        senderTypeRole,
        senderIdRole,
        toUserRole,
        toTenantRole,
        kindRole,
        dataRole,
        updatedRole
    };

    NotificationsModel();

    // ServerSynchedCahce overrides
    bool haveBatch() const noexcept override { return true; }
    QCoro::Task<bool> saveBatch(const QList<nextapp::pb::Notification>& items) override;
    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const QProtobufMessage& item) override;
    QCoro::Task<bool> loadFromCache() override;
    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasNotifications();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasNotifications();
    }
    QList<nextapp::pb::Notification> getItems(const nextapp::pb::Status& status) override{
        return status.notifications().notifications();
    }
    std::string_view itemName() const noexcept override {
        return "notification";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;
    void clear() override;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;


    static NotificationsModel *instance() noexcept;

signals:
    void stateChanged();

private:
    std::deque<nextapp::pb::Notification> notifications_;
};
