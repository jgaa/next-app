
#include "shared_grpc_server.h"

namespace nextapp::grpc {

namespace {

struct ToAction {
    enum Cols {
        ID, NODE, USER, VERSION, ORIGIN, PRIORITY, STATUS, FAVORITE, NAME, CREATED_DATE, DUE_KIND, START_TIME, DUE_BY_TIME, DUE_TIMEZONE, COMPLETED_TIME,  // core
        DESCR, TIME_ESTIMATE, DIFFICULTY, REPEAT_KIND, REPEAT_UNIT, REPEAT_WHEN, REPEAT_AFTER // remaining
    };

    static auto coreSelectCols() {
        static const auto cols = format("{}{}", ids_, core_);
        return cols;
    }

    static string_view allSelectCols() {
        static const auto cols = format("{}{}{}", ids_, core_, remaining_);
        return cols;
    }

    static string_view statementBindingStr() {
        return "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?";
    }

    static string_view updateStatementBindingStr() {
        static const auto cols = buildUpdateBindStr();
        return cols;
    }

    static string insertCols() {
        // Remove columns we don't use for insert
        auto cols = boost::replace_all_copy(string{allSelectCols()}, "created_date,", "");
        boost::replace_all(cols, "version,", "");
        return cols;
    }

    template <bool argsFirst = true, typename ...Args>
    static auto prepareBindingArgs(const pb::Action& action, const UserContext& uctx, Args... args) {
        auto bargs = make_tuple(
            toStringOrNull(action.origin()),
            pb::ActionPriority_Name(action.priority()),
            pb::ActionStatus_Name(action.status()),
            action.favorite(),
            action.name(),

            // due
            pb::ActionDueKind_Name(action.due().kind()),
            toAnsiTime(action.due().start(), uctx.tz()),
            toAnsiTime(action.due().due(),  uctx.tz()),
            toStringOrNull(action.due().timezone()),

            toAnsiTime(action.completedtime(),  uctx.tz()),
            action.descr(),
            action.timeestimate(),
            pb::ActionDifficulty_Name(action.difficulty()),
            pb::Action::RepeatKind_Name(action.repeatkind()),
            pb::Action::RepeatUnit_Name(action.repeatunits()),
            pb::Action::RepeatWhen_Name(action.repeatwhen()),
            action.repeatafter()
            );

        if constexpr (sizeof...(Args) > 0) {
            std::tuple<Args...> t1{args...};
            if constexpr (argsFirst) {
                return std::tuple_cat(t1, bargs);
            } else {
                return std::tuple_cat(bargs, t1);
            }
        } else if constexpr (sizeof...(Args)  == 0) {
            return bargs;
        }
    }

    template <ActionType T>
    static void assign(const boost::mysql::row_view& row, T& obj, const UserContext& uctx) {
        obj.set_id(row.at(ID).as_string());
        obj.set_node(row.at(NODE).as_string());
        obj.set_version(static_cast<int32_t>(row.at(VERSION).as_int64()));

        if (row.at(ORIGIN).is_string()) {
            obj.set_origin(row.at(ORIGIN).as_string());
        }

        {
            pb::ActionStatus status;
            const auto name = toUpper(row.at(STATUS).as_string());
            if (pb::ActionStatus_Parse(name, &status)) {
                obj.set_status(status);
            } else {
                LOG_WARN_N << "Invalid ActionStatus: " << name;
            }
        }

        if (row.at(FAVORITE).is_int64()) {
            obj.set_favorite(row.at(FAVORITE).as_int64() != 0);
        }

        {
            pb::ActionPriority pri;
            const auto name = toUpper(row.at(PRIORITY).as_string());
            if (pb::ActionPriority_Parse(name, &pri)) {
                obj.set_priority(pri);
            } else {
                LOG_WARN_N << "Invalid ActionPriority: " << name;
            }
        }

        obj.set_name(row.at(NAME).as_string());

        {
            auto * date = obj.mutable_createddate();
            if (row.at(CREATED_DATE).is_datetime()) {
                *date = toDate(row.at(CREATED_DATE).as_datetime());
            }
        }

        {
            pb::ActionDueKind dt;
            const auto name = toUpper(row.at(DUE_KIND).as_string());
            if (pb::ActionDueKind_Parse(name, &dt)) {
                obj.mutable_due()->set_kind(dt);
            } else {
                LOG_WARN_N << "Invalid ActionDueKind: " << name;
            }
        }

        if (row.at(DUE_BY_TIME).is_datetime()) {
            obj.mutable_due()->set_due(toTimeT(row.at(DUE_BY_TIME).as_datetime(), uctx.tz()));

            if (row.at(START_TIME).is_datetime()) {
                obj.mutable_due()->set_start(toTimeT(row.at(START_TIME).as_datetime(), uctx.tz()));
            }

            if (row.at(DUE_TIMEZONE).is_string()) {
                obj.mutable_due()->set_timezone(row.at(DUE_TIMEZONE).as_string());
            }
        }

        LOG_TRACE << "Assigning action*: " << toJson(obj, 2);

        if (obj.status() == pb::ActionStatus::DONE) {
            obj.set_kind(pb::ActionKind::AC_DONE);
        } else {
            // If we have due_by_time set, we can see a) if it's expired, b) if it's today and c) it it's in the future
            // We need to convert the time from the database and the time right now to the time-zone used by the client to get it right.
            if (row.at(START_TIME).is_datetime()) {
                const auto due = row.at(DUE_BY_TIME).as_datetime();
                const auto zt = std::chrono::zoned_time(&uctx.tz(), due.as_time_point()  - chrono::seconds(1));
                const auto due_when = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(zt.get_local_time())};

                optional<chrono::year_month_day> start;
                if (row.at(START_TIME).is_datetime()) {
                    const auto start_time = row.at(START_TIME).as_datetime();
                    const auto zt = std::chrono::zoned_time(&uctx.tz(), start_time.as_time_point());
                    start = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(zt.get_local_time())};
                }

                const auto now_zt = std::chrono::zoned_time(&uctx.tz(), chrono::system_clock::now());
                const auto now = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now_zt.get_local_time())};

                if (due_when.year() == now.year() && due_when.month() == now.month() && due_when.day() == now.day()) {
                    obj.set_kind(pb::ActionKind::AC_TODAY);
                } else if (due_when < now) {
                    obj.set_kind(pb::ActionKind::AC_OVERDUE);
                } else if (due_when > now) {
                    if (start < now) {
                        obj.set_kind(pb::ActionKind::AC_ACTIVE);
                    } else{
                        obj.set_kind(pb::ActionKind::AC_UPCOMING);
                    }
                } else {
                    assert(false); // Not possible!
                    obj.set_kind(pb::ActionKind::AC_TODAY); // Just in case the impossible occur in release build
                }
            } else {
                obj.set_kind(pb::ActionKind::AC_UNSCHEDULED);
            }
        }

        if (row.at(COMPLETED_TIME).is_datetime()) {
            obj.set_completedtime(toTimeT(row.at(COMPLETED_TIME).as_datetime(), uctx.tz()));
        }

        if constexpr (std::is_same_v<T, pb::Action>) {
            obj.set_descr(row.at(DESCR).as_string());
            obj.set_timeestimate(row.at(TIME_ESTIMATE).as_int64());
            *obj.mutable_createddate() = toDate(row.at(CREATED_DATE).as_datetime());
            {
                pb::ActionDifficulty ad;
                const auto name = toUpper(row.at(DIFFICULTY).as_string());
                if (pb::ActionDifficulty_Parse(name, &ad)) {
                    obj.set_difficulty(ad);
                } else {
                    LOG_WARN_N << "Invalid ActionDifficulty: " << name;
                }
            }
            {
                pb::Action::RepeatKind rk;
                const auto name = toUpper(row.at(REPEAT_KIND).as_string());
                if (pb::Action::RepeatKind_Parse(name, &rk)) {
                    obj.set_repeatkind(rk);
                } else {
                    LOG_WARN_N << "Invalid RepeatKind: " << name;
                }
            }
            {
                pb::Action::RepeatUnit ru;
                const auto name = toUpper(row.at(REPEAT_UNIT).as_string());
                if (pb::Action::RepeatUnit_Parse(name, &ru)) {
                    obj.set_repeatunits(ru);
                } else {
                    LOG_WARN_N << "Invalid RepeatUnit: " << name;
                }
            }
            {
                pb::Action::RepeatWhen rw;
                const auto name = toUpper(row.at(REPEAT_WHEN).as_string());
                if (pb::Action::RepeatWhen_Parse(name, &rw)) {
                    obj.set_repeatwhen(rw);
                } else {
                    LOG_WARN_N << "Invalid RepeatWhen: " << name;
                }
            }
            obj.set_repeatafter(row.at(REPEAT_AFTER).as_int64());
        }
    }

private:
    static string buildUpdateBindStr() {
        auto cols = format("{}{}", core_, remaining_);
        boost::replace_all(cols, "created_date,", "");
        boost::replace_all(cols, ",", "=?,");
        cols += "=?";
        return cols;
    }

    static constexpr string_view ids_ = "id, node, user, version, ";
    static constexpr string_view core_ = "origin, priority, status, favorite, name, created_date, due_kind, start_time, due_by_time, due_timezone, completed_time";
    static constexpr string_view remaining_ = ", descr, time_estimate, difficulty, repeat_kind, repeat_unit, repeat_when, repeat_after";
};

enum class DoneChanged {
    NO_CHANGE,
    MARKED_DONE,
    MARKED_UNDONE
};

boost::asio::awaitable<void>
replyWithAction(GrpcServer& grpc, const std::string actionId, const UserContext& uctx,
                ::grpc::CallbackServerContext *ctx, pb::Status *reply,
                DoneChanged done = DoneChanged::NO_CHANGE, bool publish = true) {

    const auto dbopts = uctx.dbOptions();

    auto res = co_await grpc.server().db().exec(
        format(R"(SELECT {} from action WHERE id=? AND user=? )",
               ToAction::allSelectCols()), dbopts, actionId, uctx.userUuid());

    assert(res.has_value());
    if (!res.rows().empty()) {
        const auto& row = res.rows().front();
        auto *action = reply->mutable_action();
        ToAction::assign(row, *action, uctx);
        if (publish) {
            // Copy the new Action to an update and publish it
            auto update = newUpdate(pb::Update::Operation::Update_Operation_UPDATED);
            *update->mutable_action() = *action;
            grpc.publish(update);
        }

        switch (done) {
        case DoneChanged::MARKED_DONE:
            co_await grpc.handleActionDone(*action, uctx, ctx);
            break;
        case DoneChanged::MARKED_UNDONE:
            co_await grpc.handleActionActive(*action, uctx, ctx);
            break;
        default:
            break;
        }

    } else {
        reply->set_error(pb::Error::NOT_FOUND);
        reply->set_message(format("Action with id={} not found for the current user.", actionId));
    }

    LOG_TRACE << "Reply: " << grpc.toJsonForLog(*reply);
}

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetActions(::grpc::CallbackServerContext *ctx, const pb::GetActionsReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& dbopts = uctx->dbOptions();

            SqlFilter filter{false};
            if (req->has_active() && req->active()) {
                filter.add("(status!='done' || DATE(completed_time) = CURDATE())");
            }
            if (!req->node().empty()) {
                filter.add(format("node='{}'", req->node()));
            }

            string order = "status DESC, priority DESC, due_by_time, created_date";

            const auto res = co_await owner_.server().db().exec(
                format(R"(SELECT {} from action WHERE user=? {} ORDER BY {})",
                       ToAction::coreSelectCols(),
                       filter.where(), order), dbopts, cuser);
            assert(res.has_value());
            auto *actions = reply->mutable_actions();
            for(const auto& row : res.rows()) {
                ToAction::assign(row, *actions->add_actions(), *uctx);
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetAction(::grpc::CallbackServerContext *ctx, const pb::GetActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto& dbopts = uctx->dbOptions();

            const auto res = co_await owner_.server().db().exec(
                format(R"(SELECT {} from action WHERE id=? AND user=? )",
                       ToAction::allSelectCols()), dbopts, uuid, cuser);

            assert(res.has_value());
            if (!res.rows().empty()) {
                const auto& row = res.rows().front();
                auto *action = reply->mutable_action();
                ToAction::assign(row, *action, *uctx);
            } else {
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message(format("Action with id={} not found for the current user.", uuid));
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<void> addAction(pb::Action action, GrpcServer& owner, ::grpc::CallbackServerContext *ctx,
                                       pb::Status *reply = {}) {
    const auto uctx = owner.userContext(ctx);
    const auto& cuser = uctx->userUuid();
    auto dbopts = uctx->dbOptions();


    if (action.node().empty()) {
        throw runtime_error{"Missing node"};
    }

    co_await owner.validateNode(action.node(), cuser);

    if (action.id().empty()) {
        action.set_id(newUuidStr());
    }
    if (action.status() == pb::ActionStatus::DONE && action.completedtime() == 0) {
        action.set_completedtime(time({}));
    }

    // Not an idempotent query
    dbopts.reconnect_and_retry_query = false;

    const auto res = co_await owner.server().db().exec(
        format("INSERT INTO action ({}) VALUES ({}) RETURNING {} ",
               ToAction::insertCols(),
               ToAction::statementBindingStr(),
               ToAction::allSelectCols()),
        dbopts,
        ToAction::prepareBindingArgs(action, *uctx, action.id(), action.node(), cuser));

    assert(!res.empty());
    // Set the reply data

    auto update = newUpdate(pb::Update::Operation::Update_Operation_ADDED);
    if (reply) {
        auto *reply_action = reply->mutable_action();
        ToAction::assign(res.rows().front(), *reply_action, *uctx);
        *update->mutable_action() = *reply_action;
    } else {
        action.Clear();
        ToAction::assign(res.rows().front(), action, *uctx);
        *update->mutable_action() = action;
    }

    // Copy the new Action to an update and publish it
    owner.publish(update);
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {

            pb::Action new_action{*req};
            new_action.set_node(req->node());

            co_await addAction(new_action, owner_, ctx, reply);
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->id());
            const auto& dbopts = uctx->dbOptions();
            DoneChanged done = DoneChanged::NO_CHANGE;

            if (req->node().empty()) {
                throw runtime_error{"Missing node"};
            }

            auto existing_res = co_await owner_.server().db().exec("SELECT node FROM action WHERE id=? AND user=?", uuid, cuser);
            if (existing_res.empty() || existing_res.rows().empty()) {
                throw db_err{pb::Error::NOT_FOUND, "Action does not exist"};
            }

            const auto current_node = existing_res.rows().front().at(0).as_string();
            if (current_node != req->node()) {
                throw db_err(pb::Error::CONSTRAINT_FAILED, "UpdateAction cannot change the node. Use MoveAction for that.");
            }

            co_await owner_.validateNode(req->node(), cuser);

            pb::Action new_action{*req};
            assert(new_action.id() == uuid);
            if (new_action.status() == pb::ActionStatus::DONE && new_action.completedtime() == 0) {
                new_action.set_completedtime(time({}));
                done = DoneChanged::MARKED_DONE;
            } else if (new_action.status() != pb::ActionStatus::DONE && new_action.completedtime() != 0) {
                new_action.set_completedtime(0);
                done = DoneChanged::MARKED_UNDONE;
            }

            auto res = co_await owner_.server().db().exec(format("UPDATE action SET {}, version=version+1 WHERE id=? AND user=? ",
                                                                 ToAction::updateStatementBindingStr()), dbopts,
                                                          ToAction::prepareBindingArgs<false>(new_action, *uctx, uuid, cuser));

            assert(!res.empty());

            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *uctx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteAction(::grpc::CallbackServerContext *ctx, const pb::DeleteActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->actionid());

            auto res = co_await owner_.server().db().exec("DELETE FROM action WHERE id=? AND user=?",
                                                          uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                reply->set_deletedactionid(uuid);
                auto update = newUpdate(pb::Update::Operation::Update_Operation_DELETED);
                update->mutable_action()->set_id(uuid);
                owner_.publish(update);
            } else {
                reply->set_uuid(uuid);
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message(format("Action with id={} not found for the current user.", uuid));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsDone(::grpc::CallbackServerContext *ctx, const pb::ActionDoneReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = uctx->dbOptions();
            DoneChanged done = DoneChanged::MARKED_UNDONE;

            optional<string> when;
            if (req->done()) {
                when = toAnsiTime(time({}), uctx->tz());
                done = DoneChanged::MARKED_DONE;
            }

            auto res = co_await owner_.server().db().exec(
                "UPDATE action SET status=?, completed_time=?, version=version+1 WHERE id=? AND user=?",
                dbopts, (req->done() ? "done" : "active"), when, uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *uctx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsFavorite(::grpc::CallbackServerContext *ctx, const pb::ActionFavoriteReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = uctx->dbOptions();

            auto res = co_await owner_.server().db().exec(
                "UPDATE action SET favorite=?, version=version+1 WHERE id=? AND user=?",
                dbopts, req->favorite(), uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *uctx, ctx, reply);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetFavoriteActions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx] (auto *reply) -> boost::asio::awaitable<void> {

            const auto uctx = owner_.userContext(ctx);
            const auto& cuser = uctx->userUuid();

            auto res = co_await owner_.server().db().exec(
                "SELECT a.id, a.name, n.id, n.name, a.version FROM action as a LEFT JOIN node as n ON n.id = a.node ORDER BY a.name, n.name WHERE a.user = ? AND a.favorite = 1 AND a.status != 'done'",
                cuser);

            enum Cols {
                A_ID, A_NAME, N_ID, N_NAME, A_VERSION
            };

            if (res.empty() || res.rows().empty()) {
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message("No favorite actions found");
                co_return;
            }

            for(const auto row : res.rows()) {
                auto *af = reply->mutable_favoriteactions()->add_fa();
                af->set_actionid(row.at(A_ID).as_string());
                af->set_actionname(row.at(A_NAME).as_string());
                af->set_nodeid(row.at(N_ID).as_string());
                af->set_nodename(row.at(N_NAME).as_string());
                af->set_actionversion(static_cast<int32_t>(row.at(A_VERSION).as_int64()));
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::validateAction(const std::string &actionId,
                                                        const std::string &userUuid,
                                                        string *name)
{
    auto res = co_await server().db().exec("SELECT id, name FROM action where id=? and user=?", actionId, userUuid);
    if (!res.has_value()) {
        throw db_err{pb::Error::INVALID_ACTION, "Action not found for the current user"};
    }

    if (name) {
        *name = res.rows().front().at(1).as_string();
    }

    co_return;
}

pb::Due GrpcServer::processDueAtDate(time_t from_timepoint, const pb::Action_RepeatUnit &units,
                                     pb::ActionDueKind kind, int repeatAfter,
                                     const UserContext& uctx)
{
    pb::Due due;
    due.set_kind(kind);
    due.set_timezone(string{uctx.tz().name()});

    using namespace date;
    assert(from_timepoint > 0);
    assert(repeatAfter > 0);

    // We need to use the date libs zone to get the local time calculations right
    const auto *local_ts = locate_zone(uctx.tz().name());
    assert(local_ts);
    if (!local_ts) {
        LOG_WARN << "Failed to locate time zone: " << uctx.tz().name();
        throw runtime_error{"Failed to locate time zone "s + string{uctx.tz().name()}};
    }

    auto ref_tp = chrono::system_clock::from_time_t(from_timepoint);
    auto ref_date = floor<date::days>(ref_tp);
    auto zoned_ref = zoned_time{local_ts, ref_tp};
    auto t_local = zoned_ref.get_local_time();

    auto start_date = year_month_day{floor<days>(t_local)};

    // Days from epoch in local time
    local_days ldays{start_date};

    // Time of day in local time
    const hh_mm_ss time{t_local - floor<days>(t_local)};

    optional<decltype(make_zoned(local_ts, date::local_days{start_date}))> start;

    switch(units) {
    case ::nextapp::pb::Action_RepeatUnit::Action_RepeatUnit_DAYS:
        //start = make_zoned(local_ts, date::local_days{start_date} + days(repeatAfter) + time.seconds());
        start = make_zoned(local_ts, date::local_days{start_date} + days(repeatAfter) + time.seconds());
        break;
    case ::nextapp::pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS:
        start = make_zoned(local_ts, date::local_days{start_date} + days(repeatAfter * 7) + time.seconds());
        break;
    case ::nextapp::pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS: {
        auto offset = start_date + months(repeatAfter);
        if (!offset.ok()) {
            offset = offset.year() / offset.month() / last;
        }
        start = make_zoned(local_ts, date::local_days{offset} + time.seconds());
    }
    break;
    case ::nextapp::pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS: {
        auto qmonth = quarters.at(static_cast<unsigned>(start_date.month()) - 1);

        auto start_point = start_date.year()/ qmonth/ start_date.day();
        auto offset = start_point + months(repeatAfter * 3);
        if (!offset.ok()) {
            offset = offset.year() / offset.month() / last;
        }
        start = make_zoned(local_ts, date::local_days{offset} + time.seconds());
    }
    break;
    case ::nextapp::pb::Action_RepeatUnit::Action_RepeatUnit_YEARS: {
        auto offset = start_date + years(repeatAfter);
        if (!offset.ok()) {
            offset = offset.year() / offset.month() / last;
        }
        start = make_zoned(local_ts, date::local_days{offset} + time.seconds());
    }
    break;
    default:
        assert(false);
        break;
    }

    assert(start);
    due.set_start(chrono::system_clock::to_time_t(start->get_sys_time()));
    due.set_due(getDueTime(*start, local_ts, kind));
    return due;
}

pb::Due GrpcServer::processDueAtDayspec(time_t from_timepoint,
                                        const pb::Action_RepeatUnit &units,
                                        pb::ActionDueKind kind,
                                        int repeatAfter, // bits
                                        const UserContext& uctx)
{
    pb::Due due;
    due.set_kind(kind);
    due.set_timezone(string{uctx.tz().name()});

    using namespace date;
    assert(from_timepoint > 0);
    assert(repeatAfter > 0);

    // We need to use the date libs zone to get the local time calculations right
    const auto *local_ts = locate_zone(uctx.tz().name());
    assert(local_ts);
    if (!local_ts) {
        LOG_WARN << "Failed to locate time zone: " << uctx.tz().name();
        throw runtime_error{"Failed to locate time zone "s + string{uctx.tz().name()}};
    }

    auto ref_tp = chrono::system_clock::from_time_t(from_timepoint);
    auto ref_date = floor<date::days>(ref_tp);
    auto zoned_ref = zoned_time{local_ts, ref_tp};
    auto t_local = zoned_ref.get_local_time();

    auto start_date = year_month_day{floor<days>(t_local)};
    auto week_date = year_month_weekday{floor<days>(t_local)};
    auto ref_day = week_date.weekday();
    assert(ref_day.ok());


    // Days from epoch in local time
    const local_days ldays{start_date};

    // Time of day in local time
    const hh_mm_ss time{t_local - floor<days>(t_local)};

    optional<decltype(make_zoned(local_ts, date::local_days{start_date}))> start;

    const auto sysday = sys_days{week_date};
    const auto wday = ref_day.c_encoding();
    const auto start_of_week_offset = uctx.sundayIsFirstWeekday() ? days(0) : days(1);

    auto best_match = ldays;

    // Check if any days are set
    if (repeatAfter & 0b01111111) {
        for(int i = 0; i < 7; ++i) {
            if (repeatAfter & (1 << i)) {
                auto next = ldays;

                if (i != wday) {
                    next += days(i - wday);
                }

                if (next <= ldays) {
                    next += days(7);
                }

                if (best_match < next) {
                    best_match = next;
                }
            }
        }
    }

    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FIRST_DAY_IN_WEEK)) {
        auto next = ldays;
        next += days(7);
        next += (days(wday) * -1) + start_of_week_offset;

        if (best_match < next) {
            best_match = next;
        }
    }

    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_LAST_DAY_OF_WEEK)) {
        auto next = ldays;
        next += (days(wday) * -1) + days(6) + start_of_week_offset;
        if (next <= ldays) {
            next += days(7);
        }
        if (best_match < next) {
            best_match = next;
        }
    }

    // Always next month
    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FIRST_DAY_IN_MONTH)) {

        auto ymd = start_date.year() / start_date.month() / 1;
        ymd += months{1};
        auto next = local_days{ymd};

        if (best_match < next) {
            best_match = next;
        }
    }

    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_LAST_DAY_IN_MONTH)) {

        auto ymd = start_date.year() / start_date.month()  / last;
        auto next = local_days{ymd};
        if (next <= ldays) {
            ymd += months{1};
            if (!ymd.ok()) {
                ymd = ymd.year() / ymd.month() / last;
            }
            next = local_days{ymd};
        }

        if (best_match < next) {
            best_match = next;
        }
    }

    // Always next quarter
    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FIRST_DAY_IN_QUARTER)) {
        // Start of current quarter
        const auto qmonth = static_cast<unsigned>(quarters.at(static_cast<unsigned>(start_date.month()) - 1));

        // Jump to the start of next quarter
        auto ymd = start_date.year() / month(qmonth) / 1;
        ymd += + months{3};
        auto next = local_days{ymd};

        if (best_match < next) {
            best_match = next;
        }
    }

    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_LAST_DAY_IN_QUARTER)) {
        // Start of current quarter
        const auto qmonth = static_cast<unsigned>(quarters.at(static_cast<unsigned>(start_date.month()) - 1));

        // Jump to the end of the quarter
        auto ymd = start_date.year() / month(qmonth) / last;
        ymd +=  months{2};
        if (!ymd.ok()) {
            ymd = ymd.year() / ymd.month() / last;
        }

        auto next = local_days{ymd};

        if (next <= ldays) {
            ymd += months{3};
            if (!ymd.ok()) {
                ymd = ymd.year() / ymd.month() / last;
            }
            next = local_days{ymd};
        }

        if (best_match < next) {
            best_match = next;
        }
    }

    // Always next year
    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FIRST_DAY_IN_YEAR)) {
        auto ymd = start_date.year() / January  / 1;
        ymd += years{1};

        auto next = local_days{ymd};

        if (best_match < next) {
            best_match = next;
        }
    }

    if (repeatAfter & (1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_LAST_DAY_IN_YEAR)) {
        auto ymd = start_date.year() / December / last;
        auto next = local_days{ymd};

        if (next <= ldays) {
            ymd += years{1};
            assert(ymd.ok());
            next = local_days{ymd};
        }

        if (best_match < next) {
            best_match = next;
        }
    }

    start = make_zoned(local_ts, best_match);

    cout << "from: " << zoned_ref.get_local_time() << endl;
    cout << "start: " << start->get_local_time()
         << " = " << chrono::system_clock::to_time_t(start->get_sys_time()) << endl;

    if (start) {
        due.set_start(chrono::system_clock::to_time_t(start->get_sys_time()));
    }

    return due;
}

nextapp::pb::Due GrpcServer::adjustDueTime(const pb::Due &fromDue, const UserContext& uctx)
{
    using namespace date;
    if (!fromDue.has_start() || fromDue.start() == 0) {
        LOG_TRACE << "No start time, in due.";
        return {};
    }

    pb::Due due = fromDue;
    assert(!due.timezone().empty());

    LOG_TRACE << "adjustDueTime after assignment due: " << toJson(due);

    // We need to use the date libs zone to get the local time calculations right
    const auto *local_ts = locate_zone(due.timezone());
    assert(local_ts);
    if (!local_ts) {
        LOG_WARN << "Failed to locate time zone: " << due.timezone();
        throw runtime_error{"Failed to locate time zone "s + due.timezone()};
    }

    auto ref_tp = chrono::system_clock::from_time_t(due.start());
    auto ref_date = floor<date::days>(ref_tp);
    auto zoned_ref = zoned_time{local_ts, ref_tp};
    auto t_local = zoned_ref.get_local_time();
    auto week_date = year_month_weekday{floor<days>(t_local)};
    auto ref_day = week_date.weekday();
    const auto wday = ref_day.c_encoding();
    const auto start_of_week_offset = uctx.sundayIsFirstWeekday() ? days(0) : days(1);

    auto start_date = year_month_day{floor<days>(t_local)};

    // Days from epoch in local time
    local_days ldays{start_date};

    // Time of day in local time
    hh_mm_ss time{t_local - floor<days>(t_local)};

    switch(due.kind()) {
    case pb::ActionDueKind::DATETIME:
        // TODO: We need a way to handle windows of time, not just a single point in time.
        due.set_due(due.start());
        break;
    case pb::ActionDueKind::DATE: {
        // End of day
        auto when = ldays + days(1) - 1min;
        due.set_due(toTimet(when, local_ts));
    } break;
    case pb::ActionDueKind::WEEK: {
        // End of week
        auto next = ldays;
        next += (days(wday) * -1) + days(6) + start_of_week_offset;
        if (next <= ldays) {
            next += days(7);
        }
        due.set_due(toTimet(next + days(1) - 1min, local_ts));
    } break;
    case pb::ActionDueKind::MONTH: {
        // End of month
        auto ymd = start_date.year() / start_date.month()  / last;
        auto next = local_days{ymd};
        if (next <= ldays) {
            ymd += months{1};
            if (!ymd.ok()) {
                ymd = ymd.year() / ymd.month() / last;
            }
            next = local_days{ymd};
        }
        due.set_due(toTimet(next + days(1) - 1min, local_ts));
    } break;
    case pb::ActionDueKind::QUARTER: {
        // End of quarter
        // Start of current quarter
        const auto qmonth = static_cast<unsigned>(quarters.at(static_cast<unsigned>(start_date.month()) - 1));

        // Jump to the end of the quarter
        auto ymd = start_date.year() / month(qmonth) / last;
        ymd +=  months{2};
        if (!ymd.ok()) {
            ymd = ymd.year() / ymd.month() / last;
        }

        auto next = local_days{ymd};

        if (next <= ldays) {
            ymd += months{3};
            if (!ymd.ok()) {
                ymd = ymd.year() / ymd.month() / last;
            }
            next = local_days{ymd};
        }

        due.set_due(toTimet(next + days(1) - 1min, local_ts));
    }break;
    case pb::ActionDueKind::YEAR: {
        // End of month
        auto ymd = start_date.year() / start_date.month()  / last;
        auto next = local_days{ymd};
        if (next <= ldays) {
            ymd += months{1};
            if (!ymd.ok()) {
                ymd = ymd.year() / ymd.month() / last;
            }
            next = local_days{ymd};
        }
        due.set_due(toTimet(next + days(1) - 1min, local_ts));
    } break;
    case pb::ActionDueKind::UNSET:
        due.clear_due();
        break;
    default:
        assert(false); // generated enums
    }

    LOG_TRACE << "adjustDueTime before return due: " << toJson(due);
    return due;
}

boost::asio::awaitable<void> GrpcServer::handleActionDone(const pb::Action &orig,
                                                          const UserContext& uctx,
                                                          ::grpc::CallbackServerContext *ctx)
{
    co_await endWorkSessionForAction(orig.id(), uctx);

    if (orig.repeatkind() == pb::Action_RepeatKind::Action_RepeatKind_NEVER) {
        co_return;
    }

    auto cloned = orig;
    cloned.set_id(newUuidStr());
    cloned.set_origin(orig.id());
    cloned.clear_completedtime();
    cloned.set_status(pb::ActionStatus::ACTIVE);

    time_t from_timepoint = 0;

    switch(orig.repeatkind()) {
    case pb::Action_RepeatKind::Action_RepeatKind_COMPLETED:
        from_timepoint = orig.completedtime();
        break;
    case pb::Action_RepeatKind::Action_RepeatKind_START_TIME:
        from_timepoint = orig.due().start();
        break;
    case pb::Action_RepeatKind::Action_RepeatKind_DUE_TIME:
        from_timepoint = orig.due().due();
        break;
    default:
        assert(false);
        break;
    }

    assert(from_timepoint != 0);

    pb::Due due;

    LOG_TRACE << "handleActionDone cloned.due: " << toJson(cloned.due());

    switch(orig.repeatwhen()) {
    case pb::Action_RepeatWhen::Action_RepeatWhen_AT_DATE:
        due = processDueAtDate(from_timepoint,
                               cloned.repeatunits(),
                               cloned.due().kind(),
                               cloned.repeatafter(),
                               uctx);
        break;
    case pb::Action_RepeatWhen::Action_RepeatWhen_AT_DAYSPEC:
        due = processDueAtDayspec(from_timepoint,
                                  cloned.repeatunits(),
                                  cloned.due().kind(),
                                  cloned.repeatafter(),
                                  uctx);
        break;
    default:
        assert(false);
        break;
    }

    LOG_TRACE << "handleActionDone before adj. due: " << toJson(due);

    due = adjustDueTime(due, uctx);
    *cloned.mutable_due() = due;

    LOG_TRACE << "handleActionDone due: " << toJson(due);
    co_await addAction(cloned, *this, ctx);

    co_return;
}

boost::asio::awaitable<void> GrpcServer::handleActionActive(const pb::Action &orig, const UserContext &uctx, ::grpc::CallbackServerContext *ctx)
{
    LOG_TRACE << "handleActionActive Action.done changed. Now it is active. Will delete its child (created if it was marked as done and recuirring) if exist, is not a parent and version is 1.";

    // We have to get the list of items to delete manually, as the database does not return that information.
    // We need the id's to notify the clients.
    const auto dres = co_await server().db().exec(
        "select b.id from action as a left join action as b on b.origin = a.id where a.id =? and b.version = 1 and a.user = ?",
        orig.id(), uctx.userUuid());

    // Do the actual delete. There is a potential race-condition here, but it is not a big deal, as it only affect notifications
    const auto res = co_await server().db().exec(
        "DELETE FROM action WHERE id IN (select b.id from action as a left join action as b on b.origin = a.id where a.id =? and b.version = 1 and a.user = ?)",
        orig.id(), uctx.userUuid());

    if (res.affected_rows() && dres.has_value()) {
        for(const auto drow : dres.rows()) {
            auto id = drow.at(0).as_string();
            pb::Update update;
            update.set_op(pb::Update::Operation::Update_Operation_DELETED);
            update.mutable_action()->set_id(id);
            publish(make_shared<pb::Update>(update));
        }
    }

    co_return;
}

} // ns
