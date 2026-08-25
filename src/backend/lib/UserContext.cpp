

#include "nextapp/logging.h"
#include "nextapp/util.h"
#include "nextapp/UserContext.h"
#include "nextapp/Server.h"
#include "nextapp/GrpcServer.h"
#include "nextapp/errors.h"
#include "grpc/grpc_security_constants.h"

#include <limits>
#include <random>

using namespace std;

namespace logfault {
std::pair<bool /* json */, std::string /* content or json */> toLog(const nextapp::UserContext& uctx, bool json) {
    if (json) {
        return make_pair(true, format(R"("user":"{}", "tenant":"{}")",
                                      uctx.userUuid(),
                                      uctx.tenantUuid()));
    }

    return make_pair(false, format("UserContext{{user={}, tenant={}}}",
                                   uctx.userUuid(),
                                   uctx.tenantUuid()));
}

}

namespace nextapp {

namespace {
constexpr size_t kReplayBacklogLimit = 1024;

uint64_t newPublishEpoch()
{
    return getRandomNumber64() & static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

auto to_string_view(::grpc::string_ref ref) {
    return string_view{ref.data(), ref.size()};
}

auto to_string_view(const boost::mysql::blob_view& blob) {
    return string_view{reinterpret_cast<const char *>(blob.data()), blob.size()};
}

bool isMobileDevice(string_view product_type, string_view os)
{
    const auto pt = toLower(product_type);
    const auto os_name = toLower(os);
    return pt == "android" || pt == "ios" || pt == "iphoneos" || pt == "ipados"
        || os_name == "android" || os_name == "ios" || os_name == "iphoneos" || os_name == "ipados";
}

template <typename IntT>
IntT randomBetween(IntT min_value, IntT max_value)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<IntT> dist(min_value, max_value);
    return dist(rng);
}

} // anon ns


SessionManager::SessionManager(Server &server)
    : server_{server}
    , skip_tls_auth_{server.config().grpc.tls_mode == "none"}
{
    startNextTimer();
}

std::shared_ptr<std::mutex> SessionManager::getUserCreationMutex_(const boost::uuids::uuid& userUuid)
{
    std::unique_lock lock{user_creation_mutexes_mutex_};
    auto& entry = user_creation_mutexes_[userUuid];
    if (!entry) {
        entry = std::make_shared<std::mutex>();
    }
    return entry;
}

boost::asio::awaitable<UserPublishState> SessionManager::loadPublishState_(const boost::uuids::uuid& userUuid,
                                                                           std::string_view userUuidStr)
{
    {
        std::lock_guard lock{publish_states_mutex_};
        if (auto it = publish_states_.find(userUuid); it != publish_states_.end()) {
            co_return it->second;
        }
    }

    auto db = co_await server_.db().getConnection();
    auto res = co_await db.exec(
        "SELECT publish_id, publish_epoch FROM user_runtime_publish_state WHERE user_id=?",
        userUuidStr);

    if (res.rows().empty()) {
        UserPublishState state{};
        state.publish_epoch = newPublishEpoch();
        {
            std::lock_guard lock{publish_states_mutex_};
            publish_states_[userUuid] = state;
        }
        co_await db.exec(
            "INSERT INTO user_runtime_publish_state (user_id, publish_id, publish_epoch) VALUES (?, ?, ?)",
            userUuidStr,
            state.publish_id,
            state.publish_epoch);
        co_return state;
    }

    const auto& row = res.rows().front();
    enum Cols {
        PUBLISH_ID,
        PUBLISH_EPOCH
    };

    UserPublishState state;
    state.publish_id = static_cast<uint32_t>(row.at(PUBLISH_ID).as_int64());
    state.publish_epoch = static_cast<uint64_t>(row.at(PUBLISH_EPOCH).as_int64());
    if (!state.publish_epoch) {
        state.publish_epoch = newPublishEpoch();
        co_await db.exec(
            "UPDATE user_runtime_publish_state SET publish_epoch=? WHERE user_id=?",
            state.publish_epoch,
            userUuidStr);
    }
    {
        std::lock_guard lock{publish_states_mutex_};
        publish_states_[userUuid] = state;
    }
    co_return state;
}

boost::asio::awaitable<void> SessionManager::savePublishState_(const boost::uuids::uuid& userUuid,
                                                               std::string_view userUuidStr,
                                                               UserPublishState state)
{
    if (!state.publish_epoch) {
        state.publish_epoch = newPublishEpoch();
    }

    {
        std::lock_guard lock{publish_states_mutex_};
        publish_states_[userUuid] = state;
    }

    co_await server_.db().exec(
        "INSERT INTO user_runtime_publish_state (user_id, publish_id, publish_epoch) VALUES (?, ?, ?) "
        "ON DUPLICATE KEY UPDATE publish_id=VALUES(publish_id), publish_epoch=VALUES(publish_epoch)",
        userUuidStr,
        state.publish_id,
        state.publish_epoch);
}

boost::asio::awaitable<void> SessionManager::persistPublishState(const std::shared_ptr<UserContext>& user)
{
    co_await savePublishState_(toUuid(user->userUuid()), user->userUuid(), user->currentPublishState());
}

void SessionManager::persistPublishStateSync_(const PendingPublishStateSave& state)
{
    auto save = boost::asio::co_spawn(
        ioContext(),
        savePublishState_(state.user_uuid, state.user_uuid_str, state.state),
        boost::asio::use_future);
    save.get();
}

boost::asio::awaitable<void> SessionManager::loadPlans()
{
    LOG_DEBUG_N << "Loading plans from database...";
    auto db = co_await server_.db().getConnection();

    // Query the plans
    auto res = co_await db.exec(
        "SELECT name, active, createdAt, max_users, max_devices, max_nodes, nodes_monthly_growth, "
        "max_actions, actions_monthly_growth, max_worksessions, work_sessions_monthly_growth, "
        "max_time_blocks, time_blocks_monthly_growth, mobile_only FROM plan");
    enum Cols {
        NAME,
        ACTIVE,
        CREATED_AT,
        MAX_USERS,
        MAX_DEVICES,
        MAX_NODES,
        NODES_MONTHLY_GROWTH,
        MAX_ACTIONS,
        ACTIONS_MONTHLY_GROWTH,
        MAX_WORKSESSIONS,
        WORK_SESSIONS_MONTHLY_GROWTH,
        MAX_TIME_BLOCKS,
        TIME_BLOCKS_MONTHLY_GROWTH,
        MOBILE_ONLY
    };

    decltype (plans_) new_plans;

    for(const auto& row : res.rows()) {
        auto pp = make_shared<Plan>();

        pp->name = row[NAME].as_string();
        pp->active = row[ACTIVE].as_int64() != 0;
        pp->created_at = row[CREATED_AT].as_datetime().as_time_point();
        pp->max_users = row[MAX_USERS].as_int64();
        pp->max_devices = row[MAX_DEVICES].as_int64();
        pp->max_nodes = row[MAX_NODES].as_int64();
        pp->nodes_monthly_growth = row[NODES_MONTHLY_GROWTH].as_int64();
        pp->max_actions = row[MAX_ACTIONS].as_int64();
        pp->actions_monthly_growth = row[ACTIONS_MONTHLY_GROWTH].as_int64();
        pp->max_worksessions = row[MAX_WORKSESSIONS].as_int64();
        pp->work_sessions_monthly_growth = row[WORK_SESSIONS_MONTHLY_GROWTH].as_int64();
        pp->max_time_blocks = row[MAX_TIME_BLOCKS].as_int64();
        pp->time_blocks_monthly_growth = row[TIME_BLOCKS_MONTHLY_GROWTH].as_int64();
        pp->mobile_only = row[MOBILE_ONLY].as_int64() != 0;

        const string_view plan_name = pp->name;
        new_plans[plan_name] = std::move(pp);
    }

    {
        LOG_DEBUG_N << "Loaded " << new_plans.size() << " plans.";
        unique_lock lock{plan_mutex_};
        plans_ = std::move(new_plans);
    }
}

boost::asio::awaitable<pb::UserSessions> SessionManager::listSessions()
{
    pb::UserSessions us;
    lock_guard lock{mutex_};
    for(const auto& [_, ux] : users_) {
        auto *u = us.add_sessions();
        assert(u);
        u->mutable_userid()->set_uuid(ux->userUuid());
        u->mutable_tenantid()->set_uuid(ux->tenantUuid());
        u->set_publishmessageid(ux->currentPublishId());
        u->set_kind(ux->isAdmin() ? pb::User_Kind::User_Kind_SUPER : pb::User_Kind::User_Kind_REGULAR);

        for(const auto& s : ux->sessions()) {
            auto *session = u->add_sessions();
            assert(session);
            session->mutable_deviceid()->set_uuid(to_string(s->deviceId()));
            session->mutable_sessionid()->set_uuid(to_string(s->sessionId()));
            session->set_durationinseconds(
                std::chrono::duration_cast<std::chrono::seconds>(s->currentDuration()).count());
            session->set_lastseensecsago(
                std::chrono::duration_cast<std::chrono::seconds>(s->durationSinceLastAccess()).count());

            if (auto d = ux->getDevice(s->deviceId()); d.has_value()) {
                if ( d->last_request_id) {
                    for(auto ix = 0u; ix < d->last_request_id->size(); ++ix) {
                        if (auto lri = d->last_request_id->get(ix)) {
                            session->add_lastreqids(*lri);
                        } else {
                            session->add_lastreqids(-1);
                        }
                    }
                }
            }
        };
    }

    co_return us;
}

boost::asio::awaitable<void> SessionManager::publishNotification(const pb::Notification &notification)
{
    // Create notification message
    auto msg = make_shared<pb::Update>();
    msg->set_op(pb::Update::Operation::Update_Operation_ADDED);
    auto * n = msg->mutable_notifications();
    n->add_notifications()->CopyFrom(notification);

    auto publish = [&](auto& user) -> boost::asio::awaitable<void> {
        // See if the user is in the cache
        shared_ptr<UserContext> u;

        {
            lock_guard lock{mutex_};
            if (auto it = users_.find(toUuid(notification.touser().uuid())); it != users_.end()) {
                u = it->second;
            }
        }

        if (u) {
            LOG_DEBUG_N << "Publishing notification #" << notification.id() << " to user " << u->userUuid();
            co_await u->publish(msg);
        } else {
            LOG_TRACE_N << "User " << notification.touser().uuid() << " not found in cache";
        };
        co_return;
    };

    if (notification.has_touser()) {
        co_await publish(notification.touser());
        co_return;
    }

    if (notification.has_totenant()) {

        vector<boost::uuids::uuid> users;

        {
            // TODO: Add indexing on tenant
            auto res = co_await server_.db().exec("SELECT id FROM user WHERE tenant=?", notification.totenant().uuid());

            users.reserve(res.rows().size());
            // Build a vector with user uuids
            for(const auto& row : res.rows()) {
                users.push_back(toUuid(row.front().as_string()));
            }
        }

        for(const auto& u : users) {
            co_await publish(u);
        }

        co_return;
    }

    // Publish to all users we have sessions for
    // Use the sessions instead of user_ to avoid overhead for offline users.
    set<shared_ptr<UserContext>> users;
    {
        unique_lock lock{mutex_};
        for(const auto& [_, ses] : sessions_) {
            users.emplace(ses->userPtr());
        }
    }

    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::steady_timer timer(executor);
    const auto ms = server_.config().options.notification_delay_ms;

    for(auto& u: users) {
        LOG_DEBUG_N << "Publishing mass-notification #" << notification.id() << " to user " << u->userUuid();
        co_await u->publish(msg);

        // co_await on a asio timer for a short period of time
        // to avoid server overload.
        if (ms) {
            timer.expires_after(std::chrono::milliseconds(ms));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }

    co_return;
}

boost::asio::awaitable<void> SessionManager::refreshTenantPlansAndPublish(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    std::string_view tenantUuid)
{
    vector<shared_ptr<UserContext>> users;
    {
        shared_lock lock{mutex_};
        users.reserve(users_.size());
        for (const auto& [_, uctx] : users_) {
            if (uctx && uctx->tenantUuid() == tenantUuid) {
                users.emplace_back(uctx);
            }
        }
    }

    for (auto& uctx : users) {
        co_await uctx->reloadTenantPlan(dbh);

        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
        *update->mutable_subscription() = uctx->getSubscription();

        LOG_INFO_N << "Publishing refreshed subscription to user " << uctx->userUuid()
                   << " on tenant " << tenantUuid;
        co_await uctx->publish(update);
    }
}

boost::asio::awaitable<bool> SessionManager::applyTenantStateAndPublish(
    std::string_view tenantUuid,
    const pb::Tenant& tenant)
{
    vector<shared_ptr<UserContext>> users;
    {
        shared_lock lock{mutex_};
        users.reserve(users_.size());
        for (const auto& [_, uctx] : users_) {
            if (uctx && uctx->tenantUuid() == tenantUuid) {
                users.emplace_back(uctx);
            }
        }
    }

    bool has_active_sessions = false;
    for (auto& uctx : users) {
        uctx->setTenantState(tenant.state());
        uctx->refreshSessionAccess();
        has_active_sessions = has_active_sessions || uctx->hasActiveSessions();

        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
        *update->mutable_tenant() = tenant;

        LOG_INFO_N << "Publishing tenant state " << pb::Tenant::State_Name(tenant.state())
                   << " to user " << uctx->userUuid()
                   << " on tenant " << tenantUuid;
        co_await uctx->publish(update);
    }

    co_return has_active_sessions;
}

std::shared_ptr<Plan> SessionManager::getPlan(const std::string_view planName) const
{
    // read lock to plan_mutex_
    shared_lock lock{plan_mutex_};
    if (auto it = plans_.find(planName); it != plans_.end()) {
        return it->second;
    }

    LOG_WARN_N << "Plan " << planName << " not found.";
    return {};
}

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid, const std::string_view timeZone,
                         bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options &dbOptions,
                         uint32_t publishId, uint64_t publishEpoch, uint64_t dataSyncEpoch,
                         std::chrono::system_clock::time_point createdAt)
    : user_uuid_{userUuid},
    tenant_uuid_{tenantUuid},
    publish_message_id_{publishId},
    publish_epoch_{publishEpoch ? publishEpoch : newPublishEpoch()},
    data_sync_epoch_{dataSyncEpoch},
    created_at_{createdAt == std::chrono::system_clock::time_point{} ? std::chrono::system_clock::now() : createdAt},
    db_options_{dbOptions} {

    if (timeZone.empty()) {
        tz_ = std::chrono::current_zone();
    } else {
        tz_ = std::chrono::locate_zone(timeZone);
        settings_.set_timezone(string{tz_->name()});
        settings_.set_firstdayofweekismonday(!sundayIsFirstWeekday);
        if (tz_ == nullptr) {
            LOG_DEBUG << "UserContext: Invalid timezone: " << timeZone;
            throw std::invalid_argument("Invalid timezone: " + std::string{timeZone});
        }
    }

    if (Server::hasInstance()) {
        plan_usage_timer_.emplace(Server::instance().ctx());
    }
}

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid,
                         pb::User::Kind kind,
                         const pb::UserGlobalSettings &settings, std::shared_ptr<TenantPlan> tenantPlan,
                         uint32_t publishId, uint64_t publishEpoch, uint64_t dataSyncEpoch,
                         std::chrono::system_clock::time_point createdAt)
    : user_uuid_{userUuid}, tenant_uuid_{tenantUuid}, publish_message_id_{publishId}, settings_(settings)
    , publish_epoch_{publishEpoch ? publishEpoch : newPublishEpoch()}
    , data_sync_epoch_{dataSyncEpoch}
    , created_at_{createdAt == std::chrono::system_clock::time_point{} ? std::chrono::system_clock::now() : createdAt}
    , kind_{kind}, tenant_plan_{std::move(tenantPlan)} {

    try {
        if (settings_.timezone().empty()) [[unlikely]] {
            tz_ = std::chrono::current_zone();
            LOG_WARN_N << "User " << userUuid << " has no time-zone set. Using " << tz_->name() << ".";
        } else {
            tz_ = std::chrono::locate_zone(settings_.timezone());
        }
    } catch (const std::exception& e) {
        tz_ = std::chrono::current_zone();
        LOG_WARN << "Failed to locate timezone " << settings_.timezone()
                 << ". Using " << tz_->name() << " instead.";

        // TODO: Send a notice to the user about their time zone setting.
    }

    assert(tz_ != nullptr);
    db_options_.time_zone = tz_->name();
    db_options_.reconnect_and_retry_query = true;

    if (Server::hasInstance()) {
        plan_usage_timer_.emplace(Server::instance().ctx());
    }
}

UserContext::~UserContext()
{
    if (plan_usage_timer_) {
        plan_usage_timer_->cancel();
    }
}

size_t UserContext::planIndex(PlanResource resource) noexcept
{
    return static_cast<size_t>(resource);
}

string_view UserContext::planResourceName(PlanResource resource) noexcept
{
    switch (resource) {
    case PlanResource::DEVICE: return "enabled device";
    case PlanResource::NODE: return "node";
    case PlanResource::ACTION: return "action";
    case PlanResource::WORK_SESSION: return "work session";
    case PlanResource::TIME_BLOCK: return "time block";
    case PlanResource::COUNT: break;
    }
    return "resource";
}

string_view UserContext::sessionAccessReason(SessionAccessMode mode) noexcept
{
    switch (mode) {
    case SessionAccessMode::FULL_ACCESS:
        return "full access";
    case SessionAccessMode::READ_ONLY_DEVICE_LIMIT:
        return "read-only because the device limit is reached";
    case SessionAccessMode::READ_ONLY_MOBILE_ONLY:
        return "read-only because this plan only allows mobile devices full access";
    case SessionAccessMode::READ_ONLY_TENANT:
        return "read-only because the tenant is in read-only mode";
    }
    return "read-only";
}

uint32_t UserContext::countTemplateNodes(const pb::NodeTemplate& root) noexcept
{
    uint32_t total = root.name().empty() ? 0u : 1u;
    for (const auto& child : root.children()) {
        total += countTemplateNodes(child);
    }
    return total;
}

uint32_t UserContext::elapsedWholeMonths(std::chrono::system_clock::time_point createdAt,
                                         std::chrono::system_clock::time_point now) noexcept
{
    if (now <= createdAt) {
        return 0;
    }

    const auto created_day = std::chrono::floor<std::chrono::days>(createdAt);
    const auto now_day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day created_ymd{created_day};
    const std::chrono::year_month_day now_ymd{now_day};

    int months = (int(now_ymd.year()) - int(created_ymd.year())) * 12
        + (unsigned(now_ymd.month()) - unsigned(created_ymd.month()));
    if (unsigned(now_ymd.day()) < unsigned(created_ymd.day())) {
        --months;
    }
    return static_cast<uint32_t>(std::max(months, 0));
}

void UserContext::ResourceReservation::commit() noexcept
{
    if (owner_ && !committed_) {
        owner_->commitReservation(resource_, amount_);
        committed_ = true;
    }
}

void UserContext::ResourceReservation::release() noexcept
{
    if (owner_ && !committed_) {
        owner_->releaseReservation(resource_, amount_);
    }
    owner_ = nullptr;
}

uint32_t UserContext::configuredGraceWindow_() const noexcept
{
    return Server::instance().config().svr.plan_delete_grace_window;
}

uint32_t UserContext::allowedWithGrace_(const PlanCounter& counter) const noexcept
{
    if (counter.allowed == 0) {
        return 0;
    }

    uint64_t effective = counter.allowed;
    if (counter.stale_after_delete) {
        effective += configuredGraceWindow_();
    }

    return static_cast<uint32_t>(std::min<uint64_t>(effective, std::numeric_limits<uint32_t>::max()));
}

boost::asio::awaitable<void> UserContext::loadPlanUsageState_(jgaa::mysqlpool::Mysqlpool::Handle& dbh)
{
    if (!Server::instance().config().payment.enable_plan || !tenant_plan_ || !tenant_plan_->plan) {
        std::lock_guard lock{plan_usage_mutex_};
        plan_usage_.loaded = true;
        plan_usage_.refresh_in_progress = false;
        plan_usage_.next_refresh_at = std::chrono::steady_clock::now() + std::chrono::hours{24};
        for (auto& counter : plan_usage_.counters) {
            counter = {};
        }
        co_return;
    }

    auto count_one = [&](std::string_view sql) -> boost::asio::awaitable<uint32_t> {
        auto res = co_await dbh.exec(sql, user_uuid_);
        if (res.rows().empty()) {
            co_return 0;
        }
        co_return static_cast<uint32_t>(std::max<int64_t>(res.rows().front().front().as_int64(), 0));
    };

    PlanUsageState next;
    next.loaded = true;
    next.refresh_in_progress = false;

    const auto months = elapsedWholeMonths(created_at_, std::chrono::system_clock::now());
    const auto* plan = tenant_plan_->plan.get();
    auto compute_allowed = [months](uint32_t base, uint32_t growth) -> uint32_t {
        if (base == 0) {
            return 0;
        }
        uint64_t total = base + uint64_t{growth} * months;
        return static_cast<uint32_t>(std::min<uint64_t>(total, std::numeric_limits<uint32_t>::max()));
    };

    next.counters[planIndex(PlanResource::DEVICE)].used = co_await count_one(
        "SELECT COUNT(*) FROM device WHERE user=? AND enabled=1");
    next.counters[planIndex(PlanResource::DEVICE)].allowed = plan->max_devices;

    next.counters[planIndex(PlanResource::NODE)].used = co_await count_one(
        "SELECT COUNT(*) FROM node WHERE user=? AND deleted=0");
    next.counters[planIndex(PlanResource::NODE)].allowed = compute_allowed(plan->max_nodes, plan->nodes_monthly_growth);

    next.counters[planIndex(PlanResource::ACTION)].used = co_await count_one(
        "SELECT COUNT(*) FROM action WHERE user=? AND status != 'deleted'");
    next.counters[planIndex(PlanResource::ACTION)].allowed = compute_allowed(plan->max_actions, plan->actions_monthly_growth);

    next.counters[planIndex(PlanResource::WORK_SESSION)].used = co_await count_one(
        "SELECT COUNT(*) FROM work_session WHERE user=? AND state != 'deleted'");
    next.counters[planIndex(PlanResource::WORK_SESSION)].allowed = compute_allowed(
        plan->max_worksessions, plan->work_sessions_monthly_growth);

    next.counters[planIndex(PlanResource::TIME_BLOCK)].used = co_await count_one(
        "SELECT COUNT(*) FROM time_block WHERE user=? AND kind != 'deleted'");
    next.counters[planIndex(PlanResource::TIME_BLOCK)].allowed = compute_allowed(
        plan->max_time_blocks, plan->time_blocks_monthly_growth);

    next.next_refresh_at = std::chrono::steady_clock::now() + std::chrono::hours{24}
        + std::chrono::minutes{randomBetween(0, 180)};

    std::lock_guard lock{plan_usage_mutex_};
    for (size_t i = 0; i < plan_usage_.counters.size(); ++i) {
        next.counters[i].reserved = plan_usage_.counters[i].reserved;
    }
    plan_usage_ = next;
    co_return;
}

boost::asio::awaitable<void> UserContext::initializePlanUsage(jgaa::mysqlpool::Mysqlpool::Handle& dbh)
{
    co_await loadPlanUsageState_(dbh);
    schedulePeriodicPlanUsageRefresh();
    co_return;
}

boost::asio::awaitable<void> UserContext::refreshPlanUsageNow()
{
    {
        std::lock_guard lock{plan_usage_mutex_};
        if (plan_usage_.refresh_in_progress) {
            co_return;
        }
        plan_usage_.refresh_in_progress = true;
    }

    try {
        auto db = co_await Server::instance().db().getConnection();
        co_await loadPlanUsageState_(db);
        schedulePeriodicPlanUsageRefresh();
    } catch (const std::exception& ex) {
        LOG_WARN_N << "Failed to refresh plan usage for user " << user_uuid_ << ": " << ex.what();
        std::lock_guard lock{plan_usage_mutex_};
        plan_usage_.refresh_in_progress = false;
        plan_usage_.next_refresh_at = std::chrono::steady_clock::now() + std::chrono::minutes{5};
    }
}

void UserContext::schedulePlanUsageRefresh(std::chrono::milliseconds delay)
{
    if (!plan_usage_timer_) {
        return;
    }
    auto weak = weak_from_this();
    const auto generation = ++plan_refresh_generation_;
    plan_usage_timer_->cancel();
    plan_usage_timer_->expires_after(delay);
    plan_usage_timer_->async_wait([weak, generation](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            LOG_WARN_N << "Plan usage timer failed: " << ec.message();
            return;
        }
        if (auto self = weak.lock(); self && self->plan_refresh_generation_ == generation) {
            boost::asio::co_spawn(Server::instance().ctx(),
                                  [self]() -> boost::asio::awaitable<void> {
                                      co_await self->refreshPlanUsageNow();
                                  },
                                  boost::asio::detached);
        }
    });
}

void UserContext::schedulePeriodicPlanUsageRefresh()
{
    std::lock_guard lock{plan_usage_mutex_};
    const auto now = std::chrono::steady_clock::now();
    const auto next = plan_usage_.next_refresh_at > now ? plan_usage_.next_refresh_at - now : std::chrono::seconds{1};
    schedulePlanUsageRefresh(std::chrono::duration_cast<std::chrono::milliseconds>(next));
}

void UserContext::scheduleShortPlanUsageRefresh()
{
    schedulePlanUsageRefresh(std::chrono::milliseconds{randomBetween(100, 2000)});
}

UserContext::ResourceReservation UserContext::reserveAddition(uint32_t amount, PlanResource resource)
{
    if (amount == 0 || !Server::instance().config().payment.enable_plan) {
        return {};
    }

    std::lock_guard lock{plan_usage_mutex_};
    auto& state = plan_usage_;
    auto& counter = state.counters[planIndex(resource)];
    if (!state.loaded) {
        throw server_err{pb::Error::TEMPORATY_FAILURE,
                         format("Plan limits are not ready yet for {}", planResourceName(resource))};
    }

    LOG_TRACE_EX(*this) << "Reserving " << amount << " " << planResourceName(resource) << "(s) for user " << userUuid()
               << ". Currently used: " << counter.used
               << ", reserved: " << counter.reserved
               << ", allowed: " << counter.allowed
               << (counter.stale_after_delete ? " (stale after delete)" : "")
               << ", effective allowed with grace: " << allowedWithGrace_(counter);

    if (state.next_refresh_at <= std::chrono::steady_clock::now() && !state.refresh_in_progress) {
        state.refresh_in_progress = true;
        boost::asio::co_spawn(Server::instance().ctx(),
                              [self = shared_from_this()]() -> boost::asio::awaitable<void> {
                                  co_await self->refreshPlanUsageNow();
                              },
                              boost::asio::detached);
    }

    const auto effective_allowed = allowedWithGrace_(counter);
    if (effective_allowed != 0) {
        const uint64_t total = uint64_t{counter.used} + counter.reserved + amount;
        if (total > effective_allowed) {
            const auto plan_name = tenant_plan_ && tenant_plan_->plan ? tenant_plan_->plan->name : "unknown";
            const auto resource_name = std::string{planResourceName(resource)};
            throw server_err{
                pb::Error::LIMIT_EXCEEDED,
                format("Plan '{}' allows {} {}{}, user currently has {} and requested {} more",
                       plan_name,
                       counter.allowed,
                       resource_name,
                       counter.stale_after_delete ? format(" (temporary grace cap {})", effective_allowed) : "",
                       counter.used + counter.reserved,
                       amount)};
        }
    }

    counter.reserved += amount;
    return ResourceReservation{this, resource, amount};
}

void UserContext::commitReservation(PlanResource resource, uint32_t amount) noexcept
{
    std::lock_guard lock{plan_usage_mutex_};
    auto& counter = plan_usage_.counters[planIndex(resource)];
    counter.reserved = counter.reserved >= amount ? counter.reserved - amount : 0;
    counter.used += amount;
}

void UserContext::releaseReservation(PlanResource resource, uint32_t amount) noexcept
{
    std::lock_guard lock{plan_usage_mutex_};
    auto& counter = plan_usage_.counters[planIndex(resource)];
    counter.reserved = counter.reserved >= amount ? counter.reserved - amount : 0;
}

void UserContext::onDeleted(PlanResource resource, uint32_t amount) noexcept
{
    if (!Server::instance().config().payment.enable_plan || amount == 0) {
        return;
    }

    std::lock_guard lock{plan_usage_mutex_};
    auto& counter = plan_usage_.counters[planIndex(resource)];
    counter.used = counter.used >= amount ? counter.used - amount : 0;
    counter.stale_after_delete = false;
}

void UserContext::onMassDelete(PlanResource resource)
{
    onMassDelete({resource});
}

void UserContext::onMassDelete(std::initializer_list<PlanResource> resources)
{
    if (!Server::instance().config().payment.enable_plan) {
        return;
    }

    {
        std::lock_guard lock{plan_usage_mutex_};
        for (const auto resource : resources) {
            plan_usage_.counters[planIndex(resource)].stale_after_delete = true;
        }
    }
    scheduleShortPlanUsageRefresh();
}

void UserContext::setDeviceMobile(const boost::uuids::uuid& deviceId, bool isMobile)
{
    {
        std::lock_guard lock{instance_mutex_};
        devices_[deviceId].is_mobile = isMobile;
    }
    refreshSessionAccess();
}

pb::SessionAccess UserContext::currentSessionAccess(const boost::uuids::uuid& deviceId) const
{
    pb::SessionAccess access;
    access.mutable_deviceid()->set_uuid(to_string(deviceId));

    SessionAccessMode mode = SessionAccessMode::FULL_ACCESS;
    {
        std::shared_lock lock{mutex_};
        for (const auto& session : sessions_) {
            if (session->deviceId() == deviceId) {
                mode = session->accessMode();
                break;
            }
        }
    }

    switch (mode) {
    case SessionAccessMode::FULL_ACCESS:
        access.set_mode(pb::SessionAccess::FULL_ACCESS);
        break;
    case SessionAccessMode::READ_ONLY_DEVICE_LIMIT:
        access.set_mode(pb::SessionAccess::READ_ONLY_DEVICE_LIMIT);
        break;
    case SessionAccessMode::READ_ONLY_MOBILE_ONLY:
        access.set_mode(pb::SessionAccess::READ_ONLY_MOBILE_ONLY);
        break;
    case SessionAccessMode::READ_ONLY_TENANT:
        access.set_mode(pb::SessionAccess::READ_ONLY_TENANT);
        break;
    }
    return access;
}

void UserContext::refreshSessionAccessLocked_(vector<pair<boost::uuids::uuid, SessionAccessMode>>& changed)
{
    lock_guard instance_lock{instance_mutex_};

    struct DeviceState {
        boost::uuids::uuid device_id;
        uint64_t connected_order = 0;
        bool is_mobile = false;
        vector<shared_ptr<Session>> sessions;
    };

    unordered_map<boost::uuids::uuid, DeviceState, UuidHash> by_device;
    for (const auto& session : sessions_) {
        auto& state = by_device[session->deviceId()];
        state.device_id = session->deviceId();
        state.is_mobile = devices_[session->deviceId()].is_mobile;
        state.sessions.push_back(session);
        if (state.connected_order == 0 || session->connectedOrder() < state.connected_order) {
            state.connected_order = session->connectedOrder();
        }
    }

    SessionAccessMode default_mode = SessionAccessMode::FULL_ACCESS;
    uint32_t max_devices = 0;
    bool mobile_only = false;
    const auto tenant_state = tenantState();
    if (tenant_state == pb::Tenant::State::Tenant_State_READ_ONLY) {
        default_mode = SessionAccessMode::READ_ONLY_TENANT;
    }

    if (Server::instance().config().payment.enable_plan && tenant_plan_ && tenant_plan_->plan) {
        max_devices = tenant_plan_->plan->max_devices;
        mobile_only = tenant_plan_->plan->mobile_only;
    } else {
        max_devices = 0;
        mobile_only = false;
    }

    vector<DeviceState*> ranked;
    ranked.reserve(by_device.size());
    for (auto& [_, state] : by_device) {
        ranked.push_back(&state);
    }
    ranges::sort(ranked, [](const DeviceState* a, const DeviceState* b) {
        if (a->connected_order != b->connected_order) {
            return a->connected_order < b->connected_order;
        }
        return a->device_id < b->device_id;
    });

    uint32_t granted = 0;
    for (auto* state : ranked) {
        auto mode = default_mode;
        if (mode == SessionAccessMode::FULL_ACCESS) {
            const bool eligible = !mobile_only || state->is_mobile;
            if (!eligible) {
                mode = SessionAccessMode::READ_ONLY_MOBILE_ONLY;
            } else if (max_devices != 0 && granted >= max_devices) {
                mode = SessionAccessMode::READ_ONLY_DEVICE_LIMIT;
            } else {
                ++granted;
            }
        }

        for (auto& session : state->sessions) {
            const auto old_mode = session->accessMode();
            if (old_mode != mode) {
                session->setAccessMode(mode);
                changed.emplace_back(session->deviceId(), mode);
            }
        }
    }
}

void UserContext::refreshSessionAccess()
{
    vector<pair<boost::uuids::uuid, SessionAccessMode>> changed;
    {
        unique_lock lock{mutex_};
        refreshSessionAccessLocked_(changed);
    }

    publishSessionAccessChanges(std::move(changed));
}

void UserContext::publishSessionAccessChanges(vector<pair<boost::uuids::uuid, SessionAccessMode>> changed)
{

    if (changed.empty() || !Server::hasInstance()) {
        return;
    }

    for (const auto& [device_id, mode] : changed) {
        auto update = make_shared<pb::Update>();
        update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
        auto* access = update->mutable_sessionaccess();
        access->mutable_deviceid()->set_uuid(to_string(device_id));
        switch (mode) {
        case SessionAccessMode::FULL_ACCESS:
            access->set_mode(pb::SessionAccess::FULL_ACCESS);
            break;
        case SessionAccessMode::READ_ONLY_DEVICE_LIMIT:
            access->set_mode(pb::SessionAccess::READ_ONLY_DEVICE_LIMIT);
            break;
        case SessionAccessMode::READ_ONLY_MOBILE_ONLY:
            access->set_mode(pb::SessionAccess::READ_ONLY_MOBILE_ONLY);
            break;
        case SessionAccessMode::READ_ONLY_TENANT:
            access->set_mode(pb::SessionAccess::READ_ONLY_TENANT);
            break;
        }
        boost::asio::co_spawn(Server::instance().ctx(),
                              [self = shared_from_this(), update]() mutable -> boost::asio::awaitable<void> {
                                  co_await self->publish(update);
                              },
                              boost::asio::detached);
    }
}

UserContext::SubscribeReplayResult
UserContext::addPublisher(const std::shared_ptr<Publisher> &publisher, std::optional<uint32_t> fromMessageId)
{
    LOG_TRACE_N << "Adding publisher " << publisher->uuid() << " to user context for user " << userUuid();
    unique_lock lock{mutex_};
    purgeExpiredPublishers();

    if (fromMessageId.has_value()) {
        const auto from = *fromMessageId;
        const auto current = publish_message_id_;
        if (from > current) {
            LOG_WARN_N << "Replay request from future message id " << from
                       << " when current publish id is " << current
                       << " for user " << userUuid();
            return SubscribeReplayResult::REPLAY_UNAVAILABLE;
        }

        if (from < current) {
            if (retained_updates_.empty()) {
                LOG_WARN_N << "Replay requested from message id " << from
                           << " for user " << userUuid()
                           << ", but no retained updates are available.";
                return SubscribeReplayResult::REPLAY_UNAVAILABLE;
            }

            const auto oldest_available = retained_updates_.front()->messageid();
            if (static_cast<uint64_t>(from) + 1 < oldest_available) {
                LOG_WARN_N << "Replay requested from message id " << from
                           << " for user " << userUuid()
                           << ", but oldest retained message id is " << oldest_available;
                return SubscribeReplayResult::REPLAY_UNAVAILABLE;
            }

            for (const auto& update : retained_updates_) {
                if (update->messageid() > from && !publisher->publish(update)) {
                    LOG_WARN_N << "Failed to queue retained update #" << update->messageid()
                               << " for publisher " << publisher->uuid();
                    return SubscribeReplayResult::REPLAY_UNAVAILABLE;
                }
            }
        }
    }

    publishers_.push_back(publisher);
    return fromMessageId.has_value() ? SubscribeReplayResult::REPLAY_QUEUED
                                     : SubscribeReplayResult::LIVE_ONLY;
}

void UserContext::retainPublishedUpdate(const std::shared_ptr<pb::Update>& update)
{
    retained_updates_.push_back(update);
    while (retained_updates_.size() > kReplayBacklogLimit) {
        retained_updates_.pop_front();
    }
}

void UserContext::removePublisher(const boost::uuids::uuid &uuid)
{
    LOG_TRACE_N << "Removing publisher " << uuid << " from user context for user " << userUuid();
    unique_lock lock{mutex_};
    std::erase_if(publishers_, [uuid](const auto& p) {
        if (auto pub = p.lock()) {
            return pub->uuid() == uuid;
        } else {
            return true; // Always remove dangling pointers
        }
    });
}

void UserContext::publishUpdates(std::shared_ptr<pb::Update> &update, set<boost::uuids::uuid>* devices) {
    unique_lock lock{mutex_};

    // Keep message-id assignment and subscriber fan-out in one ordered critical section.
    purgeExpiredPublishers();
    update->set_messageid(++publish_message_id_);
    retainPublishedUpdate(update);
    LOG_TRACE_N << "Publishing "
                << pb::Update::Operation_Name(update->op())
                << " update to " << publishers_.size() << " subscribers, Json: "
                << toJsonForLog(*update);

    for(auto& weak_pub: publishers_) {
        if (auto pub = weak_pub.lock()) {
            if (auto session = pub->getSessionWeakPtr().lock()) {
                LOG_TRACE_N << "Publish #" << update->messageid() << " to " << pub->uuid();
                if (pub->publish(update)) {
                    // The device-list will exclude the sessions device device from push notification for this update.
                    // We only add the device to the list if publish() succeeded and the update was
                    // accepted for delivery over gRPC.
                    if (devices) {
                        devices->emplace(session->deviceId());
                    }
                }
            } else {
                LOG_TRACE_N << "Publisher " << pub->uuid() << " has no valid session. Skipping.";
            }
        } else {
            LOG_WARN_N << "Failed to get a pointer to a publisher."
                       << " for user " << userUuid();
        }
    }
}

boost::asio::awaitable<void> UserContext::publish(std::shared_ptr<pb::Update> &update)
{
    set<boost::uuids::uuid> devices;

    publishUpdates(update, &devices);

    // Filter out updates that are not for push notifications
    if (!isForPush(*update)) {
        co_return;
    }

    vector<std::shared_ptr<PushNotifications>> pn;

    // Briefly lock the instance and copy any push notification handlers
    // for devices that are not already published to in the `pn` set.
    {
        unique_lock lock{instance_mutex_};
        for(auto& [id, dev] : devices_) {
            if (dev.push_notifications_ && ! devices.contains(id)) {
                pn.emplace_back(dev.push_notifications_);
            }
        }
    }  // unlock

    // Publish to push notification handlers
    // Currently we do this on a "best effort" basis with no queuing or retry.
    // TODO: Apply rate limiting here
    for(auto handler : pn) {
        try {
            co_await handler->sendNotification(update);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to send push notification for update #" << update->messageid()
                       << " to device " << handler->deviceId() << ": " << e.what();
        }
    }
}

boost::asio::awaitable<void> UserContext::publishFullResync(uint64_t dataSyncEpoch)
{
    setDataSyncEpoch(dataSyncEpoch);

    auto update = std::make_shared<pb::Update>();
    update->set_op(pb::Update::Operation::Update_Operation_UPDATED);
    update->set_resync(true);
    co_await publish(update);
}

void UserContext::purgeExpiredPublishers()
{
    // Remove expired weak_ptrs
    auto num_purged = 0u;
    publishers_.erase(
        std::remove_if(
            publishers_.begin(), publishers_.end(),
            [&](const std::weak_ptr<Publisher>& weak_pub) {
                if (weak_pub.expired()) {
                    ++num_purged;
                    return true;
                }
                return false;
            }
            ),
        publishers_.end()
        );

    if (num_purged > 0) {
        LOG_DEBUG_N << "Purged " << num_purged << " expired publishers"
                                                  " for user " << userUuid();
    };
}

pb::Subscription UserContext::getSubscription() const
{
    pb::Subscription s;

    if (tenant_plan_) {
        const auto& tp = *tenant_plan_;
        if (auto p = s.mutable_plan()) {
            auto& plan = *p;
            plan.set_name(tp.plan->name);
            plan.set_active(tp.plan->active);
            plan.mutable_createdat()->set_unixtime(std::chrono::system_clock::to_time_t(tp.plan->created_at));
            plan.set_maxusers(tp.plan->max_users);
            plan.set_maxdevices(tp.plan->max_devices);
            plan.set_maxnodes(tp.plan->max_nodes);
            plan.set_nodexmonthlygrowth(tp.plan->nodes_monthly_growth);
            plan.set_maxactions(tp.plan->max_actions);
            plan.set_actionsmonthlygrowth(tp.plan->actions_monthly_growth);
            plan.set_maxworksessions(tp.plan->max_worksessions);
            plan.set_worksessionsmonthlygrowth(tp.plan->work_sessions_monthly_growth);
            plan.set_maxtimeblocks(tp.plan->max_time_blocks);
            plan.set_timeblocksmonthlygrowth(tp.plan->time_blocks_monthly_growth);
            plan.set_mobileonly(tp.plan->mobile_only);
        }
        if (tp.updated_at) {
            s.mutable_planupdatedat()->set_unixtime(std::chrono::system_clock::to_time_t(*tp.updated_at));
        }
        if (tp.expires_at) {
            s.mutable_planexpires()->set_unixtime(std::chrono::system_clock::to_time_t(*tp.expires_at));
        }
        if (tp.grace_expires_at) {
            s.mutable_graceperiodexpires()->set_unixtime(std::chrono::system_clock::to_time_t(*tp.grace_expires_at));
        }
        if (tp.account_expires_at) {
            s.mutable_accountexpires()->set_unixtime(std::chrono::system_clock::to_time_t(*tp.account_expires_at));
        }
        s.set_planseats(tp.max_users);
    }

    return s;
}

boost::asio::awaitable<void> UserContext::reloadTenantPlan(jgaa::mysqlpool::Mysqlpool::Handle& dbh)
{
    if (!Server::instance().config().payment.enable_plan) {
        setTenantPlan({});
        co_return;
    }

    auto res = co_await dbh.exec(
        "SELECT plan, plan_updated, plan_expires, plan_seats, grace_period_expires, account_expires "
        "FROM tenant WHERE id = ?",
        tenant_uuid_);

    enum Cols {
        PLAN,
        PLAN_UPDATED,
        PLAN_EXPIRES,
        PLAN_SEATS,
        GRACE_PERIOD_EXPIRES,
        ACCOUNT_EXPIRES
    };

    if (res.rows().empty()) {
        LOG_WARN_N << "Failed to reload tenant plan for missing tenant " << tenant_uuid_;
        setTenantPlan({});
        co_return;
    }

    const auto& row = res.rows().front();
    if (row.at(PLAN).is_null()) {
        setTenantPlan({});
        co_return;
    }

    auto tenant_plan = make_shared<TenantPlan>();
    tenant_plan->plan = Server::instance().grpc().sessionManager().getPlan(row.at(PLAN).as_string());
    if (row.at(PLAN_UPDATED).is_datetime()) {
        tenant_plan->updated_at = row.at(PLAN_UPDATED).as_datetime().as_time_point();
    }
    if (row.at(PLAN_EXPIRES).is_datetime()) {
        tenant_plan->expires_at = row.at(PLAN_EXPIRES).as_datetime().as_time_point();
    }
    if (row.at(GRACE_PERIOD_EXPIRES).is_datetime()) {
        tenant_plan->grace_expires_at = row.at(GRACE_PERIOD_EXPIRES).as_datetime().as_time_point();
    }
    if (row.at(ACCOUNT_EXPIRES).is_datetime()) {
        tenant_plan->account_expires_at = row.at(ACCOUNT_EXPIRES).as_datetime().as_time_point();
    }
    if (row.at(PLAN_SEATS).is_int64()) {
        tenant_plan->max_users = row.at(PLAN_SEATS).as_int64();
    } else {
        tenant_plan->max_users = 0;
    }

    setTenantPlan(std::move(tenant_plan));
    refreshSessionAccess();
    scheduleShortPlanUsageRefresh();
}

boost::asio::awaitable<bool>
UserContext::checkForReplay(const boost::uuids::uuid &deviceId, uint instanceId, uint reqId)
{
    validateInstanceId(instanceId);
    const auto index = instanceId - 1; // Instance start at 1

    bool rval = false;
    Device::value_t last_req_id;

    auto process = [&] {
        LOG_TRACE << "Replay check for device " << deviceId
                  << ", instanceId=" << instanceId
                  << ", reqId=" << reqId
                  << ", last_req_id=" << *last_req_id
                  << ", user=" << userUuid();

        if (*last_req_id >= reqId) {
            LOG_DEBUG << "Replay detected for device" << deviceId
                      << ", instanceId=" << instanceId
                      << ", reqId=" << reqId
                      << ", for user=" << userUuid();
            rval = true;
        }
    };

    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if ( device.last_request_id) {
            last_req_id = device.last_request_id->get(index);

            // If we have a value in memory, use it now. We don't want to aquire the
            // lock twice, but we need to do that if we access the db.
            if (last_req_id.has_value()) {
                process();
                device.last_request_id->set(index, max(*last_req_id, reqId));
                co_return rval;
            };
        }
    }

    // Get the value from the database
    if (auto v = co_await getLastReqId(deviceId, instanceId, true); v.has_value()) {
        last_req_id = *v;
    } else {
        last_req_id = 0; // Initialize it
    }

    assert(last_req_id.has_value());
    process();

    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if (!device.last_request_id) {
            device.last_request_id.emplace();
        }
        device.last_request_id->set(index, max(*last_req_id, reqId));
    }

    co_return rval;
}

boost::asio::awaitable<UserContext::Device::value_t>
UserContext::getLastReqId(const boost::uuids::uuid &deviceId, uint instanceId, bool lookupInDbOnly) {
    validateInstanceId(instanceId);
    const auto index = instanceId - 1; // Instance start at 1

    if (!lookupInDbOnly) {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if (device.last_request_id) {
            if (auto value = device.last_request_id->get(index); value.has_value()) {
                co_return value;
            }
        }
    }

    // Fetch from database
    auto& db = Server::instance().db();
    auto res = co_await db.exec("SELECT request_id FROM request_state WHERE userid=? AND devid=? AND instance=?",
                                userUuid(), deviceId, instanceId);
    if (!res.rows().empty()) {
        const auto last_req_id = static_cast<uint32_t>(res.rows().front().at(0).as_int64());
        {
            lock_guard lock{instance_mutex_};
            auto& device = devices_[deviceId];
            if (!device.last_request_id) {
                device.last_request_id.emplace();
            }
            device.last_request_id->set(index, last_req_id);
        }

        co_return last_req_id;
    }

    co_return std::nullopt;
}

boost::asio::awaitable<void> UserContext::resetReplay(const boost::uuids::uuid &deviceId, uint instanceId)
{
    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        validateInstanceId(instanceId);

        const auto index = instanceId - 1; // Instance start at 1
        if (!device.last_request_id) {
            device.last_request_id.emplace();
        }
        device.last_request_id->set(index, 0);
    }
    co_await saveLastReqIds(deviceId);
}

void UserContext::saveReplayStateForDevice(const boost::uuids::uuid &deviceId)
{
    boost::asio::co_spawn(Server::instance().ctx(), [self = shared_from_this(), deviceId] () -> boost::asio::awaitable<void> {
        co_await self->saveLastReqIds(deviceId);
    }, boost::asio::detached);
}

void UserContext::Session::setHasPush(bool enabled) {
    has_push_ = enabled;
    if (auto p = publisher_.lock()) {
        p->setHasPush(enabled);
    }
}

void UserContext::Session::requireWritableForAdd(string_view resource) const
{
    switch (user().tenantState()) {
    case pb::Tenant::State::Tenant_State_ACTIVE:
    case pb::Tenant::State::Tenant_State_PENDING_ACTIVATION:
        break;
    case pb::Tenant::State::Tenant_State_READ_ONLY:
        throw server_err{pb::Error::PERMISSION_DENIED,
                         format("This tenant is currently in read-only mode. Cannot add {}.", resource)};
    case pb::Tenant::State::Tenant_State_SUSPENDED:
        throw server_err{pb::Error::TENANT_SUSPENDED,
                         format("This tenant is suspended. Cannot add {}.", resource)};
    }

    switch (accessMode()) {
    case SessionAccessMode::FULL_ACCESS:
        return;
    case SessionAccessMode::READ_ONLY_DEVICE_LIMIT:
        throw server_err{pb::Error::LIMIT_EXCEEDED,
                         format("This device is currently in read-only mode because the device limit is reached. Cannot add {}.", resource)};
    case SessionAccessMode::READ_ONLY_MOBILE_ONLY:
        throw server_err{pb::Error::LIMIT_EXCEEDED,
                         format("This device is currently in read-only mode because the active plan only allows mobile devices full access. Cannot add {}.", resource)};
    case SessionAccessMode::READ_ONLY_TENANT:
        throw server_err{pb::Error::PERMISSION_DENIED,
                         format("This tenant is currently in read-only mode. Cannot add {}.", resource)};
    default:
        assert(false && "Unhandled session access mode");
        throw server_err{pb::Error::GENERIC_ERROR,
                         "Unhandled session access mode"};
    }
}

void UserContext::Session::push(const std::shared_ptr<pb::Update> &message)
{
    if (!hasPush()) {
        LOG_TRACE_N << "Skipping push notification for update #" << message->messageid()
                    << " for user " << user().userUuid() << " on device " << deviceId()
                    << " because push notifications are assumed disabled on this device.";
        return;
    }

    boost::asio::co_spawn(Server::instance().ctx(), [user = user_, msg=message, devid=deviceId()] () -> boost::asio::awaitable<void> {
        try {
            co_await user->push(msg, devid);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to push notification for update #" << msg->messageid()
                       << " for user " << user->userUuid() << " on device " << devid
                       << ": " << e.what();
        }
    }, boost::asio::detached);
}

bool UserContext::Session::publishClientUpdate(const pb::ClientUpdate& update)
{
    std::shared_ptr<Publisher> publisher;
    {
        std::scoped_lock lock{publisher_mutex_};
        if (announced_client_update_ && announced_client_update_->version_code() == update.version_code()
            && announced_client_update_->version() == update.version()
            && announced_client_update_->required() == update.required()) {
            return false;
        }
        publisher = publisher_.lock();
        if (!publisher) return false;
        announced_client_update_ = update;
    }
    auto message = std::make_shared<pb::Update>();
    message->set_messageid(0);
    message->mutable_clientupdate()->CopyFrom(update);
    return publisher->publish(message);
}

boost::asio::awaitable<void> UserContext::push(const std::shared_ptr<pb::Update> &message, const boost::uuids::uuid deviceId)
{
    std::shared_ptr<PushNotifications> handler;

    // Briefly lock the instance and copy any push notification handlers
    // for devices that are not already published to in the `pn` set.
    {
        unique_lock lock{instance_mutex_};
        for(auto& [id, dev] : devices_) {
            if (dev.push_notifications_ && id == deviceId) {
                handler= dev.push_notifications_;
                break;
            }
        }
    }  // unlock

    if (handler) {
        try {
            // TODO: Apply rate limiting here
            co_await handler->sendNotification(message);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to send push notification for update #" << message->messageid()
            << " to device " << handler->deviceId() << ": " << e.what();
        }
    }
    co_return;
};

bool UserContext::isForPush(const pb::Update &update) {
    return update.has_calendarevents();
}

boost::asio::awaitable<void> UserContext::reloadPushers() {

    if (!Server::instance().config().push_enabled) {
        co_return;
    }

    auto db = co_await Server::instance().db().getConnection();
    const auto res = co_await db.exec("SELECT id, pushType, pushToken FROM device WHERE user=? AND pushType IS NOT NULL", userUuid());

    enum Cols {
        ID,
        TYPE,
        TOKEN
    };

    {
        unique_lock lock{instance_mutex_};

        // Remove all existing push handlers
        for(auto & [_, device] : devices_) {
            device.push_notifications_.reset();
        }

        // Add new push handlers
        for(const auto& row : res.rows()) {
            const auto deviceId = toUuid(row[ID].as_string());
            const auto type = row[TYPE].as_string();
            const auto token = row[TOKEN].as_string();

            if (type.empty() || token.empty()) {
                LOG_TRACE_N << "Skipping device " << deviceId << " with empty type or token";
                continue;
            }

            auto& device = devices_[deviceId];
            std::shared_ptr<jgaa::cpp_push::Pusher> pusher;
            if (type == "google") {
                pusher = Server::instance().getGooglePusher();
            }

            if (pusher) {
                LOG_TRACE_N << "Creating push notifications handler for device "
                            << deviceId << " with type " << type << " and token " << token.substr(0, 16) << "...";
                device.push_notifications_ = make_shared<PushNotifications>(toUuid(userUuid()), deviceId, token, pusher);
            } else {
                LOG_WARN_N << "No pusher found for device " << deviceId
                           << " with type " << type << " and token " << token.substr(0, 8) << "...";
            }
        }
    }
}

void UserContext::Session::handlePushState(const pb::PushNotificationConfig &wp)
{
    boost::asio::co_spawn(Server::instance().ctx(), [self = shared_from_this(), wp]() -> boost::asio::awaitable<void> {
        co_await self->processPushState(wp);
    }, boost::asio::detached);
}

boost::asio::awaitable<void> UserContext::Session::processPushState(pb::PushNotificationConfig wp)
{
    bool enabled = true;
    {
        auto db = co_await Server::instance().db().getConnection();
        switch(wp.kind()) {
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_DISABLE:
    disable:
            enabled = false;
            LOG_DEBUG_N << "Disabling push notifications for device " << deviceid_ << " for user " << user().userUuid();
            co_await db.exec("UPDATE device SET pushType=NULL, pushToken=NULL WHERE id=? AND user=?",
                    deviceid_, user().userUuid());
            break;
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_GOOGLE:
            LOG_DEBUG_N << "Enabling Google push notifications for device " << deviceid_ << " for user " << user().userUuid();
            if (wp.token().empty()) {
                LOG_INFO_N << "Google push token is empty. Disabling push notifications for device " << deviceid_ << " for user " << user().userUuid();
                goto disable;
            }
            co_await db.exec("UPDATE device SET pushType='google', pushToken=? WHERE id=? AND user=?",
                    wp.token(), deviceid_, user().userUuid());
            break;
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_APPLE:
            LOG_WARN_N << "Apple not supported for push notifications yet.";
            goto disable;
            break;
        default:
            LOG_WARN_N << "Unknown push notification kind: " << pb::PushNotificationConfig::Kind_Name(wp.kind());
            throw server_err{pb::Error::INVALID_ARGUMENT, "Unknown push notification kind"};
        }
    }

    co_await user_->reloadPushers();
    setHasPush(enabled);

    co_return;
}

void UserContext::setLastReadNotification(uint32_t id) noexcept {
    int id_int = static_cast<int>(id);
    if (id_int < 0) {
        LOG_WARN_N << "Invalid last read notification ID: " << id;
        return;
    }
    atomicSetIfGreater(last_read_notification_id_, id_int);
}

boost::asio::awaitable<void> UserContext::saveLastReqIds(const boost::uuids::uuid& deviceId) {
    try {
        Device::values_t values;
        {
            lock_guard lock{instance_mutex_};
            if (auto it = devices_.find(deviceId); it != devices_.end() && it->second.last_request_id.has_value()) {
                values = it->second.last_request_id.value();
            } else {
                co_return;
            }
        }
        const string devid_str = to_string(deviceId);
        auto db = co_await Server::instance().db().getConnection();
        for (size_t i = 0; i < values.size(); ++i) {
            auto v = values.get(i, 0);
            if (!v.has_value()) {
                continue;
            }
            co_await db.exec(R"(INSERT INTO request_state (userid, devid, instance, request_id)
                    VALUES (?, ?, ?, ?)
                    ON DUPLICATE KEY UPDATE
                        request_id = VALUES(request_id),
                        last_update = CURRENT_TIMESTAMP)",
                             userUuid(),  deviceId, i + 1, values.get(i, 0));
        }
    } catch(const std::exception& e) {
        LOG_WARN << "Failed to save replay state for device " << deviceId
                 << " for user " << userUuid() << ". Error: " << e.what();
    }
}

boost::uuids::uuid UserContext::newUuid()
{
    return ::nextapp::newUuid();
}

void UserContext::validateInstanceId(uint instanceId)
{
    if (instanceId < 1 || instanceId > 10) {
        throw server_err{pb::Error::INVALID_ARGUMENT, "Invalid instanceId"};
    }
}

string mapToString(const auto &map) {
    ostringstream out;
    unsigned count = 0;
    for(const auto& [key, value] : map) {
        if (++count > 1) {
            out << ", ";
        }
        out << key << ": " << value;
    }
    return out.str();
}

boost::asio::awaitable<std::shared_ptr<UserContext::Session> > SessionManager::getSession(
    const ::grpc::ServerContextBase* context, bool allowNewSession)
{
    LOG_TRACE << "Getting session for peer at " << context->peer()
              << " context: " << mapToString(context->client_metadata());

    string_view device_id;

    if (!context->auth_context()->IsPeerAuthenticated()) {
        if (skip_tls_auth_) {
            LOG_WARN << "TLS is disabled. Skipping authentication via cert.";
            if (auto it = context->client_metadata().find("did"); it != context->client_metadata().end()) {
                device_id = to_string_view(it->second);
                goto initial_auth_ok;
            }
        }
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }

    {
        auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
        if (v.empty()) {
            throw server_err{pb::Error::NOT_FOUND, "Missing device ID in client cert"};
        }
        device_id = to_string_view(v.front());
    }

initial_auth_ok:

    const auto device_uuid = toUuid(device_id);
    boost::uuids::uuid new_sid = newUuid();

    // Happy path. Just return an existing session.
    if (auto it = context->client_metadata().find("sid"); it != context->client_metadata().end()) {
        auto sid = toUuid(to_string_view(it->second));
        shared_lock lock{mutex_};
        if (auto it = sessions_.find(sid); it != sessions_.end()) {
            auto& session = *it->second;
            if (!session.user().valid()) {
                LOG_DEBUG_N << "Session " << sid << " for device " << device_uuid
                            << " is not valid because the user context is not valid. Removing it.";
                lock.unlock();
                unique_lock write_lock{mutex_};
                auto pending_save = removeSession_(sid);
                write_lock.unlock();
                if (pending_save) {
                    persistPublishStateSync_(*pending_save);
                }
                throw server_err{pb::Error::AUTH_FAILED, "User context is no longer valid. Did you delete your user account?"};
            }
            if (session.deviceId() == device_uuid) [[likely]] {
                co_return session.shared_from_this();
            }
            LOG_WARN << "Session " << sid << " is not for device " << device_uuid << ". Closing the session.";
            throw server_err{pb::Error::AUTH_FAILED, "Session is not for the connected device"};
        }

        // TODO: Enable when the client can handle a server-assigned session-id.
        //throw server_err{pb::Error::AUTH_FAILED, "Session not found"};
        new_sid = sid;
    } else if (!allowNewSession) {
        throw server_err{pb::Error::AUTH_FAILED, "Session-id 'sid' not found in the gRPC meta-data."};
    }

    // Fetch or create a user context.
    auto ucx = co_await getUserContext(device_uuid, context);
    assert(ucx);

    auto scope = ucx->isAdmin() ? server_.metrics().sessions_admin().scoped() : server_.metrics().sessions_user().scoped();
    const auto connected_order = next_connected_order_.fetch_add(1, std::memory_order_relaxed);

    auto session = make_shared<UserContext::Session>(ucx, device_uuid, new_sid, connected_order, std::move(scope));
    {
        unique_lock lock{mutex_};
        sessions_[session->sessionId()] = session.get();
        ucx->addSession(session);
    }
    LOG_INFO << "Added user-session << " << session->sessionId()
             << " for device " << session->deviceId() << " for user " << ucx->userUuid()
             << " from IP " << context->peer();

    // User logged on with a device
    auto db = co_await server_.db().getConnection();
    co_await db.exec("UPDATE device SET lastSeen=NOW(), numSessions=numSessions+1 WHERE id=? AND user=?",
                     device_id,
                     session->user().userUuid());

    // TODO: Add entry in session-table
    // TODO: Check sessions limit for user
    // TODO: Check concurrent devices limit for user

    co_return session;
}

std::shared_ptr<UserContext::Session> SessionManager::getExistingSession(const ::grpc::ServerContextBase *context)
{
    boost::uuids::uuid device_uuid;
    if (!context->auth_context()->IsPeerAuthenticated()) {
        if (skip_tls_auth_) {
            if (auto it = context->client_metadata().find("did"); it != context->client_metadata().end()) {
                device_uuid = toUuid(to_string_view(it->second));
                LOG_WARN << "TLS is disabled. Skipping authentication via cert.";
                goto initial_auth_ok;
            }
        }
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }

    {
        auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
        device_uuid = toUuid(to_string_view(v.front()));
    }

initial_auth_ok:
    assert(device_uuid != boost::uuids::nil_uuid());

    if (auto it = context->client_metadata().find("sid"); it != context->client_metadata().end()) {
        auto sid = toUuid(to_string_view(it->second));
        shared_lock lock{mutex_};
        if (auto it = sessions_.find(sid); it != sessions_.end()) {
            auto& session = *it->second;
            if (session.deviceId() == device_uuid) [[likely]] {
                return session.shared_from_this();
            }
            LOG_WARN << "Session " << sid << " is not for device " << device_uuid << ". Closing the session.";
            throw server_err{pb::Error::AUTH_FAILED, "Session is not for the connected device"};
        }
    } else {
        throw server_err{pb::Error::AUTH_FAILED, "Session-id 'sid' not found in the gRPC meta-data."};
    }

    throw server_err{pb::Error::AUTH_FAILED, "Session not found"};
}

std::vector<std::shared_ptr<UserContext::Session>> SessionManager::sessions() const
{
    std::vector<std::shared_ptr<UserContext::Session>> result;
    shared_lock lock{mutex_};
    result.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
        result.emplace_back(session->shared_from_this());
    }
    return result;
}

void SessionManager::removeSession(const boost::uuids::uuid &sessionId)
{
    unique_lock lock{mutex_};
    auto pending_save = removeSession_(sessionId);
    lock.unlock();
    if (pending_save) {
        persistPublishStateSync_(*pending_save);
    }
}

std::optional<SessionManager::PendingPublishStateSave> SessionManager::removeSession_(const boost::uuids::uuid &sessionId)
{
    LOG_TRACE_N << "Removing session " << sessionId;
    if (auto it = sessions_.find(sessionId); it != sessions_.end()) {
        auto *session = it->second;
        assert(session);
        if (session) {
            boost::asio::co_spawn(ioContext(), [uid=session->user().userUuid(), devid=session->deviceId(), sessionId] () -> boost::asio::awaitable<void>  {
                LOG_DEBUG_N << "Session " << sessionId << " from device " << devid << " from user " << uid << " is done.";
                co_await Server::instance().db().exec("UPDATE device SET lastSeen=NOW() WHERE id=?", devid);
            }, boost::asio::detached);

            // Store user UUID before erasing session
            const auto userUuid = toUuid(session->user().userUuid());
            sessions_.erase(it);
            session->user().removeSession(sessionId);

            // Check if UserContext has no more sessions and remove it from memory
            if (session->user().hasNoSessions()) {
                const auto publishState = session->user().currentPublishState();
                const auto userUuidStr = session->user().userUuid();
                LOG_DEBUG_N << "Removing UserContext for user " << userUuid << " as it has no more sessions";
                users_.erase(userUuid);
                return PendingPublishStateSave{userUuid, userUuidStr, publishState};
            }
        }
    } else {
        LOG_WARN_N << "Session " << sessionId << " not found.";
    }
    return std::nullopt;
}

void UserContext::Session::cleanup() {
    LOG_TRACE_N << "Cleaning up session " << sessionid_ << " for user " << user_->userUuid() << " on device " << deviceid_;
    for(auto& c : cleanup_) {
        try {
            c();
        } catch(const std::exception& e) {
            LOG_WARN_N << "Exception in cleanup: " << e.what();
        }
    }

    user().saveReplayStateForDevice(deviceid_);
}

void SessionManager::setUserSettings(const boost::uuids::uuid &userUuid, pb::UserGlobalSettings settings)
{
    unique_lock lock{mutex_};
    if (auto it = users_.find(userUuid); it != users_.end()) {
        it->second->setSettings(settings);
    } else {
        LOG_WARN_N << "User " << userUuid << " not found in cache";
    }
}

void SessionManager::shutdown()
{

    timer_.cancel();
    std::vector<std::shared_ptr<UserContext::Session>> to_clean;
    std::vector<PendingPublishStateSave> pending_saves;
    bool has_more = false;

    do {
        {
            unique_lock lock{mutex_};
            while(!sessions_.empty()) {
                LOG_DEBUG_N << "Removing session " << sessions_.begin()->first << " due to shutdown.";
                auto session = sessions_.begin()->second;
                to_clean.push_back(session->shared_from_this());
                if (auto pending_save = removeSession_(session->sessionId())) {
                    pending_saves.push_back(std::move(*pending_save));
                }
            }
        }

        for (const auto& pending_save : pending_saves) {
            persistPublishStateSync_(pending_save);
        }
        pending_saves.clear();

        while(!to_clean.empty()) {
            auto session = to_clean.back();
            to_clean.pop_back();
            session->cleanup();
        }

        {
            unique_lock lock{mutex_};
            has_more = !sessions_.empty();
        }

    } while(has_more);
}

void SessionManager::startNextTimer()
{
    timer_.expires_after(chrono::seconds{server_.config().svr.session_timer_interval_sec});
    timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            LOG_WARN << "Timer error: " << ec.message();
            return;
        }

        if (!server_.is_done()) {
            onTimer();
            startNextTimer();
        }
    });
}

void SessionManager::onTimer()
{
    LOG_TRACE_N << "Checking sessions...";

    const auto secs = server_.config().svr.session_timeout_sec;
    const auto now = chrono::steady_clock::now();

    std::vector<std::shared_ptr<UserContext::Session>> to_clean;
    std::vector<PendingPublishStateSave> pending_saves;

    {
        unique_lock lock{mutex_};
        for(auto it = sessions_.begin(); it != sessions_.end();) {
            auto& session = it->second;
            ++it;

            const auto expieres = session->touched() + chrono::seconds{secs};

            // Show the difference between the two time points in seconds
            auto diff = chrono::duration_cast<chrono::seconds>(now - session->touched());
            LOG_TRACE_N << "Session " << session->sessionId() << " touched " << diff.count() << " seconds ago.";

            if (expieres < now) {
                to_clean.push_back(session->shared_from_this());
                LOG_DEBUG_N << "Session " << session->sessionId() << " expired.";
                if (auto pending_save = removeSession_(session->sessionId())) {
                    pending_saves.push_back(std::move(*pending_save));
                }
            }
        }
    }

    for (const auto& pending_save : pending_saves) {
        persistPublishStateSync_(pending_save);
    }

    while(!to_clean.empty()) {
        LOG_INFO << "Session " << to_clean.back()->sessionId() << " is done.";
        auto session = to_clean.back();
        to_clean.pop_back();
        session->cleanup();
    }
}

boost::asio::io_context &SessionManager::ioContext() noexcept
{
    return Server::instance().ctx();
}

boost::asio::awaitable<std::shared_ptr<UserContext> > SessionManager::getUserContext(
    const boost::uuids::uuid &deviceId,
    const ::grpc::ServerContextBase *context)
{
    auto db = co_await server_.db().getConnection();
    const auto devid = to_string(deviceId);
    string uid;
    boost::uuids::uuid userUuid;
    bool is_mobile = false;

    {
        auto res = co_await db.exec(
            "SELECT user, certHash, enabled, productType, os FROM device where id=? ", devid);
        if (res.rows().empty()) {
            LOG_WARN << "Failed to lookup device " << devid << " in the database, although the user appears to have a signed cert with that ID.";
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
        enum Cols { USER, CERT_HASH, ENABLED, PRODUCT_TYPE, OS };

        const auto& row = res.rows().front();
        uid = row.at(USER).as_string();
        userUuid = toUuid(uid);

        if (row.at(ENABLED).as_int64() == 0) {
            throw server_err{pb::Error::DEVICE_DISABLED, "Device is disabled"};
        }

        const auto product_type = row.at(PRODUCT_TYPE).is_null()
            ? string_view{}
            : string_view{row.at(PRODUCT_TYPE).as_string().data(), row.at(PRODUCT_TYPE).as_string().size()};
        const auto os_name = row.at(OS).is_null()
            ? string_view{}
            : string_view{row.at(OS).as_string().data(), row.at(OS).as_string().size()};
        is_mobile = isMobileDevice(product_type, os_name);

        {
            // Happy path
            shared_lock lock{mutex_};
            if (auto id = users_.find(userUuid) ; id != users_.end()) {
                LOG_TRACE << "UserContext: Found user " << uid << " in cache";
                id->second->setDeviceMobile(deviceId, is_mobile);
                co_return id->second;
            }
        }

        // TODO MAYBE: Validate the cert hash in the database aginst the presented certificate.
        //             gRPC just validates the that the cert is signed. We could check the hash
        //             to make 100% sure that the device match the presented cert.
    }

    auto userCreationMutex = getUserCreationMutex_(userUuid);
    std::unique_lock userCreationLock{*userCreationMutex};

    {
        shared_lock lock{mutex_};
        if (auto id = users_.find(userUuid) ; id != users_.end()) {
            LOG_TRACE << "UserContext: Found user " << uid << " in cache after waiting for creation lock";
            id->second->setDeviceMobile(deviceId, is_mobile);
            co_return id->second;
        }
    }

    auto res = co_await db.exec(
        "SELECT u.tenant, t.kind, u.kind, t.state, u.active, s.settings, u.created, "
        "t.plan, t.plan_updated, t.plan_expires, t.plan_seats, t.grace_period_expires, t.account_expires, "
        "u.data_sync_epoch "
        "FROM user u "
        "JOIN tenant t on t.id=u.tenant "
        "LEFT JOIN user_settings s on s.user=u.id "
        "WHERE u.id=? ", uid);

    enum Cols { TENANT, TENANT_KIND, USER_KIND, TENANT_STATE, USER_ACTIVE, SETTINGS, CREATED,
                PLAN, PLAN_UPDATED, PLAN_EXPIRES, PLAN_SEATS, GRACE_PERIOD_EXPIRES, ACCOUNT_EXPIRES,
                DATA_SYNC_EPOCH};
    if (res.rows().empty()) [[unlikely]] {
        LOG_ERROR << "Database inconsistency found. A device " << devid
                  << " is linked to a user " << uid << " that does not exist.";
        throw server_err{pb::Error::AUTH_FAILED, "User not found"};
    } else {
        const auto& row = res.rows().front();
        const auto tenant = row.at(TENANT).as_string();
        pb::Tenant::State tenant_state;
        if (!pb::Tenant::State_Parse(toUpper(row.at(TENANT_STATE).as_string()), &tenant_state)) {
            LOG_WARN_N << "Failed to parse Tenant::State from " << row.at(TENANT_STATE).as_string();
            throw runtime_error{"Failed to parse Tenant::State"};
        }
        if (tenant_state == pb::Tenant::State::Tenant_State_SUSPENDED) {
            throw server_err{pb::Error::TENANT_SUSPENDED, "Tenant account is suspended"};
        }
        bool active = row.at(USER_ACTIVE).as_int64() != 0;
        if (!active) {
            throw server_err{pb::Error::USER_SUSPENDED, "User account is inactive"};
        }
        pb::UserGlobalSettings tmp_settings;
        if (row.at(SETTINGS).is_blob()) {
            const auto blob = row.at(SETTINGS).as_blob();
            if (!tmp_settings.ParseFromArray(blob.data(), blob.size())) {
                LOG_WARN_N << "Failed to parse UserGlobalSettings for user " << uid;
                throw runtime_error{"Failed to parse UserGlobalSettings"};
            }
        }

        pb::User::Kind ukind;
        if (!pb::User::Kind_Parse(toUpper(row.at(USER_KIND).as_string()), &ukind)) {
            LOG_WARN_N << "Failed to parse User::Kind from " << row.at(USER_KIND).as_string();
            throw runtime_error{"Failed to parse User::Kind"};
        }

        if (tenant_state == pb::Tenant::State::Tenant_State_PENDING_ACTIVATION) {
            co_await db.exec("UPDATE tenant SET state='active' WHERE id=?", tenant);
            LOG_INFO << "Activated pending tenant " << tenant << " because user " << uid << " logged in.";
        }

        const auto created_at = row.at(CREATED).is_datetime()
            ? row.at(CREATED).as_datetime().as_time_point()
            : std::chrono::system_clock::now();

        shared_ptr<TenantPlan> tenant_plan;
        if (server_.config().payment.enable_plan && !row.at(PLAN).is_null()) {
            tenant_plan = make_shared<TenantPlan>();
            tenant_plan->plan = getPlan(row.at(PLAN).as_string());
            if (row.at(PLAN_UPDATED).is_datetime()) {
                tenant_plan->updated_at = row.at(PLAN_UPDATED).as_datetime().as_time_point();
            }
            if (row.at(PLAN_EXPIRES).is_datetime()) {
                tenant_plan->expires_at = row.at(PLAN_EXPIRES).as_datetime().as_time_point();
            }
            if (row.at(GRACE_PERIOD_EXPIRES).is_datetime()) {
                tenant_plan->grace_expires_at = row.at(GRACE_PERIOD_EXPIRES).as_datetime().as_time_point();
            }
            if (row.at(ACCOUNT_EXPIRES).is_datetime()) {
                tenant_plan->account_expires_at = row.at(ACCOUNT_EXPIRES).as_datetime().as_time_point();
            }
            if (row.at(PLAN_SEATS).is_int64()) {
                tenant_plan->max_users = row.at(PLAN_SEATS).as_int64();
            } else {
                LOG_DEBUG_N << "Tenant " << tenant << " has no user limit in the database. Assuming unlimited users.";
                tenant_plan->max_users = 0; // Unlimited
            }

            // TODO: Enforce account expiration and grace period here.
        }

        const auto publishState = co_await loadPublishState_(userUuid, uid);
        const auto dataSyncEpoch = row.at(DATA_SYNC_EPOCH).as_uint64();
        auto ucx = make_shared<UserContext>(tenant, uid, ukind, tmp_settings, tenant_plan,
                                            publishState.publish_id, publishState.publish_epoch, dataSyncEpoch,
                                            created_at);
        ucx->setTenantState(tenant_state);
        {
            unique_lock lock{mutex_};
            users_[userUuid] = ucx;
        }

        ucx->setDeviceMobile(deviceId, is_mobile);
        co_await ucx->initializePlanUsage(db);
        co_await ucx->reloadPushers();
        co_return ucx;
    }

    assert(false);
    throw runtime_error{"Failed to create UserContext"};
}

void UserContext::Session::touch() {
    //last_access_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
    last_access_ = chrono::steady_clock::now();
    LOG_TRACE_N << "Touching session " << sessionid_ << " for user " << user_->userUuid() << " on device " << deviceid_
                << " lifetime="
                << std::chrono::duration_cast<chrono::seconds>(last_access_.load() - created_) << "s";
}

std::chrono::steady_clock::time_point UserContext::Session::touched() const {
    //return last_access_.load(std::memory_order_relaxed);
    return last_access_;
}

boost::asio::awaitable<bool> UserContext::PushNotifications::sendNotification(const std::shared_ptr<nextapp::pb::Update>& message)
{
    assert(pusher_);
    if (!pusher_->isReady()) {
        LOG_WARN_N << "Push notification pusher is closed. Cannot send notification.";
        co_return false;
    }

    enum DataSlots{
        KIND, MESSAGE, _NUM_SLOTS
    };

    // Note that the data is just a view of the values. They are not copied.
    array<pair<string_view, string_view>, _NUM_SLOTS> data;

    if (message->has_calendarevents()) {
        data[KIND] = {"kind", "calendar-event"};
    }

    if (data[KIND].first.empty()) {
        LOG_TRACE_N << "Push notification has no data to send.";
        co_return false;
    }

    // Serialize the message to protobuf binary and base-64 encode it.
    string serialized;
    if (!message->SerializeToString(&serialized)) {
        LOG_WARN_N << "Failed to serialize message";
        co_return false;
    }
    const auto value = Base64Encode(serialized);

    // TODO encrypt so Google can't snoop on this
    data[MESSAGE] = {"message", value};

    jgaa::cpp_push::PushMessage pm;
    pm.to = token_;
    pm.data = data;
    pm.type = jgaa::cpp_push::PushMessage::PushType::DATA;

    auto res = co_await pusher_->push(pm);
    if (!res) {
        LOG_WARN_N << "Failed to send push notification: " << res.message();
        co_return false;
    }

    co_return true;
}


} // ns
