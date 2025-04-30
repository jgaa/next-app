#include "ActionStatsModel.h"
#include "DbStore.h"
#include "NextAppCore.h"


ActionStatsModel::ActionStatsModel(QUuid actionId)
: action_id_{actionId}
{
    LOG_TRACE << "ActionStatsModel created";
}

ActionStatsModel::~ActionStatsModel()
{
    LOG_TRACE << "ActionStatsModel destroyed";
}

QCoro::Task<void> ActionStatsModel::fetch()
{
    LOG_TRACE_N << "Fetching action stats for: " << action_id_.toString();
    auto self = shared_from_this(); // Keep the shared pointer alive during the async operations

    actionInfo_ = co_await ActionInfoCache::instance()->get(action_id_, true);
    if (!actionInfo_) {
        setValid(false);
        LOG_WARN << "Failed to fetch action: " << action_id_.toString();
        co_return;
    }

    // Get all sessions for the action, sorted on date, grouped by date (using local timezone)
    auto& db = NextAppCore::instance()->db();

    static const QString sql{R"(SELECT
    DATE(start_time, 'localtime') AS session_date,
    MIN(start_time) AS first_start_time,
    MAX(end_time) AS last_end_time,
    SUM(duration) AS total_duration,
    COUNT(*) AS session_count
FROM work_session
WHERE action = ?
  AND start_time IS NOT NULL
  AND state <> ?
GROUP BY DATE(start_time, 'localtime')
ORDER BY session_date;
    )"};

    static const QString sql_recurse{R"(WITH RECURSIVE action_chain(id) AS (
    SELECT id
    FROM action
    WHERE id = ?  -- start from the given action ID

    UNION ALL

    SELECT a.origin
    FROM action a
    JOIN action_chain ac ON a.id = ac.id
    WHERE a.origin IS NOT NULL
)

SELECT
    DATE(ws.start_time, 'localtime') AS session_date,
    MIN(ws.start_time) AS first_start_time,
    MAX(ws.end_time) AS last_end_time,
    SUM(ws.duration) AS total_duration,
    COUNT(*) AS session_count
FROM work_session ws
JOIN action_chain ac ON ws.action = ac.id
WHERE ws.start_time IS NOT NULL
  AND ws.state <> ?
GROUP BY DATE(ws.start_time, 'localtime')
ORDER BY session_date;
)"};

    enum Cols {
        SESSION_DATE,
        FIRST_START_TIME,
        LAST_END_TIME,
        TOTAL_DURATION,
        SESSION_COUNT
    };
    auto res = co_await db.query(with_origin_ ? sql_recurse : sql, action_id_.toString(QUuid::WithoutBraces), static_cast<int>(nextapp::pb::WorkSession::State::DELETED));
    if (!res) {
        setValid(false);
        LOG_WARN << "Failed to fetch action stats: " << res.error();
        co_return;
    }
    auto& rows = res.value().rows;
    totalMinutes_ = 0;
    totalSessions_ = 0;
    workInDays_.clear();
    workInDays_.reserve(rows.size());
    for(const auto& row : rows) {
        WorkInDay day;
        day.date = row.at(SESSION_DATE).toDateTime();;
        day.minutes = row.at(TOTAL_DURATION).toInt() / 60;
        day.sessions = row.at(SESSION_COUNT).toInt() / 60;

        totalMinutes_ += day.minutes;
        totalSessions_ += day.sessions;

        workInDays_.emplace_back(std::move(day));
    }

    LOG_TRACE_N << workInDays_.size() << " work-days stats fetched for action: " << action_id_.toString();
    setValid(true);
}

QVariantList ActionStatsModel::getWorkInDays() const
{
    QVariantList outer;
    for(const auto& day : workInDays_) {
        QVariantMap inner;
        inner["date"] = day.date; //static_cast<double>(day.date.startOfDay().toMSecsSinceEpoch()); // day.date.toString(Qt::ISODate);
        inner["minutes"] = day.minutes;
        inner["sessions"] = day.sessions;
        outer.append(inner);
    }
    return outer;
}

QString ActionStatsModel::getFirstSessionDate() const
{
    if (workInDays_.empty()) {
        return QString();
    }
    //return workInDays_.front().date.toString("");
    return QLocale().toString(workInDays_.front().date.date(), QLocale::ShortFormat);
}

QString ActionStatsModel::getLastSessionDate() const
{
    if (workInDays_.empty()) {
        return QString();
    }
    //return workInDays_.back().date.toString(Qt::ISODate);
    return QLocale().toString(workInDays_.back().date.date(), QLocale::ShortFormat);
}

const nextapp::pb::ActionInfo &ActionStatsModel::actionInfo() const {
    if (actionInfo_) {
        return *actionInfo_;
    }

    static const nextapp::pb::ActionInfo emptyActionInfo;
    return emptyActionInfo;
}

void ActionStatsModel::setValid(bool valid)
{
    if (valid_ != valid) {
        valid_ = valid;
        LOG_TRACE_N << "ActionStatsModel valid: " << valid;
        emit validChanged();
    }
}

ActionStatsModelPtr::ActionStatsModelPtr(std::shared_ptr<ActionStatsModel> model)
    : model_{std::move(model)}
{
    if (model_) {
        connect(model_.get(), &ActionStatsModel::validChanged, this, &ActionStatsModelPtr::modelChanged);
        connect(model_.get(), &ActionStatsModel::validChanged, this, &ActionStatsModelPtr::validChanged);
    }
}

ActionStatsModelPtr::~ActionStatsModelPtr()
{
    LOG_TRACE << "ActionStatsModelPtr destroyed";
}
