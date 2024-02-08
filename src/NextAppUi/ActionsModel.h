#pragma once

#include <QAbstractListModel>

#include "nextapp.qpb.h"

class ActionsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    enum Roles {
        NameRole = Qt::UserRole + 1,
        UuidRole,
        PriorityRole,
        StatusRole,
        DescrRole,
        NodeRole,
        CreatedDateRole,
        DueTypeRole,
        DueByTimeRole,
        CompletedRole
    };

public:
    ActionsModel(QObject *parent = {});

    Q_INVOKABLE void populate(QString node);
    Q_INVOKABLE void addAction(const nextapp::pb::Action& action);
    Q_INVOKABLE void updateAction(const nextapp::pb::Action& action);
    Q_INVOKABLE nextapp::pb::Action newAction();

    void fetch(nextapp::pb::GetActionsReq& filter);
    void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions);

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::shared_ptr<nextapp::pb::Actions> actions_;
};
