
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

        if (active) {
            if (state_ == State::PENDING || state_ == State::ERROR) {
                fetchIf();
            };
        }
    }
}

bool ReviewModel::next()
{
    if (state_ == State::PENDING) {
        return false;
    }

    if (state_ == State::READY) {
        if (auto start = findNext(true); start != -1) {
            return moveToIx(start);
        };
    };

    return false;
}

bool ReviewModel::previous()
{
    if (state_ == State::PENDING) {
        return false;
    }

    if (state_ == State::READY) {
        if (auto start = findNext(false); start != -1) {
            return moveToIx(start);
        };
    };

    return false;
}

bool ReviewModel::first()
{
    if (auto start = findNext(true, 0); start != -1) {
        return moveToIx(start);
    };

    return false;
}

bool ReviewModel::back()
{
    // TODO: Implement back navigation
    return false;
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

void ReviewModel::setSelected(int ix)
{
    if (selected_ != ix) {
        selected_ = ix;
        emit selectedChanged();
    }
}

void ReviewModel::setNodeUuid(const QString &uuid)
{
    if (node_uuid_ != uuid) {
        node_uuid_ = uuid;
        emit nodeUuidChanged();
    }
}

nextapp::pb::Action ReviewModel::action()
{
    if (action_) {
        return *action_;
    }

    return {};
}

int ReviewModel::findNext(bool forward, int from)
{
    const int step = forward ? 1 : -1;
    const int start = from == -1  ? cache_.currentIx() + step : from;
    int tries = 0;

    for(int i = start; !tries || i != start; i += step, ++tries) {
        if (i == -1) {
            i = cache_.size() - 1;
        } else if (i == cache_.size()) {
            i = 0;
        };

        if (!cache_.at(i).done()) {
            return i;
        }
    }

    return -1;
}

bool ReviewModel::moveToIx(uint ix)
{
    // beginResetModel();
    // ScopedExit guard([this] { endResetModel(); });

    LOG_TRACE_N << "Moving to ix " << ix;

    if (cache_.setCurrent(ix)) {
        if (cache_.nodeChanged()) {
            changeNode();
        }
        setActionUuid(cache_.currentId());
        return true;
    }
    setActionUuid({});
    return false;
}

void ReviewModel::setActionUuid(const QUuid &uuid)
{
    const auto uuid_str = uuid.isNull() ? QString{} : uuid.toString(QUuid::WithoutBraces);
    if (action_uuid_ != uuid_str) {
        action_uuid_ = uuid_str;
        emit actionUuidChanged();
        fetchAction();
    }

    const auto& window = cache_.currentWindow();
    auto pos = std::ranges::find_if(window, [uuid](const auto& item) {
        return item->id_ == uuid;
    });

    if (pos != window.end()) {
        setSelected(pos - window.begin());
    };
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
    // beginResetModel();
    // endResetModel();
    co_await aic->fill(cache_.currentWindow());
    if (state_ == State::FILLING) {
        setState(State::READY);
    }
    beginResetModel();
    endResetModel();
    QString cn;
    if (!cache_.currentWindow().empty()) {
        cn = cache_.currentWindow().front()->node_id_.toString(QUuid::WithoutBraces);
    }
    setNodeUuid(cn);
}

QCoro::Task<void> ReviewModel::fetchIf()
{
    if (!active_) {
        LOG_DEBUG_N << "Not active";
        co_return;
    }
    if (state_ == State::FETCHING) {
        LOG_DEBUG_N << "Already fetching";
        co_return;
    }

    // Query suggested by ChatGPT 4o
    const auto sql = NA_FORMAT(R"(WITH RECURSIVE
    sorted_nodes(uuid, parent, name, path) AS (
        -- Base case: Start from root nodes (nodes without parents)
        SELECT
            uuid,
            parent,
            name,
            name AS path
        FROM node
        WHERE parent IS NULL
        UNION ALL
        -- Recursive case: Traverse child nodes
        SELECT
            n.uuid,
            n.parent,
            n.name,
            sn.path || ' > ' || n.name
        FROM node n
        INNER JOIN sorted_nodes sn ON n.parent = sn.uuid
    )
SELECT
    action.id AS action_id,
    node.uuid AS node_uuid
FROM action
INNER JOIN sorted_nodes node ON action.node = node.uuid
WHERE action.status != 1
ORDER BY
    node.path,          -- Sort by node hierarchy (path determines order)
    action.due_by_time, -- Then by due time
    action.priority,    -- Then by action priority
    action.name         -- Finally, alphabetically by action name
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
    first();
}

QCoro::Task<void> ReviewModel::fetchAction()
{
    const auto uuid = QUuid(action_uuid_);
    if (uuid.isNull()) {
        LOG_DEBUG_N << "No action uuid";
        co_return;
    }

    auto *aic = ActionInfoCache::instance();
    assert(aic);
    auto action = co_await aic->getAction(uuid);

    // Check that we still have the same active action
    if (uuid == QUuid(action_uuid_)) {
        action_ = action;
        emit actionChanged();
    };
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
        const auto& node_id = items_[ix].node_id_;

        auto first = ix;
        while(first > 0 && items_[first - 1].node_id_ == node_id) {
            --first;
        };

        auto end = ix;
        while(end < items_.size() && items_[end].node_id_ == node_id) {
            ++end;
        };

        current_window_.clear();
        current_window_.reserve(end - first);
        for(auto i = first; i < end; ++i) {
            current_window_.push_back(&items_[i]);
        };

        node_changed_ = true;
    };

    return true;
}
