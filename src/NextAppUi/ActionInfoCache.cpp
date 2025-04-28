#include "ActionInfoCache.h"
#include "MainTreeModel.h"
#include "ServerComm.h"
#include "util.h"

#include <QProtobufSerializer>

#include "logging.h"

using namespace std;
using namespace nextapp;

namespace {

static const QString insert_query = R"(INSERT INTO action
        (id, node, origin, priority, dyn_importance, dyn_urgency, dyn_score, status, favorite, name, descr, created_date,
        due_kind, start_time, due_by_time, due_timezone, completed_time,
        time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after,
        kind, category, time_spent, version, updated) VALUES
        (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
        updated = EXCLUDED.updated)";

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
                    " kind, version, category, time_spent FROM action where id=?";

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
        TIME_SPENT
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
            if (row[PRIORITY].isValid()) {
                p.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(row[PRIORITY].toInt()));
            }
            if (row[DYN_IMPORTANCE].isValid() && row[DYN_URGENCY].isValid()) {
                nextapp::pb::UrgencyImportance ui;
                ui.setImportance(row[DYN_IMPORTANCE].toInt());
                ui.setUrgency(row[DYN_URGENCY].toInt());
                p.setUrgencyImportance(ui);
            }
            if (row[DYN_SCORE].isValid()) {
                p.setScore(row[DYN_SCORE].toInt());
            }

            item->setDynamicPriority(p);
        }

        item->setStatus(static_cast<nextapp::pb::ActionStatusGadget::ActionStatus>(row[STATUS].toInt()));
        item->setFavorite(row[FAVORITE].toBool());
        item->setName(row[NAME].toString());
        item->setCreatedDate(toDate(row[CREATED_DATE].toDate()));
        item->setCompletedTime(row[COMPLETED_TIME].toDateTime().toSecsSinceEpoch());
        if (row[TIME_ESTIMATE].isValid()) {
            item->setTimeEstimate(row[TIME_ESTIMATE].toLongLong());
        }
        if (row[DIFFICULTY].isValid()) {
            item->setDifficulty(static_cast<nextapp::pb::ActionDifficultyGadget::ActionDifficulty>(row[DIFFICULTY].toInt()));
        }
        if (row[REPEAT_KIND].isValid()) {
            item->setRepeatKind(static_cast<nextapp::pb::Action::RepeatKind>(row[REPEAT_KIND].toInt()));
        }
        if (row[REPEAT_UNIT].isValid()) {
            item->setRepeatUnits(static_cast<nextapp::pb::Action::RepeatUnit>(row[REPEAT_UNIT].toInt()));
        }
        if (row[REPEAT_WHEN].isValid()) {
            item->setRepeatWhen(static_cast<nextapp::pb::Action::RepeatWhen>(row[REPEAT_WHEN].toInt()));
        }
        if (row[REPEAT_AFTER].isValid()) {
            item->setRepeatAfter(row[REPEAT_AFTER].toInt());
        }
        if (row[DESCR].isValid()) {
            item->setDescr(row[DESCR].toString());
        }

        item->setKind(static_cast<nextapp::pb::ActionKindGadget::ActionKind>(row[KIND].toInt()));
        item->setVersion(row[VERSION].toUInt());
        item->setCategory(row[CATEGORY].toString());

        if (row[TIME_SPENT].isValid()) {
            item->setTimeSpent(row[TIME_SPENT].toInt());
        }

        nextapp::pb::Due due;
        due.setKind(static_cast<nextapp::pb:: ActionDueKindGadget::ActionDueKind >(row[DUE_KIND].toInt()));
        due.setStart(row[START_TIME].toDateTime().toSecsSinceEpoch());
        due.setDue(row[DUE_BY_TIME].toDateTime().toSecsSinceEpoch());
        due.setTimezone(row[DUE_TIMEZONE].toString());
        item->setDue(due);

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

    co_return co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId);
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

    params = getParams(action);

    const auto rval = co_await db.legacyQuery(insert_query, &params);
    if (!rval) {
        LOG_ERROR_N << "Failed to update action: " << action.id_proto() << " " << action.name()
        << " err=" << rval.error();
        co_return false; // TODO: Add proper error handling. Probably a full resynch.
    }

    co_return true;
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
                          " kind, version, category FROM action";

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
        CATEGORY
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
                if (row[PRIORITY].isValid()) {
                    p.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(row[PRIORITY].toInt()));
                }
                if (row[DYN_IMPORTANCE].isValid() && row[DYN_URGENCY].isValid()) {
                    nextapp::pb::UrgencyImportance ui;
                    ui.setImportance(row[DYN_IMPORTANCE].toInt());
                    ui.setUrgency(row[DYN_URGENCY].toInt());
                    p.setUrgencyImportance(ui);
                }
                if (row[DYN_SCORE].isValid()) {
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
