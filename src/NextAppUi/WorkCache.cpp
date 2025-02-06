
#include <ranges>
#include <algorithm>
#include <QProtobufSerializer>

#include "WorkCache.h"
#include "DbStore.h"
#include "NextAppCore.h"
#include "format_wrapper.h"

using namespace std;

namespace {
static const QString insert_query = R"(INSERT INTO work_session (
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

    QList<QVariant> getParams(const nextapp::pb::WorkSession& work) {
        QList<QVariant> params;
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

        return params;
    }

    void sortActive(WorkCache::active_t& active)
    {
        std::ranges::sort(active, [](const auto& lhs, const auto& rhs) {
            // Sort on state, and then touched time DESC
            if (lhs->state() != rhs->state()) {
                return lhs->state() < rhs->state();
            }
            if (lhs->touched() != rhs->touched()) {
                return lhs->touched() > rhs->touched();
            }
            return false;
        });
    }
}

WorkCache::WorkCache(QObject *parent)
    : QObject{parent}
{
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &WorkCache::onTimer);
    timer_->start(5000);

    connect(&ServerComm::instance(), &ServerComm::onUpdate,
    [this](const std::shared_ptr<nextapp::pb::Update>& update) {
        onUpdate(update);
    });
}

QCoro::Task<void> WorkCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    const auto op = update->op();
    if (update->hasWork()) {
        auto work = update->work();
        updateOutcome(work);
        const auto id_str = work.id_proto();
        const QUuid id{work.id_proto()};
        bool active_changed = false;
        bool exist_in_active = false;

        // Remove from Active if the work session is not active or paused
        if (auto it = std::ranges::find_if(active_, [&id_str, &work](const auto& ws) {
            return ws->id_proto() == id_str;
        }); it != active_.end()) {
            if (work.state() != nextapp::pb::WorkSession::State::ACTIVE
                && work.state() != nextapp::pb::WorkSession::State::PAUSED) {
                active_.erase(it);
                active_changed = true;
            } else {
                exist_in_active = true;
                LOG_TRACE_N << "Active changed. id=" << id_str
                            << ", touched=" << work.touched()
                            << ", name=" << work.name();
            }
        }

        if (work.state() == nextapp::pb::WorkSession::State::DELETED) {
            emit WorkSessionDeleted(id);
            items_.erase(id);
            co_await remove(id);
        } else {
            std::shared_ptr<nextapp::pb::WorkSession> wsp;
            if (auto it = items_.find(id); it != items_.end()) {
                wsp = it->second;
                *it->second = work;
            } else {
                wsp = std::make_shared<nextapp::pb::WorkSession>(work);
                items_.emplace(id, wsp);
            }
            co_await save(*wsp);

            if (wsp->state() == nextapp::pb::WorkSession::State::ACTIVE
                || wsp->state() == nextapp::pb::WorkSession::State::PAUSED) {
                if (!exist_in_active) {
                    active_.push_back(wsp);
                }
            }

            if (op == nextapp::pb::Update::Operation::ADDED) {
                emit WorkSessionAdded(id);
            } else {
                emit WorkSessionChanged(id);
            }

            if (active_changed || exist_in_active) {
                sortActive(active_);
                emit activeChanged();
            }
        }
    } else if (update->hasAction()) {
        if (op == nextapp::pb::Update::Operation::DELETED || op == nextapp::pb::Update::Operation::MOVED) {
            auto action_id = update->action().id_proto();
            if (ranges::find_if(items_, [&action_id](const auto& pair) {
                return pair.second->action() == action_id;
            }) != items_.end()) {
                const auto id = QUuid{action_id};
                if (op == nextapp::pb::Update::Operation::DELETED) {
                    items_.erase(id);
                    co_await remove(id);
                    emit WorkSessionDeleted(id);
                } else {
                    // If the action is moved, any model may need to re-query the db to sync with the selected node
                    emit WorkSessionActionMoved(id);
                }
            }
        }
    }
}

QCoro::Task<bool> WorkCache::saveBatch(const QList<nextapp::pb::WorkSession> &items)
{
    auto& db = NextAppCore::instance()->db();
    static const QString delete_query = "DELETE FROM work_session WHERE id = ?";
    auto isDeleted = [](const auto& work) {
        return work.state() == nextapp::pb::WorkSession::State::DELETED;
    };
    auto getId = [](const auto& work) {
        return work.id_proto();
    };

    co_return co_await db.queryBatch(insert_query, delete_query, items, getParams, isDeleted, getId);
}

QCoro::Task<bool> WorkCache::save(const QProtobufMessage &item)
{
    const auto& work = static_cast<const nextapp::pb::WorkSession&>(item);
    if (work.state() == nextapp::pb::WorkSession::State::DELETED) {
        co_await remove(QUuid{work.id_proto()});
        co_return true;
    }

    auto& db = NextAppCore::instance()->db();
    const auto params = getParams(work);
    const auto rval = co_await db.legacyQuery(insert_query, &params);
    if (!rval) {
        LOG_ERROR_N << "Failed to update action: " << work.id_proto() << " " << work.name()
        << " err=" << rval.error();
        co_return false; // TODO: Add proper error handling. Probably a full resynch.
    }

    co_return true;
}

QCoro::Task<bool> WorkCache::loadFromCache()
{
    // Load the active sessions
    auto& db = NextAppCore::instance()->db();
    DbStore::param_t params;
    params << static_cast<uint>(nextapp::pb::WorkSession::State::DONE);
    auto res = co_await db.legacyQuery("SELECT data FROM work_session WHERE state <?", &params);

    if (res) {
        for (const auto& row : *res) {
            QProtobufSerializer serializer;
            nextapp::pb::WorkSession work;
            if (!work.deserialize(&serializer, row.at(0).toByteArray())) {
                LOG_ERROR_N << "Failed to parse work session";
                continue;
            }

            auto [it, _] = items_.emplace(QUuid{work.id_proto()}, std::make_shared<nextapp::pb::WorkSession>(work));
            updateOutcome(*it->second);
            active_.emplace_back(it->second);
        }
    } else {
        LOG_ERROR_N << "Failed to load work sessions: " << res.error();
        co_return false;
    }

    sortActive(active_);

    co_return true;
}

std::shared_ptr<GrpcIncomingStream> WorkCache::openServerStream(nextapp::pb::GetNewReq req)
{
    return ServerComm::instance().synchWorkSessions(req);
}

void WorkCache::clear()
{
    items_.clear();
    active_.clear();
}

QCoro::Task<std::vector<std::shared_ptr<nextapp::pb::WorkSession>>>
WorkCache::getWorkSessions(nextapp::pb::GetWorkSessionsReq req)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;
    std::vector<std::shared_ptr<nextapp::pb::WorkSession>> sessions;

    assert(req.hasPage());
    assert(req.page().pageSize() > 0);
    auto limit = min<uint>(req.page().pageSize(), 100);
    auto offset = req.page().hasOffset() ? req.page().offset() : 0;

    string where;

    auto add = [&where](const string& what) {
        if (where.empty()) {
            where = " WHERE " + what;
        } else {
            where += " AND " + what;
        }
    };

    string sql;

    auto order = nextapp::format("{} {}",
        req.sortCols() == nextapp::pb::GetWorkSessionsReq::SortCols::FROM_TIME ? "w.start_time" : "w.updated",
        req.sortOrder() == nextapp::pb::SortOrderGadget::SortOrder::ASCENDING ? "ASC" : "DESC");

    if (req.hasTimeSpan()) {
        add("w.start_time >= ? AND w.end_time < ?");
        params << QDateTime::fromSecsSinceEpoch(req.timeSpan().start());
        params << QDateTime::fromSecsSinceEpoch(req.timeSpan().end());
    }

    if (req.hasNodeId()) {
        sql = nextapp::format(R"(WITH RECURSIVE node_hierarchy AS (
    -- Base case: Select the node with the given UUID
    SELECT uuid
    FROM node
    WHERE uuid = ?

    -- Recursive case: Select the children of the current node
    UNION ALL
    SELECT n.uuid
    FROM node n
    JOIN node_hierarchy nh ON n.parent = nh.uuid
)
SELECT w.id, w.data
FROM action a
JOIN node_hierarchy nh ON a.node = nh.uuid
JOIN work_session w ON a.id = w.action
{}
ORDER BY {}
LIMIT {} OFFSET {})", where, order, limit, offset);
        params << req.nodeId();

    } else if (req.hasActionId()) {
        add("w.action = ?");
        params << req.actionId();
    }

    if (sql.empty()) {
        sql = nextapp::format(R"(SELECT w.id, w.data FROM work_session w {}
ORDER BY {}
LIMIT {} OFFSET {})", where, order, limit, offset);
    }

    auto res = co_await db.legacyQuery(QString::fromLatin1(sql), &params);
    if (!res) {
        LOG_ERROR_N << "Failed to fetch work sessions: " << res.error();
        co_return sessions;
    }

    for (const auto& row : *res) {
        assert(row.size() >= 2);

        const auto uuid = QUuid(row.at(0).toString());

        if (auto it = items_.find(uuid); it != items_.end()) {
            sessions.push_back(it->second);
            continue;
        }

        QProtobufSerializer serializer;
        nextapp::pb::WorkSession work;
        if (!work.deserialize(&serializer, row.at(1).toByteArray())) {
            LOG_ERROR_N << "Failed to parse work session " << uuid.toString();
            continue;
        }

        auto [it, added] = items_.emplace(uuid, std::make_shared<nextapp::pb::WorkSession>(work));
        sessions.push_back(it->second);
    }

    co_return sessions;
}

WorkCache *WorkCache::instance() noexcept
{
    static WorkCache instance;
    return &instance;
}

void WorkCache::purge()
{
    // Clear items with just one reference
    erase_if(items_, [](const auto& pair) {
        return pair.second.use_count() == 1;
    });
}

void WorkCache::onTimer()
{
    updateSessionsDurations();
}

void WorkCache::updateSessionsDurations()
{
    int row = 0;
    bool changed = false;
    active_duration_changes_t changes;
    changes.reserve(active_.size());
    for(auto& ws : active_) {
        const auto outcome = updateOutcome(*ws);
        auto& change = changes.emplace_back();
        if (outcome.changed()) {
            change.duration = outcome.duration;
            change.paused = outcome.paused;
            if (change.paused || change.duration) {
                changed = true;
            }
        }
    }

    if (changed) {
        LOG_DEBUG_N << "Active duration changed. Emitting signal.";
        emit activeDurationChanged(changes);
    }
}

QCoro::Task<void> WorkCache::remove(const QUuid &id)
{
    auto& db = NextAppCore::instance()->db();
    auto res = co_await db.query("DELETE FROM work_session WHERE id = ?",
                                 id.toString(QUuid::WithoutBraces));
    if (!res || !res->affected_rows.has_value() || res->affected_rows.value() != 1) {
        LOG_DEBUG_N << "Failed to delete work session: " << res.error();
    }
    co_return;
}

WorkCache::Outcome WorkCache::updateOutcome(nextapp::pb::WorkSession &work)
{
    using namespace nextapp;

    // First event *must* be a start event
    if (work.events().empty()) {
        return {}; // Nothing to do
    }

    const auto orig_start = work.start() / 60;
    const auto orig_end = work.hasEnd() ? work.end() / 60 : 0;
    const auto orig_duration = work.duration() / 60;
    const auto orig_paused = work.paused() / 60;
    const auto orig_state = work.state();
    const auto orig_name = work.name();
    const auto full_orig_duration = work.duration();

    work.setPaused(0);
    work.setDuration(0);
    work.setStart(0);

    if (work.state() != pb::WorkSession::State::DONE) {
        work.clearEnd();
    }

    time_t pause_from = 0;

    const auto end_pause = [&](const pb::WorkEvent& event) {
        if (pause_from > 0) {
            auto pduration = event.time() - pause_from;
            work.setPaused(work.paused() + pduration);
            pause_from = 0;
        }
    };

    unsigned row = 0;
    for(const auto &event : work.events()) {
        ++row;
        switch(event.kind()) {
        case pb::WorkEvent_QtProtobufNested::Kind::START:
            work.setStart(event.time());
            work.setState(pb::WorkSession::State::ACTIVE);
            break;
        case pb::WorkEvent_QtProtobufNested::Kind::STOP:
            end_pause(event);
            if (event.hasEnd()) {
                work.setEnd(event.end());
            } else {
                work.setEnd(event.time());
            }
            work.setState(pb::WorkSession::State::DONE);
            break;
        case pb::WorkEvent_QtProtobufNested::Kind::PAUSE:
            if (!pause_from) {
                pause_from = event.time();
            }
            work.setState(pb::WorkSession::State::PAUSED);
            break;
        case pb::WorkEvent_QtProtobufNested::Kind::RESUME:
            end_pause(event);
            work.setState(pb::WorkSession::State::ACTIVE);
            break;
        case pb::WorkEvent_QtProtobufNested::Kind::TOUCH:
            work.setTouched(event.time());
            break;
        case pb::WorkEvent_QtProtobufNested::Kind::CORRECTION:
            if (event.hasStart()) {
                work.setStart(event.start());
            }
            if (event.hasEnd()) {
                if (work.state() != pb::WorkSession::State::DONE) {
                    throw runtime_error{"Cannot correct end time of an active session"};
                }
                work.setEnd(event.end());
            }
            if (event.hasDuration()) {
                work.setDuration(event.duration());
            }
            if (event.hasPaused()) {
                work.setPaused(event.paused());
                if (pause_from) {
                    // Start the pause timer at the events time
                    pause_from = event.time();
                }
            }
            if (event.hasName()) {
                work.setName(event.name());
            }
            if (event.hasNotes()) {
                work.setNotes(event.notes());
            }
            break;
        default:
            assert(false);
            throw runtime_error{"Invalid work event kind"s + toString(event.kind())};
        }
    }

    if (pause_from) {
        // If we are paused, we need to account for the time between the last pause and now
        pb::WorkEvent event;
        event.setTime(time({}));
        end_pause(event);
    }

    if (!work.start()) [[unlikely]] {
        work.clearEnd();
        work.setDuration(0);
        work.setPaused(0);
    } else if (work.hasEnd()) {
        if (work.end() <= work.start()) {
                work.setDuration(0);
            } else {
                auto duration = work.end() - work.start();
                work.setDuration(std::max<long>(0, duration - work.paused()));
            }
    } else {
        const auto now = time({});
        if (now <= work.start()) {
                work.setDuration(0);
            } else {
                work.setDuration(std::min<long>(std::max<long>(0, (now - work.start()) - work.paused()), 3600 * 24 *7));
            }
    }

    if (orig_state == pb::WorkSession::State::DONE) {
        work.setState(orig_state);
    }

    Outcome outcome;
    outcome.start = orig_start != work.start() / 60;
    outcome.end = orig_end != (work.hasEnd() ? work.end() / 60 : 0);
    outcome.duration = orig_duration != work.duration() / 60;
    outcome.paused= orig_paused != work.paused() / 60;
    outcome.name = orig_name != work.name();

    LOG_DEBUG << "Updated work session " << work.name() << " from " << full_orig_duration << " to "
              << work.duration()
              << " outcome.duration= " << outcome.duration;

    return outcome;
}
