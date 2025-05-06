#include <array>
#include <optional>
#include <ctime>
#include <chrono>
#include <algorithm>

#include <QCryptographicHash>
#include <QProtobufSerializer>
#include <QSqlQuery>
#include <QSqlError>

#include "ActionInfoCache.h"
#include "MainTreeModel.h"
#include "ServerComm.h"
#include "util.h"
#include "logging.h"

using namespace std;
using namespace nextapp;

namespace {

QByteArray computeTagsHash(QStringView tags) {
    if (tags.isEmpty()) {
        return {};
    }
    auto bytes = tags.toUtf8();
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QString tagsToString(const QList<QString>& tags) {
    QString rval;
    for (const auto& tag : tags) {
        if (!tags.isEmpty()) {
            rval += ' ';
        }
        rval += tag;
    }
    return rval;
}

QByteArray computeTagsHash(const QList<QString>& tags) {
    if (tags.isEmpty()) {
        return {};
    }
    const auto str = tagsToString(tags);
    return computeTagsHash(str);
}

static const QString insert_query = R"(INSERT INTO action
        (id, node, origin, priority, dyn_importance, dyn_urgency, dyn_score, status, favorite, name, descr, created_date,
        due_kind, start_time, due_by_time, due_timezone, completed_time,
        time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after,
        kind, category, time_spent, version, updated, score, tags, tags_hash) VALUES
        (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
        node = EXCLUDED.node,
        origin = EXCLUDED.origin,
        priority = EXCLUDED.priority,
        dyn_importance = EXCLUDED.dyn_importance,
        dyn_urgency = EXCLUDED.dyn_urgency,
        dyn_score = EXCLUDED.dyn_score,
        status = EXCLUDED.status,
        favorite = EXCLUDED.favorite,
        name = EXCLUDED.name,
        descr = EXCLUDED.descr,
        created_date = EXCLUDED.created_date,
        due_kind = EXCLUDED.due_kind,
        start_time = EXCLUDED.start_time,
        due_by_time = EXCLUDED.due_by_time,
        due_timezone = EXCLUDED.due_timezone,
        completed_time = EXCLUDED.completed_time,
        time_estimate = EXCLUDED.time_estimate,
        difficulty = EXCLUDED.difficulty,
        repeat_kind = EXCLUDED.repeat_kind,
        repeat_unit = EXCLUDED.repeat_unit,
        repeat_when = EXCLUDED.repeat_when,
        repeat_after = EXCLUDED.repeat_after,
        kind = EXCLUDED.kind,
        category = EXCLUDED.category,
        time_spent = EXCLUDED.time_spent,
        version = EXCLUDED.version,
        updated = EXCLUDED.updated,
        score = EXCLUDED.score,
        tags = EXCLUDED.tags,
        tags_hash = EXCLUDED.tags_hash
        )";


QList<QVariant> getParams(const pb::Action& action) {
    QList<QVariant> params;

    params.append(action.id_proto());
    params.append(action.node());
    if (action.hasOrigin()) {
        params.append(action.origin());
    } else {
        params.append(QString{});
    }

    if (action.dynamicPriority().hasPriority()) {
        params << static_cast<quint32>(action.dynamicPriority().priority());
    } else {
        params << QVariant{};
    }

    if (action.dynamicPriority().hasUrgencyImportance()) {
        params << static_cast<quint32>(action.dynamicPriority().urgencyImportance().importance());
        params << static_cast<quint32>(action.dynamicPriority().urgencyImportance().urgency());
    } else {
        params << QVariant{};
        params << QVariant{};
    }

    if (action.dynamicPriority().hasScore()) {
        params << static_cast<qlonglong>(action.dynamicPriority().score());
    } else {
        params << QVariant{};
    }

    params.append(static_cast<quint32>(action.status()));
    params.append(action.favorite());
    params.append(action.name());
    params.append(action.descr());
    params.append(toQDate(action.createdDate()));
    if (action.hasDue()) {
        params.append(static_cast<quint32>(action.due().kind()));
        if (action.due().hasStart()) {
            params.append(QDateTime::fromSecsSinceEpoch(action.due().start()));
        } else {
            params.append(QVariant{});
        }
        if (action.due().hasDue()) {
            auto end = QDateTime::fromSecsSinceEpoch(action.due().due());
            // Handle the case where the due time is 00:00:00 at the next day.
            // Set it to the last second the day before so queries will work as expected.
            if (action.due().kind() != nextapp::pb::ActionDueKindGadget::ActionDueKind::DATETIME) {
                const auto time = end.time();
                if (time.hour() == 0 && time.minute() == 0 && time.second() == 0) {
                    end = end.addSecs(-1);
                };
            };
            params.append(end);
        } else {
            params.append(QVariant{});
        }
        params.append(action.due().timezone());
        if (auto seconds = action.completedTime()) {
            params.append(QDateTime::fromSecsSinceEpoch(seconds));
        } else {
            params.append(QVariant{});
        }
    } else {
        params.append(static_cast<quint32>(nextapp::pb::ActionDueKindGadget::ActionDueKind::UNSET));
        params.append(QVariant{});
        params.append(QVariant{});
        params.append(QVariant{});
    }
    params.append(static_cast<qlonglong>(action.timeEstimate()));
    params.append(static_cast<quint32>(action.difficulty()));
    params.append(static_cast<quint32>(action.repeatKind()));
    params.append(static_cast<quint32>(action.repeatUnits()));
    params.append(static_cast<quint32>(action.repeatWhen()));
    params.append(static_cast<quint32>(action.repeatAfter()));
    params.append(static_cast<quint32>(action.kind()));
    params.append(action.category());
    params << static_cast<quint32>(action.timeSpent());
    params.append(static_cast<quint32>(action.version()));
    params.append(static_cast<qlonglong>(action.updated()));
    params << ActionInfoCache::getScore(action);

    if (!action.tags().empty()) {
        const QString tags = tagsToString(action.tags());
        params << tags;
        params << computeTagsHash(tags);
    } else {
        params << QVariant{};
        params << QVariant{};
    }

    return params;
}

pb::ActionInfo toActionInfo(const pb::Action& action) {
    pb::ActionInfo ai;
    ai.setId_proto(action.id_proto());
    ai.setNode(action.node());
    if (action.hasOrigin()) {
        ai.setOrigin(action.origin());
    }
    ai.setDynamicPriority(action.dynamicPriority());
    ai.setStatus(action.status());
    ai.setFavorite(action.favorite());
    ai.setName(action.name());
    ai.setCreatedDate(ai.createdDate());
    auto due = ai.due();
    due.setKind(action.due().kind());
    if (action.due().hasStart()) {
        due.setStart(action.due().start());
    }
    if (action.due().hasDue()) {
        due.setDue(action.due().due());
    }
    due.setTimezone(action.due().timezone());
    ai.setDue(due);
    ai.setCompletedTime(ai.completedTime());
    ai.setKind(action.kind());
    ai.setCategory(action.category());
    ai.setTimeEstimate(action.timeEstimate());
    ai.setTimeSpent(action.timeSpent());
    ai.setTags(action.tags());
    return ai;
}

template <typename T, typename C>
bool add(C& container, const T& action, ActionInfoCache *cache = {}) {
    const auto uuid = toQuid(action.id_proto());
    bool changed = false;

    bool added = false;
    auto it = container.find(uuid);
    if (it == container.end()) {
        it = container.emplace(uuid, std::make_shared<nextapp::pb::ActionInfo>()).first;
        *it->second = toActionInfo(action);
        it->second->setScore(ActionInfoCache::getScore(action));
        added = true;
    }

    auto &existing = *it->second;

    if (existing.version() >= action.version()) {
        return false;
    }

    if (existing.node() != action.node()) {
        existing.setNode(action.node());
        changed = true;
    }

    if (existing.name() != action.name()) {
        existing.setName(action.name());
        changed = true;
    }

    if (existing.dynamicPriority() != action.dynamicPriority()) {
        existing.setDynamicPriority(action.dynamicPriority());
        changed = true;
    }

    if (existing.status() != action.status()) {
        existing.setStatus(action.status());
        changed = true;
    }

    if (existing.favorite() != action.favorite()) {
        existing.setFavorite(action.favorite());
        changed = true;
    }

    if (existing.due() != action.due()) {
        existing.setDue(action.due());
        changed = true;
    }

    if (existing.completedTime() != action.completedTime()) {
        existing.setCompletedTime(action.completedTime());
        changed = true;
    }

    if (existing.kind() != action.kind()) {
        existing.setKind(action.kind());
        changed = true;
    }

    if (existing.version() != action.version()) {
        existing.setVersion(action.version());
        changed = true;
    }

    if (existing.category() != action.category()) {
        existing.setCategory(action.category());
        changed = true;
    }

    {
        const auto new_score = ActionInfoCache::getScore(action);
        if (existing.score() != new_score) {
            existing.setScore(new_score);
            changed = true;
        }
    }

    return changed;
}

} // anon ns

ActionInfoCache *ActionInfoCache::instance_;

ActionInfoCache::ActionInfoCache(QObject *parent)
: QObject(parent)
{
    assert(!instance_);
    instance_ = this;

    connect(&ServerComm::instance(), &ServerComm::onUpdate, this,
            [this](const std::shared_ptr<nextapp::pb::Update>& update) {
        onUpdate(update);
    });

    connect(MainTreeModel::instance(), &MainTreeModel::nodeDeleted, [this]() -> QCoro::Task<void> {
        LOG_TRACE << "Node was deleted. Will re-load actions cache.";
        clear();
        emit cacheReloaded();
        co_await loadFromCache();
        emit cacheReloaded();
    });
}


namespace  {

constexpr double normalize(double x, double min, double max) noexcept {
    if (x <= min) return 0.0;
    if (x >= max) return 1.0;
    return (x - min) / (max - min);
}

inline double due_urgency(const std::optional<std::time_t>& due,
                          double horizon_hours = 72.0) noexcept
{
    if (!due) return 0.0;
    using namespace std::chrono;
    auto now_t = system_clock::to_time_t(system_clock::now());
    double hours_left = double(std::difftime(*due, now_t)) / 3600.0;
    if (hours_left <= 0.0) return 1.0;
    return std::min(1.0, horizon_hours / hours_left);
}

// Threshold for penalizing long tasks (in hours)
constexpr double kEstimateThreshold = 8.0;

// Relative weights (excluding base priority): {due, estimate, spent}
constexpr std::array<double, 3> priority_extra_weights = { 0.2, 0.1, 0.1 };
// Weights for: {importance, urgency, due, estimate, spent}
constexpr std::array<double, 5> eisenhower_weights      = { 0.35, 0.25, 0.3, 0.05, 0.05 };

/// Compute score from a 0–7 integer priority + optional due date,
/// optional time estimate, and optional time spent.
/// Base score = p_norm. Additional urgency/estimate/spent contributions
/// are scaled by (1 - p_norm) so that high-priority tasks remain high.
inline double computeScore(int priority,
                           const std::optional<std::time_t>& due = std::nullopt,
                           const std::optional<double>& time_estimate = std::nullopt,
                           const std::optional<double>& time_spent   = std::nullopt) noexcept
{
    double p = normalize(static_cast<double>(priority), 0.0, 7.0);  // base priority
    double d = due_urgency(due);
    double est_norm = time_estimate ? normalize(*time_estimate, 0.0, kEstimateThreshold) : 0.0;
    double e = 1.0 - est_norm; // penalize longer tasks
    double s = 0.0;
    if (time_estimate && time_spent) {
        double comp = *time_spent / *time_estimate;
        s = std::clamp(comp, 0.0, 1.0);
    }
    double extra = priority_extra_weights[0]*d
                   + priority_extra_weights[1]*e
                   + priority_extra_weights[2]*s;
    return p + (1.0 - p) * extra;
}

/// Compute score from 0.0–10.0 importance & urgency + optional due date,
/// optional time estimate, and optional time spent:
/// S = w_i·i_norm + w_u·u_norm + w_d·d + w_e·(1-est_norm) + w_s·spent_norm
inline double computeScore(double importance,
                           double urgency,
                           const std::optional<std::time_t>& due = std::nullopt,
                           const std::optional<double>& time_estimate = std::nullopt,
                           const std::optional<double>& time_spent   = std::nullopt) noexcept
{
    double i = normalize(importance, 0.0, 10.0);
    double u = normalize(urgency,    0.0, 10.0);
    double d = due_urgency(due);
    double est_norm = time_estimate ? normalize(*time_estimate, 0.0, kEstimateThreshold) : 0.0;
    double e = 1.0 - est_norm;
    double s = 0.0;
    if (time_estimate && time_spent) {
        double comp = *time_spent / *time_estimate;
        s = std::clamp(comp, 0.0, 1.0);
    }
    return eisenhower_weights[0]*i
           + eisenhower_weights[1]*u
           + eisenhower_weights[2]*d
           + eisenhower_weights[3]*e
           + eisenhower_weights[4]*s;
}

/// Map a score [0.0,1.0] → a QColor along a gradient:
/// light blue → green → orange → red → purple.
inline QColor scoreToColor(double score) noexcept
{
    using Stop = std::pair<double, std::array<int,3>>;
    static constexpr std::array<Stop,5> stops = {{
        { 0.0,  {173,216,230} }, // light blue
        { 0.61, {  0,128,  0} }, // green
        { 0.80,  {255,165,  0} }, // orange
        { 0.92, {255,  0,  0} }, // red
        { 1.0,  {128,  0,128} }  // purple
    }};

    double s = std::clamp(score, 0.0, 1.0);
    auto it = std::upper_bound(stops.begin(), stops.end(), s,
                               [](double value, const Stop &stop){ return value < stop.first; });
    if (it == stops.begin()) {
        auto& rgb = stops.front().second;
        return QColor(rgb[0], rgb[1], rgb[2]);
    }
    if (it == stops.end()) {
        auto& rgb = stops.back().second;
        return QColor(rgb[0], rgb[1], rgb[2]);
    }
    const auto& upper = *it;
    const auto& lower = *(it - 1);
    double t = (s - lower.first) / (upper.first - lower.first);
    int r = static_cast<int>(lower.second[0] + t * (upper.second[0] - lower.second[0]));
    int g = static_cast<int>(lower.second[1] + t * (upper.second[1] - lower.second[1]));
    int b = static_cast<int>(lower.second[2] + t * (upper.second[2] - lower.second[2]));
    return QColor(r, g, b);
}

template <typename T>
double calculateScore(const T& action) {
    optional<time_t> due_by;
    optional<double> time_estimate;
    optional<double> time_spent;

    if (action.hasDue() && action.due().hasDue()) {
        due_by = action.due().due();
    }
    if (action.timeEstimate() > 0) {
        time_estimate = action.timeEstimate() * 60.0;
    }
    if (action.timeSpent() > 0) {
        time_spent = action.timeSpent() * 60.0;
    }


    if (action.dynamicPriority().hasPriority()) {
        const auto pri = 7 - static_cast<int>(action.dynamicPriority().priority());
        return computeScore(pri, due_by, time_estimate, time_spent);
    } else if (action.dynamicPriority().hasUrgencyImportance()) {
        const auto ui = action.dynamicPriority().urgencyImportance();
        return computeScore(ui.importance(), ui.urgency(), due_by, time_estimate, time_spent);
    }

    LOG_DEBUG <<  "Action " << action.id_proto() << ' ' << action.name()  << " has no priority or urgency.";
    return 0;
}

} // ns


float ActionInfoCache::getScore(const nextapp::pb::ActionInfo &action)
{
    return static_cast<float>(calculateScore(action));
}

float ActionInfoCache::getScore(const nextapp::pb::Action &action)
{
    return static_cast<float>(calculateScore(action));
}

QColor ActionInfoCache::getScoreColor(double score)
{
    return scoreToColor(score);
}

QCoro::Task<void> ActionInfoCache::updateAllScores()
{
    auto cache_copy = hot_cache_;

    if (updating_scores_.exchange(true)) {
        for (auto& [uuid, action] : cache_copy) {
            if (action->status() == nextapp::pb::ActionStatusGadget::ActionStatus::DELETED) {
                continue;
            }
            const auto new_score = getScore(*action);
            constexpr float epsilon = 1e-6;
            if (std::fabs(action->score() - new_score) < epsilon) {
                continue;
            }
            action->setScore(getScore(*action));

            // save to db
            auto& db = NextAppCore::instance()->db();
            co_await db.query("UPDATE action SET score=? WHERE id=?", new_score, action->id_proto());
            //emit actionChanged(uuid);
        }
        //emit cacheReloaded();
        updating_scores_.store(false);
    }
}

QCoro::Task<std::shared_ptr<pb::ActionInfo> > ActionInfoCache::get(const QString &action_uuid, bool fetch)
{
    const auto uuid = toQuid(action_uuid);
    co_return co_await get(uuid, fetch);
}

QCoro::Task<std::shared_ptr<pb::ActionInfo> > ActionInfoCache::get(const QUuid &uuid, bool fetch)
{
    if (auto action = get_(uuid)) {
        co_return action;
    }

    if (fetch) {
        co_await fetchFromDb(uuid);
        co_return get_(uuid);
    }

    co_return {};
}

QCoro::Task<std::shared_ptr<pb::Action> > ActionInfoCache::getAction(const QUuid &action_uuid)
{
    auto& db = NextAppCore::instance()->db();
    DbStore::param_t params;
    params << action_uuid.toString(QUuid::WithoutBraces);

    QString query = "SELECT id, node, origin, priority, dyn_importance, dyn_urgency, dyn_score, status, favorite, name, created_date, "
                    " due_kind, start_time, due_by_time, due_timezone, completed_time, "
                    " time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after, "
                    " descr,"
                    " kind, version, category, time_spent, score, tags FROM action where id=?";

    enum Cols {
        ID,
        NODE,
        ORIGIN,
        PRIORITY,
        DYN_IMPORTANCE,
        DYN_URGENCY,
        DYN_SCORE,
        STATUS,
        FAVORITE,
        NAME,
        CREATED_DATE,
        DUE_KIND,
        START_TIME,
        DUE_BY_TIME,
        DUE_TIMEZONE,
        COMPLETED_TIME,
        TIME_ESTIMATE,
        DIFFICULTY,
        REPEAT_KIND,
        REPEAT_UNIT,
        REPEAT_WHEN,
        REPEAT_AFTER,
        DESCR,
        KIND,
        VERSION,
        CATEGORY,
        TIME_SPENT,
        SCORE,
        TAGS
    };

    auto rval = co_await db.legacyQuery(query, &params);
    if (rval && !rval->empty()) {
        auto& row = rval->front();
        auto item = make_shared<pb::Action>();
        const auto id = row[ID].toString();
        const auto uuid = QUuid{id};
        if (uuid.isNull()) {
            LOG_WARN_N << "Invalid UUID: " << id;
            co_return {};
        }
        item->setId_proto(id);
        item->setNode(row[NODE].toString());
        item->setOrigin(row[ORIGIN].toString());

        {
            nextapp::pb::Priority p;
            if (!row[PRIORITY].isNull()) {
                p.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(row[PRIORITY].toInt()));
            }
            if (!row[DYN_IMPORTANCE].isNull() && !row[DYN_URGENCY].isNull()) {
                nextapp::pb::UrgencyImportance ui;
                ui.setImportance(row[DYN_IMPORTANCE].toInt());
                ui.setUrgency(row[DYN_URGENCY].toInt());
                p.setUrgencyImportance(ui);
            }
            if (row[DYN_SCORE].isNull()) {
                p.setScore(row[DYN_SCORE].toInt());
            }

            item->setDynamicPriority(p);
        }

        item->setStatus(static_cast<nextapp::pb::ActionStatusGadget::ActionStatus>(row[STATUS].toInt()));
        item->setFavorite(row[FAVORITE].toBool());
        item->setName(row[NAME].toString());
        item->setCreatedDate(toDate(row[CREATED_DATE].toDate()));
        item->setCompletedTime(row[COMPLETED_TIME].toDateTime().toSecsSinceEpoch());
        if (!row[TIME_ESTIMATE].isNull()) {
            item->setTimeEstimate(row[TIME_ESTIMATE].toUInt());
        }
        if (!row[DIFFICULTY].isNull()) {
            item->setDifficulty(static_cast<nextapp::pb::ActionDifficultyGadget::ActionDifficulty>(row[DIFFICULTY].toInt()));
        }
        if (!row[REPEAT_KIND].isNull()) {
            item->setRepeatKind(static_cast<nextapp::pb::Action::RepeatKind>(row[REPEAT_KIND].toInt()));
        }
        if (!row[REPEAT_UNIT].isNull()) {
            item->setRepeatUnits(static_cast<nextapp::pb::Action::RepeatUnit>(row[REPEAT_UNIT].toInt()));
        }
        if (!row[REPEAT_WHEN].isNull()) {
            item->setRepeatWhen(static_cast<nextapp::pb::Action::RepeatWhen>(row[REPEAT_WHEN].toInt()));
        }
        if (!row[REPEAT_AFTER].isNull()) {
            item->setRepeatAfter(row[REPEAT_AFTER].toInt());
        }
        if (!row[DESCR].isNull()) {
            item->setDescr(row[DESCR].toString());
        }

        item->setKind(static_cast<nextapp::pb::ActionKindGadget::ActionKind>(row[KIND].toInt()));
        item->setVersion(row[VERSION].toUInt());
        item->setCategory(row[CATEGORY].toString());

        if (!row[TIME_SPENT].isNull()) {
            item->setTimeSpent(row[TIME_SPENT].toInt());
        }

        nextapp::pb::Due due;
        due.setKind(static_cast<nextapp::pb:: ActionDueKindGadget::ActionDueKind >(row[DUE_KIND].toInt()));
        due.setStart(row[START_TIME].toDateTime().toSecsSinceEpoch());
        due.setDue(row[DUE_BY_TIME].toDateTime().toSecsSinceEpoch());
        due.setTimezone(row[DUE_TIMEZONE].toString());
        item->setDue(due);

        if (!row[SCORE].isNull()) {
            item->setScore(row[SCORE].toDouble());
        }

        if (!row[TAGS].isNull()) {
            auto tags = row[TAGS].toString();
            auto tags_list = tags.split(' ');
            item->setTags(std::move(tags_list));
        }

        co_return item;
    }
    co_return {};
}

QCoro::Task<void> ActionInfoCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    const bool deleted = update->op() == nextapp::pb::Update::Operation::DELETED;

    if (update->hasAction()) {
        const auto &action = update->action();
        const auto uuid = toQuid(action.id_proto());
        if (deleted) [[unlikely ]] {
            assert(action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DELETED);
            co_await save(action);
            hot_cache_.erase(uuid);
            emit actionDeleted(uuid);
        } else {
            assert(action.status() != nextapp::pb::ActionStatusGadget::ActionStatus::DELETED);
            co_await save(action);
            const auto changed = add(hot_cache_, action);
            if (update->op() == nextapp::pb::Update::Operation::ADDED) {
                LOG_TRACE_N << "Action " << action.id_proto() << ' ' << action.name() << " added.";
                if (auto ai = get_(uuid)) {
                    emit actionAdded(ai);
                } else {
                    assert(false);
                }
            } else if (changed) {
                LOG_TRACE_N << "Action " << action.id_proto() << ' ' << action.name() << " changed.";
                emit actionChanged(uuid);
            }
        }
    }
}

QCoro::Task<bool> ActionInfoCache::saveBatch(const QList<nextapp::pb::Action> &items)
{
    auto& db = NextAppCore::instance()->db();
    static const QString delete_query = "DELETE FROM action WHERE id = ?";
    auto isDeleted = [](const auto& action) -> bool {
        return action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DELETED;
    };
    auto getId = [](const auto& action) -> QString {
        return action.id_proto();
    };
    auto perRowfn = [this](const auto& action) -> bool {
        return updateTagsDirect(action);
    };

    co_return co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId, perRowfn);
}

QCoro::Task<bool> ActionInfoCache::save(const QProtobufMessage &item)
{
    const auto& action = static_cast<const nextapp::pb::Action&>(item);

    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    if (action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DELETED) {
        QString sql = R"(DELETE FROM action WHERE id = ?)";
        params.append(action.id_proto());
        LOG_TRACE_N << "Deleting action " << action.id_proto() << " " << action.name();

        const auto rval = co_await db.legacyQuery(sql, &params);
        if (!rval) {
            LOG_WARN_N << "Failed to delete action " << action.id_proto() << " " << action.name()
            << " err=" << rval.error();
        }
        co_return true; // TODO: Add proper error handling. Probably a full resynch if the action is in the db.
    }

    co_await updateTags(action);

    params = getParams(action);
    const auto rval = co_await db.legacyQuery(insert_query, &params);
    if (!rval) {
        LOG_ERROR_N << "Failed to update action: " << action.id_proto() << " " << action.name()
        << " err=" << rval.error();
        co_return false; // TODO: Add proper error handling. Probably a full resynch.
    }

    co_return true;
}

// Must be called before the action is updated with a new hash if is_full_sync() is false
QCoro::Task<bool> ActionInfoCache::updateTags(const nextapp::pb::Action &action)
{
    auto& db = NextAppCore::instance()->db();

    if (!action.tags().empty()) {
        if (!isFullSync()) {
            const auto hash_rval = co_await db.query("SELECT tags_hash FROM action WHERE id=?", action.id_proto());
            if (hash_rval.has_value() && !hash_rval.value().rows.empty()) {
                const auto& hash = hash_rval.value().rows.front()[0].toByteArray();
                const auto new_hash = computeTagsHash(action.tags());
                if (hash == new_hash) {
                    co_return true;
                }
            }

            co_await db.query("DELETE FROM tag WHERE action=?", action.id_proto());
        }

        QString sql = "INSERT INTO tags (action, tag) VALUES (?, ?)";
        for(const auto& tag : action.tags()) {
            const auto rval = co_await db.query(sql, action.id_proto(), tag);
            if (!rval) {
                LOG_ERROR_N << "Failed to update action tags: " << action.id_proto() << " " << action.name()
                << " err=" << rval.error();
                co_return false; // TODO: Add proper error handling. Probably a full resynch.
            }
        };

        co_return true;
    }

    if (!isFullSync()) {
        co_await db.query("DELETE FROM tag WHERE action=?", action.id_proto());
    }

    co_return true;
}

bool ActionInfoCache::updateTagsDirect(const nextapp::pb::Action& action)
{
    auto& db = NextAppCore::instance()->db();
    QSqlDatabase& sqlDb = db.db();

    QSqlQuery query(sqlDb);

    if (!action.tags().empty()) {
        if (!isFullSync()) {
            // Compare tag hashes
            query.prepare("SELECT tags_hash FROM action WHERE id = ?");
            query.addBindValue(action.id_proto());

            if (!query.exec()) {
                LOG_ERROR_N << "Failed to read tag hash for action: " << query.lastError().text();
                return false;
            }

            if (query.next()) {
                const QByteArray oldHash = query.value(0).toByteArray();
                const QByteArray newHash = computeTagsHash(action.tags());

                if (oldHash == newHash) {
                    return true; // No change in tags
                }
            }

            // Delete existing tags
            query.prepare("DELETE FROM tag WHERE action = ?");
            query.addBindValue(action.id_proto());
            if (!query.exec()) {
                LOG_ERROR_N << "Failed to delete old tags: " << query.lastError().text();
                return false;
            }
        }

        // Insert new tags
        query.prepare("INSERT INTO tag (action, tag) VALUES (?, ?)");
        for (const auto& tag : action.tags()) {
            query.addBindValue(action.id_proto());
            query.addBindValue(tag);

            if (!query.exec()) {
                LOG_ERROR_N << "Failed to insert tag: " << tag << ", error: " << query.lastError().text();
                return false;
            }
        }

        return true;
    }

    // If tags are empty and not full sync, delete old tags
    if (!isFullSync()) {
        query.prepare("DELETE FROM tag WHERE action = ?");
        query.addBindValue(action.id_proto());
        if (!query.exec()) {
            LOG_ERROR_N << "Failed to delete tags for empty tag list: " << query.lastError().text();
            return false;
        }
    }

    return true;
}



QCoro::Task<bool> ActionInfoCache::loadFromCache()
{
    co_return co_await loadSomeFromCache({});
}

QCoro::Task<bool> ActionInfoCache::loadSomeFromCache(std::optional<QString> id)
{
    auto& db = NextAppCore::instance()->db();
    QString query = "SELECT id, node, origin, priority, dyn_importance, dyn_urgency, dyn_score, status, favorite, name, created_date, "
                          " due_kind, start_time, due_by_time, due_timezone, completed_time, "
                          " kind, version, category, time_estimate, time_spent, score, tags FROM action";

    if (id && !QUuid{*id}.isNull()) {
        query += " WHERE id = '" + *id + "'";
    }

    enum Cols {
        ID,
        NODE,
        ORIGIN,
        PRIORITY,
        DYN_IMPORTANCE,
        DYN_URGENCY,
        DYN_SCORE,
        STATUS,
        FAVORITE,
        NAME,
        CREATED_DATE,
        DUE_KIND,
        START_TIME,
        DUE_BY_TIME,
        DUE_TIMEZONE,
        COMPLETED_TIME,
        KIND,
        VERSION,
        CATEGORY,
        TIME_ESTIMATE,
        TIME_SPENT,
        SCORE,
        TAGS
    };

    uint count = 0;

    auto rval = co_await db.legacyQuery(query);
    if (rval) {
        for (const auto& row : rval.value()) {
            nextapp::pb::ActionInfo item;
            const auto id = row[ID].toString();
            const auto uuid = QUuid{id};
            if (uuid.isNull()) {
                LOG_WARN_N << "Invalid UUID: " << id;
                continue;
            }
            item.setId_proto(id);
            item.setNode(row[NODE].toString());
            item.setOrigin(row[ORIGIN].toString());
            {
                nextapp::pb::Priority p;
                if (!row[PRIORITY].isNull()) {
                    p.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(row[PRIORITY].toInt()));
                    assert(p.hasPriority());
                    assert(!p.hasUrgencyImportance());
                }
                if (!row[DYN_IMPORTANCE].isNull()
                    && !row[DYN_URGENCY].isNull()) {
                    nextapp::pb::UrgencyImportance ui;
                    ui.setImportance(row[DYN_IMPORTANCE].toInt());
                    ui.setUrgency(row[DYN_URGENCY].toInt());
                    p.setUrgencyImportance(ui);
                    assert(p.hasUrgencyImportance());
                    assert(!p.hasPriority());
                }
                if (!row[DYN_SCORE].isNull()) {
                    p.setScore(row[DYN_SCORE].toInt());
                }

                item.setDynamicPriority(p);
            }
            item.setStatus(static_cast<nextapp::pb::ActionStatusGadget::ActionStatus>(row[STATUS].toInt()));
            item.setFavorite(row[FAVORITE].toBool());
            item.setName(row[NAME].toString());
            item.setCreatedDate(toDate(row[CREATED_DATE].toDate()));
            item.setKind(static_cast<nextapp::pb::ActionKindGadget::ActionKind>(row[KIND].toInt()));
            item.setVersion(row[VERSION].toUInt());
            item.setCategory(row[CATEGORY].toString());
            if (row[COMPLETED_TIME].isValid()) {
                item.setCompletedTime(row[COMPLETED_TIME].toDateTime().toSecsSinceEpoch());
            }

            nextapp::pb::Due due;
            due.setKind(static_cast<nextapp::pb:: ActionDueKindGadget::ActionDueKind >(row[DUE_KIND].toInt()));
            due.setStart(row[START_TIME].toDateTime().toSecsSinceEpoch());
            due.setDue(row[DUE_BY_TIME].toDateTime().toSecsSinceEpoch());
            due.setTimezone(row[DUE_TIMEZONE].toString());
            item.setDue(due);

            if (!row[TIME_ESTIMATE].isNull()) {
                item.setTimeEstimate(row[TIME_ESTIMATE].toUInt());
            }
            if (!row[TIME_SPENT].isNull()) {
                item.setTimeSpent(row[TIME_SPENT].toInt());
            }
            if (!row[SCORE].isNull()) {
                item.setScore(row[SCORE].toDouble());
            }
            if (!row[TAGS].isNull()) {
                auto tags = row[TAGS].toString();
                auto tags_list = tags.split(' ');
                item.setTags(std::move(tags_list));
            }


            hot_cache_[uuid] = std::make_shared<nextapp::pb::ActionInfo>(std::move(item));
            ++count;
        }
    }

    co_return true;
}

std::shared_ptr<GrpcIncomingStream> ActionInfoCache::openServerStream(nextapp::pb::GetNewReq req)
{
    return ServerComm::instance().synchActions(req);
}

void ActionInfoCache::clear()
{
    hot_cache_.clear();
}

std::shared_ptr<nextapp::pb::ActionInfo> ActionInfoCache::get_(const QUuid &action_uuid)
{
    auto it = hot_cache_.find(action_uuid);
    if (it != hot_cache_.end()) {
        return it->second;
    }

    return {};
}

QCoro::Task<bool> ActionInfoCache::fetchFromDb(QUuid action_uuid)
{
    assert(hot_cache_.find(action_uuid) == hot_cache_.end());
    const auto result = co_await loadSomeFromCache(action_uuid.toString(QUuid::WithoutBraces));
    if (result) {
        emit actionChanged(action_uuid);
    }
    co_return result;
}

std::shared_ptr<nextapp::pb::ActionInfo> ActionInfoCache::get_(const QString &action_uuid)
{
    QUuid uuid{action_uuid};
    if (!uuid.isNull()) [[likely]] {
        return get_(uuid);
    }

    LOG_WARN_N << "Invalid UUID: " << action_uuid;
    return {};
}
