#pragma once

#include <QAbstractListModel>
#include <QStringListModel>

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

// class SimpleListModel : public QStringListModel {
//     Q_OBJECT
//     QML_ELEMENT
// public:
//     SimpleListModel(QObject *parent = {})
//         : QStringListModel{parent}
//     {
//     }
// };

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
        SectionRole,
        SectionNameRole,
        DueRole,
        FavoriteRole,
    };

    enum Shortcuts {
        TODAY,
        TOMORROW,
        THIS_WEEKEND,
        NEXT_MONDAY,
        THIS_WEEK,
        AFTER_ONE_WEEK,
        NEXT_WEEK,
        THIS_MONTH,
        NEXT_MONTH,
        THIS_QUARTER,
        NEXT_QUARTER,
        THIS_YEAR,
        NEXT_YEAR,
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
    Q_INVOKABLE void markActionAsFavorite(const QString& actionUuid, bool favorite);
    Q_INVOKABLE QString toName(nextapp::pb::ActionKindGadget::ActionKind kind) const;
    Q_INVOKABLE QString formatWhen(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt);
    Q_INVOKABLE QString formatDue(const nextapp::pb::Due& due);
    Q_INVOKABLE QString whenListElement(uint64_t when,
                                        nextapp::pb::ActionDueKindGadget::ActionDueKind dt,
                                        nextapp::pb::ActionDueKindGadget::ActionDueKind btn);
    Q_INVOKABLE QStringListModel *getDueSelections(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt);
    Q_INVOKABLE nextapp::pb::Due adjustDue(time_t start, nextapp::pb::ActionDueKindGadget::ActionDueKind kind) const;
    Q_INVOKABLE nextapp::pb::Due changeDue(int shortcut, const nextapp::pb::Due& fromDue) const;

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
