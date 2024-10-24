
#include <algorithm>

#include "shared_grpc_server.h"

using namespace std;
using namespace std::string_view_literals;

namespace nextapp::grpc {

auto createCalendarEventUpdate(const pb::TimeBlock& tb, pb::Update::Operation op)
{
    auto update = newUpdate(op);
    auto *event = update->mutable_calendarevents()->add_events();
    event->mutable_timeblock()->CopyFrom(tb);
    if (tb.has_timespan()) {
        event->mutable_timespan()->CopyFrom(tb.timespan());
    }
    event->set_id(tb.id());
    return update;
}


namespace {

struct ToTimeBlock {
    enum Columns {
        ID, USER, NAME, START_TIME, END_TIME, KIND, CATEGORY, ACTIONS, VERSION, UPDATED
    };

    static constexpr auto columns = "id, user, name, start_time, end_time, kind, category, actions, version, updated";

    static void assign(const boost::mysql::row_view& row, pb::TimeBlock& tb, const UserContext& uctx) {
        tb.set_id(pb_adapt(row.at(ID).as_string()));
        tb.set_user(pb_adapt(row.at(USER).as_string()));
        if (row.at(NAME).is_string()) {
            tb.set_name(pb_adapt(row.at(NAME).as_string()));
        }
        if (row.at(START_TIME).is_datetime()) {
            tb.mutable_timespan()->set_start(toTimeT(row.at(START_TIME).as_datetime()));
        }
        if (row.at(END_TIME).is_datetime()) {
            tb.mutable_timespan()->set_end(toTimeT(row.at(END_TIME).as_datetime()));
        }
        pb::TimeBlock::Kind kind;
        if (pb::TimeBlock_Kind_Parse(toUpper(row.at(KIND).as_string()), &kind)) {
            tb.set_kind(kind);
        }
        if (const auto& category = row.at(CATEGORY); category.is_string()) {
            tb.set_category(pb_adapt(category.as_string()));
        }

        if (const auto& actions = row.at(ACTIONS); actions.is_blob()) {
            auto& tba = *tb.mutable_actions();
            const auto blob = row.at(ACTIONS).as_blob();
            if (!tba.ParseFromArray(blob.data(), blob.size())) {
                LOG_WARN_N << "Failed to parse actions for time block rom stored protobuf message: " << tb.id();
                throw server_err(pb::Error::GENERIC_ERROR, "Failed to parse time_block.actions from saved protobuf message");
            }
        }

        tb.set_version(static_cast<int32_t>(row.at(VERSION).as_int64()));
        tb.set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), uctx.tz()));
    }
};


void validate(const pb::TimeBlock& tb, const UserContext& uctx)
{
    if (!tb.has_timespan()) {
        throw server_err(pb::Error::CONSTRAINT_FAILED, "TimeBlock must have a time-span");
    }

    // check if the start-time is before the end-time
    if (tb.timespan().start() >= tb.timespan().end()) {
        throw server_err(pb::Error::CONSTRAINT_FAILED, "Start time must be before end time");
    }

    // Check that the start-time and the end-time are ate the same date, using the users timezone in uctx
    if (toYearMonthDay(tb.timespan().start(), uctx.tz()) != toYearMonthDay(tb.timespan().end(), uctx.tz())) {
        throw server_err(pb::Error::CONSTRAINT_FAILED, "Start and end time must be on the same day");
    }

    if (tb.timespan().start() >= tb.timespan().end()) {
        throw server_err(pb::Error::CONSTRAINT_FAILED, "Start time must be before end time");
    }
}

} // anon ns


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateTimeblock(::grpc::CallbackServerContext *ctx, const pb::TimeBlock *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            validate(*req, *rctx.uctx);

            if (!req->actions().list().empty()) {
                throw server_err(pb::CONSTRAINT_FAILED, "Cannot create a time block with actions. Add the actions later.");
            }

            auto res = co_await rctx.dbh->exec(
                format("INSERT INTO time_block (user, start_time, end_time, name, kind, category) VALUES (?, ?, ?, ?, ?, ?) RETURNING {} ",
                       ToTimeBlock::columns),
                cuser,
                toAnsiTime(req->timespan().start(), true),
                toAnsiTime(req->timespan().end(), true),
                req->name(),
                toLower(pb::TimeBlock::Kind_Name(req->kind())),
                toStringOrNull(req->category()));

            assert(!res.empty());
            assert(!res.rows().empty());

            if (!res.empty() && !res.rows().empty()) [[likely]] {
                pb::TimeBlock tb;
                ToTimeBlock::assign(res.rows().front(), tb, *rctx.uctx);
                rctx.publishLater(createCalendarEventUpdate(tb, pb::Update::Operation::Update_Operation_ADDED));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateTimeblock(::grpc::CallbackServerContext *ctx, const pb::TimeBlock *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& id = req->id();

            validate(*req, *rctx.uctx);

            if (req->actions().list().size() > owner_.server_.config().svr.time_block_max_actions) [[unlikely]] {
                throw server_err{pb::Error::CONSTRAINT_FAILED,
                             format("Too many actions. The limit is {}", owner_.server_.config().svr.time_block_max_actions)};
            }

            auto trx = co_await rctx.dbh->transaction();
            co_await owner_.validateTimeBlock(*rctx.dbh, id, cuser);

            // TODO: Optimize so we only update the actions that has changed
            co_await rctx.dbh->exec("DELETE FROM time_block_actions WHERE time_block = ?", id);

            for(const auto& action : req->actions().list()) {
                co_await owner_.validateAction(*rctx.dbh, action, cuser);
                co_await rctx.dbh->exec("INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)", id, action);
            }

            auto res = co_await rctx.dbh->exec(
                format("UPDATE time_block SET start_time=?, end_time=?, name=?, kind=?, category=?, actions=? WHERE id=? AND user=? ",
                       ToTimeBlock::columns),
                toAnsiTime(req->timespan().start()),
                toAnsiTime(req->timespan().end()),
                req->name(),
                toLower(pb::TimeBlock::Kind_Name(req->kind())),
                toStringOrNull(req->category()),
                toBlob(req->actions()),
                id, cuser);

            co_await trx.commit();

            assert(!res.empty());

            if (!res.empty()) [[likely]] {
                rctx.publishLater(createCalendarEventUpdate(*req, pb::Update::Operation::Update_Operation_UPDATED));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteTimeblock(::grpc::CallbackServerContext *ctx, const pb::DeleteTimeblockReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& id = req->id();

            auto trx = co_await rctx.dbh->transaction();
            co_await owner_.validateTimeBlock(*rctx.dbh, id, cuser);

            auto res = co_await rctx.dbh->exec(
                format("UPDATE time_block SET start_time=NULL, end_time=NULL, name='', kind='deleted', category=NULL, actions=NULL WHERE id=? AND user=? ",
                       ToTimeBlock::columns),
                id, cuser);

            co_await trx.commit();

            // Remove all data. We don't actually delete the row, but mark it as deleted
            if (!res.empty() && res.affected_rows() > 0) [[likely]] {
                pb::TimeBlock tb;
                tb.set_id(id);
                tb.set_kind(pb::TimeBlock::Kind::TimeBlock_Kind_DELETED);
                rctx.publishLater(createCalendarEventUpdate(tb, pb::Update::Operation::Update_Operation_DELETED));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetCalendarEvents(::grpc::CallbackServerContext *ctx, const pb::TimeSpan *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            // We need actions that are directly assigned to the relevant time-frame within a day,
            // and time blocks with their actions inside
            // In the furture, we also need events from external calendars

            auto& events = *reply->mutable_calendarevents();

            // Get actions for the calendar
            co_await owner_.fetchActionsForCalendar(events, rctx, req->start());

            static const auto tb_col_names = prefixNames(ToTimeBlock::columns, "tb.");

            // Get time blocks for the calendar
            auto res = co_await rctx.dbh->exec(
                format("SELECT {} FROM time_block tb WHERE tb.user=? AND tb.start_time >= ? AND tb.end_time <= ? and Kind != 'deleted'"
                       "ORDER BY tb.start_time, tb.end_time, id",
                       tb_col_names),
                cuser, toAnsiTime(req->start()), toAnsiTime(req->end()));

            assert(res.has_value());
            for(const auto& row : res.rows()) {
                auto &event = *events.add_events();
                ToTimeBlock::assign(row, *event.mutable_timeblock(), *rctx.uctx);
                event.mutable_timespan()->CopyFrom(event.timeblock().timespan());
                event.set_id(event.timeblock().id());
            }

            auto compare = [](const auto& a, const auto& b) noexcept {
                const auto& ats = a.timespan();
                const auto& bts = b.timespan();

                if (const auto cmp = ats.start() - bts.start()) {
                    return cmp < 0;
                }

                if (const auto cmp = ats.end() - bts.end()) {
                    return cmp < 0;
                }

                // Make the sort-order predictible, even for equal time-spans
                return a.id() < b.id();
            };

            // We now have all the relevant items for the calendar in the list. Now, sort them.
            ranges::sort(*events.mutable_events(), compare);
            co_return;
        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::validateTimeBlock(jgaa::mysqlpool::Mysqlpool::Handle &handle, const std::string &timeBlockId, const std::string &userUuid)
{
    auto res = co_await handle.exec("SELECT COUNT(*), name FROM time_block where id=? and user=?", timeBlockId, userUuid);
    if (!res.has_value()) {
        throw server_err{pb::Error::INVALID_ACTION, "TimeBlock not found for the current user"};
    }

    co_return;
}

::grpc::ServerWriteReactor<pb::Status> *
GrpcServer::NextappImpl::GetNewTimeBlocks(::grpc::CallbackServerContext *ctx, const pb::GetNewReq *req)
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
            format("SELECT {} from time_block WHERE user=? AND updated > ?", ToTimeBlock::columns),
            uctx->dbOptions(), cuser, toMsDateTime(req->since(), uctx->tz()));

        nextapp::pb::Status reply;

        auto *tb = reply.mutable_timeblocks();
        auto num_rows_in_batch = 0u;
        auto total_rows = 0u;
        auto batch_num = 0u;

        auto flush = [&]() -> boost::asio::awaitable<void> {
            reply.set_error(::nextapp::pb::Error::OK);
            assert(reply.has_timeblocks());
            ++batch_num;
            reply.set_message(format("Fetched {} time-blocks in batch {}", reply.timeblocks().blocks().size(), batch_num));
            co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
            reply.Clear();
            tb = reply.mutable_timeblocks();
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
                auto * b = tb->add_blocks();
                ToTimeBlock::assign(row, *b, *rctx.uctx);
                ++total_rows;
                // Do we need to flush?
                if (++num_rows_in_batch >= batch_size) {
                    co_await flush();
                }
            }

        } // read more from db loop

        co_await flush();

        LOG_DEBUG_N << "Sent " << total_rows << " time-blocks to client.";
        co_return;

    }, __func__);
}

boost::asio::awaitable<void> GrpcServer::deleteActionInTimeBlocks(const std::string& uuid, RequestCtx& rctx) {

    const auto& dbopts = rctx.uctx->dbOptions();
    enum Cols {
        ID,
        ACTIONS
    };

    const auto res = co_await rctx.dbh->exec(
        "SELECT tb.id, tb.actions FROM time_block tb JOIN time_block_actions tba ON tba.time_block = tb.id "
        "WHERE tba.action=? AND tba.user=?",
        dbopts, uuid, rctx.uctx->userUuid());

    for(const auto& row : res.rows()) {
        const auto& tb_id = row.at(ID).as_string();

        if (const auto& actions = row.at(ACTIONS); actions.is_blob()) {
            const auto blob = row.at(ACTIONS).as_blob();
            pb::StringList sl;
            if (sl.ParseFromArray(blob.data(), blob.size())) {
                auto *list = sl.mutable_list();
                if (auto it = ranges::find(*list, uuid); it != list->end()) {
                    list->erase(it);
                    co_await rctx.dbh->exec("DELETE FROM time_block_actions WHERE time_block=? AND action=?",
                                            dbopts, tb_id, uuid);

                    const auto update_res = co_await rctx.dbh->exec(
                        "UPDATE time_block SET actions=? WHERE id=? AND user=?",
                        toBlob(sl), tb_id, rctx.uctx->userUuid());

                    if (!update_res.affected_rows()) [[unlikely]] {
                        LOG_WARN_N << "Failed to update time block with id=" << tb_id;
                    } else {
                        // Publish an updated CalendarEvent with the updated list of actions
                        const auto fres = co_await rctx.dbh->exec(
                            format("SELECT {} FROM time_block WHERE id=? and user=?", ToTimeBlock::columns),
                            dbopts, tb_id, rctx.uctx->userUuid());
                        if (res.rows().size() == 1) {
                            auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                            auto *ev = update.mutable_calendarevents()->add_events();
                            auto *tb = ev->mutable_timeblock();

                            ToTimeBlock::assign(fres.rows().front(), *tb, *rctx.uctx);
                            ev->set_id(tb->id());
                            ev->set_user(rctx.uctx->userUuid());
                            ev->mutable_timespan()->CopyFrom(tb->timespan());
                        } else {
                            assert(false);
                        }
                    }
                }
            }
        }
    }
}

} // namespace nextapp::grpc
