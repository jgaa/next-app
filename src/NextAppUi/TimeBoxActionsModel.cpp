#include "TimeBoxActionsModel.h"

#include "ActionInfoCache.h"
#include "CalendarDayModel.h"
#include "ServerComm.h"
#include "ActionsWorkedOnTodayCache.h"
#include "logging.h"
#include "util.h"

using namespace std;

ostream& operator << (ostream& os, const nextapp::pb::StringList& tb) {

    os << '{';
    for(auto& t : tb.list()) {
        os << t << ", ";
    }
    os << '}';
    return os;
}


TimeBoxActionsModel::TimeBoxActionsModel(const QUuid TimeBoxUuid, CalendarDayModel *day, QObject *parent)
    : QAbstractListModel(parent), uuid_(TimeBoxUuid), day_{day}
{
    assert(day_);

    resetState();

    connect(ActionInfoCache::instance(), &ActionInfoCache::actionChanged, this, [this](const QUuid &uuid) {
        if (state_) {
            if (state_->actions().list().contains(uuid.toString(QUuid::WithoutBraces))) {
                reSync();
            }
        }
    });

    connect(ActionInfoCache::instance(), &ActionInfoCache::actionDeleted, this, [this](const QUuid &uuid) {
        if (state_) {
            if (state_->actions().list().contains(uuid.toString(QUuid::WithoutBraces))) {
                reSync();
            }
        }
    });

    connect(day_, &CalendarDayModel::validChanged, [this] {
        if (day_->valid()) {
            resetState();
            reSync();
        }
    });

    connect(day_, &CalendarDayModel::eventChanged, [this](const QString &eventId) {
        if (state_->hasTb() && day_->valid() && state_->tb().id_proto() == eventId) {
            reSync();
        }
    });

    if (!state_->isSynching()) {
        reSync();
    }

    connect(ActionsWorkedOnTodayCache::instance(), &ActionsWorkedOnTodayCache::modelReset, this, [this] {
        if (state_ && state_->valid() && !state_->isSynching()) {
            QMetaObject::invokeMethod(this, [this]() {
                beginResetModel();
                endResetModel();
            }, Qt::QueuedConnection);
        }
    });

    connect(this, &TimeBoxActionsModel::modelAboutToBeReset, this, [this] {
        LOG_TRACE << toString() << "TimeBoxActionsModel: modelAboutToBeReset";
    });
    connect(this, &TimeBoxActionsModel::modelReset, this, [this] {
        LOG_TRACE << toString() << "TimeBoxActionsModel: modelReset";
    });
}

void TimeBoxActionsModel::removeAction(const QString &eventId, const QString &action)
{
    assert(day_);

    if (!state_ || !state_->valid()) {
        return;
    }

    // Find the index.
    const auto& actions = state_->actions().list();
    const auto index = actions.indexOf(action);
    if (index == -1) {
        return;
    }
    //Start remove notification
    beginRemoveRows({}, index, index);
    state_->removeAt(index);
    endRemoveRows();
    emit actionsChanged();

    return;
}

nextapp::pb::TimeBlock *TimeBoxActionsModel::getTb()
{
    return day_->lookupTimeBlock(uuid_);
}

nextapp::pb::TimeBlock *TimeBoxActionsModel::getTb() const
{
    return day_->lookupTimeBlock(uuid_);
}

void TimeBoxActionsModel::reSync()
{
    LOG_TRACE_N << toString() << ": Resynching ";
    if (state_ && state_->isSynching()) {
        LOG_WARN_N << toString() << ": Resychning while resynching!";
        resetState();
    }

    assert(state_);
    state_->invalidate();

    // sync() may return before the synch is actually done, so we will call begin/reste state again when it has finished.
    LOG_TRACE_N << toString() << ": Calling sync()";
    state_->sync();
}

void TimeBoxActionsModel::resetState()
{
    LOG_TRACE_N << toString() << ": Resetting state";
    beginResetModel();
    if (state_) {
        state_->cancel();
    }
    state_ = make_shared<State>(*this);
    endResetModel();
}

void TimeBoxActionsModel::onSynched()
{
    // The data has changed. Emit the signal.
    LOG_TRACE_N << toString() << ": data changed";
    beginResetModel();
    endResetModel();
    emit actionsChanged();
}

QString TimeBoxActionsModel::toString() const
{
    QString date;
    if (day_) {
        date = day_->date().toString(Qt::ISODate);
    }
    return QString("TimeBoxActionsModel{uuid=%1, date=%2}").arg(uuid_.toString()).arg(date);
}



int TimeBoxActionsModel::rowCount(const QModelIndex &parent) const
{
    int rows = 0;
    if (state_ && state_->valid()) {
        rows = state_->tb().actions().list().size();
    }

    LOG_TRACE_N << toString() << " rows: " << rows;
    return rows;
}

QVariant TimeBoxActionsModel::data(const QModelIndex &index, int role) const
{
    if (!state_ || !state_->valid() || !index.isValid()) {
        return {};
    }

    const auto& actions = state_->tb().actions().list();
    if (index.row() >= actions.size()) {
        return {};
    }

    const auto &action = actions[index.row()];
    const auto& ai = state_->ai();
    assert(actions.size() == ai.size());

    switch (role) {
    case NameRole:
        return ai[index.row()]->name();
    case UuidRole:
        return action;
    case ActionRole:
        LOG_TRACE_N << "Returning action pointer for " << ai.at(index.row())->name() << " for row " << index.row() << " and uuid " << action;
        return QVariant::fromValue(*ai.at(index.row()));
    case CategoryRole:
        return ai.at(index.row())->category();
    case DoneRole:
        return ai.at(index.row())->status() == ::nextapp::pb::ActionStatusGadget::ActionStatus::DONE;
    case WorkedOnTodayRole:
        return ActionsWorkedOnTodayCache::instance()->contains(QUuid{action});
    default:
        return {};
    }
}

QHash<int, QByteArray> TimeBoxActionsModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {UuidRole, "uuid"},
        {ActionRole, "action"},
        {CategoryRole, "category"},
        {DoneRole, "done"},
        {WorkedOnTodayRole, "workedOnToday"}
    };
}

nextapp::pb::StringList TimeBoxActionsModel::actions() const
{
    if (state_ && state_->valid()) {
        return state_->actions();
    }

    return {};
}

TimeBoxActionsModel::State::State(TimeBoxActionsModel &parent)
    : parent_(parent) {
}

QCoro::Task<bool> TimeBoxActionsModel::State::sync()
{
    // Don't allow owner to destroy us while we are synching
    auto self = shared_from_this();

    if (is_synching_ || done_) {
        co_return false;
    }

    LOG_TRACE << "TimeBoxActionsModel: Starting async sync data...";

    is_synching_ = true;
    ScopedExit later {[&] {
        is_synching_ = false;
        parent_.onSynched();
        LOG_TRACE << "TimeBoxActionsModel: Syncing is finished";
    }};

    valid_ = false;

    tb_.reset();
    if (const auto * tb = parent_.getTb()) {
        tb_.emplace(*tb);
    }
    if (!tb_) {
        LOG_TRACE_N << "No tb_";
        parent_.onSynched();
        co_return false;
    }

    assert(tb_.has_value());
    const auto& tb_id = tb_->id_proto();

    ai_.clear();
    const auto& tb_list = tb_->actions().list();
    for (const auto action : tb_list) {
        auto ai = co_await ActionInfoCache::instance()->get(action, true);
        // We may have been cancelled while waiting for the action.
        if (done_) {
            co_return false;
        }

        assert(tb_);
        if (ai && ai->status() != ::nextapp::pb::ActionStatusGadget::ActionStatus::DELETED) {
            LOG_TRACE_N << "Adding action " << ai->name() << " to the model. uuid=" << tb_id
                        << ", status="
                        << static_cast<int>(ai->status());
            ai_.emplace_back(ai);
        } else {
            LOG_TRACE_N << "Action " << action << " not found or deleted. tb=" << tb_id;
            auto empty = make_shared<nextapp::pb::ActionInfo>();
            auto name = action + " [" + tr("Deleted") + "]";
            empty->setName(name);
            empty->setId_proto(action);
            empty->setStatus(nextapp::pb::ActionStatusGadget::ActionStatus::DELETED);
            ai_.emplace_back(empty);
        }
    }

    assert(!done_);
    assert(tb_);
    actions_ = tb_->actions();
    assert(tb_->actions().list().size() == ai_.size());
    valid_ = true;
    co_return true;
}

void TimeBoxActionsModel::State::removeAt(uint index)
{
    assert(index < ai_.size());
    ai_.erase(ai_.begin() + index);
    actions_ = nextapp::remove(actions_, index);
}
