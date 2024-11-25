
#include <algorithm>
#include <ranges>

#include "ReviewModel.h"
#include "format_wrapper.h"
#include "DbStore.h"
#include "NextAppCore.h"
#include "ActionInfoCache.h"
#include "MainTreeModel.h"
#include "ActionsModel.h"

#include "util.h"
#include "logging.h"

using namespace std;


ReviewModel::ReviewModel(QObject *parent)
    : QAbstractListModel{parent}
{}

ReviewModel &ReviewModel::instance()
{
    static ReviewModel instance;
    return instance;
}

void ReviewModel::setActive(bool active)
{
    if (active_ != active) {
        active_ = active;
        emit activeChanged();
    }
}

bool ReviewModel::next()
{

}

bool ReviewModel::previous()
{

}

bool ReviewModel::first()
{
   return moveToIx(0);
}

bool ReviewModel::back()
{

}

int ReviewModel::rowCount(const QModelIndex &parent) const
{
    if (state_ == State::READY) {
        return cache_.currentWindow().size();
    }
    return 0;
}

QVariant ReviewModel::data(const QModelIndex &index, int role) const
{
    if (state_ != State::READY || !index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= cache_.currentWindow().size()) {
        return {};
    }

    const auto& item = *cache_.currentWindow()[row];
    if (!item.action) {
        LOG_WARN_N << "No action for item #" << row << " " << item.id_.toString();
        return {};
    }

    const auto& action = *item.action;

    switch(role) {
    case NameRole:
        return action.name();
    case UuidRole:
        return action.id_proto();
    case PriorityRole:
        return static_cast<int>(action.priority());
    case StatusRole:
        return static_cast<uint>(action.status());
    case NodeRole:
        return action.node();
    case CreatedDateRole:
        return QDate{action.createdDate().year(), action.createdDate().month(), action.createdDate().mday()}.toString();
    case DueTypeRole:
        return static_cast<uint>(action.due().kind());
    case DueByTimeRole:
        return static_cast<quint64>(action.due().due());
    case CompletedRole:
        return action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DONE;
    case CompletedTimeRole:
        if (action.completedTime()) {
            return QDateTime::fromSecsSinceEpoch(action.completedTime());
        }
        return {};
    case SectionRole:
        return static_cast<uint>(ActionsModel::toKind(action));
    case SectionNameRole:
        return ActionsModel::toName(ActionsModel::toKind(action));
    case DueRole:
        // Only return if it's
        return ActionsModel::formatDue(action.due());
    case FavoriteRole:
        return action.favorite();
    case HasWorkSessionRole:
        //return worked_on_.contains(toQuid(action.id_proto()));
        return false;
    case ListNameRole:
        if (MainTreeModel::instance()->selected() == action.node()) {
            return {};
        }
        return MainTreeModel::instance()->nodeNameFromUuid(action.node(), true);
    case CategoryRole:
        return action.category();
    }
    return {};
}

QHash<int, QByteArray> ReviewModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[UuidRole] = "uuid";
    roles[PriorityRole] = "priority";
    roles[StatusRole] = "status";
    roles[NodeRole] = "node";
    roles[CreatedDateRole] = "createdDate";
    roles[DueTypeRole] = "dueType";
    roles[DueByTimeRole] = "dueBy";
    roles[CompletedRole] = "done";
    roles[CompletedTimeRole] = "completedTime";
    roles[SectionRole] = "section";
    roles[SectionNameRole] = "sname";
    roles[DueRole] = "due";
    roles[FavoriteRole] = "favorite";
    roles[HasWorkSessionRole] = "hasWorkSession";
    roles[ListNameRole] = "listName";
    roles[CategoryRole] = "category";
    return roles;
}

bool ReviewModel::moveToIx(uint ix)
{
    if (cache_.setCurrent(ix)) {
        setCurrentUuid(cache_.currentId());
        if (cache_.nodeChanged()) {
            changeNode();
        }
        return true;
    }
    setCurrentUuid({});
    return false;
}

void ReviewModel::setCurrentUuid(const QUuid &uuid)
{
    if (current_uuid_ != uuid) {
        current_uuid_ = uuid;
        emit currentUuidChanged();
    }
}

void ReviewModel::setState(State state)
{
    if (state_ != state) {
        state_ = state;
        emit stateChanged();
    }
}

QCoro::Task<void> ReviewModel::changeNode()
{
    if (state_ != State::READY) {
        LOG_WARN_N << "Not ready";
        co_return;
    }

    auto *aic = ActionInfoCache::instance();
    assert(aic);
    setState(State::FILLING);
    beginResetModel();
    endResetModel();
    co_await aic->fill(cache_.currentWindow());
    if (state_ == State::FILLING) {
        setState(State::READY);
    }
    beginResetModel();
    endResetModel();
}

QCoro::Task<void> ReviewModel::fetchIf()
{
    if (state_ == State::FETCHING) {
        LOG_DEBUG_N << "Already fetching";
        co_return;
    }

    const auto sql = NA_FORMAT(R"(WITH RECURSIVE
-- Recursive CTE to build the node hierarchy sorted by name
sorted_nodes AS (
    SELECT
        uuid,
        parent,
        name,
        0 AS depth
    FROM
        node
    WHERE
        parent IS NULL -- Root nodes
    UNION ALL
    SELECT
        n.uuid,
        n.parent,
        n.name,
        sn.depth + 1 AS depth
    FROM
        node n
    INNER JOIN
        sorted_nodes sn
    ON
        n.parent = sn.uuid
    ORDER BY
        sn.depth,
        n.name -- Sort nodes by name at each hierarchy level
)
-- Final query to combine actions with sorted nodes
SELECT
    action.id AS action_id,
    action.node AS action_node
FROM
    action
INNER JOIN
    sorted_nodes
ON
    action.node = sorted_nodes.uuid
ORDER BY
    sorted_nodes.depth, -- Maintain hierarchy order
    sorted_nodes.name, -- Ensure node name sorting within each level
    action.priority,
    action.name;
)");

    setState(State::FETCHING);
    auto& db = NextAppCore::instance()->db();
    auto rval = co_await db.query(QString::fromLatin1(sql));
    if (!rval) {
        LOG_ERROR_N << "Error fetching: " << rval.error();
        setState(State::ERROR);
        co_return;
    }

    const auto& rows = rval.value();
    LOG_DEBUG_N << "Fetched " << rows.size() << " rows";
    cache_.clear();
    cache_.reserve(rows.size());

    for(auto& row : rows) {
        auto action_id = QUuid(row[0].toString());
        auto node_id = QUuid(row[1].toString());
        cache_.add(action_id, node_id);
    };

    setState(State::READY);
}

void ReviewModel::Cache::add(const QUuid& actionId, const QUuid& nodeId) {
    by_quuid_.emplace(actionId, items_.size());
    auto &item = items_.emplace_back(actionId, nodeId);
}

bool ReviewModel::Cache::setCurrent(uint ix)
{
    node_changed_ = false;

    if (ix >= items_.size()) {
        return false;
    };

    QUuid prev_node;
    if (!current_window_.empty()) {
        assert(current_ix_ < items_.size());
        prev_node = items_[current_ix_].node_id_;
    }

    current_ix_ = ix;
    if (items_[ix].node_id_ != prev_node) {
        auto window = ranges::equal_range(items_, items_[ix].node_id_, {}, &Item::node_id_)
                      | ranges::views::transform([] (Item &value){ return &value; });
        current_window_.assign(window.begin(), window.end());
        node_changed_ = true;
    };
    return true;
}
