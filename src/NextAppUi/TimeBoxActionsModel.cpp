#include "TimeBoxActionsModel.h"

#include "ActionInfoCache.h"
#include "CalendarDayModel.h"
#include "logging.h"
#include "util.h"


TimeBoxActionsModel::TimeBoxActionsModel(const QUuid TimeBoxUuid, CalendarDayModel *day, QQuickItem *parent)
    : QAbstractListModel(parent), uuid_(TimeBoxUuid), day_{day}
{
    assert(day_);
    tb_ = getTb();

    connect(ActionInfoCache::instance(), &ActionInfoCache::actionChanged, this, [this](const QUuid &uuid) {
        if (tb_) {
            if (tb_->actions().list().contains(uuid.toString(QUuid::WithoutBraces))) {
                sync();
            }
        }
    });

    connect(ActionInfoCache::instance(), &ActionInfoCache::actionDeleted, this, [this](const QUuid &uuid) {
        if (tb_) {
            if (tb_->actions().list().contains(uuid.toString(QUuid::WithoutBraces))) {
                sync();
            }
        }
    });

    connect(day_, &CalendarDayModel::validChanged, [this] {
        if (day_->valid()) {
            if ((tb_ = getTb())) {
                sync();
            } else {
                beginResetModel();
                endResetModel();
            }
        }
    });

    connect(day_, &CalendarDayModel::eventChanged, [this](const QString &eventId) {
        if (tb_ && day_->valid() && tb_->id_proto() == eventId) {
            sync();
        }
    });

    if (tb_) {
        sync();
    }
}

nextapp::pb::TimeBlock *TimeBoxActionsModel::getTb()
{
    return day_->lookupTimeBlock(uuid_);
}

void TimeBoxActionsModel::sync()
{
    beginResetModel();

    const ScopedExit later { [this] {
        endResetModel();
    }};

    aiPrx_.clear();
    for (const auto &action : tb_->actions().list()) {
        auto *ai = ActionInfoCache::instance()->getAction(action);
        assert(ai);
        QQmlEngine::setObjectOwnership(ai, QQmlEngine::CppOwnership);
        aiPrx_.emplace_back(ai);
    }
}

int TimeBoxActionsModel::rowCount(const QModelIndex &parent) const
{
    if (tb_) {
        return tb_->actions().list().size();
    }

    return 0;
}

QVariant TimeBoxActionsModel::data(const QModelIndex &index, int role) const
{
    if (!tb_ || !index.isValid() ) {
        return {};
    }

    const auto actions = tb_->actions().list();
    if (index.row() >= actions.size()) {
        return {};
    }

    const auto &action = actions[index.row()];
    assert(actions.size() == aiPrx_.size());

    switch (role) {
    case NameRole:
        if (const auto* a = aiPrx_[index.row()]->getAction()) {
            LOG_TRACE_N << "Returning " << a->name() << " for row " << index.row() << " and uuid " << action;
            return a->name();
        }
    case UuidRole:
        return action;
    case ActionRole:
        if (auto *a = aiPrx_[index.row()]->getAction()) {
            LOG_TRACE_N << "Returning action pointer for " << a->name() << " for row " << index.row() << " and uuid " << action;
            return QVariant::fromValue(a);
        }
    case CategoryRole:
        if (auto *a = aiPrx_[index.row()]->getAction()) {
            return a->category();
        }
    case DoneRole:
        if (auto *a = aiPrx_[index.row()]->getAction()) {
            return a->status() == ::nextapp::pb::ActionStatusGadget::ActionStatus::DONE;
        }
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
    };
}
