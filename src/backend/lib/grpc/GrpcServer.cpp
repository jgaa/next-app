
#include "shared_grpc_server.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

namespace nextapp::grpc {

namespace {

struct ToNode {
    enum Cols {
        ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, VERSION
    };

    static constexpr string_view selectCols = "id, user, name, kind, descr, active, parent, version";

    static void assign(const boost::mysql::row_view& row, pb::Node& node) {
        node.set_uuid(row.at(ID).as_string());
        node.set_user(row.at(USER).as_string());
        node.set_name(row.at(NAME).as_string());
        node.set_version(row.at(VERSION).as_int64());
        const auto kind = row.at(KIND).as_int64();
        if (pb::Node::Kind_IsValid(kind)) {
            node.set_kind(static_cast<pb::Node::Kind>(kind));
        }
        if (!row.at(DESCR).is_null()) {
            node.set_descr(row.at(DESCR).as_string());
        }
        node.set_active(row.at(ACTIVE).as_int64() != 0);
        if (!row.at(PARENT).is_null()) {
            node.set_parent(row.at(PARENT).as_string());
        }
    }
};

struct ToWorkSession {
    enum Cols {
        ID, ACTION, USER, START, END, DURATION, PAUSED, STATE, VERSION, EVENTS
    };

    static constexpr string_view selectCols = "id, action, user, start, end, duration, paused, state, version, events";

    static void assign(const boost::mysql::row_view& row, pb::WorkSession& ws) {
        ws.set_id(row.at(ID).as_string());
        ws.set_action(row.at(ACTION).as_string());
        ws.set_user(row.at(USER).as_string());
        ws.set_start(row.at(START).as_int64());
        if (row.at(END).is_uint64()) {
            ws.set_end(row.at(END).as_int64());
        }
        ws.set_end(row.at(END).as_int64());
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
        we.set_time(row.at(TIME).as_int64());

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
            we.set_start(row.at(C_START).as_int64());
        }
        if (row.at(C_END).is_int64()) {
            we.set_end(row.at(C_END).as_int64());
        }

        if (row.at(C_DURATION).is_int64()) {
            we.set_duration(row.at(C_DURATION).as_int64());
        }

        if (row.at(C_PAUSED).is_int64()) {
            we.set_paused(row.at(C_PAUSED).as_int64() != 0);
        }
    }
};

template <typename T>
concept ActionType = std::is_same_v<T, pb::ActionInfo> || std::is_same_v<T, pb::Action>;

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
            obj.mutable_due()->set_due(toTimeT(row.at(DUE_BY_TIME).as_datetime()));

            if (row.at(START_TIME).is_datetime()) {
                obj.mutable_due()->set_start(toTimeT(row.at(START_TIME).as_datetime()));
            }

            if (row.at(DUE_TIMEZONE).is_string()) {
                obj.mutable_due()->set_timezone(row.at(DUE_TIMEZONE).as_string());
            }
        }

        if (obj.status() == pb::ActionStatus::DONE) {
            obj.set_kind(pb::ActionKind::AC_DONE);
        } else {
            // If we have due_by_time set, we can see a) if it's expired, b) if it's today and c) it it's in the future
            // We need to convert the time from the database and the time right now to the time-zone used by the client to get it right.
            if (row.at(START_TIME).is_datetime()) {
                const auto due = row.at(DUE_BY_TIME).as_datetime();
                const auto zt = std::chrono::zoned_time(&uctx.tz(), due.as_time_point());
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
            obj.set_completedtime(toTimeT(row.at(COMPLETED_TIME).as_datetime()));
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
            auto update = make_shared<pb::Update>();
            update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
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

    LOG_TRACE << "Reply: " << toJson(*reply);
}

} // anon ns

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetServerInfo(::grpc::CallbackServerContext *ctx,
                                       const pb::Empty *,
                                       pb::ServerInfo *reply)
{
    assert(ctx);
    assert(reply);

    auto add = [&reply](string key, string value) {
        auto prop = reply->mutable_properties()->Add();
        prop->set_key(key);
        prop->set_value(value);
    };

    add("version", NEXTAPP_VERSION);

    auto* reactor = ctx->DefaultReactor();
    reactor->Finish(::grpc::Status::OK);
    return reactor;
}

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
            dc->set_id(row.at(ID).as_string());
            dc->set_color(row.at(COLOR).as_string());
            dc->set_name(row.at(NAME).as_string());
            dc->set_score(static_cast<int32_t>(row.at(SCORE).as_int64()));
        }

        boost::asio::deadline_timer timer{owner_.server().ctx()};
        timer.expires_from_now(boost::posix_time::seconds{2});
        //co_await timer.async_wait(asio::use_awaitable);

        LOG_TRACE_N << "Finish day colors lookup.";
        LOG_TRACE << "Reply is: " << toJson(*reply);
        co_return;
    });

    LOG_TRACE_N << "Leaving the coro do do it's magic...";
    return rval;
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetDay(::grpc::CallbackServerContext *ctx,
                                const pb::Date *req,
                                pb::CompleteDay *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::CompleteDay *reply) -> boost::asio::awaitable<void> {

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        auto res = co_await owner_.server().db().exec(
            "SELECT date, user, color, notes, report FROM day WHERE user=? AND date=? ORDER BY date",
            cutx->dbOptions(), cuser, toAnsiDate(*req));

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

        LOG_TRACE << "Finish day lookup: " << toJson(*reply);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Month *reply) -> boost::asio::awaitable<void> {

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        auto res = co_await owner_.server().db().exec(
            "SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? ORDER BY date",
            cutx->dbOptions(), cuser, req->year(), req->month() + 1);

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

        LOG_TRACE_N << "Finish month lookup.";
        LOG_TRACE << "Reply is: " << toJson(*reply);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetColorOnDay(::grpc::CallbackServerContext *ctx, const pb::SetColorReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (auto *reply) -> boost::asio::awaitable<void> {

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        optional<string> color;
        if (!req->color().empty()) {
            color = req->color();
        }


        co_await owner_.server().db().exec(
            R"(INSERT INTO day (date, user, color) VALUES (?, ?, ?)
                ON DUPLICATE KEY UPDATE color=?)", cutx->dbOptions(),
            // insert
            toAnsiDate(req->date()),
            cuser,
            color,
            // update
            color
            );


        LOG_TRACE_N << "Finish updating color for " << toAnsiDate(req->date());

        auto update = make_shared<pb::Update>();
        auto dc = update->mutable_daycolor();
        *dc->mutable_date() = req->date();
        dc->set_user(cuser);
        dc->set_color(req->color());

        owner_.publish(update);
        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetDay(::grpc::CallbackServerContext *ctx, const pb::CompleteDay *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (auto *reply) -> boost::asio::awaitable<void> {
        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

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

        co_await owner_.server().db().exec(
            R"(INSERT INTO day (date, user, color, notes, report) VALUES (?, ?, ?, ?, ?)
                ON DUPLICATE KEY UPDATE color=?, notes=?, report=?)", cutx->dbOptions(),
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

        auto update = make_shared<pb::Update>();
        *update->mutable_day() = *req;

        LOG_DEBUG << "req: " << toJson(*req);
        LOG_DEBUG << "update: " << toJson(*update);

        owner_.publish(update);
        co_return;
    });
}

::grpc::ServerWriteReactor<pb::Update> *GrpcServer::NextappImpl::SubscribeToUpdates(::grpc::CallbackServerContext *context, const pb::UpdatesReq *request)
{
    if (!owner_.active()) {
        LOG_WARN << "Rejecting subscription. We are shutting down.";
        return {};
    }

    class ServerWriteReactorImpl
        : public std::enable_shared_from_this<ServerWriteReactorImpl>
        , public Publisher
        , public ::grpc::ServerWriteReactor<pb::Update> {
    public:
        enum class State {
            READY,
            WAITING_ON_WRITE,
            DONE
        };

        ServerWriteReactorImpl(GrpcServer& owner, ::grpc::CallbackServerContext *context)
            : owner_{owner}, context_{context} {
        }

        ~ServerWriteReactorImpl() {
            LOG_DEBUG_N << "Remote client " << uuid() << " is going...";
        }

        void start() {
            // Tell owner about us
            LOG_DEBUG << "Remote client " << context_->peer() << " is subscribing to updates as subscriber " << uuid();
            self_ = shared_from_this();
            owner_.addPublisher(self_);
            reply();
        }

        /*! Callback event when the RPC is completed */
        void OnDone() override {
            {
                scoped_lock lock{mutex_};
                state_ = State::DONE;
            }

            owner_.removePublisher(uuid());
            self_.reset();
        }

        /*! Callback event when a write operation is complete */
        void OnWriteDone(bool ok) override {
            if (!ok) [[unlikely]] {
                LOG_WARN << "The write-operation failed.";

                // We still need to call Finish or the request will remain stuck!
                Finish({::grpc::StatusCode::UNKNOWN, "stream write failed"});
                scoped_lock lock{mutex_};
                state_ = State::DONE;
                return;
            }

            {
                scoped_lock lock{mutex_};
                updates_.pop();
            }

            reply();
        }

        void publish(const std::shared_ptr<pb::Update>& message) override {
            {
                scoped_lock lock{mutex_};
                updates_.emplace(message);
            }

            reply();
        }

        void close() override {
            LOG_TRACE << "Closing subscription " << uuid();
            Finish(::grpc::Status::OK);
        }

    private:
        void reply() {
            scoped_lock lock{mutex_};
            if (state_ != State::READY || updates_.empty()) {
                return;
            }

            if (!owner_.active()) {
                close();
                return;
            }

            StartWrite(updates_.front().get());

            // TODO: Implement finish if the server shuts down.
            //Finish(::grpc::Status::OK);
        }

        GrpcServer& owner_;
        State state_{State::READY};
        std::queue<std::shared_ptr<pb::Update>> updates_;
        std::mutex mutex_;
        std::shared_ptr<ServerWriteReactorImpl> self_;
        ::grpc::CallbackServerContext *context_;
    };

    try {
        auto handler = make_shared<ServerWriteReactorImpl>(owner_, context);
        handler->start();
        return handler.get(); // The object maintains ownership over itself
    } catch (const exception& ex) {
        LOG_ERROR_N << "Caught exception while adding subscriber to update: " << ex.what();
    }

    return {};
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateTenant(::grpc::CallbackServerContext *ctx, const pb::CreateTenantReq *req, pb::Status *reply)
{
    // Do some basic checks before we attempt to create anything...
    if (!req->has_tenant() || req->tenant().name().empty()) {
        setError(*reply, pb::Error::MISSING_TENANT_NAME);
    } else {

        for(const auto& user : req->users()) {
            if (user.email().empty()) {
                setError(*reply, pb::Error::MISSING_USER_EMAIL);
            } else if (user.name().empty()) {
                setError(*reply, pb::Error::MISSING_USER_NAME);
            }
        }
    }

    if (reply->error() != pb::Error::OK) {
        auto* reactor = ctx->DefaultReactor();
        reactor->Finish(::grpc::Status::OK);
        return reactor;
    }

    LOG_DEBUG_N << "Request to create tenant " << req->tenant().name();

    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        pb::Tenant tenant{req->tenant()};

        if (tenant.uuid().empty()) {
            tenant.set_uuid(newUuidStr());
        }
        if (tenant.properties().empty()) {
            tenant.mutable_properties();
        }

        const auto properties = toJson(*tenant.mutable_properties());
        if (!tenant.has_kind()) {
            tenant.set_kind(pb::Tenant::Tenant::Kind::Tenant_Kind_Guest);
        }

        co_await owner_.server().db().exec(
            "INSERT INTO tenant (id, name, kind, descr, active, properties) VALUES (?, ?, ?, ?, ?, ?)",
                tenant.uuid(),
                tenant.name(),
                pb::Tenant::Kind_Name(tenant.kind()),
                tenant.descr(),
                tenant.active(),
                properties);

        LOG_INFO << "User " << cuser
                 << " has created tenant name=" << tenant.name() << ", id=" << tenant.uuid()
                 << ", kind=" << pb::Tenant::Kind_Name(tenant.kind());

        // create users
        for(const auto& user_template : req->users()) {
            pb::User user{user_template};

            if (user.uuid().empty()) {
                user.set_uuid(newUuidStr());
            }

            user.set_tenant(tenant.uuid());
            auto kind = user.kind();
            if (!user.has_kind()) {
                user.set_kind(pb::User::Kind::User_Kind_Regular);
            }

            if (!user.has_active()) {
                user.set_active(true);
            }

            auto user_props = toJson(*user.mutable_properties());
            co_await owner_.server().db().exec(
                "INSERT INTO user (id, tenant, name, email, kind, active, descr, properties) VALUES (?,?,?,?,?,?,?,?)",
                    user.uuid(),
                    user.tenant(),
                    user.name(),
                    user.email(),
                    pb::User::Kind_Name(user.kind()),
                    user.active(),
                    user.descr(),
                    user_props);

            LOG_INFO << "User " << cuser
                     << " has created user name=" << user.name() << ", id=" << user.uuid()
                     << ", kind=" << pb::User::Kind_Name(user.kind())
                     << ", tenant=" << user.tenant();
        }

        // TODO: Publish the new tenant and users

        *reply->mutable_tenant() = tenant;

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateNode(::grpc::CallbackServerContext *ctx, const pb::CreateNodeReq *req, pb::Status *reply)
{
    LOG_DEBUG << "Request to create node " << req->node().uuid() << " for tenant "
              << owner_.userContext(ctx)->tenantUuid();

    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {


        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();
        auto dbopts = cutx->dbOptions();

        optional<string> parent = req->node().parent();
        if (parent->empty()) {
            parent.reset();
        } else {
            co_await owner_.validateNode(*parent, cuser);
        }

        auto id = req->node().uuid();
        if (id.empty()) {
            id = newUuidStr();
        }

        bool active = true;
        if (!req->node().has_active()) {
            active = req->node().active();
        }

        enum Cols {
            ID, USER, NAME, KIND, DESCR, ACTIVE, PARENT, VERSION
        };

        dbopts.reconnect_and_retry_query = false;
        const auto res = co_await owner_.server().db().exec(format(
            "INSERT INTO node (id, user, name, kind, descr, active, parent) VALUES (?, ?, ?, ?, ?, ?, ?) "
            "RETURNING {}", ToNode::selectCols), dbopts,
               id,
               cuser,
               req->node().name(),
               static_cast<int>(req->node().kind()),
               req->node().descr(),
               active,
               parent);

        if (!res.empty()) {
            auto node = reply->mutable_node();
            ToNode::assign(res.rows().front(), *node);
            reply->set_error(pb::Error::OK);
        } else {
            assert(false); // Should get exception on error
        }

        // Notify clients
        auto update = make_shared<pb::Update>();
        auto node = update->mutable_node();
        *node = reply->node();
        update->set_op(pb::Update::Operation::Update_Operation_ADDED);
        owner_.publish(update);

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateNode(::grpc::CallbackServerContext *ctx, const pb::Node *req, pb::Status *reply)
{
    LOG_DEBUG << "Request to update node " << req->uuid() << " for tenant "
              << owner_.userContext(ctx)->tenantUuid();

    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            // Get the existing node

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();
        const auto& dbopts = cutx->dbOptions();

        bool moved = false;
        bool data_changed = false;

        for(auto retry = 0;; ++retry) {

            const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser);

            // Check if any data has changed
            data_changed = req->name() != existing.name()
                                || req->active() != existing.active()
                                || req->kind() != existing.kind()
                                || req->descr() != existing.descr();

            // Check if the parent has changed.
            if (req->parent() != existing.parent()) {
                throw db_err{pb::Error::DIFFEREENT_PARENT, "UpdateNode cannot move nodes in the tree"};
            }

            // Update the data, if version is unchanged
            auto res = co_await owner_.server().db().exec(
                "UPDATE node SET name=?, active=?, kind=?, descr=?, version=version+1 WHERE id=? AND user=? AND version=?",
                dbopts,
                req->name(),
                req->active(),
                static_cast<int>(req->kind()),
                req->descr(),
                req->uuid(),
                cuser,
                existing.version()
                );

            if (res.affected_rows() > 0) {
                break; // Only succes-path out of the loop
            }

            LOG_DEBUG << "updateNode: Failed to update. Looping for retry.";
            if (retry >= 5) {
                throw db_err(pb::Error::DATABASE_UPDATE_FAILED, "I failed to update, despite retrying");
            }

            boost::asio::steady_timer timer{owner_.server().ctx()};
            timer.expires_from_now(100ms);
            co_await timer.async_wait(boost::asio::use_awaitable);
        }

        // Get the current record
        const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser);

        // Notify clients about changes

        reply->set_error(pb::Error::OK);
        *reply->mutable_node() = current;

        // Notify clients
        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
        *update->mutable_node() = current;
        owner_.publish(update);

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MoveNode(::grpc::CallbackServerContext *ctx, const pb::MoveNodeReq *req, pb::Status *reply)
{
    LOG_DEBUG << "Request to move node " << req->uuid() << " for tenant "
              << owner_.userContext(ctx)->tenantUuid();

    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
        // Get the existing node

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        for(auto retry = 0;; ++retry) {

            const pb::Node existing = co_await owner_.fetcNode(req->uuid(), cuser);

            if (existing.parent() == req->parentuuid()) {
                reply->set_error(pb::Error::NO_CHANGES);
                reply->set_message("The parent has not changed. Ignoring the reqest!");
                co_return;
            }

            if (req->parentuuid() == req->uuid()) {
                reply->set_error(pb::Error::CONSTRAINT_FAILED);
                reply->set_message("A node cannot be its own parent. Ignoring the request!");
                LOG_DEBUG << "A node cannot be its own parent. Ignoring the request for node-id " << req->uuid();
                co_return;
            }

            optional<string> parent;
            if (!req->parentuuid().empty()) {
                co_await owner_.validateNode(req->parentuuid(), cuser);
                parent = req->parentuuid();
            }

            // Update the data, if version is unchanged
            auto res = co_await owner_.server().db().exec(
                "UPDATE node SET parent=?, version=version+1 WHERE id=? AND user=? AND version=?",
                parent,
                req->uuid(),
                cuser,
                existing.version()
                );

            if (res.affected_rows() > 0) {
                break; // Only succes-path out of the loop
            }

            LOG_DEBUG << "updateNode: Failed to update. Looping for retry.";
            if (retry >= 5) {
                throw db_err(pb::Error::DATABASE_UPDATE_FAILED, "I failed to update, despite retrying");
            }

            boost::asio::steady_timer timer{owner_.server().ctx()};
            timer.expires_from_now(100ms);
            co_await timer.async_wait(boost::asio::use_awaitable);
        }

        // Get the current record
        const pb::Node current = co_await owner_.fetcNode(req->uuid(), cuser);
        // Notify clients about changes

        reply->set_error(pb::Error::OK);
        *reply->mutable_node() = current;

        // Notify clients
        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_MOVED);
        *update->mutable_node() = current;
        owner_.publish(update);

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteNode(::grpc::CallbackServerContext *ctx, const pb::DeleteNodeReq *req, pb::Status *reply)
{
    LOG_DEBUG << "Request to delete node " << req->uuid() << " for tenant "
              << owner_.userContext(ctx)->tenantUuid();

    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
        // Get the existing node

        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();

        const auto node = co_await owner_.fetcNode(req->uuid(), cuser);

        auto res = co_await owner_.server().db().exec(format("DELETE from node where id=? and user=?", ToNode::selectCols),
                                                      req->uuid(), cuser);

        if (!res.has_value() || res.affected_rows() == 0) {
            throw db_err{pb::Error::NOT_FOUND, format("Node {} not found", req->uuid())};
        }

        reply->set_error(pb::Error::OK);
        *reply->mutable_node() = node;

        // Notify clients
        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_DELETED);
        *update->mutable_node() = node;
        owner_.publish(update);

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetNodes(::grpc::CallbackServerContext *ctx,
                                                              const pb::GetNodesReq *req,
                                                              pb::NodeTree *reply)
{
    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::NodeTree *reply) -> boost::asio::awaitable<void> {
        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();
        const auto& dbopts = cutx->dbOptions();

        const auto res = co_await owner_.server().db().exec(format(R"(
        WITH RECURSIVE tree AS (
          SELECT * FROM node WHERE user=?
          UNION
          SELECT n.* FROM node AS n, tree AS p
          WHERE n.parent = p.id or n.parent IS NULL
        )
        SELECT {} from tree ORDER BY parent, name)", ToNode::selectCols), dbopts, cuser);

        std::deque<pb::NodeTreeItem> pending;
        map<string, pb::NodeTreeItem *> known;

        // Root level
        known[""] = reply->mutable_root();

        assert(res.has_value());
        for(const auto& row : res.rows()) {
            pb::Node n;
            ToNode::assign(row, n);
            const auto parent = n.parent();

            if (auto it = known.find(parent); it != known.end()) {
                auto child = it->second->add_children();
                child->mutable_node()->Swap(&n);
                known[child->node().uuid()] = child;
            } else {
                // Track it for later
                const auto id = n.uuid();
                pending.push_back({});
                auto child = &pending.back();
                child->mutable_node()->Swap(&n);
                known[child->node().uuid()] = child;
            }
        }


        // By now, all the parents are in the known list.
        // We can safely move all the pending items to the child lists of the parents
        for(auto& v : pending) {
            if (auto it = known.find(v.node().parent()); it != known.end()) {
                auto id = v.node().uuid();
                auto& parent = *it->second;
                parent.add_children()->Swap(&v);
                // known lookup must point to the node's new memory location
                assert(parent.children().size() > 0);
                known[id] = &parent.mutable_children()->at(parent.children().size()-1);
            } else {
                assert(false);
            }
        }

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetActions(::grpc::CallbackServerContext *ctx, const pb::GetActionsReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
                        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
        const auto cutx = owner_.userContext(ctx);
        const auto& cuser = cutx->userUuid();
        const auto& dbopts = cutx->dbOptions();

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
            ToAction::assign(row, *actions->add_actions(), *cutx);
        }

        LOG_TRACE << toJson(*reply);

        co_return;
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetAction(::grpc::CallbackServerContext *ctx, const pb::GetActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto& dbopts = cutx->dbOptions();

            const auto res = co_await owner_.server().db().exec(
                format(R"(SELECT {} from action WHERE id=? AND user=? )",
                       ToAction::allSelectCols()), dbopts, uuid, cuser);

            assert(res.has_value());
            if (!res.rows().empty()) {
                const auto& row = res.rows().front();
                auto *action = reply->mutable_action();
                ToAction::assign(row, *action, *cutx);
            } else {
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message(format("Action with id={} not found for the current user.", uuid));
            }

            LOG_TRACE << toJson(*reply);

            co_return;
        });
}

boost::asio::awaitable<void> addAction(pb::Action action, GrpcServer& owner, ::grpc::CallbackServerContext *ctx,
                                       pb::Status *reply = {}) {
    const auto cutx = owner.userContext(ctx);
    const auto& cuser = cutx->userUuid();
    auto dbopts = cutx->dbOptions();


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
        ToAction::prepareBindingArgs(action, *cutx, action.id(), action.node(), cuser));

    assert(!res.empty());
    // Set the reply data

    auto update = make_shared<pb::Update>();
    if (reply) {
        auto *reply_action = reply->mutable_action();
        ToAction::assign(res.rows().front(), *reply_action, *cutx);
        *update->mutable_action() = *reply_action;
    } else {
        action.Clear();
        ToAction::assign(res.rows().front(), action, *cutx);
        *update->mutable_action() = action;
    }

    // Copy the new Action to an update and publish it
    update->set_op(pb::Update::Operation::Update_Operation_ADDED);
    owner.publish(update);
}


::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::CreateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {

        pb::Action new_action{*req};
        new_action.set_node(req->node());

        co_await addAction(new_action, owner_, ctx, reply);
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            const auto& uuid = validatedUuid(req->id());
            const auto& dbopts = cutx->dbOptions();
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
                                                                ToAction::prepareBindingArgs<false>(new_action, *cutx, uuid, cuser));

            assert(!res.empty());

            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *cutx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }
     });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteAction(::grpc::CallbackServerContext *ctx, const pb::DeleteActionReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            const auto& uuid = validatedUuid(req->actionid());

            auto res = co_await owner_.server().db().exec("DELETE FROM action WHERE id=? AND user=?",
                                                          uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                reply->set_deletedactionid(uuid);
                auto update = make_shared<pb::Update>();
                update->set_op(pb::Update::Operation::Update_Operation_DELETED);
                update->mutable_action()->set_id(uuid);
                owner_.publish(update);
            } else {
                reply->set_uuid(uuid);
                reply->set_error(pb::Error::NOT_FOUND);
                reply->set_message(format("Action with id={} not found for the current user.", uuid));
            }

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsDone(::grpc::CallbackServerContext *ctx, const pb::ActionDoneReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = cutx->dbOptions();
            DoneChanged done = DoneChanged::MARKED_UNDONE;

            optional<string> when;
            if (req->done()) {
                when = toAnsiTime(time({}), cutx->tz());
                done = DoneChanged::MARKED_DONE;
            }

            auto res = co_await owner_.server().db().exec(
                "UPDATE action SET status=?, completed_time=?, version=version+1 WHERE id=? AND user=?",
                dbopts, (req->done() ? "done" : "active"), when, uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *cutx, ctx, reply, done);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::MarkActionAsFavorite(::grpc::CallbackServerContext *ctx, const pb::ActionFavoriteReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply) -> boost::asio::awaitable<void> {
            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();
            const auto& uuid = validatedUuid(req->uuid());
            const auto dbopts = cutx->dbOptions();

            auto res = co_await owner_.server().db().exec(
                "UPDATE action SET favorite=?, version=version+1 WHERE id=? AND user=?",
                dbopts, req->favorite(), uuid, cuser);

            assert(res.has_value());
            if (res.affected_rows() == 1) {
                co_await replyWithAction(owner_, uuid, *cutx, ctx, reply);
            } else {
                reply->set_error(pb::Error::GENERIC_ERROR);
                reply->set_message(format("Action with id={} was not updated.", uuid));
            }

            co_return;
        });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetFavoriteActions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
         [this, ctx] (auto *reply) -> boost::asio::awaitable<void> {

            const auto cutx = owner_.userContext(ctx);
            const auto& cuser = cutx->userUuid();

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
        });
}

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
                format("INSERT INTO work_event (session, kind) VALUES (?, 'start')"
                       "RETURNS {}", ToWorkEvent::selectCols),  dbopts, session.id());

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
    auto res = co_await server().db().exec(format("SELECT {} from work_session where id=? and user = ?", ToWorkSession::selectCols), uuid, uctx.userUuid());
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
    if (work.has_end()) {
        assert(work.state() == pb::WorkSession::State::WorkSession_State_DONE);

        pb::SavedWorkEvents events;
        *events.mutable_events() = work.events();
        string events_buffer;
        events.SerializeToString(&events_buffer);


        co_await server().db().exec("UPDATE work_session SET start_time=?, end_time=?, state = 'done', "
                                    "duration = ?, paused = ?, events = ? version=version+1 "
                                    "WHERE id=? AND user=?",
                                    dbo,
                                    toAnsiTime(work.start(), uctx.tz()), toAnsiTime(work.end(), uctx.tz()),
                                    work.duration(), work.paused(), events_buffer, work.id(), uctx.userUuid());
    } else {
        co_await server().db().exec("UPDATE work_session SET start_time = ?, duration = ?, paused = ?, state=?, version=version+1 "
                                    "WHERE id=? AND user=?",
                                    dbo,
                                    toAnsiTime(work.start(), uctx.tz()), work.duration(), work.paused(),
                                    pb::WorkSession_State_Name(work.state()), work.id(),
                                    uctx.userUuid());
    }
}

GrpcServer::GrpcServer(Server &server)
    : server_{server}
{
}

void GrpcServer::start() {
    ::grpc::ServerBuilder builder;

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
    builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<NextappImpl>(*this);
    builder.RegisterService(service_.get());

    // Finally assemble the server.
    grpc_server_ = builder.BuildAndStart();
    LOG_INFO
        // Fancy way to print the class-name.
        // Useful when I copy/paste this code around ;)
        << boost::typeindex::type_id_runtime(*this).pretty_name()

        // The useful information
        << " listening on " << config().address;

    active_ = true;
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down GrpcServer.";
    active_ = false;
    for(auto& [_, wp] : publishers_) {
        if (auto pub = wp.lock()) {
            pub->close();
        }
    }
    auto deadline = std::chrono::system_clock::now() + 6s;
    grpc_server_->Shutdown(deadline);
    publishers_.clear();
    //grpc_server_->Wait();
    grpc_server_.reset();
    LOG_DEBUG << "GrpcServer is done.";
}

void GrpcServer::addPublisher(const std::shared_ptr<Publisher> &publisher)
{
    LOG_TRACE_N << "Adding publisher " << publisher->uuid();
    scoped_lock lock{mutex_};
    publishers_[publisher->uuid()] = publisher;
}

void GrpcServer::removePublisher(const boost::uuids::uuid &uuid)
{
    LOG_TRACE_N << "Removing publisher " << uuid;
    scoped_lock lock{mutex_};
    publishers_.erase(uuid);
}

void GrpcServer::publish(const std::shared_ptr<pb::Update>& update)
{
    scoped_lock lock{mutex_};

    LOG_DEBUG_N << "Publishing update to " << publishers_.size() << " subscribers, Json: "
                << toJson(*update);

    for(auto& [uuid, weak_pub]: publishers_) {
        if (auto pub = weak_pub.lock()) {
            pub->publish(update);
        } else {
            LOG_WARN_N << "Failed to get a pointer to publisher " << uuid;
        }
    }
}

boost::asio::awaitable<void> GrpcServer::validateNode(const std::string &parentUuid, const std::string &userUuid)
{
    auto res = co_await server().db().exec("SELECT id FROM node where id=? and user=?", parentUuid, userUuid);
    if (!res.has_value()) {
        throw db_err{pb::Error::INVALID_PARENT, "Node id must exist and be owned by the user"};
    }
}

boost::asio::awaitable<void> GrpcServer::validateAction(const std::string &actionId, const std::string &userUuid)
{
    auto res = co_await server().db().exec("SELECT id FROM action where id=? and user=?", actionId, userUuid);
    if (!res.has_value()) {
        throw db_err{pb::Error::INVALID_ACTION, "Action not found for the current user"};
    }
}

boost::asio::awaitable<pb::Node> GrpcServer::fetcNode(const std::string &uuid, const std::string &userUuid)
{
    auto res = co_await server().db().exec(format("SELECT {} from node where id=? and user=?", ToNode::selectCols),
                                           uuid, userUuid);
    if (!res.has_value()) {
        throw db_err{pb::Error::NOT_FOUND, format("Node {} not found", uuid)};
    }

    pb::Node rval;
    ToNode::assign(res.rows().front(), rval);
    co_return rval;
}

const std::shared_ptr<UserContext> GrpcServer::userContext(::grpc::CallbackServerContext *ctx) const
{
    static constexpr auto system_tenant = "a5e7bafc-9cba-11ee-a971-978657e51f0c";
    static constexpr auto system_user = "dd2068f6-9cbb-11ee-bfc9-f78040cadf6b";

    jgaa::mysqlpool::Options dbo;
    dbo.reconnect_and_retry_query = true;

    // TODO: Implement sessions and authentication
    lock_guard lock{mutex_};
    if (sessions_.empty()) {
        const auto zone_name = chrono::current_zone()->name();
        dbo.time_zone = zone_name;
        auto ux = make_shared<UserContext>(system_tenant, system_user, zone_name, true, dbo);
        sessions_[ux->sessionId()] = ux;
        return ux;
    }

    // For now we have only one session
    return sessions_.begin()->second;
}

// TODO: Add the time, adjusted for local time zone.
std::time_t addDays(std::time_t input, int n)
{
    auto today = floor<date::days>(chrono::system_clock::from_time_t(input));
    today += date::days(n);
    return chrono::system_clock::to_time_t(today);
}

static constexpr auto quarters = to_array({date::January, date::January, date::January,
                                           date::April, date::April, date::April,
                                           date::July, date::July, date::July,
                                           date::October, date::October, date::October});


time_t getDueTime(const auto& start, const auto& ts, pb::ActionDueKind kind) {
    using namespace date;

    auto t_local = start.get_local_time();
    auto start_date = year_month_day{floor<days>(t_local)};

    // Days from epoch in local time
    local_days ldays{start_date};

    // Time of day in local time
    const hh_mm_ss time{t_local - floor<days>(t_local)};

    switch(kind) {
    case pb::ActionDueKind::DATETIME: {
        return chrono::system_clock::to_time_t(start.get_sys_time());
        }
    }

    return {};
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

auto toTimet(const auto& when, const auto *ts) {
    const auto zoned = date::make_zoned(ts, when);
    return chrono::system_clock::to_time_t(zoned.get_sys_time());
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
        format("INSERT INTO work_event (session, kind) VALUES (?, 'stop')"
               "RETURNS {}", ToWorkEvent::selectCols),  dbopts, work.id());

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

boost::asio::awaitable<void> GrpcServer::handleActionDone(const pb::Action &orig,
                                                          const UserContext& uctx,
                                                          ::grpc::CallbackServerContext *ctx)
{
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
    // Find the appropriate repeat time

} // ns
