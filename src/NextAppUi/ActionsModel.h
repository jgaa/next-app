#pragma once

#include <QAbstractListModel>

#include "nextapp.qpb.h"

class ActionPrx : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(nextapp::pb::Action action READ getAction NOTIFY actionChanged)
    Q_PROPERTY(bool valid READ getValid NOTIFY validChanged)
public:
    ActionPrx(QString actionUuid);
    ActionPrx();

    nextapp::pb::Action getAction() const {
        return action_;
    }

    bool getValid() const noexcept {
        return valid_;
    }

    void receivedAction(const nextapp::pb::Status& status);

signals:
    void actionChanged();
    void validChanged();

private:
    bool valid_{true};
    QString uuid_;
    nextapp::pb::Action action_;
};

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
        NodeRole,
        CreatedDateRole,
        DueTypeRole,
        DueByTimeRole,
        CompletedRole,
        CompletedTimeRole,
        SectionRole
    };

public:
    ActionsModel(QObject *parent = {});

    Q_INVOKABLE void populate(QString node);
    Q_INVOKABLE void addAction(const nextapp::pb::Action& action);
    Q_INVOKABLE void updateAction(const nextapp::pb::Action& action);
    Q_INVOKABLE void deleteAction(const QString& uuid);
    Q_INVOKABLE nextapp::pb::Action newAction();
    Q_INVOKABLE ActionPrx *getAction(QString uuid);
    Q_INVOKABLE void markActionAsDone(const QString& actionUuid, bool done);

    void start();
    void fetch(nextapp::pb::GetActionsReq& filter);
    void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions);
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::shared_ptr<nextapp::pb::Actions> actions_;
};
