
#include "shared_grpc_server.h"

using namespace std;
using namespace std::string_view_literals;

namespace nextapp::grpc {

namespace {

auto createWorkEvent(pb::WorkEvent::Kind kind) {
    pb::WorkEvent we;
    we.set_kind(kind);
    we.set_time(time(nullptr));
    return we;
}

template <ProtoMessage T>
auto toBlob(const T& msg) {
    boost::mysql::blob blob;
    blob.resize(msg.ByteSizeLong());
    if (!msg.SerializeToArray(blob.data(), blob.size())) {
        throw runtime_error{"Failed to serialize protobuf message"};
    }
    return blob;
}

auto eventsToBlob(const pb::WorkSession& ws) {
    pb::SavedWorkEvents events;
    *events.mutable_events() = ws.events();
    return toBlob(events);
}

struct ToWorkSession {
    enum Cols {
        ID, ACTION, USER, START, END, DURATION, PAUSED, STATE, VERSION, TOUCHED, NAME, NOTES, EVENTS
    };

    static constexpr string_view selectCols = "id, action, user, start_time, end_time, duration, paused, state, version, touch_time, "
                                              "name, note, events";

    static void assign(const boost::mysql::row_view& row, pb::WorkSession& ws, const UserContext& uctx) {
        ws.set_id(row.at(ID).as_string());
        ws.set_action(row.at(ACTION).as_string());
        ws.set_user(row.at(USER).as_string());
        ws.set_start(toTimeT(row.at(START).as_datetime(), uctx.tz()));
        ws.set_touched(toTimeT(row.at(TOUCHED).as_datetime(), uctx.tz()));
        if (row.at(END).is_datetime()) {
            ws.set_end(toTimeT(row.at(END).as_datetime(), uctx.tz()));
        }
        ws.set_duration(row.at(DURATION).as_int64());
        ws.set_paused(row.at(PAUSED).as_int64() != 0);
        ws.set_version(row.at(VERSION).as_int64());
        ws.set_name(row.at(NAME).as_string());
        if (row.at(NOTES).is_string()) {
            ws.set_notes(row.at(NOTES).as_string());
        }

        if (!row.at(EVENTS).is_null()) {
            auto blob = row.at(EVENTS).as_blob();
            pb::SavedWorkEvents events;
            if (events.ParseFromArray(blob.data(), blob.size())) {
                ws.mutable_events()->Swap(events.mutable_events());
            } else {
                LOG_WARN_N << "Failed to parse WorkSession.events for WorkSession " << ws.id();
                throw runtime_error{"Failed to parse WorkSession.events"};
            }
        }

        {
            pb::WorkSession_State state;
            const auto name = toUpper(row.at(STATE).as_string());
            if (pb::WorkSession_State_Parse(name, &state)) {
                ws.set_state(state);
            } else {
                LOG_WARN_N << "Invalid WorkSession.State: " << name;
            }
        }

        //LOG_TRACE_N << "Loaded WorkSession: " << toJson(ws, 2);
    }
};

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateWorkSession(::grpc::CallbackServerContext *ctx, const pb::CreateWorkReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            string name;
            co_await owner_.validateAction(req->actionid(), cuser, &name);

            // TODO: Execute in transaction

            // Pause any active work session
            co_await owner_.pauseWork(*uctx);

            pb::SavedWorkEvents events;
            events.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_START));
            const auto start_time = toAnsiTime(events.events(0).time(), uctx->tz());
            const auto blob = toBlob(events);
            // Create the work session record
            const auto res = co_await owner_.server().db().exec(
                format("INSERT INTO work_session (start_time, touch_time, action, user, name, events) VALUES (?, ?, ?, ?, ?, ?) "
                       "RETURNING {}", ToWorkSession::selectCols),
                dbopts,
                start_time,
                start_time,
                req->actionid(),
                cuser,
                name,
                blob);

            assert(res.has_value() && !res.rows().empty());

            pb::WorkSession session;
            ToWorkSession::assign(res.rows().front(), session, *uctx);

            auto& final_session = *reply->mutable_work();
            final_session = std::move(session);

            auto update = newUpdate(pb::Update::Operation::Update_Operation_ADDED);
            *update->mutable_work() = final_session;
            owner_.publish(update);

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::AddWorkEvent(::grpc::CallbackServerContext *ctx, const pb::AddWorkEventReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            auto work = co_await owner_.fetchWorkSession(req->worksessionid(), *uctx);
            const auto& event = req->event();

            // Here we do the logic. When the sitch exits, `work` is assumed to be updated
            switch(event.kind()) {
            case pb::WorkEvent::Kind::WorkEvent_Kind_START:
                throw db_err{pb::Error::INVALID_REQUEST, "Use CreateWorkSession to start a new session"};
            case pb::WorkEvent::Kind::WorkEvent_Kind_STOP:
                if (work.has_end()) {
                    throw db_err{pb::Error::INVALID_REQUEST, "Session is already stopped"};
                }
                co_await owner_.stopWorkSession(work, *uctx);
                co_await owner_.activateNextWorkSession(*uctx);
                break;
            case pb::WorkEvent::Kind::WorkEvent_Kind_PAUSE:
                if (work.state() != pb::WorkSession_State::WorkSession_State_ACTIVE) {
                    throw db_err{pb::Error::INVALID_REQUEST, "Session must be active to pause"};
                }
                co_await owner_.pauseWorkSession(work, *uctx);
                break;
            case pb::WorkEvent::Kind::WorkEvent_Kind_RESUME:
                if (work.state() != pb::WorkSession_State::WorkSession_State_PAUSED) {
                    throw db_err{pb::Error::INVALID_REQUEST, "Session must be paused to resume"};
                }
                co_await owner_.resumeWorkSession(work, *uctx);
                break;
            case pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH: {
                    auto ne = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH);
                    work.add_events()->Swap(&ne);
                    co_await owner_.saveWorkSession(work, *uctx);
                    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
                    *update->mutable_work() = work;
                    owner_.publish(update);
                } break;
            case pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION: {
                auto ne = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION);
                if (event.has_start()) {
                    ne.set_start(event.start());
                }
                if (event.has_end()) {
                    ne.set_end(event.end());
                }
                if (event.has_duration()) {
                    ne.set_duration(event.duration());
                }
                if (event.has_paused()) {
                    ne.set_paused(event.paused());
                }
                if (event.has_notes()) {
                    ne.set_notes(event.notes());
                }
                if (event.has_name()) {
                    ne.set_name(event.name());
                }
                work.add_events()->Swap(&ne);
                updateOutcome(work, *uctx);
                co_await owner_.saveWorkSession(work, *uctx, false);
                auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
                *update->mutable_work() = work;
                owner_.publish(update);
            } break;
            default:
                assert(false);
                throw db_err{pb::Error::INVALID_REQUEST, "Invalid WorkEvent.Kind"};
            }

            *reply->mutable_work() = std::move(work);
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::ListCurrentWorkSessions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();

            // TODO: Handle overlap between days
            auto res = co_await owner_.server().db().exec(
                format("SELECT {} from work_session where user = ? and state in ('active', 'paused') ORDER BY state, touch_time DESC",
                       ToWorkSession::selectCols), cuser);
            if (res.has_value()) {
                auto sessions = reply->mutable_worksessions()->mutable_sessions();
                for(const auto& row : res.rows()) {
                    ToWorkSession::assign(row, *sessions->Add(), *uctx);
                }
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetWorkSummary(::grpc::CallbackServerContext *ctx, const pb::WorkSummaryRequest *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();

            // Update the active work session to calculate the current duration and pause
            co_await owner_.syncActiveWork(*uctx);

            // Calculate the start and end timestamps
            TimePeriod period;
            switch(req->kind()) {
            case pb::WorkSummaryKind::WSK_DAY:
                period = toTimePeriodDay(time(nullptr), *uctx);
                break;
            case pb::WorkSummaryKind::WSK_WEEK:
                period = toTimePeriodWeek(time(nullptr), *uctx);
                break;
            default:
                throw db_err{pb::Error::INVALID_REQUEST, "Invalid WorkSummaryRequest.Kind"};
            }

            // Query for the sum of duration and paused for that period
            auto res = co_await owner_.server().db().exec(
                "SELECT SUM(duration), SUM(paused) FROM work_session WHERE user=? AND start_time >= ? AND start_time < ? ",
                cuser, toAnsiTime(period.start, uctx->tz()), toAnsiTime(period.end, uctx->tz()));

            enum Cols { DURATION, PAUSED };
            if (res.has_value() && !res.rows().empty()) {
                auto& result = *reply->mutable_worksummary();
                result.set_start(period.start);
                result.set_end(period.end);
                result.set_duration(res.rows().front().at(DURATION).as_int64());
                result.set_paused(res.rows().front().at(PAUSED).as_int64());
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<pb::WorkSession> GrpcServer::fetchWorkSession(const std::string &uuid, const UserContext &uctx)
{
    auto res = co_await server().db().exec(
        format("SELECT {} from work_session where id=? and user = ?",
               ToWorkSession::selectCols), uuid, uctx.userUuid());
    if (!res.has_value() || res.rows().empty()) {
        throw db_err{pb::Error::NOT_FOUND, format("Work session {} not found", uuid)};
    }

    pb::WorkSession rval;
    ToWorkSession::assign(res.rows().front(), rval, uctx);

    co_return rval;
}

boost::asio::awaitable<std::optional<pb::WorkSession> > GrpcServer::fetchActiveWorkSession(const UserContext &uctx)
{
    std::optional<pb::WorkSession> rval;

    auto res = co_await server().db().exec(
        format("SELECT {} from work_session where user = ? and state='active'", ToWorkSession::selectCols), uctx.userUuid());
    if (!res.has_value() || res.rows().empty()) {
        co_return rval;
    }

    rval.emplace();
    ToWorkSession::assign(res.rows().front(), *rval, uctx);
    co_return rval;
}

boost::asio::awaitable<void> GrpcServer::endWorkSessionForAction(const std::string_view &actionId, const UserContext &uctx)
{
    auto res = co_await server().db().exec(
        format("SELECT {} from work_session where action = ? and user = ? and state in ('active', paused') ", ToWorkSession::selectCols), actionId, uctx.userUuid());
    if (res.has_value()) {
        for(const auto &row : res.rows()) {
            pb::WorkSession ws;
            ToWorkSession::assign(row, ws, uctx);

            // TODO: This should be made optional per users global settings
            const auto need_start_next = ws.state() == pb::WorkSession_State::WorkSession_State_ACTIVE;
            co_await stopWorkSession(ws, uctx);
            if (need_start_next) {
                co_await activateNextWorkSession(uctx);
            }
        }
    }
    co_return; // Make QT Creator happy
}

boost::asio::awaitable<void> GrpcServer::saveWorkSession(pb::WorkSession &work, const UserContext &uctx, bool touch)
{
    const auto& dbo = uctx.dbOptions();

    const auto blob = eventsToBlob(work);

    const auto now = time(nullptr);
    const string touched = touch ? format(", touch_time='{}'", *toAnsiTime(now, uctx.tz())) : ""s;

    if (work.has_end()) {
        // Is done
        assert(work.state() == pb::WorkSession::State::WorkSession_State_DONE);

        co_await server().db().exec(
            format("UPDATE work_session SET name=?, note=?, start_time=?, end_time=?, state='done', "
                "duration=?, paused=?, events=?, version=version+1 {} "
                "WHERE id=? AND user=?", touched),
            dbo,
            work.name(), work.notes(),
            toAnsiTime(work.start(), uctx.tz()), toAnsiTime(work.end(), uctx.tz()),
            work.duration(), work.paused(), blob, work.id(), uctx.userUuid());
    } else {
        // active or paused
        co_await server().db().exec(
            format("UPDATE work_session SET name=?, note=?, start_time = ?, duration = ?, paused = ?, events=?, state=?, "
                "version=version+1 {} "
                "WHERE id=? AND user=?", touched),
            dbo,
            work.name(), work.notes(),
            toAnsiTime(work.start(), uctx.tz()), work.duration(), work.paused(), blob,
            pb::WorkSession_State_Name(work.state()), work.id(),
            uctx.userUuid());
            }

    if (touch) {
        work.set_touched(now);
    }
}

/* This function is called when a work session is maked as done.

    - It calculates the outcome (duration, paused)
    - It updates the work_session record with the end time.
    - It adds the events to the work_session `events column.
    - It sets the state to done
*/
boost::asio::awaitable<void> GrpcServer::stopWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    auto dbopts = uctx.dbOptions();
    dbopts.reconnect_and_retry_query = false;

    // Add the final event to the list of events before we calculate the outcome
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_STOP));

    updateOutcome(work, uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_DONE);

    co_await saveWorkSession(work, uctx);

    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
    *update->mutable_work() = work;
    publish(update);
}

boost::asio::awaitable<void> GrpcServer::pauseWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    // TODO: use transaction
    auto dbopts = uctx.dbOptions();
    dbopts.reconnect_and_retry_query = false;

    // Add the final event to the list of events before we calculate the outcome
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_PAUSE));
    updateOutcome(work, uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_PAUSED);

    co_await saveWorkSession(work, uctx);

    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
    *update->mutable_work() = work;
    publish(update);
}

boost::asio::awaitable<void> GrpcServer::touchWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH));
    co_await saveWorkSession(work, uctx);
    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
    *update->mutable_work() = work;
    publish(update);
}

boost::asio::awaitable<void> GrpcServer::resumeWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    // TODO: use transaction
    auto dbopts = uctx.dbOptions();
    dbopts.reconnect_and_retry_query = false;

    if (auto current_ws = co_await fetchActiveWorkSession(uctx)) {
        co_await pauseWorkSession(*current_ws, uctx);
    }

    // Add the final event to the list of events before we calculate the outcome
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_RESUME));

    updateOutcome(work, uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_ACTIVE);

    co_await saveWorkSession(work, uctx);

    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
    *update->mutable_work() = work;
    publish(update);
}

void GrpcServer::updateOutcome(pb::WorkSession &work, const UserContext &uctx)
{
    // First event *must* be a start event
    if (work.events_size() == 0) {
        return; // Nothing to do
    }

    if (work.events(0).kind() != pb::WorkEvent::Kind::WorkEvent_Kind_START) {
        LOG_WARN << "First event in a work-session must be a start event. In work-session " << work.id()
                 << " The first event is a " << pb::WorkEvent::Kind_Name(work.events(0).kind());
        throw db_err{pb::Error::CONSTRAINT_FAILED, "First event in a work-session must be a start event"};
    }

    work.set_paused(0);
    work.set_duration(0);
    work.clear_end();

    time_t pause_from = 0;

    const auto end_pause = [&](const pb::WorkEvent& event) {
        if (pause_from > 0) {
            auto pduration = event.time() - pause_from;
            work.set_paused(work.paused() + pduration);
            pause_from = 0;
        }
    };

    unsigned row = 0;
    for(const auto &event : work.events()) {
        ++row;
        switch(event.kind()) {
        case pb::WorkEvent::Kind::WorkEvent_Kind_START:
            assert(row == 1);
            work.set_start(event.time());
            work.set_state(pb::WorkSession_State::WorkSession_State_ACTIVE);
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_STOP:
            end_pause(event);
            work.set_end(event.time());
            work.set_state(pb::WorkSession_State::WorkSession_State_DONE);
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_PAUSE:
            if (!pause_from) {
                pause_from = event.time();
            }
            work.set_state(pb::WorkSession_State::WorkSession_State_PAUSED);
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_RESUME:
            end_pause(event);
            work.set_state(pb::WorkSession_State::WorkSession_State_ACTIVE);
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH:
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION:
            if (event.has_start()) {
                work.set_start(event.start());
            }
            if (event.has_end()) {
                if (work.state() != pb::WorkSession_State::WorkSession_State_DONE) {
                    throw db_err{pb::Error::INVALID_REQUEST, "Cannot correct end time of an active session"};
                }
                work.set_end(event.end());
            }
            if (event.has_duration()) {
                work.set_duration(event.duration());
            }
            if (event.has_paused()) {
                work.set_paused(event.paused());
                if (pause_from) {
                    // Start the pause timer at the events time
                    pause_from = event.time();
                }
            }
            if (event.has_name()) {
                work.set_name(event.name());
            }
            if (event.has_notes()) {
                work.set_notes(event.notes());
            }
            break;
        default:
            assert(false);
            throw runtime_error{"Invalid work event kind"s + to_string(event.kind())};
        }
    }

    // Now set the duration. That is, the duration from start to end - paused
    if (work.has_end()) {
        work.set_duration(work.end() - work.start() - work.paused());
    } else {
        assert(work.state() != pb::WorkSession_State::WorkSession_State_DONE);
        work.set_duration(time({}) - work.start() - work.paused());
    }
}

boost::asio::awaitable<void> GrpcServer::activateNextWorkSession(const UserContext &uctx)
{
    // TODO: Use transaction
    // Validate that no work sessions are active
    auto res = co_await server().db().exec(
        "SELECT id FROM work_session WHERE user=? AND state='active'",
        uctx.userUuid());
    if (res.has_value() && !res.rows().empty()) {
        LOG_TRACE_N << "There is already an active work session for user "
                    << uctx.userUuid() << " - skipping activation of next work session";
        co_return;
    }

    // Find the next work session to activate
    auto next = co_await server().db().exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='paused' ORDER BY touch_time DESC LIMIT 1",
               ToWorkSession::selectCols),
        uctx.userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No paused work sessions found for user " << uctx.userUuid();
        co_return;
    }

    pb::WorkSession work;
    ToWorkSession::assign(next.rows().front(), work, uctx);
    work.set_state(pb::WorkSession_State::WorkSession_State_ACTIVE);

    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_RESUME));

    updateOutcome(work, uctx);
    co_await saveWorkSession(work, uctx);

    auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
    *update->mutable_work() = work;
    publish(update);
}

boost::asio::awaitable<void> GrpcServer::pauseWork(const UserContext &uctx)
{

    auto next = co_await server().db().exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='active'",
               ToWorkSession::selectCols),
        uctx.userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No active work sessions found for user " << uctx.userUuid();
        co_return;
    }

    // There is only one active work session if things works as supposed.
    for(const auto& row : next.rows()) {
        pb::WorkSession work;
        ToWorkSession::assign(row, work, uctx);
        co_await pauseWorkSession(work, uctx);
    }
}

boost::asio::awaitable<void> GrpcServer::syncActiveWork(const UserContext &uctx)
{
    auto next = co_await server().db().exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='active'",
               ToWorkSession::selectCols),
        uctx.userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No active work sessions found for user " << uctx.userUuid();
        co_return;
    }

    for(const auto& row : next.rows()) {
        pb::WorkSession work;
        ToWorkSession::assign(row, work, uctx);
        updateOutcome(work, uctx);
        co_await saveWorkSession(work, uctx);
    }
}


} // ns
