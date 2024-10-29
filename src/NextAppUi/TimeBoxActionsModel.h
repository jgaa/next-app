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

    Q_PROPERTY(nextapp::pb::StringList actions READ actions() NOTIFY actionsChanged)

public:
    TimeBoxActionsModel(const QUuid TimeBoxUuid, CalendarDayModel *day, QObject *parent = nullptr);

    Q_INVOKABLE void removeAction(const QString& eventId, const QString& action);

signals:
    void actionsChanged();

private:

    // QT is not very good at handlinng models that update themselves asyncronously.
    // Therefore, we need to keep the state in a separate object and only update the model
    // when a sync is complete.
    struct State : public std::enable_shared_from_this<State> {
        State(TimeBoxActionsModel& parent);;
        QCoro::Task<bool> sync();

        void cancel() {
            done_ = true;
            tb_ = {};
            ai_.clear();
        }

        bool done() const noexcept {
            return done_;
        }

        bool isSynching() const noexcept {
            return is_synching_;
        }

        bool hasTb() const noexcept {
            return tb_;
        }

        nextapp::pb::TimeBlock* tb() const noexcept {
            assert(!done_);
            assert(tb_);
            return tb_;
        }

        auto& ai() const noexcept {
            return ai_;
        }

        const auto& actions() const noexcept {
            return actions_;
        }

        bool valid() const noexcept {
            return !done_ && valid_;
        }

        void invalidate() noexcept {
            valid_ = false;
        }

        void removeAt(uint index);

    private:
        TimeBoxActionsModel& parent_;
        nextapp::pb::TimeBlock *tb_{};
        nextapp::pb::StringList actions_;
        std::vector<std::shared_ptr<nextapp::pb::ActionInfo>> ai_{};
        bool is_synching_{};
        bool done_{};
        bool valid_{};
        //QCoro::Task<bool> sync();
    };

    nextapp::pb::TimeBlock *getTb();
    void reSync();
    void resetState();
    void onSynched();
    void notifyBeginResetModel();
    void notifyEndResetModel();

    QUuid uuid_;
    QQuickItem* timeBox_{};
    CalendarDayModel* day_{};
    std::shared_ptr<State> state_;
    bool pending_reset_model_{};

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    nextapp::pb::StringList actions() const;
};
