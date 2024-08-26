
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
             dc->set_id(to_view(row.at(ID).as_string()));
             dc->set_color(to_view(row.at(COLOR).as_string()));
             dc->set_name(to_view(row.at(NAME).as_string()));
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
                "SELECT date, user, color, notes, report FROM day WHERE user=? AND date=? ORDER BY date",
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
                    day->set_user(row.at(USER).as_string());
                }
                if (row.at(COLOR).is_string()) {
                    day->set_color(row.at(COLOR).as_string());
                }
                if (row.at(NOTES).is_string()) {
                    day->set_hasnotes(true);
                    reply->set_notes(row.at(NOTES).as_string());
                }
                if (row.at(REPORT).is_string()) {
                    day->set_hasreport(true);
                    reply->set_report(row.at(REPORT).as_string());
                }
            } else {
                *day->mutable_date() = *req;
                day->set_user(cuser);
            }

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Month *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();

            auto res = co_await owner_.server().db().exec(
                "SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? ORDER BY date",
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
                    current_day->set_user(row.at(USER).as_string());
                    if (row.at(COLOR).is_string()) {
                        current_day->set_color(row.at(COLOR).as_string());
                    }
                    current_day->set_hasnotes(row.at(NOTES).as_int64() != 1);
                    current_day->set_hasreport(row.at(REPORT).as_int64() != 1);
                }
            }

            co_return;
        });
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
                   ON DUPLICATE KEY UPDATE color=?)", uctx->dbOptions(),
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
        });
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
        });
}

} // ns
