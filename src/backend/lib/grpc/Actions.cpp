
#include "shared_grpc_server.h"
#include "nextapp/UserContext.h"
#include <boost/multi_array.hpp>

namespace nextapp::grpc {

namespace {

struct ToActionCategory {
    enum Cols {
        ID, NAME, COLOR, DESCR, VERSION, ICON
    };

    static constexpr auto columns = "id, name, color, descr, version, icon";

    static void assign(const boost::mysql::row_view& row, pb::ActionCategory& cat) {
        cat.set_id(pb_adapt(row.at(ID).as_string()));
        cat.set_name(pb_adapt(row.at(NAME).as_string()));
        if (row.at(COLOR).is_string()) {
            cat.set_color(pb_adapt(row.at(COLOR).as_string()));
        }
        if (row.at(DESCR).is_string()) {
            cat.set_descr(pb_adapt(row.at(DESCR).as_string()));
        }
        cat.set_version(static_cast<int32_t>(row.at(VERSION).as_int64()));
        if (row.at(ICON).is_string()) {
            cat.set_icon(pb_adapt(row.at(ICON).as_string()));
        }
    }
};

struct ToAction {
    enum Cols {
        ID, NODE, USER, VERSION, UPDATED, CREATED_DATE, ORIGIN, PRIORITY, URGENCY, IMPORTANCE, STATUS, FAVORITE, NAME, DUE_KIND,START_TIME, DUE_BY_TIME, DUE_TIMEZONE, COMPLETED_TIME, CATEGORY, // core
        DESCR, TIME_ESTIMATE, DIFFICULTY, REPEAT_KIND, REPEAT_UNIT, REPEAT_WHEN, REPEAT_AFTER, TIME_SPENT, TAGS // remaining
    };

    enum class ColType {
        IDS,
        READ_ONLY, // Read only, informative in normal queries
        CORE,
        REMAINING
    };


    static auto coreSelectCols() {
        static const auto cols = filteredCols({ColType::IDS, ColType::READ_ONLY, ColType::CORE});
        return cols;
    }

    static string_view allSelectCols() {
        static const auto cols = filteredCols({ColType::IDS, ColType::READ_ONLY, ColType::CORE, ColType::REMAINING});
        return cols;
    }

    static std::string makeBindingStr() {
        auto cols = filteredCols({ColType::IDS, ColType::CORE, ColType::REMAINING});
        auto numCols = std::count(cols.begin(), cols.end(), ',') + 1;

        std::string binding;
        for (int i = 0; i < numCols; ++i) {
            if (i > 0) {
                binding += ",";
            }
            binding += "?";
        }

        return binding;
    }

    static string_view statementBindingStr() {
        static const auto bs = makeBindingStr();
        return bs;
    }

    static string_view updateStatementBindingStr() {
        static const string cols = filteredCols({ColType::CORE, ColType::REMAINING}, true);
        return cols;
    }

    static string_view insertCols() {
        static const string cols = filteredCols({ColType::IDS, ColType::CORE, ColType::REMAINING});
        return cols;
    }

    template <bool argsFirst = true, typename ...Args>
    static auto prepareBindingArgs(const pb::Action& action, const UserContext& uctx, Args... args) {
        optional<string> priority;
        optional<int32_t> urgency, importance;

        if (action.dynamicpriority().has_urgencyimportance()) {
            urgency = action.dynamicpriority().urgencyimportance().urgency();
            importance = action.dynamicpriority().urgencyimportance().importance();
        }
        if (action.dynamicpriority().has_priority()) {
            priority = pb::ActionPriority_Name(action.dynamicpriority().priority());
        }

        string tags;
        tags.reserve(120);
        for(const auto& tag: action.tags()) {
            if (!tags.empty()) {
                tags += ' ';
            }
            tags += tag;
        }

        auto bargs = make_tuple(
            toStringViewOrNull(action.origin()),
            priority,
            urgency,
            importance,
            pb::ActionStatus_Name(action.status()),
            action.favorite(),
            action.name(),

            // due
            pb::ActionDueKind_Name(action.due().kind()),
            toAnsiTime(action.due().start()),
            toAnsiTime(action.due().due()),
            toStringViewOrNull(action.due().timezone()),

            toAnsiTime(action.completedtime(),  uctx.tz()),
            toStringViewOrNull(action.category()),
            action.descr(),
            action.timeestimate(),
            pb::ActionDifficulty_Name(action.difficulty()),
            pb::Action::RepeatKind_Name(action.repeatkind()),
            toStringViewOrNull(pb::Action::RepeatUnit_Name(action.repeatunits())),
            toStringViewOrNull(pb::Action::RepeatWhen_Name(action.repeatwhen())),
            action.repeatafter(),
            action.timespent(),
            toStringOrNull(tags)
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
        obj.set_id(pb_adapt(row.at(ID).as_string()));
        if (row.at(NODE).is_string()) {
            obj.set_node(pb_adapt(row.at(NODE).as_string()));
        }
        obj.set_version(static_cast<int32_t>(row.at(VERSION).as_int64()));

        if (row.at(ORIGIN).is_string()) {
            obj.set_origin(pb_adapt(row.at(ORIGIN).as_string()));
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
            if (row.at(URGENCY).is_int64() && row.at(IMPORTANCE).is_int64()) {
                auto *ui = obj.mutable_dynamicpriority()->mutable_urgencyimportance();
                assert(ui);
                if (ui) {
                    ui->set_urgency(row.at(URGENCY).as_int64());
                    ui->set_importance(row.at(IMPORTANCE).as_int64());
                }
            } else if (row.at(PRIORITY).is_string()) {
                auto *d = obj.mutable_dynamicpriority();
                assert(d);
                if (d) {
                    pb::ActionPriority pri;
                    const auto name = toUpper(row.at(PRIORITY).as_string());
                    if (pb::ActionPriority_Parse(name, &pri)) {
                        d->set_priority(pri);
                    } else {
                        LOG_WARN_N << "Invalid ActionPriority: " << name;
                    }
                }
            }
        }

        if (row.at(NAME).is_string()) {
            obj.set_name(pb_adapt(row.at(NAME).as_string()));
        }

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
            obj.mutable_due()->set_due(toTimeT(row.at(DUE_BY_TIME).as_datetime()));

            if (row.at(START_TIME).is_datetime()) {
                obj.mutable_due()->set_start(toTimeT(row.at(START_TIME).as_datetime()));
            }

            if (row.at(DUE_TIMEZONE).is_string()) {
                obj.mutable_due()->set_timezone(pb_adapt(row.at(DUE_TIMEZONE).as_string()));
            }
        }

        //LOG_TRACE << "Assigning action*: " << toJson(obj, 2);

        if (obj.status() == pb::ActionStatus::DONE) {
            obj.set_kind(pb::ActionKind::AC_DONE);
        } else {
            // TODO: Add this. Currently the client will do this for all received actions.
            obj.set_kind(pb::ActionKind::AC_UNSET);
        }

        if (row.at(COMPLETED_TIME).is_datetime()) {
            obj.set_completedtime(toTimeT(row.at(COMPLETED_TIME).as_datetime(), uctx.tz()));
        }

        if (row.at(CATEGORY).is_string()) {
            obj.set_category(pb_adapt(row.at(CATEGORY).as_string()));
        }

        if constexpr (std::is_same_v<T, pb::Action>) {
            if (row.at(DESCR).is_string()) {
                obj.set_descr(pb_adapt(row.at(DESCR).as_string()));
            }
            if (row.at(TIME_ESTIMATE).is_int64()) {
                obj.set_timeestimate(row.at(TIME_ESTIMATE).as_int64());
            }
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
            if (row.at(REPEAT_UNIT).is_string()) {
                pb::Action::RepeatUnit ru;
                const auto name = toUpper(row.at(REPEAT_UNIT).as_string());
                if (pb::Action::RepeatUnit_Parse(name, &ru)) {
                    obj.set_repeatunits(ru);
                } else {
                    LOG_WARN_N << "Invalid RepeatUnit: " << name;
                }
            }
            if (row.at(REPEAT_WHEN).is_string()) {
                pb::Action::RepeatWhen rw;
                const auto name = toUpper(row.at(REPEAT_WHEN).as_string());
                if (pb::Action::RepeatWhen_Parse(name, &rw)) {
                    obj.set_repeatwhen(rw);
                } else {
                    LOG_WARN_N << "Invalid RepeatWhen: " << name;
                }
            }
            if (row.at(REPEAT_AFTER).is_int64()) {
                obj.set_repeatafter(row.at(REPEAT_AFTER).as_int64());
            }
            obj.set_updated(toMsTimestamp(row.at(UPDATED).as_datetime(), uctx.tz()));

            if (row.at(TIME_SPENT).is_int64()) {
                obj.set_timespent(row.at(TIME_SPENT).as_int64());
            }

            if (row.at(TAGS).is_string()) {
                auto tags = pb_adapt(row.at(TAGS).as_string());
                if (auto *tag = obj.mutable_tags()) {
                    tag->Clear();
                    for (auto part : tags | std::views::split(' ')) {
                        const string t{part.begin(), part.end()};
                        if (auto *tptr = tag->Add()) {
                            *tptr = std::move(t);
                        }
                    }
                }
            }
        }
    }

private:
    static constexpr array<pair<string_view, ColType>, 28> cols_ = {{
        make_pair("id", ColType::IDS),
        make_pair("node", ColType::IDS),
        make_pair("user", ColType::IDS),
        make_pair("version", ColType::READ_ONLY),
        make_pair("updated", ColType::READ_ONLY),
        make_pair("created_date", ColType::READ_ONLY),
        make_pair("origin", ColType::CORE),
        make_pair("priority", ColType::CORE),
        make_pair("dyn_urgency", ColType::CORE),
        make_pair("dyn_importance", ColType::CORE),
        make_pair("status", ColType::CORE),
        make_pair("favorite", ColType::CORE),
        make_pair("name", ColType::CORE),
        make_pair("due_kind", ColType::CORE),
        make_pair("start_time", ColType::CORE),
        make_pair("due_by_time", ColType::CORE),
        make_pair("due_timezone", ColType::CORE),
        make_pair("completed_time", ColType::CORE),
        make_pair("category", ColType::CORE),
        make_pair("descr", ColType::REMAINING),
        make_pair("time_estimate", ColType::REMAINING),
        make_pair("difficulty", ColType::REMAINING),
        make_pair("repeat_kind", ColType::REMAINING),
        make_pair("repeat_unit", ColType::REMAINING),
        make_pair("repeat_when", ColType::REMAINING),
        make_pair("repeat_after", ColType::REMAINING),
        make_pair("time_spent", ColType::REMAINING),
        make_pair("tags", ColType::REMAINING)
    }};

    static std::string filteredCols(std::initializer_list<ColType> ct, bool update = false) {
        std::string filtered;
        bool first = true;

        auto filter = [&ct](const auto& row) {
            return std::find(ct.begin(), ct.end(), row.second) != ct.end();
        };

        auto relevant = cols_ | std::views::filter(filter)
                        | std::views::transform([](const auto& row) { return row.first; });

        for (const auto& col : relevant) {
            if (col.empty()) {
                continue; // why???
            }
            if (first) [[unlikely]] {
                first = false;
            } else {
                filtered += ", ";
            }

            filtered += col;

            if (update) {
                filtered += "=?";
            }
        }

        return filtered;
    }
};

enum class DoneChanged {
    NO_CHANGE,
    MARKED_DONE,
    MARKED_UNDONE
};

boost::asio::awaitable<void>
 replyWithAction(GrpcServer& grpc, const std::string actionId, RequestCtx& rctx,
                ::grpc::CallbackServerContext *ctx, pb::Status *reply,
                DoneChanged done = DoneChanged::NO_CHANGE, bool publish = true,
                pb::Update::Operation op = pb::Update::Operation::Update_Operation_UPDATED) {

    const auto dbopts = rctx.uctx->dbOptions();

    auto *action = reply->mutable_action();
    co_await grpc.getAction(*action, actionId, rctx);

    auto res = co_await rctx.dbh->exec(
        format(R"(SELECT {} from action WHERE id=? AND user=? )",
               ToAction::allSelectCols()), dbopts, actionId, rctx.uctx->userUuid());

    assert(res.has_value());
    if (publish) {
        auto& update = rctx.publishLater(op);
        *update.mutable_action() = *action;
    }

    switch (done) {
    case DoneChanged::MARKED_DONE:
        co_await grpc.handleActionDone(*action, rctx, ctx);
        break;
    case DoneChanged::MARKED_UNDONE:
        co_await grpc.handleActionActive(*action, rctx, ctx);
        break;
    default:
        break;
    }

    LOG_TRACE << "Reply: " << grpc.toJsonForLog(*reply);
}

void sanitize(pb::Due& d) {
    if (!d.has_due() || d.due() == 0) {
        d.clear_start();
        d.set_kind(pb::ActionDueKind::UNSET);
        d.clear_timezone();
    }
}

void sanitize(pb::Action& action) {
    if (action.has_due()) {
        auto* d = action.mutable_due();
        sanitize(*d);
    }
}

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetActions(::grpc::CallbackServerContext *ctx, const pb::GetActionsReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto& dbopts = uctx->dbOptions();

            auto limit = owner_.server().config().options.max_page_size;
            if (auto page_size = req->page().pagesize(); page_size > 0) {
                limit = min<decltype(limit)>(page_size, limit);
            }

            // We are building a dynamic query.
            // We need to add the arguments in the same order as we add '?' to the query
            // as boost.mysql lacks the ability to bind by name.
            std::vector<boost::mysql::field_view> args;
            std::string query;
            std::string_view sortcol = "a.due_by_time";
            static const string prefixed_cols = prefixNames(ToAction::coreSelectCols(), "a.");

            if (req->has_nodeidandchildren()) {
                query = format(R"(WITH RECURSIVE node_tree AS (
                SELECT id FROM node WHERE id=? and user=?
                    UNION ALL
                    SELECT n.id FROM node n INNER JOIN node_tree nt ON n.parent = nt.id
                )
                SELECT {}, {} AS sort_ts
                FROM action a
                INNER JOIN node_tree nt ON a.node = nt.id)",
                                   prefixed_cols, sortcol);
                    args.emplace_back(req->nodeidandchildren());
                    args.emplace_back(cuser);
            } else if (req->has_ontodayscalendar() && req->ontodayscalendar()) {
                query = format(R"(SELECT UNIQUE {}, {} AS sort_ts FROM action a
                    JOIN time_block_actions tba on a.id=tba.action
                    JOIN time_block tb on tba.time_block=tb.id
                    WHERE a.user=?
                    AND (DATE(tb.start_time) <= CURDATE() AND DATE(tb.end_time) >= CURDATE())
                     )", prefixed_cols, sortcol);
                args.emplace_back(cuser);
            } else {
                query = format("SELECT {}, {} AS sort_ts FROM action a WHERE user=? ",
                               prefixed_cols, sortcol);
                args.emplace_back(cuser);

                if (req->has_favorites() && req->favorites()) {
                    query += " AND a.favorite = 1 ";
                }

                if (req->has_nodeid()) {
                    query += " AND a.node = ? ";
                    args.emplace_back(req->nodeid());
                }
            }

            string unscheduled;
            if (req->flags().unscheduled()) {
                unscheduled = " (a.due_by_time IS NULL) ";
            }

            optional<string> startspan_start, startspan_end;
            if (req->has_startspan()) {
                startspan_start = toAnsiTime(req->startspan().start());
                startspan_end = toAnsiTime(req->startspan().end());
                query += " AND (( a.start_time >= ? AND a.start_time < ? ) ";
                args.emplace_back(*startspan_start);
                args.emplace_back(*startspan_end);
                if (!unscheduled.empty()) {
                    unscheduled += " OR " + unscheduled;
                }
                query += ") ";
            }

            optional<string> duespan_start, duespan_end;
            if (req->has_duespan()) {
                duespan_start = toAnsiTime(req->duespan().start());
                duespan_end = toAnsiTime(req->duespan().end());
                query += " AND (( a.due_by_time >= ? AND a.due_by_time < ?) ";
                args.emplace_back(*duespan_start);
                args.emplace_back(*duespan_end);
                if (!unscheduled.empty()) {
                    unscheduled += " OR " + unscheduled;
                }

                query += ") ";
            }

            if (req->flags().active() && !req->flags().done()) {
                query += " AND (a.status!='done' || DATE(completed_time) = CURDATE()) ";
            } else if (req->flags().done() && !req->flags().active()) {
                query += " AND a.status='done' ";
            }

            string_view order = "a.status DESC, a.priority DESC, sort_ts, a.created_date";

            query += format(" ORDER BY {} LIMIT ? OFFSET ?", order);
            args.emplace_back(limit + 1);
            args.emplace_back(req->page().offset());

            const auto res = co_await owner_.server().db().exec(query, dbopts, args);

            if (res.has_value()) {
                const auto rows_count = res.rows().size();
                const auto has_more = rows_count > limit;
                size_t rows = 0;
                reply->set_hasmore(has_more);
                auto *actions = reply->mutable_actions();
                for(const auto& row : res.rows()) {
                    if (++rows > limit) {
                        assert(has_more && reply->has_hasmore());
                        break;
                    }
                    ToAction::assign(row, *actions->add_actions(), *uctx);;
                }
            }

            reply->set_fromstart(req->page().offset() == 0);

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetAction(::grpc::CallbackServerContext *ctx, const pb::GetActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
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

boost::asio::awaitable<void> addAction(pb::Action action, GrpcServer& owner, RequestCtx& rctx,
                                       pb::Status *reply = {}) {
    const auto& cuser = rctx.uctx->userUuid();
    auto dbopts = rctx.uctx->dbOptions();


    if (action.node().empty()) {
        throw runtime_error{"Missing node"};
    }

    co_await owner.validateNode(*rctx.dbh, action.node(), cuser);

    if (action.id().empty()) {
        action.set_id(newUuidStr());
    }
    if (action.status() == pb::ActionStatus::DONE && action.completedtime() == 0) {
        action.set_completedtime(time({}));
    }

    sanitize(action);

    // Not an idempotent query
    dbopts.reconnect_and_retry_query = false;

    const auto res = co_await rctx.dbh->exec(
        format("INSERT INTO action ({}) VALUES ({}) RETURNING {} ",
               ToAction::insertCols(),
               ToAction::statementBindingStr(),
               ToAction::allSelectCols()),
        dbopts,
        ToAction::prepareBindingArgs(action, *rctx.uctx, action.id(), action.node(), cuser));

    assert(!res.empty());
    // Set the reply data

    auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
    if (reply) {
        auto *reply_action = reply->mutable_action();
        ToAction::assign(res.rows().front(), *reply_action, *rctx.uctx);
        *update.mutable_action() = *reply_action;
    } else {
        ToAction::assign(res.rows().front(), *update.mutable_action(), *rctx.uctx);
    }
}

boost::asio::awaitable<void> GrpcServer::saveActions(jgaa::mysqlpool::Mysqlpool::Handle& dbh,
                                         const pb::CompleteActions& actions,
                                         RequestCtx& rctx) {

    const auto& cuser = rctx.uctx->userUuid();

    const auto sql = format("INSERT INTO action ({}) VALUES ({}) RETURNING {} ",
                            ToAction::insertCols(),
                            ToAction::statementBindingStr(),
                            ToAction::allSelectCols());

    auto generator = jgaa::mysqlpool::BindTupleGenerator(actions.actions(),
                                                         [cuser, &rctx] (const auto& v) {
        string_view id = v.id();
        string_view node = v.node();
        string_view user = cuser;

        LOG_TRACE_N << "Binding action: " << v.id() << " " << v.name() << ", category: " << v.category();
        return ToAction::prepareBindingArgs(v, *rctx.uctx, id, node, user);
    });

    co_await dbh.exec(sql, generator);
    LOG_TRACE_N << "done saving action batch";
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            pb::Action new_action{*req};
            new_action.set_node(req->node());

            auto trx = co_await rctx.dbh->transaction();

            co_await addAction(new_action, owner_, rctx, reply);

            co_await trx.commit();
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& uuid = validatedUuid(req->id());
            const auto& dbopts = rctx.uctx->dbOptions();
            DoneChanged done = DoneChanged::NO_CHANGE;

            if (req->node().empty()) {
                throw runtime_error{"Missing node"};
            }

            auto existing_res = co_await rctx.dbh->exec("SELECT node FROM action WHERE id=? AND user=?", uuid, cuser);
            if (existing_res.empty() || existing_res.rows().empty()) {
                throw server_err{pb::Error::NOT_FOUND, "Action does not exist"};
            }

            const auto current_node = existing_res.rows().front().at(0).as_string();
            if (current_node != req->node()) {
                throw server_err(pb::Error::CONSTRAINT_FAILED, "UpdateAction cannot change the node. Use MoveAction for that.");
            }

            co_await owner_.validateNode(*rctx.dbh, req->node(), cuser);

            auto trx = co_await rctx.dbh->transaction();

            pb::Action new_action{*req};
            assert(new_action.id() == uuid);
            if (new_action.status() == pb::ActionStatus::DONE && new_action.completedtime() == 0) {
                new_action.set_completedtime(time({}));
                done = DoneChanged::MARKED_DONE;
            } else if (new_action.status() != pb::ActionStatus::DONE && new_action.completedtime() != 0) {
                new_action.set_completedtime(0);
                done = DoneChanged::MARKED_UNDONE;
            }
            sanitize(new_action);

            const string sql = format("UPDATE action SET {} WHERE id=? AND user=? ",
                              ToAction::updateStatementBindingStr());
            const auto args = ToAction::prepareBindingArgs<false>(new_action, *rctx.uctx, uuid, cuser);
            auto res = co_await rctx.dbh->exec(sql, dbopts, args);

            assert(!res.empty());

            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, rctx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_await trx.commit();

        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateActions(::grpc::CallbackServerContext *ctx, const pb::UpdateActionsReq *req, pb::Status *reply) {
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            if (req->actions().size() > owner_.server().config().options.max_batch_updates) {
                throw server_err{pb::Error::INVALID_ARGUMENT,
                                 format("Too many actions to update. The limit is {}",
                                        owner_.server().config().options.max_batch_updates)};
            };

            const auto& cuser = rctx.uctx->userUuid();
            const auto& dbopts = rctx.uctx->dbOptions();
            DoneChanged done = DoneChanged::NO_CHANGE;
            auto trx = co_await rctx.dbh->transaction();

            string updates;
            auto append = [&updates](const string& col) {
                if (!updates.empty()) {
                    updates += ", ";
                }
                updates += col;
            };

            if (req->has_due()) {
                append("start_time=?, due_by_time=?, due_timezone=?, due_kind=?");
            };

            if (req->has_priority()) {
                append("priority=?");
            }

            if (req->has_status()) {
                append("status=?");
                throw server_err(pb::Error::GENERIC_ERROR, "status update not supported yet");
            }

            if (req->has_favorite()) {
                append("favorite=?");
            }

            if (req->has_category()) {
                append("category=?");
            }

            if (req->has_difficulty()) {
                append("difficulty=?");
            }

            if (req->has_timeestimate()) {
                append("time_estimate=?");
            }

            string sql = format("UPDATE action SET {} WHERE user=? AND id=? ", updates);

            for (const auto& uuid: req->actions()) {
                //co_await owner_.validateAction(*rctx.dbh, uuid.uuid(), cuser);

                // Build the binding args
                std::vector<boost::mysql::field_view> args;

                // We are using views to bind args, so we need data in actual buffers to view.
                optional<string> start, due, time_zone;
                string kind, priority, difficulty;

                if (req->has_due()) {
                    auto d = req->due();
                    sanitize(d);
                    if (d.has_start()) {
                        if (start = toAnsiTime(d.start()); start.has_value()) {
                            args.emplace_back(*start);
                        } else {
                            args.emplace_back(nullptr);
                        }
                    } else {
                        args.emplace_back(nullptr);
                    }
                    if (d.has_due()) {
                        if (due = toAnsiTime(d.due()); due.has_value()) {
                            args.emplace_back(*due);
                        } else {
                            args.emplace_back(nullptr);
                        }
                    } else {
                        args.emplace_back(nullptr);
                    }
                    if (time_zone = toStringOrNull(d.timezone()); time_zone.has_value()) {
                        args.emplace_back(*time_zone);
                    } else {
                        args.emplace_back(nullptr);
                    }
                    kind = pb::ActionDueKind_Name(d.kind());
                    args.emplace_back(kind);
                }

                if (req->has_priority()) {
                    priority = pb::ActionPriority_Name(req->priority());
                    args.emplace_back(priority);
                }

                if (req->has_favorite()) {
                    args.emplace_back(req->favorite());
                }

                if (req->has_category()) {
                    args.emplace_back(req->category().uuid());
                }

                if (req->has_difficulty()) {
                    difficulty = pb::ActionDifficulty_Name(req->difficulty());
                    args.emplace_back(difficulty);
                }

                if (req->has_timeestimate()) {
                    args.emplace_back(req->timeestimate());
                }

                args.emplace_back(cuser);
                args.emplace_back(uuid.uuid());

                auto res = co_await rctx.dbh->exec(sql, dbopts, args);
                assert(res.has_value());
                auto& publish = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                auto* action = publish.mutable_action();
                co_await owner_.getAction(*action, uuid.uuid(), rctx);
            }

            co_await trx.commit();

        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteAction(::grpc::CallbackServerContext *ctx, const pb::DeleteActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->actionid());
            auto trx = co_await rctx.dbh->transaction();
            co_await owner_.deleteAction(uuid, rctx);
            co_await trx.commit();
            reply->set_deletedactionid(uuid);
            co_return;
        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::deleteAction(const std::string& uuid, RequestCtx& rctx) {
    const auto& cuser = rctx.uctx->userUuid();
    const auto dbopts = rctx.uctx->dbOptions();
    const auto uctx = rctx.uctx;

    co_await deleteActionInTimeBlocks(uuid, rctx);

    // Fetch all work-sessions that have this action and delete them
    auto wss = co_await rctx.dbh->exec("SELECT id FROM work_session WHERE action=? AND user=?", dbopts, uuid, cuser);
    for (const auto& ws : wss.rows()) {
        co_await deleteWorkSession(ws.at(0).as_string(), rctx);
    }

    // Now, mark the action as deleted
    const auto res = co_await rctx.dbh->exec(R"(UPDATE action SET
        node=NULL,
        origin=NULL,
        priority='pri_normal',
        status='deleted',
        favorite=0,
        name=NULL,
        descr=NULL,
        due_kind='unset',
        start_time=NULL,
        due_by_time=NULL,
        due_timezone=NULL,
        completed_time=NULL,
        time_estimate=NULL,
        difficulty='normal',
        repeat_kind='never',
        repeat_unit='days',
        repeat_when='at_date',
        repeat_after=0,
        category=NULL
        WHERE id=? AND user=?)", dbopts, uuid, cuser);

    auto& pub = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
    if (auto* action = pub.mutable_action()) {
        co_await getAction(*action, uuid, rctx);
    } else {
        assert(false);
    }

    assert(res.has_value());
    co_return;
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsDone(::grpc::CallbackServerContext *ctx, const pb::ActionDoneReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = rctx.uctx->dbOptions();
            DoneChanged done = DoneChanged::MARKED_UNDONE;

            optional<string> when;
            if (req->done()) {
                when = toAnsiTime(time({}), rctx.uctx->tz());
                done = DoneChanged::MARKED_DONE;
            }

            auto trx = co_await rctx.dbh->transaction();

            const auto res = co_await rctx.dbh->exec(
                "UPDATE action SET status=?, completed_time=? WHERE id=? AND user=?",
                dbopts, (req->done() ? "done" : "active"), when, uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, rctx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated. affected_rows={}", uuid, res.affected_rows()));
            }

            co_await trx.commit();

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsFavorite(::grpc::CallbackServerContext *ctx,
                                                                          const pb::ActionFavoriteReq *req,
                                                                          pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;
            const auto& cuser = uctx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = uctx->dbOptions();

            auto trx = co_await rctx.dbh->transaction();

            auto res = co_await rctx.dbh->exec(
                "UPDATE action SET favorite=? WHERE id=? AND user=?",
                dbopts, req->favorite(), uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, rctx, ctx, reply);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_await trx.commit();
            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetFavoriteActions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx] (auto *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto uctx = rctx.uctx;
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
                af->set_actionid(pb_adapt(row.at(A_ID).as_string()));
                af->set_actionname(pb_adapt(row.at(A_NAME).as_string()));
                af->set_nodeid(pb_adapt(row.at(N_ID).as_string()));
                af->set_nodename(pb_adapt(row.at(N_NAME).as_string()));
                af->set_actionversion(static_cast<int32_t>(row.at(A_VERSION).as_int64()));
            }

            co_return;
        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::validateAction(const std::string &actionId, const std::string &userUuid, std::string *name) {
    auto handle = co_await server().db().getConnection();
    co_await validateAction(handle , actionId, userUuid, name);
}

boost::asio::awaitable<void> GrpcServer::validateAction(jgaa::mysqlpool::Mysqlpool::Handle &handle, const std::string &actionId, const std::string &userUuid, std::string *name)
{
    auto res = co_await handle.exec("SELECT id, name FROM action where id=? and user=?", actionId, userUuid);
    if (!res.has_value() && !res.rows().empty()) {
        throw server_err{pb::Error::INVALID_ACTION, "Action not found for the current user"};
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
                                                          RequestCtx& rctx,
                                                          ::grpc::CallbackServerContext *ctx)
{
    co_await endWorkSessionForAction(orig.id(), rctx);

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
                               *rctx.uctx);
        break;
    case pb::Action_RepeatWhen::Action_RepeatWhen_AT_DAYSPEC:
        due = processDueAtDayspec(from_timepoint,
                                  cloned.repeatunits(),
                                  cloned.due().kind(),
                                  cloned.repeatafter(),
                                  *rctx.uctx);
        break;
    default:
        assert(false);
        break;
    }

    LOG_TRACE << "handleActionDone before adj. due: " << toJson(due);

    due = adjustDueTime(due, *rctx.uctx);
    *cloned.mutable_due() = due;

    LOG_TRACE << "handleActionDone due: " << toJson(due);
    co_await addAction(cloned, *this, rctx);

    co_return;
}

boost::asio::awaitable<void> GrpcServer::handleActionActive(const pb::Action &orig, RequestCtx& rctx, ::grpc::CallbackServerContext *ctx)
{
    LOG_TRACE << "handleActionActive Action.done changed. Now it is active. Will delete its child (created if it was marked as done and recuirring) if exist, is not a parent and version is 1.";

    // We have to get the list of items to delete manually, as the database does not return that information.
    // We need the id's to notify the clients.
    const auto dres = co_await rctx.dbh->exec(
        "select b.id from action as a left join action as b on b.origin = a.id where a.id =? and b.version = 1 and a.user = ?",
        orig.id(), rctx.uctx->userUuid());

    // There should only be at maximum one item in the result set.
    assert(dres.rows().size() <= 1);
    for(const auto drow : dres.rows()) {
        auto id = drow.at(0).as_string();
        co_await deleteAction(id, rctx);
    }

    co_return;
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MoveAction(::grpc::CallbackServerContext *ctx, const pb::MoveActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& action_uuid = validatedUuid(req->actionid());
            const auto& node_uuid = validatedUuid(req->nodeid());
            DoneChanged done = DoneChanged::NO_CHANGE;

            co_await owner_.validateNode(*rctx.dbh, node_uuid, cuser);
            co_await owner_.validateAction(*rctx.dbh, action_uuid, cuser);

            auto res = co_await rctx.dbh->exec("UPDATE action SET node=? WHERE id=? AND user=? and node != ?",
                                                      rctx.uctx->dbOptions(), node_uuid, action_uuid, cuser, node_uuid);

            assert(!res.empty());

            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, action_uuid, rctx, ctx, reply, DoneChanged::NO_CHANGE,
                                         true, pb::Update::Operation::Update_Operation_MOVED);

            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not moved.", action_uuid));
            }

        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::fetchActionsForCalendar(pb::CalendarEvents &events, RequestCtx &rctx, const time_t &day)
{
    auto res = co_await rctx.dbh->exec(
        format("SELECT {} FROM action WHERE "
               "DATE(start_time) = DATE(FROM_UNIXTIME(?)) and DATE(due_by_time) = DATE(start_time) AND due_kind='datetime' AND user=? "
               "ORDER BY start_time, due_by_time, id",
               ToAction::coreSelectCols()), day, rctx.uctx->userUuid());

    if (res.has_value()) {
        for(const auto& row : res.rows()) {
            auto *event = events.add_events();
            auto *ai = event->mutable_action();
            ToAction::assign(row, *ai, *rctx.uctx);
            auto& tspan = *event->mutable_timespan();
            tspan.set_start(ai->due().start());
            tspan.set_end(ai->due().due());
        }
    }

    co_return;
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateActionCategory(::grpc::CallbackServerContext *ctx, const pb::ActionCategory *req, pb::Status *reply) {
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            const auto& cuser = rctx.uctx->userUuid();
            const auto& dbopts = rctx.uctx->dbOptions();

            auto res = co_await rctx.dbh->exec(
                format("INSERT INTO action_category (USER, NAME, DESCR, COLOR, ICON) VALUES (?,?,?,?,?) RETURNING {}",
                       ToActionCategory::columns),
                dbopts,
                cuser, req->name(), toStringOrNull(req->descr()), req->color(), toStringOrNull(req->icon()));

            assert(!res.empty());
            if (res.rows().empty()) [[unlikely]] {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("ActionCategory with id={} was not created.", req->id()));
            } else {
                auto *reply_ac = reply->mutable_actioncategory();
                ToActionCategory::assign(res.rows().front(), *reply_ac);

                auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_ADDED);
                *update.mutable_actioncategory() = *reply_ac;
            }

        }, __func__);
}

boost::asio::awaitable<void> GrpcServer::saveActionCategories(jgaa::mysqlpool::Mysqlpool::Handle& dbh,
                                                  const pb::ActionCategories& categories,
                                                  RequestCtx& rctx) {

    const auto& cuser = rctx.uctx->userUuid();
    const auto &cats = categories.categories();
    const size_t items = cats.size();

    auto sql = "INSERT INTO action_category (id, user, name, descr, color, icon) VALUES (?,?,?,?,?,?)";
    enum Cols {
        ID, USER, NAME, DESCR, COLOR, ICON, COLS_
    };

    jgaa::mysqlpool::FieldViewMatrix values{items, COLS_};

    size_t index = 0;
    for(const auto& row : cats) {
        assert(index < values.rows());
        assert(!row.id().empty() && "ActionCategory id must not be empty");
        values.set(index, ID, row.id());
        values.set(index, USER, cuser);
        values.set(index, NAME, row.name());
        values.set(index, DESCR, toStringViewOrNull(row.descr()));
        values.set(index, COLOR, row.color());
        values.set(index, ICON, toStringViewOrNull(row.icon()));
        ++index;
    }

    co_await dbh.exec(sql, values);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateActionCategory(::grpc::CallbackServerContext *ctx, const pb::ActionCategory *req, pb::Status *reply) {

    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();
            const auto& dbopts = rctx.uctx->dbOptions();

            auto res = co_await rctx.dbh->exec(
                "UPDATE action_category SET name=?, descr=?, color=?, icon=?  WHERE id=? AND user=?",
                dbopts,
                req->name(), toStringOrNull(req->descr()), req->color(), toStringOrNull(req->icon()), req->id(), cuser);

            assert(!res.empty());
            if (res.affected_rows() == 1) {
                auto *reply_ac = reply->mutable_actioncategory();
                *reply_ac = *req;

                auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                *update.mutable_actioncategory() = *reply_ac;
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("ActionCategory with id={} was not updated.", req->id()));
            }

        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteActionCategory(::grpc::CallbackServerContext *ctx, const pb::DeleteActionCategoryReq *req, pb::Status *reply) {

    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto& cuser = rctx.uctx->userUuid();

            auto res = co_await rctx.dbh->exec("DELETE FROM action_category WHERE id=? AND user=?", req->id(), cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                reply->set_uuid(req->id());
                auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
                update.mutable_actioncategory()->set_id(req->id());
            } else {
                reply->set_uuid(req->id());
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message(format("ActionCategory with id={} not found for the current user.", req->id()));
            }
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetActionCategories(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) {
    return unaryHandler(ctx, req, reply,
        [this, ctx] (auto *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            const auto uctx = rctx.uctx;

            auto dbh = co_await owner_.server().db().getConnection();
            reply->mutable_actioncategories()->CopyFrom(
                co_await owner_.getActionCategories(dbh, uctx->userUuid()));

        }, __func__);
}

boost::asio::awaitable<pb::ActionCategories> GrpcServer::getActionCategories(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh, std::string_view userId) {

    pb::ActionCategories acats;

    auto res = co_await dbh.exec(
        format("SELECT {} FROM action_category WHERE user=?", ToActionCategory::columns),
        userId);

    assert(!res.empty());

    for(const auto& row : res.rows()) {
        auto *ac = acats.add_categories();
        ToActionCategory::assign(row, *ac);
    }

    co_return acats;
}

::grpc::ServerWriteReactor<pb::Status> *
GrpcServer::NextappImpl::GetNewActions(::grpc::CallbackServerContext *ctx, const pb::GetNewReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();

        auto flush = [&](pb::Status& status) -> boost::asio::awaitable<void> {
            co_await stream->sendMessage(std::move(status), boost::asio::use_awaitable);
        };

        const auto total_rows = co_await owner_.exportActions(req->since(), *rctx.dbh, flush, rctx);

        LOG_DEBUG_N << "Sent " << total_rows << " actions to client.";
        co_return;

    }, __func__);
}

boost::asio::awaitable<uint64_t> GrpcServer::exportActions(const uint64_t since,
                                                           jgaa::mysqlpool::Mysqlpool::Handle& dbh,
                                                           const export_flush_fn_t& flush_fn,
                                                           RequestCtx& rctx,
                                                           bool removeDeleted) {

    const auto uctx = rctx.uctx;
    const auto& cuser = uctx->userUuid();
    const auto batch_size = server().config().options.stream_batch_size;

    // Use batched reading from the database, so that we can get all the data, but
    // without running out of memory.
    // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
    co_await  dbh.start_exec(
        format("SELECT {} from action WHERE user=? AND updated > ? {} ORDER BY created_date",
               ToAction::allSelectCols(),
               removeDeleted ? "AND status != 'deleted'" : ""),
        uctx->dbOptions(), cuser, toMsDateTime(since, uctx->tz()));

    nextapp::pb::Status reply;

    auto *actions = reply.mutable_completeactions();
    auto num_rows_in_batch = 0u;
    auto total_rows = 0u;
    auto batch_num = 0u;

    auto flush = [&]() -> boost::asio::awaitable<void> {
        reply.set_error(::nextapp::pb::Error::OK);
        assert(reply.has_completeactions());
        ++batch_num;
        reply.set_message(format("Fetched {} actions in batch {}", reply.actions().actions_size(), batch_num));
        co_await flush_fn(reply);
        reply.Clear();
        actions = reply.mutable_completeactions();
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
            auto * action = actions->add_actions();
            ToAction::assign(row, *action, *rctx.uctx);
            ++total_rows;
            // Do we need to flush?
            if (++num_rows_in_batch >= batch_size) {
                co_await flush();
            }
        }

    } // read more from db loop

    co_await flush();

    LOG_TRACE_N << "Copied " << total_rows << " actions.";
    co_return total_rows;
}

boost::asio::awaitable<void> GrpcServer::getAction(nextapp::pb::Action& action, const std::string& uuid, RequestCtx& rctx) {

    const auto res = co_await rctx.dbh->exec(
        format(R"(SELECT {} from action WHERE id=? AND user=? )",
               ToAction::allSelectCols()), rctx.uctx->dbOptions(), uuid, rctx.uctx->userUuid());

    assert(res.has_value());
    if (!res.rows().empty()) {
        const auto& row = res.rows().front();
        ToAction::assign(row, action, *rctx.uctx);
    } else {
        throw server_err{pb::Error::NOT_FOUND, format("Action {} not found for user {}", uuid, rctx.uctx->userUuid())};
    }

}


} // ns
