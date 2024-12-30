#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include "qcorotask.h"

#include "nextapp.qpb.h"

class DevicesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid MEMBER valid_ NOTIFY validChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        UserRole,
        NameRole,
        CreatedRole,
        HostNameRole,
        OsRole,
        OsVersionRole,
        AppVersionRole,
        ProductTypeRole,
        ProductVersionRole,
        ArchRole,
        PrettyNameRole,
        LastSeenRole,
        EnabledRole,
        NumSessionsRole
    };

    DevicesModel();

    Q_INVOKABLE void enableDevice(QString deviceId, bool active);
    Q_INVOKABLE void deleteDevice(QString deviceId);
    Q_INVOKABLE void refresh();

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static DevicesModel *instance();

signals:
    void validChanged();

private:
    void setValid(bool valid);
    QCoro::Task<void> fetchIf();
    QCoro::Task<void> doEnableDevice(QString deviceId, bool active);
    QCoro::Task<void> doDeleteDevice(QString deviceId);

    bool valid_{false};
    std::vector<nextapp::pb::Device> devices_;
};
