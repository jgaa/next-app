#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUuid>
#include <QAbstractListModel>
#include <QQuickItem>
#include "ActionInfoCache.h"

#include "nextapp.qpb.h"

class CalendarDayModel;

class TimeBoxActionsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    enum RoleNames {
        NameRole = Qt::UserRole + 1,
        UuidRole,
        ActionRole,
        CategoryRole,
        DoneRole
    };

    Q_PROPERTY(nextapp::pb::StringList actions MEMBER actions_ NOTIFY actionsChanged)

public:
    TimeBoxActionsModel(const QUuid TimeBoxUuid, CalendarDayModel *day, QObject *parent = nullptr);

    Q_INVOKABLE void removeAction(const QString& eventId, const QString& action);

signals:
    void actionsChanged();

private:
    QUuid uuid_;
    QQuickItem* timeBox_{};
    CalendarDayModel* day_{};
    nextapp::pb::TimeBlock* tb_{};
    nextapp::pb::StringList actions_;
    std::vector<std::unique_ptr<ActionInfoPrx>> aiPrx_{};

    nextapp::pb::TimeBlock *getTb();
    void sync();

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};
