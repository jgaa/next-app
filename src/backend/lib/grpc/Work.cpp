
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

struct ToWorkSession {
    enum Cols {
        ID, ACTION, USER, START, END, DURATION, PAUSED, STATE, VERSION, EVENTS
    };

    static constexpr string_view selectCols = "id, action, user, start_time, end_time, duration, paused, state, version, events";

    static void assign(const boost::mysql::row_view& row, pb::WorkSession& ws) {
        ws.set_id(row.at(ID).as_string());
        ws.set_action(row.at(ACTION).as_string());
        ws.set_user(row.at(USER).as_string());
        ws.set_start(toTimeT(row.at(START).as_datetime()));
        if (row.at(END).is_datetime()) {
            ws.set_end(toTimeT(row.at(END).as_datetime()));
        }
        ws.set_duration(row.at(DURATION).as_int64());
        ws.set_paused(row.at(PAUSED).as_int64() != 0);
        ws.set_version(row.at(VERSION).as_int64());

        if (!row.at(EVENTS).is_null()) {
            auto blob = row.at(EVENTS).as_blob();
            pb::SavedWorkEvents events;
            if (events.ParseFromArray(blob.data(), blob.size())) {
                *ws.mutable_events() = std::move(events.events());
            } else {
                LOG_WARN_N << "Failed to parse WorkSession.events";
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
    }
};

struct ToWorkEvent {
    enum Cols {
        ID, SESSION, KIND, TIME, C_START, C_END, C_DURATION, C_PAUSED
    };

    static constexpr string_view selectCols = "id, session, kind, event_time, start_time, end_time, duration, paused";

    static void assign(const boost::mysql::row_view& row, pb::WorkEvent& we) {
        we.set_id(row.at(ID).as_string());
        we.set_session(row.at(SESSION).as_string());
        we.set_time(toTimeT(row.at(TIME).as_datetime()));

        {
            pb::WorkEvent_Kind kind;
            const auto name = toUpper(row.at(KIND).as_string());
            if (pb::WorkEvent_Kind_Parse(name, &kind)) {
                we.set_kind(kind);
            } else {
                LOG_WARN_N << "Invalid WorkEvent.Kind: " << name;
            }
        }

        // Corrections
        if (row.at(C_START).is_int64()) {
            we.set_start(toTimeT(row.at(C_START).as_datetime()));
        }
        if (row.at(C_END).is_int64()) {
            we.set_end(toTimeT(row.at(C_END).as_datetime()));
        }

        if (row.at(C_DURATION).is_int64()) {
            we.set_duration(row.at(C_DURATION).as_int64());
        }

        if (row.at(C_PAUSED).is_int64()) {
            we.set_paused(row.at(C_PAUSED).as_int64() != 0);
        }
    }
};

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateWorkSession(::grpc::CallbackServerContext *ctx, const pb::CreateWorkReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            auto dbopts = cutx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            co_await owner_.validateAction(req->actionid(), cuser);

            // TODO: Execute in transaction
            // Create the work session record
            const auto res = co_await owner_.server().db().exec(
                format("INSERT INTO work_session (action, user) VALUES (?,?) "
                       "RETURNING {}", ToWorkSession::selectCols),
                dbopts,
                req->actionid(),
                cuser);

            assert(res.has_value() && !res.rows().empty());

            pb::WorkSession session;
            ToWorkSession::assign(res.rows().front(), session);

            // Add the START event
            const auto evres = co_await owner_.server().db().exec(
                format("INSERT INTO work_event (session, action, kind) VALUES (?, ?, 'start') "
                       "RETURNING {}", ToWorkEvent::selectCols),  dbopts, session.id(), session.action());

            assert(evres.has_value() && !evres.rows().empty());
            pb::WorkEvent we;
            ToWorkEvent::assign(evres.rows().front(), we);

            // Add the start event to the list of the session we return and publish
            *session.mutable_events()->Add() = std::move(we);

            auto& final_session = *reply->mutable_work();
            final_session = std::move(session);

            auto update = make_shared<pb::Update>();
            update->set_op(pb::Update::Operation::Update_Operation_ADDED);
            *update->mutable_work() = final_session;
            owner_.publish(update);

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::AddWorkEvent(::grpc::CallbackServerContext *ctx, const pb::WorkEvent *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            auto dbopts = cutx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            auto update = make_shared<pb::Update>();
            update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
            auto work = co_await owner_.fetchWorkSession(req->session(), *cutx, true);

            // Here we do the logic. When the sitch exits, `work` is assumed to be updated
            switch(req->kind()) {
            case pb::WorkEvent::Kind::WorkEvent_Kind_START:
                throw db_err{pb::Error::INVALID_REQUEST, "Use CreateWorkSession to start a new session"};
            case pb::WorkEvent::Kind::WorkEvent_Kind_STOP:
                if (work.has_end()) {
                    throw db_err{pb::Error::INVALID_REQUEST, "Session is already stopped"};
                }
                co_await owner_.stopWorkSession(work, *cutx);
                break;
            }

            *update->mutable_work() = work;
            owner_.publish(update);
            *reply->mutable_work() = std::move(work);
            co_return;
        });
}

boost::asio::awaitable<pb::WorkSession> GrpcServer::fetchWorkSession(const std::string &uuid, const UserContext &uctx, bool includeEvents)
{
    auto res = co_await server().db().exec(
        format("SELECT {} from work_session where id=? and user = ?",
               ToWorkSession::selectCols), uuid, uctx.userUuid());
    if (!res.has_value() || res.rows().empty()) {
        throw db_err{pb::Error::NOT_FOUND, format("Work session {} not found", uuid)};
    }

    pb::WorkSession rval;
    ToWorkSession::assign(res.rows().front(), rval);

    if (includeEvents) {
        auto evres = co_await server().db().exec(format("SELECT {} from work_event where session=? ORDER BY event_time", ToWorkEvent::selectCols), uuid);
        if (evres.has_value()) {
            for(const auto& row : evres.rows()) {
                pb::WorkEvent we;
                ToWorkEvent::assign(row, we);
                *rval.mutable_events()->Add() = std::move(we);
            }
        }
    }

    co_return rval;
}

boost::asio::awaitable<void> GrpcServer::saveWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    const auto& dbo = uctx.dbOptions();

    pb::SavedWorkEvents events;
    *events.mutable_events() = work.events();
    string events_buffer;
    events.SerializeToString(&events_buffer);
    boost::mysql::blob_view blob{reinterpret_cast<const unsigned char *>(events_buffer.data()), events_buffer.size()};

    if (work.has_end()) {
        // Is done
        assert(work.state() == pb::WorkSession::State::WorkSession_State_DONE);

        co_await server().db().exec(
            "UPDATE work_session SET start_time=?, end_time=?, state='done', "
            "duration=?, paused=?, events=?, version=version+1 "
            "WHERE id=? AND user=?",
            dbo,
            toAnsiTime(work.start(), uctx.tz()), toAnsiTime(work.end(), uctx.tz()),
            work.duration(), work.paused(), blob, work.id(), uctx.userUuid());
    } else {
        // active or paused
        co_await server().db().exec(
            "UPDATE work_session SET start_time = ?, duration = ?, paused = ?, events=?, state=?, version=version+1 "
            "WHERE id=? AND user=?",
            dbo,
            toAnsiTime(work.start(), uctx.tz()), work.duration(), work.paused(), blob,
            pb::WorkSession_State_Name(work.state()), work.id(),
            uctx.userUuid());
            }
}

/* This function is called when a work session is maked as done.

    - It calculates the outcome (duration, paused)
    - It updates the work_session record with the end time.
    - It adds the events to the work_session `events column.
    - It sets the state to done
    - It deletes the records in the work_events table - if configured to do so.
*/
boost::asio::awaitable<void> GrpcServer::stopWorkSession(pb::WorkSession &work, const UserContext &uctx)
{
    auto dbopts = uctx.dbOptions();
    dbopts.reconnect_and_retry_query = false;

    // Write the final event to the database. That way we use the db servers clock for all the events.
    const auto evres = co_await server().db().exec(
        format("INSERT INTO work_event (session, action, kind) VALUES (?, ?, 'stop')"
               "RETURNING {}", ToWorkEvent::selectCols),  dbopts, work.id(), work.action());

    assert(evres.has_value() && !evres.rows().empty());
    pb::WorkEvent we;
    ToWorkEvent::assign(evres.rows().front(), we);

    // Add the final event to the list of events before we calculate the outcome
    *work.mutable_events()->Add() = std::move(we);

    updateOutcome(work, uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_DONE);

    co_await saveWorkSession(work, uctx);

    if (server().config().options.delete_work_events_from_db) {
        LOG_TRACE_N << "Deleting events from work_event table for work session " << work.id();
        co_await server().db().exec("DELETE FROM work_event WHERE session=?", work.id());
    }

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

            //TODO: Add corrections

        default:
            assert(false);
            throw runtime_error{"Invalid work event kind"s + to_string(event.kind())};
        }
    }
}


} // ns
