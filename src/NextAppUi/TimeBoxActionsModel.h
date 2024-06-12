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

public:
    TimeBoxActionsModel(const QUuid TimeBoxUuid, CalendarDayModel *day, QQuickItem *parent = nullptr);

private:
    QUuid uuid_;
    QQuickItem* timeBox_{};
    CalendarDayModel* day_{};
    nextapp::pb::TimeBlock* tb_{};
    std::vector<std::unique_ptr<ActionInfoPrx>> aiPrx_{};

    nextapp::pb::TimeBlock *getTb();
    void sync();

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
};
