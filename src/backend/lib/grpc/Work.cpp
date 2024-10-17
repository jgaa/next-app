
#include <ranges>

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

auto eventsToBlob(const pb::WorkSession& ws) {
    pb::SavedWorkEvents events;
    *events.mutable_events() = ws.events();
    return toBlob(events);
}

struct ToWorkSession {
    enum Cols {
        ID, ACTION, USER, START, END, DURATION, PAUSED, STATE, VERSION, TOUCHED, NAME, NOTES, EVENTS, UPDATED
    };

    static constexpr string_view selectCols = "id, action, user, start_time, end_time, duration, paused, state, version, touch_time, "
                                              "name, note, events, updated";


    static void assign(const boost::mysql::row_view& row, pb::WorkSession& ws, const UserContext& uctx) {
        ws.set_id(pb_adapt(row.at(ID).as_string()));
        ws.set_action(pb_adapt(row.at(ACTION).as_string()));
        ws.set_user(pb_adapt(row.at(USER).as_string()));
        if (row.at(START).is_datetime()) {
            ws.set_start(toTimeT(row.at(START).as_datetime(), uctx.tz()));
        } else {
            ws.set_start(0);
        }
        ws.set_touched(toTimeT(row.at(TOUCHED).as_datetime(), uctx.tz()));
        if (row.at(END).is_datetime()) {
            ws.set_end(toTimeT(row.at(END).as_datetime(), uctx.tz()));
        }
        ws.set_duration(row.at(DURATION).as_int64());
        ws.set_paused(row.at(PAUSED).as_int64() != 0);
        ws.set_version(row.at(VERSION).as_int64());
        ws.set_name(pb_adapt(row.at(NAME).as_string()));
        if (row.at(NOTES).is_string()) {
            ws.set_notes(pb_adapt(row.at(NOTES).as_string()));
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

        ws.set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), uctx.tz()));

        //LOG_TRACE_N << "Loaded WorkSession: " << toJson(ws, 2);
    }
};

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateWorkSession(::grpc::CallbackServerContext *ctx,
                                                                       const pb::CreateWorkReq *req,
                                                                       pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            auto dbopts = rctx.uctx->dbOptions();
            dbopts.reconnect_and_retry_query = false;
            auto trx = co_await rctx.dbh->transaction();

            string name;

            vector<string> actions;

            if (req->has_actionid()) {
                actions.push_back(req->actionid());
            } else if (req->has_timeblockid()) {
                auto res = co_await rctx.dbh->exec(
                    R"(SELECT tba.action
                        FROM time_block_actions tba
                        JOIN action a ON tba.action = a.id
                        WHERE tba.time_block=? AND a.status = 'active')",
                    dbopts, req->timeblockid());
                if (res.has_value() && !res.rows().empty()) {
                    actions.reserve(res.rows().size());
                    for(const auto& row : res.rows()) {
                        actions.push_back(row.at(0).as_string());
                    }
                }
            }

            auto res = co_await rctx.dbh->exec(
                "SELECT action FROM work_session WHERE user=? and state!='done'", dbopts, cuser);
            if (res.has_value()) {
                for(const auto& r : res.rows()) {
                    const auto id = r.at(0).as_string();
                    if (auto it = std::ranges::find(actions, id); it != actions.end()) {
                        actions.erase(it); // Avoid duplicates
                    }
                }
            }

            if (actions.empty()) {
                throw server_err{pb::Error::NO_RELEVANT_DATA, "No new actions to add to work sessions"};
            }

            rctx.updates.reserve(actions.size());
            const auto opt_autostart_session = rctx.uctx->settings().autostartnewworksession();

            int count = 0;
            for(const auto& actionId : actions) {
                co_await owner_.validateAction(*rctx.dbh, actionId, cuser, &name);
                pb::WorkSession init;
                init.set_name(name);
                init.set_action(actionId);

                optional<pb::WorkSession> active_session;
                if (opt_autostart_session) {
                    // Don't start a new session if we alreadyt have an active session
                    active_session = co_await owner_.fetchActiveWorkSession(rctx);
                }
                const bool do_start_event = !active_session && opt_autostart_session && (++count == 1);

                auto res = co_await owner_.insertWork(init, rctx, do_start_event);

                assert(res.has_value() && !res.rows().empty());

                auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
                auto& work = *update.mutable_work();
                ToWorkSession::assign(res.rows().front(), work, *rctx.uctx);
            }

            co_await trx.commit();

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::AddWorkEvent(::grpc::CallbackServerContext *ctx,
                                                                  const pb::AddWorkEventReq *req,
                                                                  pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            auto dbopts = rctx.uctx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            auto trx = co_await rctx.dbh->transaction();

            auto work = co_await owner_.fetchWorkSession(req->worksessionid(), rctx);
            for(const auto &event : req->events()) {

                LOG_TRACE_N << "Event: " << owner_.toJsonForLog(event);

                // Here we do the logic. When the sitch exits, `work` is assumed to be updated
                switch(event.kind()) {
                case pb::WorkEvent::Kind::WorkEvent_Kind_START:
                    throw server_err{pb::Error::INVALID_REQUEST, "Use CreateWorkSession to start a new session"};
                case pb::WorkEvent::Kind::WorkEvent_Kind_STOP:
                    if (work.state() == pb::WorkSession_State::WorkSession_State_DONE) {
                        throw server_err{pb::Error::INVALID_REQUEST, "Session is already stopped"};
                    }
                    if (work.has_end()) {
                        LOG_ERROR << "Work-Session " << work.id() << " already has an end time, but it is not DONE.";
                    }
                    co_await owner_.stopWorkSession(work, rctx, &event);
                    if (rctx.uctx->settings().autostartnextworksession()) {
                        co_await owner_.activateNextWorkSession(rctx);
                    }
                    break;
                case pb::WorkEvent::Kind::WorkEvent_Kind_PAUSE:
                    if (work.state() != pb::WorkSession_State::WorkSession_State_ACTIVE) {
                        throw server_err{pb::Error::INVALID_REQUEST, "Session must be active to pause"};
                    }
                    co_await owner_.pauseWorkSession(work, rctx);
                    break;
                case pb::WorkEvent::Kind::WorkEvent_Kind_RESUME:
                    if (work.state() != pb::WorkSession_State::WorkSession_State_PAUSED) {
                        throw server_err{pb::Error::INVALID_REQUEST, "Session must be paused to resume"};
                    }
                    co_await owner_.resumeWorkSession(work, rctx);
                    break;
                case pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH: {
                        auto ne = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH);
                        work.add_events()->Swap(&ne);
                        co_await owner_.saveWorkSession(work, rctx);
                        rctx.dbh.reset();
                        auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                        *update.mutable_work() = work;
                    } break;
                case pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION: {
                    auto ne = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION);
                    if (event.has_start()) {
                        ne.set_start(event.start());
                    }
                    if (event.has_end()) {
                        if (work.state() != pb::WorkSession_State::WorkSession_State_DONE) {
                            throw server_err{pb::Error::INVALID_REQUEST, "Can not correct end time on an active session"};
                        }
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
                    updateOutcome(work, *rctx.uctx);
                    co_await owner_.saveWorkSession(work, rctx, false);
                    rctx.dbh.reset();
                    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                    *update.mutable_work() = work;
                } break;
                default:
                    assert(false);
                    throw server_err{pb::Error::INVALID_REQUEST, "Invalid WorkEvent.Kind"};
                }
            } // events

            // If it's not committed by now, commit!
            assert(!trx.empty());
            co_await trx.commit();

            *reply->mutable_work() = std::move(work);
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::ListCurrentWorkSessions(::grpc::CallbackServerContext *ctx,
                                                                             const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
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
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            // Update the active work session to calculate the current duration and pause
            co_await owner_.syncActiveWork(rctx);

            // Calculate the start and end timestamps
            TimePeriod period = toTimePeriod(time({}), *rctx.uctx, req->kind());

            // Query for the sum of duration and paused for that period
            auto res = co_await rctx.dbh->exec(
                "SELECT SUM(duration), SUM(paused) FROM work_session WHERE user=? AND start_time >= ? AND start_time < ? ",
                cuser, toAnsiTime(period.start, rctx.uctx->tz()), toAnsiTime(period.end, rctx.uctx->tz()));
            rctx.dbh.reset();

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

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteWorkSession(::grpc::CallbackServerContext *ctx,
                                                                       const pb::DeleteWorkReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx, req] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            auto dbopts = uctx->dbOptions();
            dbopts.reconnect_and_retry_query = false;

            auto res = co_await owner_.server().db().exec(
                "DELETE FROM work_session WHERE id=? AND user=?", req->worksessionid(), cuser);

            if (res.affected_rows()) {
                pb::WorkSession ws;
                ws.set_id(req->worksessionid());
                auto update = newUpdate(pb::Update::Operation::Update_Operation_DELETED);
                *update->mutable_work() = ws;
                rctx.publishLater(update);
                *reply->mutable_work() = ws;
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetWorkSessions(::grpc::CallbackServerContext *ctx, const pb::GetWorkSessionsReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx, req] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto &dbopts = uctx->dbOptions();

        uint32_t limit = 250;
        if (auto page_size = req->page().pagesize(); page_size > 0) {
            limit = min(page_size, limit);
        }

        // We are building a dynamic query.
        // We need to add the arguments in the same order as we add '?' to the query
        // as boost.mysql lacks the ability to bind by name.
        std::vector<boost::mysql::field_view> args;
        std::string query;
        std::string_view sortcol;
        static const string prefixed_cols = prefixNames(ToWorkSession::selectCols, "ws.");
        bool first_page = true;

        switch(req->sortcols()) {
            case pb::GetWorkSessionsReq_SortCols::GetWorkSessionsReq_SortCols_FROM_TIME:
                sortcol = "UNIX_TIMESTAMP(ws.start_time)";
                break;
            case pb::GetWorkSessionsReq_SortCols::GetWorkSessionsReq_SortCols_CHANGE_TIME:
                sortcol = "UNIX_TIMESTAMP(ws.touch_time)";
                break;
            default:
                throw server_err{pb::Error::INVALID_REQUEST, "Invalid sort column: #"s + to_string(req->sortcols())};
        }

        if (req->has_nodeid()) {
            query = format(R"(WITH RECURSIVE node_tree AS (
            SELECT id FROM node WHERE id=? and user=?
                UNION ALL
                SELECT n.id FROM node n INNER JOIN node_tree nt ON n.parent = nt.id
            )
            SELECT {}, {} AS sort_ts
            FROM work_session ws
            INNER JOIN action a ON ws.action = a.id
            INNER JOIN node_tree nt ON a.node = nt.id)",
                           prefixed_cols, sortcol);
            args.emplace_back(req->nodeid());
            args.emplace_back(cuser);
        } else {
            query = format("SELECT {}, {} AS sort_ts FROM work_session ws WHERE user=? ",
                           prefixed_cols, sortcol);
            args.emplace_back(cuser);
        }

        if (req->has_actionid()) {
            query += " AND ws.action=? ";
            args.emplace_back(req->actionid());
        }

        auto start_time = req->page().has_prevtime() ? toAnsiTime(req->page().prevtime(), uctx->tz()) : std::nullopt;
        if (start_time) {
            query += format(" AND ws.start_time {} ? ",
                            req->sortorder() == pb::SortOrder::ASCENDING ? ">=" : "<=");
            args.emplace_back(*start_time);
        }

        optional<string> span_start, span_end;
        if (req->has_timespan()) {
            span_start = toAnsiTime(req->timespan().start(), uctx->tz());
            span_end = toAnsiTime(req->timespan().end(), uctx->tz());
            query += " AND ws.start_time >= ? AND ws.start_time < ? ";
            args.emplace_back(*span_start);
            args.emplace_back(*span_end);
        }

        query += format(" ORDER BY sort_ts {}, ws.id LIMIT ?",
                        // ASC/DESC cannot be a bind argument.
                        req->sortorder() == pb::SortOrder::ASCENDING ? "ASC" : "DESC");
        args.emplace_back(limit);

        const auto res = co_await owner_.server().db().exec(query, dbopts, args);

        if (res.has_value()) {
            const auto rows_count = res.rows().size();
            const auto has_more = rows_count >= limit;
            size_t rows = 0;
            reply->set_hasmore(has_more);
            auto sessions = reply->mutable_worksessions()->mutable_sessions();
            for(const auto& row : res.rows()) {
                if (++rows > limit) {
                    assert(has_more && reply->has_hasmore());

                    if (req->page().has_prevtime()) {
                        pb::WorkSession ws;
                        ToWorkSession::assign(row, ws, *uctx);

                        if (ws.start() == req->page().prevtime()) {
                            // Avoid an endless request-loop over the same timestamp, if the user has re-used it for more sessions than the page size!
                            throw server_err{pb::Error::PAGE_SIZE_TOO_SMALL, "There are more rows with the same start_time than the page size"};
                        }
                    }

                    break;
                }
                ToWorkSession::assign(row, *sessions->Add(), *uctx);
            }
        }

        co_return;

    }, __func__);
}
boost::asio::awaitable<boost::mysql::results> GrpcServer::insertWork(const pb::WorkSession &work,
                                                                     RequestCtx& rctx,
                                                                     bool addStartEvent)
{
    const auto& cuser = rctx.uctx->userUuid();
    auto dbopts = rctx.uctx->dbOptions();
    dbopts.reconnect_and_retry_query = false;

    time_t start_t{};
    string_view state = "active";
    pb::SavedWorkEvents events;
    if (addStartEvent) {
        events.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_START));
        start_t = events.events(0).time();
    } else {
        state = "paused";
    }


    const auto start_time = toAnsiTime(start_t, rctx.uctx->tz());
    const auto touch_time = toAnsiTime(start_t ? start_t : time({}), rctx.uctx->tz());
    const auto blob = toBlob(events);
    // Create the work session record
    const auto res = co_await rctx.dbh->exec(
        format("INSERT INTO work_session (start_time, touch_time, action, user, name, events, state) VALUES (?, ?, ?, ?, ?, ?, ?) "
               "RETURNING {}", ToWorkSession::selectCols),
        dbopts,
        start_time,
        touch_time,
        work.action(),
        cuser,
        work.name(),
        blob,
        state);

    co_return std::move(res);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::AddWork(::grpc::CallbackServerContext *ctx,
                                                             const pb::AddWorkReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto trx = co_await rctx.dbh->transaction();

            auto work = req->work();
            string action_name;
            co_await owner_.validateAction(*rctx.dbh, work.action(), cuser, &action_name);
            if (!work.user().empty() && work.user() != cuser) {
                throw server_err{pb::Error::INVALID_REQUEST, "Invalid user"};
            }
            work.set_user(cuser);
            if (work.name().empty()) {
                work.set_name(action_name);
            }

            // Re-use our method to add a work-session to the database.
            auto res = co_await owner_.insertWork(work, rctx, false /* don't add start event */);
            if (!res.has_value() || res.rows().empty()) {
                throw server_err{pb::Error::GENERIC_ERROR, "Failed to fetch inserted work session"};
            }

            // Pause in a "fixed" session without a series of events must be added as a correction
            if (const auto duration = work.paused()) {
                auto ev = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_CORRECTION);
                ev.set_paused(duration);
                work.add_events()->Swap(&ev);
            }


            updateOutcome(work, *rctx.uctx);

            const auto id = res.rows().front().at(ToWorkSession::ID).as_string();
            work.set_id(pb_adapt(id));

            // Save the work session again to get all the columns saved.
            co_await owner_.saveWorkSession(work, rctx);

            co_await trx.commit();

            auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
            *update.mutable_work() = work;

            *reply->mutable_work() = std::move(work);
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetDetailedWorkSummary(::grpc::CallbackServerContext *ctx, const pb::DetailedWorkSummaryRequest *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto &dbopts = rctx.uctx->dbOptions();

            // Update the active work session to calculate the current duration
            co_await owner_.syncActiveWork(rctx);

            TimePeriod period = toTimePeriod(req->start(), *rctx.uctx, req->kind());

            // SUM() returns a DECIMAL type, so we need to cast it to an INTEGER for simplicity.
            // boost.mysql returns a string if we don't cast it.
            auto query = R"(SELECT CAST(SUM(ws.duration) AS INTEGER), DATE(ws.start_time) AS date, a.id, n.id FROM work_session ws
                INNER JOIN action a ON a.id = ws.action
                INNER JOIN node as n ON n.id = a.node
                WHERE ws.user = ? AND ws.start_time >= ? AND ws.start_time < ?
                GROUP BY date, a.id, n.id;)";

            auto res = co_await rctx.dbh->exec(query, dbopts,
                cuser, toAnsiTime(period.start, rctx.uctx->tz()), toAnsiTime(period.end, rctx.uctx->tz()));
            rctx.dbh.reset();

            enum Cols { DURATION, DATE, ACTION, NODE };
            if (res.has_value()) {
                auto& result = *reply->mutable_detailedworksummary();
                for(const auto &row : res.rows()) {
                    auto& item = *result.add_items();
                    item.set_duration(row.at(DURATION).as_int64());
                    *item.mutable_date() = toDate(row.at(DATE).as_date());
                    item.set_action(pb_adapt(row.at(ACTION).as_string()));
                    item.set_node(pb_adapt(row.at(NODE).as_string()));
                }
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<pb::WorkSession> GrpcServer::fetchWorkSession(const std::string &uuid, RequestCtx& rctx)
{
    auto res = co_await rctx.dbh->exec(
        format("SELECT {} from work_session where id=? and user = ?",
               ToWorkSession::selectCols), uuid, rctx.uctx->userUuid());
    if (!res.has_value() || res.rows().empty()) {
        throw server_err{pb::Error::NOT_FOUND, format("Work session {} not found", uuid)};
    }

    pb::WorkSession rval;
    ToWorkSession::assign(res.rows().front(), rval, *rctx.uctx);

    co_return rval;
}

boost::asio::awaitable<std::optional<pb::WorkSession> > GrpcServer::fetchActiveWorkSession(RequestCtx& rctx)
{
    std::optional<pb::WorkSession> rval;

    auto res = co_await rctx.dbh->exec(
        format("SELECT {} from work_session where user = ? and state='active'", ToWorkSession::selectCols),
        rctx.uctx->userUuid());
    if (!res.has_value() || res.rows().empty()) {
        co_return rval;
    }

    rval.emplace();
    ToWorkSession::assign(res.rows().front(), *rval, *rctx.uctx);
    co_return rval;
}

boost::asio::awaitable<void> GrpcServer::endWorkSessionForAction(const std::string_view &actionId, RequestCtx& rctx)
{
    auto res = co_await rctx.dbh->exec(
        format("SELECT {} from work_session where action = ? and user = ? and state in ('active', 'paused') ",
               ToWorkSession::selectCols), actionId, rctx.uctx->userUuid());
    if (res.has_value()) {
        bool was_active = false;
        for(const auto &row : res.rows()) {
            pb::WorkSession ws;
            ToWorkSession::assign(row, ws, *rctx.uctx);
            if (ws.state() == pb::WorkSession_State::WorkSession_State_ACTIVE) {
                was_active = true;
            }
            co_await stopWorkSession(ws, rctx);
        }

        if (was_active) {
            // TODO: This should be made optional per users global settings
            if (was_active) {
                co_await activateNextWorkSession(rctx);
            }
        }
    }
    co_return; // Make QT Creator happy
}

boost::asio::awaitable<void> GrpcServer::saveWorkSession(pb::WorkSession &work, RequestCtx&  rctx, bool touch)
{
    const auto& dbo = rctx.uctx->dbOptions();
    const auto blob = eventsToBlob(work);
    const auto now = time(nullptr);
    const string touched = touch ? format(", touch_time='{}'", *toAnsiTime(now, rctx.uctx->tz())) : ""s;

    if (work.has_end()) {
        // Is done
        assert(work.state() == pb::WorkSession::State::WorkSession_State_DONE);

        co_await rctx.dbh->exec(
            format("UPDATE work_session SET name=?, note=?, start_time=?, end_time=?, state='done', "
                "duration=?, paused=?, events=?, version=version+1 {} "
                "WHERE id=? AND user=?", touched),
            dbo,
            work.name(), work.notes(),
            toAnsiTime(work.start(), rctx.uctx->tz()), toAnsiTime(work.end(), rctx.uctx->tz()),
            work.duration(), work.paused(), blob, work.id(), rctx.uctx->userUuid());
    } else {
        // active or paused
        co_await rctx.dbh->exec(
            format("UPDATE work_session SET name=?, note=?, start_time = ?, duration = ?, paused = ?, events=?, state=?, "
                "version=version+1 {} "
                "WHERE id=? AND user=?", touched),
            dbo,
            work.name(), work.notes(),
            toAnsiTime(work.start(), rctx.uctx->tz()), work.duration(), work.paused(), blob,
            pb::WorkSession_State_Name(work.state()), work.id(),
            rctx.uctx->userUuid());
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
boost::asio::awaitable<void> GrpcServer::stopWorkSession(pb::WorkSession &work,
                                                         RequestCtx& rctx,
                                                         const pb::WorkEvent *event)
{
    auto dbopts = rctx.uctx->dbOptions();
    dbopts.reconnect_and_retry_query = false;

    // Add the final event to the list of events before we calculate the outcome
    auto stopevent = createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_STOP);
    if (event && event->has_end()) {
        stopevent.set_end(event->end());
    }
    work.mutable_events()->Add(std::move(stopevent));

    updateOutcome(work, *rctx.uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_DONE);

    co_await saveWorkSession(work, rctx);

    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
    *update.mutable_work() = work;
}

boost::asio::awaitable<void> GrpcServer::pauseWorkSession(pb::WorkSession &work, RequestCtx&  rctx)
{
    // TODO: use transaction
    auto dbopts = rctx.uctx->dbOptions();
    dbopts.reconnect_and_retry_query = false;

    // Add the final event to the list of events before we calculate the outcome
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_PAUSE));
    updateOutcome(work, *rctx.uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_PAUSED);

    co_await saveWorkSession(work, rctx);

    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
    *update.mutable_work() = work;
}

boost::asio::awaitable<void> GrpcServer::touchWorkSession(pb::WorkSession &work, RequestCtx&  rctx)
{
    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_TOUCH));
    co_await saveWorkSession(work, rctx);
    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
    *update.mutable_work() = work;
}

boost::asio::awaitable<void> GrpcServer::resumeWorkSession(pb::WorkSession &work, RequestCtx&  rctx, bool makeUpdate)
{
    // TODO: use transaction
    auto dbopts = rctx.uctx->dbOptions();
    dbopts.reconnect_and_retry_query = false;

    if (auto current_ws = co_await fetchActiveWorkSession(rctx)) {
        co_await pauseWorkSession(*current_ws, rctx);
    }

    // Check if we have an initial start event
    if (std::ranges::any_of(work.events(), [](const auto& v) {
            return v.kind() == pb::WorkEvent::Kind::WorkEvent_Kind_START;
        })) {
        // Add the event to the end of the list of events before we calculate the outcome
        work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_RESUME));
    } else {
        // If there is no start event, we need to add one
        LOG_TRACE_N << "Adding START event in stead of RESUME. There was no initial start event.";
        work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_START));
    }

    updateOutcome(work, *rctx.uctx);
    assert(work.state() == pb::WorkSession_State::WorkSession_State_ACTIVE);

    co_await saveWorkSession(work, rctx);

    if (makeUpdate) {
        auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
        *update.mutable_work() = work;
    }

    co_return;
}

void GrpcServer::updateOutcome(pb::WorkSession &work, const UserContext& uctx)
{
    // First event *must* be a start event
    if (work.events_size() == 0) {
        return; // Nothing to do
    }

    work.set_start(0);
    work.set_paused(0);
    work.set_duration(0);

    if (work.state() != pb::WorkSession_State::WorkSession_State_DONE) {
        work.clear_end();
    }
    const auto orig_state = work.state();

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
            work.set_start(event.time());
            work.set_state(pb::WorkSession_State::WorkSession_State_ACTIVE);
            break;
        case pb::WorkEvent::Kind::WorkEvent_Kind_STOP:
            end_pause(event);
            if (event.has_end()) {
                work.set_end(event.end());
            } else {
                work.set_end(event.time());
            }
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
                    throw server_err{pb::Error::INVALID_REQUEST, "Cannot correct end time of an active session"};
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
    if (!work.start()) [[unlikely]] {
        LOG_TRACE_N << "WorkSession " << work.id() << " has no start time";
        work.set_duration(0);
        work.set_paused(0);
    } else if (work.has_end()) {
        if (work.end() <= work.start()) {
            work.set_duration(0);
        } else {
            auto duration = work.end() - work.start();
            work.set_duration(std::max<long>(0, duration - work.paused()));
        }
    } else {
        assert(work.state() != pb::WorkSession_State::WorkSession_State_DONE);
        const auto now = time({});
        if (now <= work.start()) {
            work.set_duration(0);
        } else {
            work.set_duration(std::min<long>(std::max<long>(0, (now - work.start()) - work.paused()), 3600 * 24 *7));
        }
    }

    if (orig_state == pb::WorkSession_State::WorkSession_State_DONE) {
        work.set_state(orig_state);
    }
}

boost::asio::awaitable<void> GrpcServer::activateNextWorkSession(RequestCtx& rctx)
{
    // Validate that no work sessions are active
    auto res = co_await rctx.dbh->exec(
        "SELECT id FROM work_session WHERE user=? AND state='active'",
        rctx.uctx->userUuid());
    if (res.has_value() && !res.rows().empty()) {
        LOG_TRACE_N << "There is already an active work session for user "
                    << rctx.uctx->userUuid() << " - skipping activation of next work session";
        co_return;
    }

    // Find the next work session to activate
    auto next = co_await rctx.dbh->exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='paused' ORDER BY touch_time DESC LIMIT 1",
               ToWorkSession::selectCols),
        rctx.uctx->userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No paused work sessions found for user " << rctx.uctx->userUuid();
        co_return;
    }

    pb::WorkSession work;
    ToWorkSession::assign(next.rows().front(), work, *rctx.uctx);
    work.set_state(pb::WorkSession_State::WorkSession_State_ACTIVE);

    work.mutable_events()->Add(createWorkEvent(pb::WorkEvent::Kind::WorkEvent_Kind_RESUME));

    updateOutcome(work, *rctx.uctx);
    co_await saveWorkSession(work, rctx);

    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
    *update.mutable_work() = work;
}

boost::asio::awaitable<void> GrpcServer::pauseWork(RequestCtx&  rctx)
{

    auto next = co_await rctx.dbh->exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='active'",
               ToWorkSession::selectCols),
        rctx.uctx->userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No active work sessions found for user " << rctx.uctx->userUuid();
        co_return;
    }

    // There is only one active work session if things works as supposed.
    for(const auto& row : next.rows()) {
        pb::WorkSession work;
        ToWorkSession::assign(row, work, *rctx.uctx);
        co_await pauseWorkSession(work, rctx);
    }
}

boost::asio::awaitable<void> GrpcServer::syncActiveWork(RequestCtx&  rctx)
{
    auto next = co_await rctx.dbh->exec(
        format("SELECT {} FROM work_session WHERE user=? AND state='active'",
               ToWorkSession::selectCols),
        rctx.uctx->userUuid());

    if (!next.has_value() || next.rows().empty()) {
        LOG_TRACE_N << "No active work sessions found for user " << rctx.uctx->userUuid();
        co_return;
    }

    for(const auto& row : next.rows()) {
        pb::WorkSession work;
        ToWorkSession::assign(row, work, *rctx.uctx);
        updateOutcome(work, *rctx.uctx);
        co_await saveWorkSession(work, rctx);
    }
}

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::GetNewWork(
    ::grpc::CallbackServerContext *ctx, const pb::GetNewReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();
        const auto uctx = rctx.uctx;
        const auto& cuser = uctx->userUuid();
        const auto batch_size = owner_.server().config().options.stream_batch_size;

        // Use batched reading from the database, so that we can get all the data, but
        // without running out of memory.
        // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
        assert(rctx.dbh);
        co_await  rctx.dbh->start_exec(
            format("SELECT {} from work_session WHERE user=? AND updated > ?", ToWorkSession::selectCols),
            uctx->dbOptions(), cuser, toMsDateTime(req->since(), uctx->tz()));

        nextapp::pb::Status reply;

        auto *ws = reply.mutable_worksessions();
        auto num_rows_in_batch = 0u;
        auto total_rows = 0u;
        auto batch_num = 0u;

        auto flush = [&]() -> boost::asio::awaitable<void> {
            reply.set_error(::nextapp::pb::Error::OK);
            assert(reply.has_worksessions());
            ++batch_num;
            reply.set_message(format("Fetched {} work-sessions in batch {}", reply.worksessions().sessions_size(), batch_num));
            co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
            reply.Clear();
            ws = reply.mutable_worksessions();
            num_rows_in_batch = {};
        };

        bool read_more = true;
        for(auto rows = co_await rctx.dbh->readSome()
           ; read_more
           ; rows = co_await rctx.dbh->readSome()) {

          read_more = rctx.dbh->shouldReadMore(); // For next iteration

          if (rows.empty()) {
              LOG_TRACE_N << "Out of rows to iterate... num_rows_in_batch=" << num_rows_in_batch;
              break;
          }

          for(const auto& row : rows) {
              auto * session = ws->add_sessions();
              ToWorkSession::assign(row, *session, *rctx.uctx);
              ++total_rows;
              // Do we need to flush?
              if (++num_rows_in_batch >= batch_size) {
                  co_await flush();
              }
          }

        } // read more from db loop

        co_await flush();

        LOG_DEBUG_N << "Sent " << total_rows << " work-sessions to client.";
        co_return;

    }, __func__);
}


} // ns
