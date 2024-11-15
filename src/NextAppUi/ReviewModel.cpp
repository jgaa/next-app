#include "ReviewModel.h"
#include "format_wrapper.h"

#include "util.h"
#include "logging.h"

using namespace std;

ReviewModel::ReviewModel(QObject *parent)
    : QObject{parent}
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

bool ReviewModel::first()
{
    if (cache_.setCurrent(0)) {
        setCurrentUuid(cache_.currentId());
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
    auto [res, err] = co_await db::instance().query(sql);
    if (err) {
        LOG_ERROR_N << "Error fetching: " << err;
        setState(State::ERROR);
        co_return;
    }

    LOG_DEBUG_N << "Fetched " << res.size() << " rows";

    for(auto& row : res) {
        auto action_id = QUuid(row[0].toString());
        auto node_id = QUuid(row[1].toString());
        cache_.add(action_id, node_id);
    };

    setState(State::READY);
}

void ReviewModel::Cache::add(const QUuid& id) {
    auto [it, added] = by_quuid_.emplace(id, id);
    assert(added);
    items_.push_back(&it->second);
}

bool ReviewModel::Cache::setCurrent(uint ix)
{
    if (ix >= items_.size()) {
        return false;
    };

    current_ix_ = ix;
    return true;
}
