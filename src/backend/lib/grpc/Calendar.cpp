
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
        ID, USER, NAME, START_TIME, END_TIME, KIND, CATEGORY
    };

    static constexpr auto columns = "id, user, name, start_time, end_time, kind, category";

    static void assign(const boost::mysql::row_view& row, pb::TimeBlock& tb, const UserContext& uctx) {
        tb.set_id(row.at(ID).as_string());
        tb.set_user(row.at(USER).as_string());
        tb.set_name(row.at(NAME).as_string());
        tb.mutable_timespan()->set_start(toTimeT(row.at(START_TIME).as_datetime()));
        tb.mutable_timespan()->set_end(toTimeT(row.at(END_TIME).as_datetime()));
        pb::TimeBlock::Kind kind;
        if (pb::TimeBlock_Kind_Parse(toUpper(row.at(KIND).as_string()), &kind)) {
            tb.set_kind(kind);
        }
        if (const auto& category = row.at(CATEGORY); category.is_string()) {
            tb.set_category(category.as_string());
        }
    }
};


void validate(const pb::TimeBlock& tb, const UserContext& uctx)
{
    if (!tb.has_timespan()) {
        throw db_err(pb::Error::CONSTRAINT_FAILED, "TimeBlock must have a time-span");
    }

    // check if the start-time is before the end-time
    if (tb.timespan().start() >= tb.timespan().end()) {
        throw db_err(pb::Error::CONSTRAINT_FAILED, "Start time must be before end time");
    }

    // Check that the start-time and the end-time are ate the same date, using the users timezone in uctx
    if (toYearMonthDay(tb.timespan().start(), uctx.tz()) != toYearMonthDay(tb.timespan().end(), uctx.tz())) {
        throw db_err(pb::Error::CONSTRAINT_FAILED, "Start and end time must be on the same day");
    }

    if (tb.timespan().start() >= tb.timespan().end()) {
        throw db_err(pb::Error::CONSTRAINT_FAILED, "Start time must be before end time");
    }
}

} // anon ns


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateTimeblock(::grpc::CallbackServerContext *ctx, const pb::TimeBlock *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            validate(*req, *rctx.uctx);

            if (!req->actions().empty()) {
                throw db_err(pb::CONSTRAINT_FAILED, "Cannot create a time block with actions. Add the actions later.");
            }

            for(const auto& action : req->actions()) {
                co_await owner_.validateAction(*rctx.dbh, action.id(), cuser);
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
                owner_.publish(createCalendarEventUpdate(tb, pb::Update::Operation::Update_Operation_ADDED));
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

            if (req->actions().size() > owner_.server_.config().svr.time_block_max_actions) [[unlikely]] {
                throw db_err{pb::Error::CONSTRAINT_FAILED,
                             format("Too many actions. The limit is {}", owner_.server_.config().svr.time_block_max_actions)};
            }

            auto trx = co_await rctx.dbh->transaction();
            co_await owner_.validateTimeBlock(*rctx.dbh, id, cuser);

            // TODO: Optimize so we only update the actions that has changed
            co_await rctx.dbh->exec("DELETE FROM time_block_actions WHERE time_block = ?", id);

            for(const auto& action : req->actions()) {
                co_await owner_.validateAction(*rctx.dbh, action.id(), cuser);
                co_await rctx.dbh->exec("INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)", id, action.id());
            }

            auto res = co_await rctx.dbh->exec(
                format("UPDATE time_block SET start_time=?, end_time=?, name=?, kind=?, category=? WHERE id=? AND user=? ",
                       ToTimeBlock::columns),
                toAnsiTime(req->timespan().start()),
                toAnsiTime(req->timespan().end()),
                req->name(),
                toLower(pb::TimeBlock::Kind_Name(req->kind())),
                toStringOrNull(req->category()), id, cuser);

            co_await trx.commit();

            assert(!res.empty());

            if (!res.empty()) [[likely]] {
                owner_.publish(createCalendarEventUpdate(*req, pb::Update::Operation::Update_Operation_UPDATED));
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
            auto res = co_await rctx.dbh->exec("DELETE FROM time_block WHERE id=? AND user=?", id, cuser);

            co_await trx.commit();

            if (!res.empty() && res.affected_rows() > 0) [[likely]] {
                pb::TimeBlock tb;
                tb.set_id(id);
                owner_.publish(createCalendarEventUpdate(tb, pb::Update::Operation::Update_Operation_DELETED));
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
                format("SELECT {} FROM time_block tb WHERE tb.user=? AND tb.start_time >= ? AND tb.end_time <= ? "
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
        throw db_err{pb::Error::INVALID_ACTION, "TimeBlock not found for the current user"};
    }

    co_return;
}



} // namespace nextapp::grpc
