#include "ActionInfoCache.h"
#include "ServerComm.h"
#include "util.h"

#include <QProtobufSerializer>

#include "logging.h"

using namespace nextapp;

namespace {

pb::ActionInfo toActionInfo(const pb::Action& action) {
    pb::ActionInfo ai;
    ai.setId_proto(action.id_proto());
    ai.setNode(action.node());
    if (action.hasOrigin()) {
        ai.setOrigin(action.origin());
    }
    ai.setPriority(action.priority());
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
    // ai.due().setKind(action.due().kind());
    // if (action.due().hasStart()) {
    //     ai.due().setStart(action.due().start());
    // }
    // if (action.due().hasDue()) {
    //     ai.due().setDue(action.due().due());
    // }
    //ai.due().setTimezone(action.due().timezone());
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

    if (existing.priority() != action.priority()) {
        existing.setPriority(action.priority());
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

    if (added && cache) {
        cache->actionWasAdded(it->second);
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

    // connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
    //     //setOnline(ServerComm::instance().connected());
    //     if (ServerComm::instance().connected()) {
    //         synch();
    //     } else {
    //         setState(State::LOCAL);
    //     }
    // });

    // connect(&ServerComm::instance(), &ServerComm::receivedActions, this, &ActionInfoCache::receivedActions);
    // connect(&ServerComm::instance(), &ServerComm::receivedAction, this, &ActionInfoCache::receivedAction);
    connect(&ServerComm::instance(), &ServerComm::onUpdate, this,
            [this](const std::shared_ptr<nextapp::pb::Update>& update) {
        onUpdate(update);
    });

    // if (ServerComm::instance().connected()) {
    //     synch();
    // }
}

ActionInfoPrx *ActionInfoCache::getAction(QString uuidStr)
{
    auto uuid = toQuid(uuidStr);
    auto ai = std::make_unique<ActionInfoPrx>(uuid, this);

    if (auto& existing = get_(uuid)) {
        ai->setAction(existing);
    } else {
        fetchFromDb(uuid);
    }

    QQmlEngine::setObjectOwnership(ai.get(), QQmlEngine::JavaScriptOwnership);
    return ai.release();
}

std::shared_ptr<nextapp::pb::ActionInfo> ActionInfoCache::get(const QString &action_uuid, bool doFetch)
{
    const auto uuid = toQuid(action_uuid);
    if (auto action = get_(uuid)) {
        return action;
    }

    if (doFetch) {
        fetchFromDb(uuid);
    }

    return {};
}

// void ActionInfoCache::setOnline(bool online) {
//     if (online_ == online) {
//         return;
//     }
//     online_ = online;
//     emit onlineChanged();
// }

void ActionInfoCache::actionWasAdded(const std::shared_ptr<nextapp::pb::ActionInfo> &ai)
{
    emit actionReceived(ai);
}

QCoro::Task<void> ActionInfoCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    const bool deleted = update->op() == nextapp::pb::Update::Operation::DELETED;

    if (update->hasAction()) {
        const auto &action = update->action();
        const auto uuid = toQuid(action.id_proto());
        if (deleted) [[unlikely ]] {
            emit actionDeleted(uuid);
            co_await save(action);
            hot_cache_.erase(uuid);
        } else {
            co_await save(action);
            if (add(hot_cache_, action)) {
                emit actionChanged(uuid);
            }
            if (update->op() == nextapp::pb::Update::Operation::ADDED) {
                if (auto ai = get_(uuid)) {
                    emit actionAdded(ai);
                } else {
                    assert(false);
                }
            }
        }
    }
}

QCoro::Task<bool> ActionInfoCache::save(const QProtobufMessage &item)
{
    const auto& action = static_cast<const nextapp::pb::Action&>(item);

    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    if (action.deleted()) {
        QString sql = R"(DELETE FROM action WHERE id = ?)";
        params.append(action.id_proto());
        LOG_TRACE_N << "Deleting action " << action.id_proto() << " " << action.name();

        const auto rval = co_await db.query(sql, &params);
        if (!rval) {
            LOG_WARN_N << "Failed to delete action " << action.id_proto() << " " << action.name()
            << " err=" << rval.error();
        }
        co_return true; // TODO: Add proper error handling. Probably a full resynch if the action is in the db.
    }

    // TODO: See if we can use a prepared statement.
    static const QString sql = R"(INSERT INTO action
        (id, node, origin, priority, status, favorite, name, descr, created_date,
        due_kind, start_time, due_by_time, due_timezone, completed_time,
        time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after,
        kind, category, version, updated, deleted) VALUES
        (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
        node = EXCLUDED.node,
        origin = EXCLUDED.origin,
        priority = EXCLUDED.priority,
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
        version = EXCLUDED.version,
        updated = EXCLUDED.updated,
        deleted = EXCLUDED.deleted)";

    params.append(action.id_proto());
    params.append(action.node());
    if (action.hasOrigin()) {
        params.append(action.origin());
    } else {
        params.append(QString{});
    }
    params.append(static_cast<quint32>(action.priority()));
    params.append(static_cast<quint32>(action.status()));
    params.append(action.favorite());
    params.append(action.name());
    params.append(action.descr());
    params.append(toQDate(action.createdDate()));
    params.append(static_cast<quint32>(action.due().kind()));
    if (action.due().hasStart()) {
        params.append(QDateTime::fromSecsSinceEpoch(action.due().start()));
    } else {
        params.append(QVariant{});
    }
    if (action.due().hasDue()) {
        params.append(QDateTime::fromSecsSinceEpoch(action.due().due()));
    } else {
        params.append(QVariant{});
    }
    params.append(action.due().timezone());
    params.append(QDateTime::fromSecsSinceEpoch(action.completedTime()));
    params.append(static_cast<qlonglong>(action.timeEstimate()));
    params.append(static_cast<quint32>(action.difficulty()));
    params.append(static_cast<quint32>(action.repeatKind()));
    params.append(static_cast<quint32>(action.repeatUnits()));
    params.append(static_cast<quint32>(action.repeatWhen()));
    params.append(static_cast<quint32>(action.repeatAfter()));
    params.append(static_cast<quint32>(action.kind()));
    params.append(action.category());
    params.append(static_cast<quint32>(action.version()));
    params.append(static_cast<qlonglong>(action.updated()));
    params.append(action.deleted());

    const auto rval = co_await db.query(sql, &params);
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
    QString query = "SELECT id, node, origin, priority, status, favorite, name, created_date, "
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

    auto rval = co_await db.query(query);
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
            item.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(row[PRIORITY].toInt()));
            item.setStatus(static_cast<nextapp::pb::ActionStatusGadget::ActionStatus>(row[STATUS].toInt()));
            item.setFavorite(row[FAVORITE].toBool());
            item.setName(row[NAME].toString());
            item.setCreatedDate(toDate(row[CREATED_DATE].toDate()));
            item.setKind(static_cast<nextapp::pb::ActionKindGadget::ActionKind>(row[KIND].toInt()));
            item.setVersion(row[VERSION].toUInt());
            item.setCategory(row[CATEGORY].toString());
            item.setCompletedTime(row[COMPLETED_TIME].toDateTime().toSecsSinceEpoch());

            nextapp::pb::Due due;
            due.setKind(static_cast<nextapp::pb:: ActionDueKindGadget::ActionDueKind >(row[DUE_KIND].toInt()));
            due.setStart(row[START_TIME].toDateTime().toSecsSinceEpoch());
            due.setDue(row[DUE_BY_TIME].toDateTime().toSecsSinceEpoch());
            due.setTimezone(row[DUE_TIMEZONE].toString());
            item.setDue(due);

            hot_cache_[uuid] = std::make_shared<nextapp::pb::ActionInfo>(std::move(item));
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

std::shared_ptr<nextapp::pb::ActionInfo> &ActionInfoCache::get_(const QUuid &action_uuid)
{
    static std::shared_ptr<nextapp::pb::ActionInfo> empty_action;
    assert(!empty_action);

    auto it = hot_cache_.find(action_uuid);
    if (it != hot_cache_.end()) {
        return it->second;
    }

    return empty_action;
}

QCoro::Task<void> ActionInfoCache::fetchFromDb(QUuid action_uuid)
{
    assert(hot_cache_.find(action_uuid) == hot_cache_.end());

    co_await loadSomeFromCache(action_uuid.toString(QUuid::WithoutBraces));

    emit actionChanged(action_uuid);
}

// void ActionInfoCache::fetch(const QUuid &action_uuid)
// {
//     if (pending_fetch_.count(action_uuid)) {
//         return;
//     }

// #ifdef _DEBUG
//     assert(!get_(action_uuid));
// #endif

//     pending_fetch_.insert(action_uuid);

//     nextapp::pb::GetActionReq req;
//     req.setUuid(action_uuid.toString(QUuid::WithoutBraces));
//     ServerComm::instance().getAction(req);
// }

std::shared_ptr<nextapp::pb::ActionInfo> &ActionInfoCache::get_(const QString &action_uuid)
{
    return get_(toQuid(action_uuid));
}


ActionInfoPrx::ActionInfoPrx(QUuid actionUuid, ActionInfoCache *model)
: uuid_{actionUuid}
{
    connect(model, &ActionInfoCache::actionReceived, this, &ActionInfoPrx::setAction);
    connect(model, &ActionInfoCache::actionDeleted, this, &ActionInfoPrx::actionDeleted);
    connect(model, &ActionInfoCache::actionChanged, this, &ActionInfoPrx::onActionChanged);
}

const nextapp::pb::ActionInfo *ActionInfoPrx::getActionInfo(const QUuid &uuid)
{
    return action_.get();
}

void ActionInfoPrx::onActionChanged(const QUuid &uuid) {
    if (uuid_ == uuid) {
        if (!action_) {
            auto action = ActionInfoCache::instance()->get(uuid_.toString(QUuid::WithoutBraces));
            if (action) {
                setAction(std::move(action));
            }
        }
        emit actionChanged();
    }
}
