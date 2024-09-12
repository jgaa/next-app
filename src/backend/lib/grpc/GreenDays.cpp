
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

} // anon ns

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDayColorDefinitions(::grpc::CallbackServerContext *ctx,
                                                const pb::Empty *req,
                                                pb::DayColorDefinitions *reply)
{
    auto rval = unaryHandler(ctx, req, reply,
     [this] (auto *reply) -> boost::asio::awaitable<void> {
         auto res = co_await owner_.server().db().exec(
             "SELECT id, name, color, score FROM day_colors WHERE tenant IS NULL ORDER BY score DESC");

         enum Cols {
             ID, NAME, COLOR, SCORE
         };

         for(const auto row : res.rows()) {
             auto *dc = reply->add_daycolors();
             dc->set_id(pb_adapt(row.at(ID).as_string()));
             dc->set_color(pb_adapt(row.at(COLOR).as_string()));
             dc->set_name(pb_adapt(row.at(NAME).as_string()));
             dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
         }

         boost::asio::deadline_timer timer{owner_.server().ctx()};
         timer.expires_from_now(boost::posix_time::seconds{2});
         co_return;
     }, __func__);

    LOG_TRACE_N << "Leaving the coro do do it's magic...";
    return rval;
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDay(::grpc::CallbackServerContext *ctx,
                                const pb::Date *req,
                                pb::CompleteDay *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::CompleteDay *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            auto res = co_await owner_.server().db().exec(
                "SELECT date, user, color, notes, report FROM day WHERE user=? AND date=? AND deleted=0 ORDER BY date",
                uctx->dbOptions(), cuser, toAnsiDate(*req));

            enum Cols {
                DATE, USER, COLOR, NOTES, REPORT
            };

            auto* day = reply->mutable_day();
            if (!res.empty() && !res.rows().empty()) {
                const auto& row = res.rows().front();
                const auto date_val = row.at(DATE).as_date();

                *day->mutable_date() = toDate(date_val);
                if (row.at(USER).is_string()) {
                    day->set_user(pb_adapt(row.at(USER).as_string()));
                }
                if (row.at(COLOR).is_string()) {
                    day->set_color(pb_adapt(row.at(COLOR).as_string()));
                }
                if (row.at(NOTES).is_string()) {
                    day->set_hasnotes(true);
                    reply->set_notes(pb_adapt(row.at(NOTES).as_string()));
                }
                if (row.at(REPORT).is_string()) {
                    day->set_hasreport(true);
                    reply->set_report(pb_adapt(row.at(REPORT).as_string()));
                }
            } else {
                *day->mutable_date() = *req;
                day->set_user(cuser);
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Month *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            auto res = co_await owner_.server().db().exec(
                "SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? and deleted=0 ORDER BY date",
                uctx->dbOptions(), cuser, req->year(), req->month() + 1);

            enum Cols {
                DATE, USER, COLOR, NOTES, REPORT
            };

            reply->set_year(req->year());
            reply->set_month(req->month());

            for(const auto& row : res.rows()) {
                const auto date_val = row.at(DATE).as_date();
                if (date_val.valid()) {
                    auto current_day = reply->add_days();
                    *current_day->mutable_date() = toDate(date_val);
                    current_day->set_user(pb_adapt(row.at(USER).as_string()));
                    if (row.at(COLOR).is_string()) {
                        current_day->set_color(pb_adapt(row.at(COLOR).as_string()));
                    }
                    current_day->set_hasnotes(row.at(NOTES).as_int64() != 1);
                    current_day->set_hasreport(row.at(REPORT).as_int64() != 1);
                }
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetColorOnDay(::grpc::CallbackServerContext *ctx, const pb::SetColorReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (auto *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            optional<string> color;
            if (!req->color().empty()) {
                color = req->color();
            }


            auto res = co_await owner_.server().db().exec(
                R"(INSERT INTO day (date, user, color) VALUES (?, ?, ?)
                   ON DUPLICATE KEY UPDATE color=?, deleted=0)", uctx->dbOptions(),
                // insert
                toAnsiDate(req->date()),
                cuser,
                color,
                // update
                color
                );


            LOG_TRACE_N << "Finish updating color for " << toAnsiDate(req->date());

            res.affected_rows();

            auto update = newUpdate(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);
            auto dc = update->mutable_daycolor();
            *dc->mutable_date() = req->date();
            dc->set_user(cuser);
            dc->set_color(req->color());
            rctx.publishLater(update);
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetDay(::grpc::CallbackServerContext *ctx, const pb::CompleteDay *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (auto *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            optional<string> color;
            if (!req->day().color().empty()) {
                color = req->day().color();
            }

            // We want non-existing entries stored as NULL in the database
            optional<string> notes;
            optional<string> report;

            if (!req->notes().empty()) {
                notes = req->notes();
            }

            if (!req->report().empty()) {
                report = req->report();
            }

            auto res = co_await owner_.server().db().exec(
                R"(INSERT INTO day (date, user, color, notes, report) VALUES (?, ?, ?, ?, ?)
                   ON DUPLICATE KEY UPDATE color=?, notes=?, report=?)", uctx->dbOptions(),
                // insert
                toAnsiDate(req->day().date()),
                cuser,
                color,
                notes,
                report,
                // update
                color,
                notes,
                report
                );



            auto update = newUpdate(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);
            *update->mutable_day() = *req;
            rctx.publishLater(update);
            co_return;
        }, __func__);
}

::grpc::ServerWriteReactor< ::nextapp::pb::Status>*
GrpcServer::NextappImpl::GetNewDays(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req)
{
    LOG_DEBUG << "GetNewDays called";

    return writeStreamHandler(ctx, req,
        [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            const auto batch_size = owner_.server().config().options.stream_batch_size;

            // TODO: Switch to cursor/batched reads when we get support in mysqlpool.
            //       For now, just read everything into memory.

            auto res = co_await owner_.server().db().exec(
                R"(SELECT deleted, UNIX_TIMESTAMP(updated), date, user, color, notes, report FROM day
                   WHERE user=? AND UNIX_TIMESTAMP(updated) >= ?)",
                uctx->dbOptions(), cuser, req->since());

            enum Cols {
                DELETED, UPDATED, DATE, USER, COLOR, NOTES, REPORT
            };

            nextapp::pb::Status reply;

            auto *days = reply.mutable_days();
            auto rows = 0u;

            auto flush = [this, &reply, &stream, &days, &rows]() -> boost::asio::awaitable<void> {
                reply.set_error(::nextapp::pb::Error::OK);
                assert(reply.has_days());
                assert(reply.days().days_size() == rows);
                reply.set_message(format("Fetched {} days", reply.days().days_size()));
                co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
                reply.Clear();
                days = reply.mutable_days();
                rows = {};
            };

            for(const auto& row : res.rows()) {
                auto current_day = days->add_days();
                auto *d = current_day->mutable_day();
                assert(row.at(DELETED).is_int64());
                d->set_deleted(row.at(DELETED).as_int64() == 1);
                assert(row.at(UPDATED).is_int64());
                d->set_updated(row.at(UPDATED).as_int64());
                if (row.at(USER).is_string()) {
                    d->set_user(pb_adapt(row.at(USER).as_string()));
                }
                const auto date_val = row.at(DATE).as_date();
                if (!date_val.valid()) {
                    LOG_ERROR_N << "Invalid date in database.day for user " << cuser;
                    continue;
                }
                *d->mutable_date() = toDate(date_val);

                if (!d->deleted()) {

                    if (row.at(COLOR).is_string()) {
                        d->set_color(pb_adapt(row.at(COLOR).as_string()));
                    }
                    if (row.at(NOTES).is_string()) {
                        d->set_hasnotes(true);
                        current_day->set_notes(pb_adapt(row.at(NOTES).as_string()));
                    }
                    if (row.at(REPORT).is_string()) {
                        d->set_hasreport(true);
                        current_day->set_report(pb_adapt(row.at(REPORT).as_string()));
                    }
                }

                // Do we need to flush?
                if (++rows > batch_size) {
                    co_await flush();
                }
            }

            co_await flush();
            co_return;
    }, __func__ );
}


} // ns
