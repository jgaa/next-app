
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
        auto dbh = co_await owner_.server().db().getConnection();
        *reply = co_await owner_.getDayColorDefinitions(dbh, {});
        co_return;
     }, __func__);

    return rval;
}

boost::asio::awaitable<pb::DayColorDefinitions> GrpcServer::getDayColorDefinitions(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,const std::string& tenantUuid) {

    auto res = co_await dbh.exec(
        "SELECT id, name, color, score FROM day_colors WHERE tenant IS NULL ORDER BY score DESC");

    enum Cols {
        ID, NAME, COLOR, SCORE
    };

    pb::DayColorDefinitions dcd;

    for(const auto row : res.rows()) {
        auto *dc = dcd.add_daycolors();
        dc->set_id(pb_adapt(row.at(ID).as_string()));
        dc->set_color(pb_adapt(row.at(COLOR).as_string()));
        dc->set_name(pb_adapt(row.at(NAME).as_string()));
        dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
    }

    co_return dcd;
}

boost::asio::awaitable<std::optional<pb::CompleteDay>>
GrpcServer::fetchDay(const pb::Date& date, RequestCtx& rctx) {
    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();

    auto res = co_await rctx.dbh->exec(
        "SELECT date, color, notes, report, updated FROM day WHERE user=? AND date=? AND deleted=0 ORDER BY date",
        uctx->dbOptions(), cuser, toAnsiDate(date));

    enum Cols {
        DATE, COLOR, NOTES, REPORT, UPDATED
    };

    pb::CompleteDay reply;
    auto day = reply.mutable_day();
    if (!res.empty() && !res.rows().empty()) {
        const auto& row = res.rows().front();
        const auto date_val = row.at(DATE).as_date();

        *day->mutable_date() = toDate(date_val);
        if (row.at(COLOR).is_string()) {
            day->set_color(pb_adapt(row.at(COLOR).as_string()));
        }
        if (row.at(NOTES).is_string()) {
            day->set_hasnotes(true);
            reply.set_notes(pb_adapt(row.at(NOTES).as_string()));
        }
        if (row.at(REPORT).is_string()) {
            day->set_hasreport(true);
            reply.set_report(pb_adapt(row.at(REPORT).as_string()));
        }

        day->set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), rctx.uctx->tz()));

        co_return reply;
    }

    co_return std::nullopt;
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDay(::grpc::CallbackServerContext *ctx,
                                const pb::Date *req,
                                pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            if (auto full_day = co_await owner_.fetchDay(*req, rctx)) {
                if (auto * day = reply->mutable_day()) {
                    *day = std::move(*full_day);
                } else {
                    assert(false);
                }
            } else {
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message("Day not found");
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
                "SELECT date, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? and deleted=0 ORDER BY date",
                uctx->dbOptions(), cuser, req->year(), req->month() + 1);

            enum Cols {
                DATE, COLOR, NOTES, REPORT
            };

            reply->set_year(req->year());
            reply->set_month(req->month());

            for(const auto& row : res.rows()) {
                const auto date_val = row.at(DATE).as_date();
                if (date_val.valid()) {
                    auto current_day = reply->add_days();
                    *current_day->mutable_date() = toDate(date_val);
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

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetColorOnDay(::grpc::CallbackServerContext *ctx,
                                                                   const pb::SetColorReq *req,
                                                                   pb::Status *reply)
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

            auto update = newUpdate(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);

            if (auto full_day = co_await owner_.fetchDay(req->date(), rctx)) {
                auto& update = rctx.publishLater(res.affected_rows() == 1 /* inserted, 2 == updated */
                                                    ? pb::Update::Operation::Update_Operation_ADDED
                                                    : pb::Update::Operation::Update_Operation_UPDATED);

                *update.mutable_day() = std::move(*full_day);
            } else {
                LOG_WARN_N << "Failed to fetch day after setting color. "
                           << " date=" << toAnsiDate(req->date()) << " user=" << cuser;
            }

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
                color,
                notes,
                report
                );


            if (auto full_day = co_await owner_.fetchDay(req->day().date(), rctx)) {
                auto& update = rctx.publishLater(res.affected_rows() == 1 /* inserted, 2 == updated */
                                        ? pb::Update::Operation::Update_Operation_ADDED
                                        : pb::Update::Operation::Update_Operation_UPDATED);

                *update.mutable_day() = std::move(*full_day);
            } else {
                LOG_WARN_N << "Failed to fetch day after setting color. "
                           << " date=" << toAnsiDate(req->day().date()) << " user=" << cuser;
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::saveDays(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::ListOfDays& days, RequestCtx& rctx) {
    const auto& cuser = rctx.uctx->userUuid();
    const auto &items = days.days();
    const size_t num_items = items.size();

    auto sql = "INSERT INTO day (date, user, color, notes, report) VALUES (?, ?, ?, ?, ?)";
    enum Cols {
        DATE, USER, COLOR, NOTES, REPORT, COLS_
    };

    jgaa::mysqlpool::FieldViewMatrix values{num_items, COLS_};

    size_t index = 0;
    for(const auto& row : items) {
        assert(index < values.rows());
        values.copy(index, DATE, toAnsiDate(row.day().date()));
        values.set(index, USER, cuser);
        values.set(index, COLOR, toStringViewOrNull(row.day().color()));
        string_view notes, report;
        if (row.has_notes()) notes = row.notes();
        if (row.has_report()) report = row.report();
        values.set(index, NOTES, toStringViewOrNull(notes));
        values.set(index, REPORT, toStringViewOrNull(report));
        ++index;
    }

    auto res = co_await dbh.exec(sql, rctx.uctx->dbOptions(), values);
}

::grpc::ServerWriteReactor< ::nextapp::pb::Status>*
GrpcServer::NextappImpl::GetNewDays(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req)
{
    return writeStreamHandler(ctx, req,
        [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto stream_scope = owner_.server().metrics().data_streams_days().scoped();

            auto flush = [&](pb::Status& reply) -> boost::asio::awaitable<void> {
                co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
            };

            assert(rctx.dbh);
            const auto total_rows = co_await owner_.exportDays(
                req->since(), rctx.dbh.value(), flush, rctx);

            LOG_DEBUG_N << "Sent " << total_rows << " days to client.";
            co_return;
    }, __func__ );
}

boost::asio::awaitable<uint64_t> GrpcServer::exportDays(
    const uint64_t since,
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const export_flush_fn_t& flush_fn,
    RequestCtx& rctx) {

    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();
    const auto batch_size = server().config().options.stream_batch_size;

    // Use batched reading from the database, so that we can get all the data, but
    // without running out of memory.
    // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
    assert(rctx.dbh);
    co_await  rctx.dbh->start_exec(
        R"(SELECT deleted, updated, date, color, notes, report FROM day
                   WHERE user=? AND updated > ?)",
        uctx->dbOptions(), cuser, toMsDateTime(since, uctx->tz(), true));

    enum Cols {
        DELETED, UPDATED, DATE, COLOR, NOTES, REPORT
    };

    nextapp::pb::Status reply;

    auto *days = reply.mutable_days();
    auto num_rows_in_batch = 0u;
    auto total_rows = 0u;
    auto batch_num = 0u;

    auto flush = [&]() -> boost::asio::awaitable<void> {
        reply.set_error(::nextapp::pb::Error::OK);
        assert(reply.has_days());
        assert(reply.days().days_size() == num_rows_in_batch);
        ++batch_num;
        reply.set_message(format("Fetched {} days in batch {}", reply.days().days_size(), batch_num));
        //co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
        co_await flush_fn(reply);
        reply.Clear();
        days = reply.mutable_days();
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
            auto current_day = days->add_days();
            auto *d = current_day->mutable_day();
            assert(row.at(DELETED).is_int64());
            d->set_deleted(row.at(DELETED).as_int64() == 1);
            d->set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), rctx.uctx->tz()));
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

            ++total_rows;

            // Do we need to flush?
            if (++num_rows_in_batch >= batch_size) {
                co_await flush();
            }
        }

    } // read more from db loop

    co_await flush();
    co_return total_rows;
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetNewDayColorDefinitions(::grpc::CallbackServerContext *ctx,
                                                                               const pb::GetNewReq *req,
                                                                               pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
    [this, req] (auto *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        auto res = co_await rctx.dbh->exec(
            "SELECT id, name, color, score, updated "
            "FROM day_colors "
            "WHERE tenant IS NULL AND updated > ?",
            rctx.uctx->dbOptions(),
            toMsDateTime(req->since(), rctx.uctx->tz()));

        enum Cols {
            ID, NAME, COLOR, SCORE, UPDATED
        };

        if (auto *dcd = reply->mutable_daycolordefinitions()) {
            for(const auto& row : res.rows()) {
                auto *dc = dcd->add_daycolors();
                dc->set_id(pb_adapt(row.at(ID).as_string()));
                dc->set_color(pb_adapt(row.at(COLOR).as_string()));
                dc->set_name(pb_adapt(row.at(NAME).as_string()));
                dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
                dc->set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), rctx.uctx->tz()));
            }
        } else {
            assert(false);
            throw server_err{pb::Error::GENERIC_ERROR, "Could not allocate daycolordefinitions"};
        }

        reply->set_message(format("Fetched {} day color definitions", res.rows().size()));
        co_return;
    }, __func__);
}


} // ns
