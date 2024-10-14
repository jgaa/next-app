#include <algorithm>
#include <QProtobufSerializer>

#include "WorkCache.h"
#include "DbStore.h"
#include "NextAppCore.h"

WorkCache::WorkCache(QObject *parent)
    : QObject{parent}
{}

QCoro::Task<void> WorkCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    if (update->hasWork()) {
        const auto &work = update->work();
        const QUuid id{work.id_proto()};
        if (work.state() == nextapp::pb::WorkSession::State::DELETED) {
            emit WorkSessionDeleted(id);
            items_.erase(id);
            co_await remove(id);
        } else {
            if (auto it = items_.find(id); it != items_.end()) {
                *it->second = work;
                emit WorkSessionChanged(id);
            } else {
                it->second = std::make_shared<nextapp::pb::WorkSession>(work);
                emit WorkSessionAdded(id);
            }
            co_await save(work);
            auto [it, added] = items_.insert_or_assign(id, std::make_shared<nextapp::pb::WorkSession>(work));
        }
    }
}

QCoro::Task<bool> WorkCache::save(const QProtobufMessage &item)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    static const QString sql = R"(INSERT INTO work_session (
        id, action, state, start_time, end_time, duration, paused, data, updated
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
        action = EXCLUDED.action,
        state = EXCLUDED.state,
        start_time = EXCLUDED.start_time,
        end_time = EXCLUDED.end_time,
        duration = EXCLUDED.duration,
        paused = EXCLUDED.paused,
        data = EXCLUDED.data,
        updated = EXCLUDED.updated
    )";

    const auto& work = static_cast<const nextapp::pb::WorkSession&>(item);
    params << work.id_proto();
    params << work.action();
    params << static_cast<uint>(work.state());

    if (work.start() > 0) {
        params << QDateTime::fromSecsSinceEpoch(work.start());
    } else {
        params << QVariant{};
    }

    if (work.hasEnd() && work.end() > 0) {
        params << QDateTime::fromSecsSinceEpoch(work.end());
    } else {
        params << QVariant{};
    }

    params << work.duration();
    params << work.paused();

    QProtobufSerializer serializer;
    params << work.serialize(&serializer);
    params << static_cast<qlonglong>(work.updated());

    const auto rval = co_await db.query(sql, &params);
    if (!rval) {
        LOG_ERROR_N << "Failed to update action: " << work.id_proto() << " " << work.name()
        << " err=" << rval.error();
        co_return false; // TODO: Add proper error handling. Probably a full resynch.
    }

    co_return true;
}

QCoro::Task<bool> WorkCache::loadFromCache()
{
    co_return true;
}

QCoro::Task<bool> WorkCache::loadSomeFromCache(std::optional<QString> id)
{
    co_return true;
}

std::shared_ptr<GrpcIncomingStream> WorkCache::openServerStream(nextapp::pb::GetNewReq req)
{
    return ServerComm::instance().synchWorkSessions(req);
}

void WorkCache::clear()
{
    items_.clear();
}

void WorkCache::purge()
{
    // Clear items with just one reference
    erase_if(items_, [](const auto& pair) {
        return pair.second.use_count() == 1;
    });
}

QCoro::Task<void> WorkCache::remove(const QUuid &id)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    params << id.toString();
    auto res = co_await db.query("DELETE FROM work_sessions WHERE id = ?", &params);
    co_return;
}
