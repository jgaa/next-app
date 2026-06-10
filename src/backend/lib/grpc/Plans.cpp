#include "nextapp/Plans.h"

#include <format>
#include <random>
#include <stdexcept>

#include <grpcpp/security/credentials.h>

#include "nextapp/logging.h"
#include "nextapp/Server.h"
#include "nextapp/GrpcServer.h"
#include "nextapp/error_mapping.h"
#include "grpc/shared_grpc_server.h"
#include "mysqlpool/mysqlpool.h"

using namespace std;
namespace asio = boost::asio;

namespace nextapp {

namespace {

constexpr string_view kRegistrationStateLocalOnly = "local_only";
constexpr string_view kRegistrationStatePending = "pending_reg";
constexpr string_view kRegistrationStateRegistered = "registered";
constexpr auto kRegistrationSweepInterval = std::chrono::hours{1};
constexpr auto kPerTenantSweepDelayMin = std::chrono::milliseconds{50};
constexpr auto kPerTenantSweepDelayMax = std::chrono::milliseconds{500};
constexpr uint32_t kRegistrationRetryMinSeconds = 5 * 60;
constexpr uint32_t kRegistrationRetryMaxSeconds = 65 * 60;

uint64_t getUint64(const boost::mysql::field_view& field)
{
    if (field.is_uint64()) {
        return field.as_uint64();
    }
    if (field.is_int64()) {
        const auto value = field.as_int64();
        if (value < 0) {
            throw runtime_error{"Expected non-negative integer from database"};
        }
        return static_cast<uint64_t>(value);
    }
    throw runtime_error{"Expected integer value from database"};
}

template <typename IntT>
IntT randomBetween(IntT min_value, IntT max_value)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<IntT> dist(min_value, max_value);
    return dist(rng);
}

} // namespace

Plans::Plans(Server& server)
    : server_{server}
{
}

Plans::~Plans() = default;

const PaymentOptions& Plans::config() const noexcept
{
    return server_.config().payment;
}

int Plans::logProtobufMode() const noexcept
{
    return server_.config().options.log_protobuf_messages;
}

boost::asio::awaitable<void> Plans::downgradeTenantToFreePlan(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const boost::uuids::uuid& tenant_id,
    std::string_view reason)
{
    auto free_plan_res = co_await dbh.exec(
        "SELECT name FROM plan WHERE name='free' AND active=TRUE LIMIT 1");
    const auto has_free_plan = !free_plan_res.rows().empty();

    auto tenant_res = co_await dbh.exec(R"(
        SELECT
            plan,
            plan_updated,
            plan_expires,
            plan_seats,
            grace_period_expires,
            account_expires,
            state
        FROM tenant
        WHERE id = ?
    )", tenant_id);

    if (tenant_res.rows().empty()) {
        LOG_WARN_N << "Cannot downgrade tenant " << tenant_id
                   << " because it does not exist. reason=" << reason;
        co_return;
    }

    enum Cols {
        PLAN,
        PLAN_UPDATED,
        PLAN_EXPIRES,
        PLAN_SEATS,
        GRACE_PERIOD_EXPIRES,
        ACCOUNT_EXPIRES,
        STATE
    };

    const auto& row = tenant_res.rows().front();
    const auto current_plan = row.at(PLAN).is_null() ? string{} : string{row.at(PLAN).as_string()};
    const auto current_plan_expires = row.at(PLAN_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(PLAN_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_plan_seats = row.at(PLAN_SEATS).as_int64();
    const auto current_grace_expires = row.at(GRACE_PERIOD_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(GRACE_PERIOD_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_account_expires = row.at(ACCOUNT_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(ACCOUNT_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_state = row.at(STATE).as_string();

    const auto target_plan = has_free_plan ? string{"free"} : string{};
    const auto target_state = has_free_plan ? string{"active"} : string{"suspended"};
    constexpr int64_t target_plan_seats = 1;

    if (current_plan == target_plan
        && current_plan_expires == 0
        && current_plan_seats == target_plan_seats
        && current_grace_expires == 0
        && current_account_expires == 0
        && current_state == target_state) {
        co_return;
    }

    co_await dbh.exec(R"(
        UPDATE tenant
        SET
            plan = NULLIF(?, ''),
            plan_updated = UTC_TIMESTAMP(),
            plan_expires = NULL,
            plan_seats = ?,
            grace_period_expires = NULL,
            account_expires = NULL,
            state = ?
        WHERE id = ?
    )",
        target_plan,
        target_plan_seats,
        target_state,
        tenant_id);

    if (has_free_plan) {
        LOG_INFO << "Downgraded tenant " << tenant_id
                 << " to free plan. reason=" << reason
                 << " previous_plan=" << (current_plan.empty() ? "<null>" : current_plan)
                 << " previous_state=" << current_state;
    } else {
        LOG_INFO << "Suspended tenant " << tenant_id
                 << " because no active free plan exists. reason=" << reason
                 << " previous_plan=" << (current_plan.empty() ? "<null>" : current_plan)
                 << " previous_state=" << current_state;
    }
}

bool Plans::tryBeginTenantRegistration(const boost::uuids::uuid& tenant_id)
{
    auto guard = std::scoped_lock{tenant_registration_mutex_};
    return tenant_registrations_in_flight_.insert(to_string(tenant_id)).second;
}

void Plans::endTenantRegistration(const boost::uuids::uuid& tenant_id) noexcept
{
    auto guard = std::scoped_lock{tenant_registration_mutex_};
    tenant_registrations_in_flight_.erase(to_string(tenant_id));
}

boost::asio::awaitable<std::string> Plans::getTenantRegistrationState(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const boost::uuids::uuid& tenant_id)
{
    auto res = co_await dbh.exec(
        "SELECT registration_state FROM tenant WHERE id = ?",
        tenant_id);
    if (res.rows().empty()) {
        co_return string{};
    }
    co_return string{res.rows().front().front().as_string()};
}

boost::asio::awaitable<bool> Plans::updatePlanFromEntitlement(
//    const payments::v1::EntitlementChangeEvent &event,
    jgaa::mysqlpool::Mysqlpool::Handle &dbh,
    const boost::uuids::uuid& tenant_id,
    const payments::v1::Entitlement& entitlement)
{
    LOG_DEBUG_N << "Processing entitlement change for tenant "
                << tenant_id << " version " << entitlement.version()
                << " Plan  " << entitlement.plan_id()
                << " product " << entitlement.product_id()
                << " " << toJsonForLog(entitlement);

    // Check version
    {
        auto res = co_await dbh.exec(
            "SELECT version FROM entitlement WHERE tenant_id = ? AND product_id = ?",
            tenant_id, entitlement.product_id());
        if (!res.rows().empty()) {
            const auto current_version = getUint64(res.rows().front().at(0));
            if (entitlement.version() < current_version) {
                LOG_WARN_N << "Ignoring stale entitlement change"
                << " for tenant " << tenant_id
                << " product " << entitlement.product_id()
                << ": current version=" << current_version
                << ", incoming version=" << entitlement.version();
                co_return false;
            }
            if (entitlement.version() == current_version) {
                LOG_WARN_N << "Overwriting entitlement with identical version "
                << " tenant " << tenant_id
                << " product " << entitlement.product_id()
                << " version=" << entitlement.version();
            }
        }
    }

    // Update current entitlement
    const auto valid_until_seconds = entitlement.has_valid_until()
                                         ? entitlement.valid_until().seconds()
                                         : int64_t{};
    const auto updated_at_seconds = entitlement.has_updated_at()
                                        ? entitlement.updated_at().seconds()
                                        : int64_t{};
    const auto plan_id = entitlement.plan_id();
    const auto seats = entitlement.has_seats() ? entitlement.seats() : -1;
    const auto source_ref = entitlement.source_ref();

    if (!plan_id.empty()) {
        auto plan_res = co_await dbh.exec("SELECT 1 FROM plan WHERE name = ? LIMIT 1", plan_id);
        if (plan_res.rows().empty()) {
            LOG_WARN_N << "Entitlement for tenant " << tenant_id
                       << " references unknown plan '" << plan_id
                       << "'. Syncing plans before applying entitlement.";
            co_await syncPlans();
            co_await loadActivePlans();
            plan_res = co_await dbh.exec("SELECT 1 FROM plan WHERE name = ? LIMIT 1", plan_id);
            if (plan_res.rows().empty()) {
                throw runtime_error{format("Entitlement references unknown plan '{}'", plan_id)};
            }
        }
    }

    co_await dbh.exec(R"(
        INSERT INTO entitlement (
            tenant_id, product_id, plan_id, seats, state, valid_until, source, source_ref, version, updated_at
        ) VALUES (
            ?, ?, NULLIF(?, ''), NULLIF(?, -1), ?, FROM_UNIXTIME(NULLIF(?, 0)), ?, NULLIF(?, ''), ?, FROM_UNIXTIME(NULLIF(?, 0))
        )
        ON DUPLICATE KEY UPDATE
            plan_id = VALUES(plan_id),
            seats = VALUES(seats),
            state = VALUES(state),
            valid_until = VALUES(valid_until),
            source = VALUES(source),
            source_ref = VALUES(source_ref),
            version = VALUES(version),
            updated_at = VALUES(updated_at)
        )",
        tenant_id,
        entitlement.product_id(),
        plan_id,
        seats,
        static_cast<uint32_t>(entitlement.state()),
        valid_until_seconds,
        static_cast<uint32_t>(entitlement.source()),
        source_ref,
        entitlement.version(),
        updated_at_seconds);

    auto tenant_res = co_await dbh.exec(R"(
        SELECT
            plan,
            plan_updated,
            plan_expires,
            plan_seats,
            grace_period_expires,
            account_expires,
            state
        FROM tenant
        WHERE id = ?
    )", tenant_id);

    if (tenant_res.rows().empty()) {
        LOG_WARN_N << "Tenant " << tenant_id
                   << " disappeared while applying entitlement version "
                   << entitlement.version() << " for product " << entitlement.product_id();
        co_return true;
    }

    enum TenantCols {
        PLAN,
        PLAN_UPDATED,
        PLAN_EXPIRES,
        PLAN_SEATS,
        GRACE_PERIOD_EXPIRES,
        ACCOUNT_EXPIRES,
        STATE
    };

    const auto& row = tenant_res.rows().front();
    const auto current_plan = row.at(PLAN).is_null() ? string{} : string{row.at(PLAN).as_string()};
    const auto current_plan_expires = row.at(PLAN_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(PLAN_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_plan_seats = row.at(PLAN_SEATS).as_int64();
    const auto current_grace_expires = row.at(GRACE_PERIOD_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(GRACE_PERIOD_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_account_expires = row.at(ACCOUNT_EXPIRES).is_datetime()
        ? static_cast<uint64_t>(grpc::toTimeT(row.at(ACCOUNT_EXPIRES).as_datetime()))
        : uint64_t{};
    const auto current_state = row.at(STATE).as_string();

    const auto now = static_cast<int64_t>(time(nullptr));
    const auto target_plan_updated = entitlement.has_updated_at() ? updated_at_seconds : now;

    if (entitlement.state() == payments::v1::ENTITLEMENT_STATE_EXPIRED && config().grace_period_days > 0) {
        const auto target_plan = entitlement.plan_id();
        const auto target_plan_expires = valid_until_seconds;
        const auto target_grace_expires = now + static_cast<int64_t>(config().grace_period_days) * 24 * 60 * 60;
        constexpr int64_t target_plan_seats = 1;
        constexpr uint64_t target_account_expires = 0;
        const auto target_state = string{"active"};

        if (current_plan == target_plan
            && current_plan_expires == static_cast<uint64_t>(target_plan_expires)
            && current_plan_seats == target_plan_seats
            && current_grace_expires == static_cast<uint64_t>(target_grace_expires)
            && current_account_expires == target_account_expires
            && current_state == target_state) {
            co_return true;
        }

        co_await dbh.exec(R"(
            UPDATE tenant
            SET
                plan = NULLIF(?, ''),
                plan_updated = FROM_UNIXTIME(?),
                plan_expires = FROM_UNIXTIME(NULLIF(?, 0)),
                plan_seats = ?,
                grace_period_expires = FROM_UNIXTIME(?),
                account_expires = NULL,
                state = ?
            WHERE id = ?
        )",
            target_plan,
            target_plan_updated,
            target_plan_expires,
            target_plan_seats,
            target_grace_expires,
            target_state,
            tenant_id);

        LOG_INFO << "Updated tenant " << tenant_id
                 << " to grace period after expired entitlement."
                 << " product=" << entitlement.product_id()
                 << " version=" << entitlement.version()
                 << " plan=" << (target_plan.empty() ? "<null>" : target_plan)
                 << " plan_expires_unix=" << target_plan_expires
                 << " grace_expires_unix=" << target_grace_expires
                 << " reason=expired_entitlement";
        co_return true;
    }

    if (entitlement.state() != payments::v1::ENTITLEMENT_STATE_ACTIVE) {
        co_await downgradeTenantToFreePlan(
            dbh,
            tenant_id,
            format("entitlement_state={} product={} version={}",
                   static_cast<uint32_t>(entitlement.state()),
                   entitlement.product_id(),
                   entitlement.version()));
        co_return true;
    }

    const auto target_plan = entitlement.plan_id();
    const auto target_plan_expires = valid_until_seconds;
    constexpr int64_t target_plan_seats = 1;
    constexpr uint64_t target_grace_expires = 0;
    constexpr uint64_t target_account_expires = 0;
    const auto target_state = string{"active"};

    if (current_plan == target_plan
        && current_plan_expires == static_cast<uint64_t>(target_plan_expires)
        && current_plan_seats == target_plan_seats
        && current_grace_expires == target_grace_expires
        && current_account_expires == target_account_expires
        && current_state == target_state) {
        co_return true;
    }

    co_await dbh.exec(R"(
        UPDATE tenant
        SET
            plan = NULLIF(?, ''),
            plan_updated = FROM_UNIXTIME(?),
            plan_expires = FROM_UNIXTIME(NULLIF(?, 0)),
            plan_seats = ?,
            grace_period_expires = NULL,
            account_expires = NULL,
            state = ?
        WHERE id = ?
    )",
        target_plan,
        target_plan_updated,
        target_plan_expires,
        target_plan_seats,
        target_state,
        tenant_id);

    LOG_INFO << "Updated tenant " << tenant_id
             << " from active entitlement."
             << " product=" << entitlement.product_id()
             << " version=" << entitlement.version()
             << " plan=" << (target_plan.empty() ? "<null>" : target_plan)
             << " seats=" << target_plan_seats
             << " plan_expires_unix=" << target_plan_expires;

    co_return true;
}

boost::asio::awaitable<bool> Plans::refreshTenantEntitlement(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const boost::uuids::uuid& tenant_id)
{
    const auto registration_state = co_await getTenantRegistrationState(dbh, tenant_id);
    if (registration_state.empty()) {
        co_return false;
    }
    if (registration_state == kRegistrationStateLocalOnly) {
        LOG_DEBUG_N << "Skipping entitlement refresh for local-only tenant " << tenant_id;
        co_return false;
    }

    payments::v1::GetEntitlementRequest req;
    req.set_tenant_id(to_string(tenant_id));
    req.set_product_id(config().product_id);

    const auto response = co_await getEntitlement(std::move(req));
    const auto& entitlement = response.entitlement();

    if (entitlement.product_id().empty()) {
        LOG_WARN_N << "Ignoring pulled entitlement for tenant " << tenant_id
                   << " because product_id is empty.";
        co_return false;
    }

    if (!entitlement.tenant_id().empty()) {
        const auto response_tenant = toUuid(entitlement.tenant_id());
        if (response_tenant != tenant_id) {
            LOG_WARN_N << "Pulled entitlement tenant mismatch for tenant " << tenant_id
                       << ": response tenant_id=" << entitlement.tenant_id()
                       << ". Ignoring response.";
            co_return false;
        }
    }

    co_return co_await updatePlanFromEntitlement(dbh, tenant_id, entitlement);
}

boost::asio::awaitable<bool> Plans::ensureTenantRegistered(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const boost::uuids::uuid& tenant_id,
    bool publish_changes)
{
    if (!tryBeginTenantRegistration(tenant_id)) {
        LOG_DEBUG_N << "Tenant registration already in flight for tenant " << tenant_id;
        co_return false;
    }
    ScopedExit clear_inflight{[this, tenant_id] {
        endTenantRegistration(tenant_id);
    }};

    const auto state = co_await getTenantRegistrationState(dbh, tenant_id);
    if (state.empty()) {
        LOG_DEBUG_N << "Skipping registration for tenant " << tenant_id << " because it no longer exists.";
        co_return false;
    }
    if (state == kRegistrationStateLocalOnly || state == kRegistrationStateRegistered) {
        co_return true;
    }

    co_await dbh.exec(R"(
        UPDATE tenant
        SET
            registration_attempts = registration_attempts + 1,
            last_registration_attempt = UTC_TIMESTAMP()
        WHERE id = ?
    )", tenant_id);

    std::optional<payments::v1::EnsureTenantInitializedResponse> response;
    std::string error_message;
    try {
        payments::v1::EnsureTenantInitializedRequest request;
        request.set_tenant_id(to_string(tenant_id));
        request.set_product_id(config().product_id);
        response = co_await ensureTenantInitialized(std::move(request));
    } catch (const std::exception& ex) {
        error_message = ex.what();
    }

    if (!response) {
        const auto retry_after = randomBetween<uint32_t>(
            kRegistrationRetryMinSeconds, kRegistrationRetryMaxSeconds);
        co_await dbh.exec(R"(
            UPDATE tenant
            SET next_registration_retry = UTC_TIMESTAMP() + INTERVAL ? SECOND
            WHERE id = ? AND registration_state = ?
        )", retry_after, tenant_id, kRegistrationStatePending);

        LOG_WARN_N << "Failed to register tenant " << tenant_id
                   << " with the payment service: " << error_message
                   << ". Will retry in " << retry_after << " seconds.";
        co_return false;
    }

    auto trx = co_await dbh.transaction();
    if (const auto& entitlement = response->entitlement(); !entitlement.product_id().empty()) {
        (void)co_await updatePlanFromEntitlement(dbh, tenant_id, entitlement);
    } else {
        LOG_WARN_N << "EnsureTenantInitialized for tenant " << tenant_id
                   << " returned no entitlement payload. Keeping the local fallback plan.";
    }

    co_await dbh.exec(R"(
        UPDATE tenant
        SET
            registration_state = ?,
            next_registration_retry = NULL
        WHERE id = ?
    )", kRegistrationStateRegistered, tenant_id);
    co_await trx.commit();

    LOG_INFO << "Tenant " << tenant_id
             << " is now registered with the payment service."
             << " initialized=" << (response->initialized() ? "true" : "false");

    if (publish_changes && server_.hasGrpcService()) {
        co_await server_.grpc().sessionManager().refreshTenantPlansAndPublish(dbh, to_string(tenant_id));
    }
    co_return true;
}

boost::asio::awaitable<void> Plans::refreshTenantSubscription(
    jgaa::mysqlpool::Mysqlpool::Handle& dbh,
    const boost::uuids::uuid& tenant_id)
{
    const auto registration_state = co_await getTenantRegistrationState(dbh, tenant_id);
    if (registration_state.empty() || registration_state == kRegistrationStateLocalOnly) {
        co_return;
    }

    if (registration_state == kRegistrationStatePending) {
        (void)co_await ensureTenantRegistered(dbh, tenant_id, true);
        co_return;
    }

    auto trx = co_await dbh.transaction();
    (void)co_await refreshTenantEntitlement(dbh, tenant_id);
    co_await trx.commit();
    if (server_.hasGrpcService()) {
        co_await server_.grpc().sessionManager().refreshTenantPlansAndPublish(dbh, to_string(tenant_id));
    }
}

string Plans::grpcServerAddress() const
{
    auto server_address = config().service_url;
    if (auto pos = server_address.find("://"); pos != string::npos) {
        server_address = server_address.substr(pos + 3);
    }
    return server_address;
}

shared_ptr<::grpc::ChannelCredentials> Plans::createCredentials() const
{
    if (config().service_url.starts_with("https://")) {
        ::grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = readFileToBuffer(config().tls_ca);
        ssl_opts.pem_cert_chain = readFileToBuffer(config().tls_cert);
        ssl_opts.pem_private_key = readFileToBuffer(config().tls_key);
        return ::grpc::SslCredentials(ssl_opts);
    }

    LOG_WARN_N << "Not using TLS on payment service gRPC connection. Do not use this over a public network.";
    return ::grpc::InsecureChannelCredentials();
}

asio::awaitable<void> Plans::connect()
{
    LOG_INFO << "Connecting to payment service at " << config().service_url;
    stopping_.store(false);

    server().metrics().setPaymentNotificationsConnected(false);

    ::grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, server_.config().grpc.keepalive_time_sec * 1000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, server_.config().grpc.keepalive_timeout_sec * 1000);

    channel_ = ::grpc::CreateCustomChannel(grpcServerAddress(), createCredentials(), args);
    stub_ = payments::v1::PaymentsService::NewStub(channel_);
    notifications_stub_ = payments::v1::EntitlementNotificationsService::NewStub(channel_);

    if (!stub_ || !notifications_stub_) {
        throw runtime_error{"Failed to create payment service gRPC client"};
    }

    co_return;
}

asio::awaitable<void> Plans::onServerReady()
{
    co_await reconcileLocalOnlyTenants();
    startEntitlementSubscription();
    co_await processPendingTenantRegistrations("startup");

    auto expected = false;
    if (tenant_registration_loop_running_.compare_exchange_strong(expected, true)) {
        asio::co_spawn(server_.ctx(), [this]() -> asio::awaitable<void> {
            co_await runTenantRegistrationLoop();
        }, asio::detached);
    }
}

asio::awaitable<void> Plans::reconcileLocalOnlyTenants()
{
    auto db = co_await server_.db().getConnection();
    auto res = co_await db.exec(R"(
        UPDATE tenant
        SET
            registration_state = ?,
            plan = 'pro',
            plan_updated = UTC_TIMESTAMP(),
            plan_expires = NULL,
            plan_seats = 1,
            grace_period_expires = NULL,
            account_expires = NULL,
            next_registration_retry = NULL
        WHERE system_tenant = 1
          AND (
              registration_state <> ?
              OR registration_state IS NULL
              OR
              plan <> 'pro'
              OR plan IS NULL
              OR plan_expires IS NOT NULL
              OR grace_period_expires IS NOT NULL
              OR account_expires IS NOT NULL
          )
    )", kRegistrationStateLocalOnly, kRegistrationStateLocalOnly);

    if (res.affected_rows() > 0) {
        LOG_INFO << "Reconciled " << res.affected_rows()
                 << " local-only system tenant(s) back to the local pro plan.";
    }
}

void Plans::startEntitlementSubscription()
{
    auto expected = false;
    if (entitlement_subscription_running_.compare_exchange_strong(expected, true)) {
        asio::co_spawn(server_.ctx(), [this]() -> asio::awaitable<void> {
            co_await runEntitlementSubscriptionLoop();
        }, asio::detached);
    }
}

void Plans::shutdown()
{
    stopping_.store(true);
    {
        std::scoped_lock lock{entitlement_stream_mutex_};
        if (entitlement_stream_) {
            entitlement_stream_->cancel();
        }
    }
    notifications_stub_.reset();
    stub_.reset();
    channel_.reset();
}

asio::awaitable<void> Plans::queueTenantRegistration(const boost::uuids::uuid& tenant_id)
{
    auto db = co_await server_.db().getConnection();
    co_await db.exec(R"(
        UPDATE tenant
        SET
            registration_state = CASE
                WHEN registration_state = ? THEN registration_state
                ELSE ?
            END,
            next_registration_retry = UTC_TIMESTAMP()
        WHERE id = ?
    )", kRegistrationStateLocalOnly, kRegistrationStatePending, tenant_id);

    (void)co_await ensureTenantRegistered(db, tenant_id, true);
}

asio::awaitable<void> Plans::runTenantRegistrationLoop()
{
    ScopedExit clear_running{[this] {
        tenant_registration_loop_running_.store(false);
    }};

    while (!stopping_.load() && !server_.is_done()) {
        asio::steady_timer timer{server_.ctx()};
        timer.expires_after(kRegistrationSweepInterval);
        try {
            co_await timer.async_wait(asio::use_awaitable);
        } catch (const boost::system::system_error& e) {
            if (e.code() != asio::error::operation_aborted) {
                throw;
            }
        }

        if (stopping_.load() || server_.is_done()) {
            break;
        }

        try {
            co_await processPendingTenantRegistrations("timer");
        } catch (const std::exception& ex) {
            LOG_WARN_N << "Caught exception during tenant registration sweep: " << ex.what();
        }
    }
}

asio::awaitable<void> Plans::processPendingTenantRegistrations(std::string_view reason)
{
    auto db = co_await server_.db().getConnection();
    auto res = co_await db.exec(R"(
        SELECT id
        FROM tenant
        WHERE registration_state = ?
          AND (next_registration_retry IS NULL OR next_registration_retry <= UTC_TIMESTAMP())
        ORDER BY COALESCE(next_registration_retry, '1970-01-01 00:00:00'), id
    )", kRegistrationStatePending);

    if (res.rows().empty()) {
        LOG_DEBUG_N << "No tenant registrations are pending for reason=" << reason;
        co_return;
    }

    LOG_INFO << "Processing " << res.rows().size()
             << " pending tenant registrations. reason=" << reason;

    bool first = true;
    for (const auto& row : res.rows()) {
        if (!first) {
            asio::steady_timer timer{server_.ctx()};
            timer.expires_after(std::chrono::milliseconds{
                randomBetween<int>(
                    static_cast<int>(kPerTenantSweepDelayMin.count()),
                    static_cast<int>(kPerTenantSweepDelayMax.count()))
            });
            try {
                co_await timer.async_wait(asio::use_awaitable);
            } catch (const boost::system::system_error& e) {
                if (e.code() != asio::error::operation_aborted) {
                    throw;
                }
            }
        }
        first = false;

        const auto tenant_id = toUuid(row.at(0).as_string());
        (void)co_await ensureTenantRegistered(db, tenant_id, true);
    }
}

asio::awaitable<void> Plans::runEntitlementSubscriptionLoop()
{
    ScopedExit clear_running{[this] {
        entitlement_subscription_running_.store(false);
    }};

    auto backoff = std::chrono::seconds{1};

    while (!stopping_.load() && !server_.is_done()) {
        if (!notifications_stub_) {
            LOG_WARN_N << "Payment entitlement subscription stopped because the notification stub is not available.";
            break;
        }

        payments::v1::SubscribeEntitlementChangesRequest req;
        req.set_backend_instance_id(server_.serverId());

        auto stream = std::make_shared<EntitlementStream>(
            server_.ctx(),
            std::move(req),
            [this](::grpc::ClientContext& ctx,
                   const payments::v1::SubscribeEntitlementChangesRequest* request,
                   ::grpc::ClientReadReactor<payments::v1::EntitlementChangeEvent>* reactor) {
                notifications_stub_->async()->SubscribeEntitlementChanges(&ctx, request, reactor);
            },
            [this] (bool ok) {
                server().metrics().setPaymentNotificationsConnected(ok);
            });

        {
            std::scoped_lock lock{entitlement_stream_mutex_};
            entitlement_stream_ = stream;
        }

        LOG_INFO << "Subscribing to payment entitlement changes as backend instance " << server_.serverId();
        stream->start();

        try {
            for (;;) {
                auto event = co_await stream->read();
                if (!event) {
                    break;
                }

                try {
                    co_await applyEntitlementChange(*event);
                } catch (const std::exception& ex) {
                    LOG_WARN_N << "Caught exception while applying entitlement change event "
                               << " for tenant " << event->subject_id()
                               << " product " << event->entitlement().product_id()
                               << ": " << ex.what();
                }
            }
        } catch (const std::exception& ex) {
            LOG_WARN_N << "Caught exception while consuming entitlement change stream: " << ex.what();
        }

        {
            std::scoped_lock lock{entitlement_stream_mutex_};
            if (entitlement_stream_ == stream) {
                entitlement_stream_.reset();
            }
        }

        const auto status = co_await stream->waitForDone();
        if (status.ok()) {
            LOG_INFO << "Payment entitlement change stream ended normally.";
        } else {
            LOG_WARN_N << "Payment entitlement change stream ended with status code "
                       << static_cast<int>(status.error_code())
                       << ": " << status.error_message();
        }

        if (stopping_.load() || server_.is_done()) {
            break;
        }

        asio::steady_timer timer{server_.ctx()};
        timer.expires_after(backoff);
        try {
            co_await timer.async_wait(asio::use_awaitable);
        } catch (const boost::system::system_error& e) {
            if (e.code() != asio::error::operation_aborted) {
                throw;
            }
        }
        backoff = std::min(backoff * 2, std::chrono::seconds{30});
    }
}

asio::awaitable<void> Plans::applyEntitlementChange(const payments::v1::EntitlementChangeEvent& event)
{
    LOG_DEBUG_N << "Received entitlement change event: " << toJsonForLog(event);

    if (event.event_id().empty()) {
        LOG_WARN_N << "Ignoring entitlement change event without event_id.";
        co_return;
    }

    if (event.subject_id().empty()) {
        LOG_WARN_N << "Ignoring entitlement change event "
                   << event.event_id() << " without subject_id.";
        co_return;
    }

    const auto event_id = toUuid(event.event_id());
    const auto subject_id = toUuid(event.subject_id());
    const auto& entitlement = event.entitlement();
    auto tenant_id = subject_id;
    if (!entitlement.tenant_id().empty()) {
        const auto entitlement_tenant_id = toUuid(entitlement.tenant_id());
        if (entitlement_tenant_id != subject_id) {
            LOG_WARN_N << "Entitlement change event " << event.event_id()
                       << " has mismatching subject_id=" << event.subject_id()
                       << " and entitlement.tenant_id=" << entitlement.tenant_id()
                       << ". Using subject_id.";
        } else {
            tenant_id = entitlement_tenant_id;
        }
    }

    if (entitlement.product_id().empty()) {
        LOG_WARN_N << "Ignoring entitlement change event " << event.event_id()
                   << " because entitlement.product_id is empty.";
        co_return;
    }

    auto db = co_await server_.db().getConnection();
    auto trx = co_await db.transaction();

    // Todo: Add filtering on the nextapp instance-id level when the payment server starts sending it.
    // Check if tenant exists. If not, the entitlement change event is not relevant for this instance.
    {
        auto res = co_await db.exec(
            "SELECT registration_state FROM tenant WHERE id = ?",
            tenant_id);
        if (res.rows().empty()) {
            LOG_DEBUG_N << "Ignoring entitlement change event " << event.event_id()
                       << " for non-existent tenant " << tenant_id;
            co_return;
        }
        if (res.rows().front().front().as_string() == kRegistrationStateLocalOnly) {
            LOG_DEBUG_N << "Ignoring entitlement change event " << event.event_id()
                        << " for local-only tenant " << tenant_id;
            co_return;
        }
    }

    try {
        co_await db.exec(
            "INSERT INTO entitlement_event (event_id, subject_id) VALUES (?, ?)",
            event_id, subject_id);
    } catch (const jgaa::mysqlpool::db_err_exists&) {
        LOG_DEBUG_N << "Skipping duplicate entitlement change event " << event.event_id();
        co_return;
    }

    if (!co_await updatePlanFromEntitlement(db, tenant_id, entitlement)) {
        co_return;
    }

    co_await db.exec(R"(
        UPDATE tenant
        SET
            registration_state = CASE
                WHEN registration_state = ? THEN registration_state
                ELSE ?
            END,
            next_registration_retry = NULL
        WHERE id = ?
    )", kRegistrationStateLocalOnly, kRegistrationStateRegistered, tenant_id);

    co_await trx.commit();
    co_await server_.grpc().sessionManager().refreshTenantPlansAndPublish(db, to_string(tenant_id));
    co_return;
}

asio::awaitable<void> Plans::loadActivePlans()
{
    bool available{false};
    if (!is_loading_plans_.compare_exchange_strong(available, true)) {
        LOG_WARN_N << "Already loading plans. Skipping.";
        // If it fails, the server will normally retry in the plan sync schedule,
        // so we can just skip this load and wait for the next one.
        co_return;
    }
    ScopedExit clear_syncing{[this] {
        is_loading_plans_.store(false);
    }};

    auto snapshot = make_shared<ActivePlans>();
    auto db = co_await server_.db().getConnection();

    {
        auto res = co_await db.exec(R"(SELECT name, max_users, max_devices, max_nodes, nodes_monthly_growth,
            max_actions, actions_monthly_growth, max_worksessions, work_sessions_monthly_growth,
            max_time_blocks, time_blocks_monthly_growth, mobile_only
            FROM plan WHERE active=TRUE ORDER BY name)");
        enum Cols {
            NAME,
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

        for (const auto& row : res.rows()) {
            PlanProperties plan;
            plan.max_users = row.at(MAX_USERS).as_int64();
            plan.max_devices = row.at(MAX_DEVICES).as_int64();
            plan.max_nodes = row.at(MAX_NODES).as_int64();
            plan.nodes_monthly_growth = row.at(NODES_MONTHLY_GROWTH).as_int64();
            plan.max_actions = row.at(MAX_ACTIONS).as_int64();
            plan.actions_monthly_growth = row.at(ACTIONS_MONTHLY_GROWTH).as_int64();
            plan.max_worksessions = row.at(MAX_WORKSESSIONS).as_int64();
            plan.work_sessions_monthly_growth = row.at(WORK_SESSIONS_MONTHLY_GROWTH).as_int64();
            plan.max_time_blocks = row.at(MAX_TIME_BLOCKS).as_int64();
            plan.time_blocks_monthly_growth = row.at(TIME_BLOCKS_MONTHLY_GROWTH).as_int64();
            plan.mobile_only = row.at(MOBILE_ONLY).as_int64() != 0;
            snapshot->plans.emplace(row.at(NAME).as_string(), std::move(plan));
        }
    }

    {
        auto res = co_await db.exec(
            "SELECT name, value FROM config WHERE name IN ('trial_days', 'default_for_signup', 'default_for_free')");
        enum Cols { NAME, VALUE };

        for (const auto& row : res.rows()) {
            const auto name = row.at(NAME).as_string();
            const auto value = row.at(VALUE).as_string();
            if (name == "trial_days") {
                size_t pos = 0;
                auto parsed = stoul(value, &pos);
                if (pos != value.size()) {
                    throw runtime_error{format("Invalid trial_days value '{}' in config table",
                                               string_view{value.data(), value.size()})};
                }
                snapshot->trial_days = static_cast<uint32_t>(parsed);
                continue;
            }
            if (name == "default_for_signup") {
                snapshot->default_for_signup = value;
            } else if (name == "default_for_free") {
                snapshot->default_for_free = value;
            }
        }
    }

    active_plans_.store(std::move(snapshot));
    LOG_INFO << "Loaded active payment plan snapshot with " << active_plans_.load()->plans.size() << " plans.";
}

namespace {

struct DbPlan {
    string name;
    bool active = true;
    int64_t max_users = 1;
    int64_t max_devices = 5;
    int64_t max_nodes = 2000;
    int64_t nodes_monthly_growth = 0;
    int64_t max_actions = 1240;
    int64_t actions_monthly_growth = 0;
    int64_t max_worksessions = 1240;
    int64_t work_sessions_monthly_growth = 0;
    int64_t max_time_blocks = 1240;
    int64_t time_blocks_monthly_growth = 0;
    bool mobile_only = false;

    bool operator==(const DbPlan&) const = default;
};

int64_t getIntValue(const payments::v1::Plan& plan, string_view key,
                    std::optional<int64_t> defaultValue = {})
{
    if (auto it = plan.values().find(string{key}); it != plan.values().end()) {
        size_t pos = 0;
        try {
            auto value = stoll(it->second, &pos);
            if (pos != it->second.size()) {
                throw runtime_error{format("Invalid integer value '{}' for payment plan '{}' field '{}'",
                                          it->second, plan.plan_id(), key)};
            }
            return value;
        } catch (const std::exception& ex) {
            LOG_WARN_N << "Failed to parse integer value for payment plan field. plan_id=" << plan.plan_id()
                       << " field=" << key
                       << " value='" << it->second << "'"
                       << " error=" << ex.what();
        }
    }

    if (defaultValue) {
        return *defaultValue;
    }

    throw runtime_error{format("Missing required payment plan '{}' field '{}'", plan.plan_id(), key)};
}

bool getBoolValue(const payments::v1::Plan& plan, string_view key,
                  std::optional<bool> defaultValue = {})
{
    if (auto it = plan.values().find(string{key}); it != plan.values().end()) {
        const auto value = toLower(it->second);
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "0" || value == "false" || value == "no" || value == "off") {
            return false;
        }
        throw runtime_error{format("Invalid boolean value '{}' for payment plan '{}' field '{}'",
                                  it->second, plan.plan_id(), key)};
    }

    if (defaultValue) {
        return *defaultValue;
    }

    throw runtime_error{format("Missing required payment plan '{}' field '{}'", plan.plan_id(), key)};
}

DbPlan toDbPlan(const payments::v1::Plan& plan)
{
    if (plan.plan_id().empty()) {
        throw runtime_error{"Payment service returned a plan without plan_id"};
    }

    DbPlan out;
    out.name = plan.plan_id();
    out.active = getBoolValue(plan, "active", true);
    out.max_users = getIntValue(plan, "max_users", 1);
    out.max_devices = getIntValue(plan, "max_devices", 0);
    out.max_nodes = getIntValue(plan, "max_nodes", 0);
    out.nodes_monthly_growth = getIntValue(plan, "nodes_monthly_growth", 0);
    out.max_actions = getIntValue(plan, "max_actions", 0);
    out.actions_monthly_growth = getIntValue(plan, "actions_monthly_growth", 0);
    out.max_worksessions = getIntValue(plan, "max_worksessions");
    out.work_sessions_monthly_growth = getIntValue(plan, "work_sessions_monthly_growth", 0);
    out.max_time_blocks = getIntValue(plan, "max_time_blocks", 0);
    out.time_blocks_monthly_growth = getIntValue(plan, "time_blocks_monthly_growth", 0);
    out.mobile_only = getBoolValue(plan, "mobile_only", false);
    return out;
}

} // namespace

boost::asio::awaitable<void> Plans::syncPlans()
{
    auto available = false;
    if (!is_syncing_plans_.compare_exchange_strong(available, true)) {
        LOG_WARN_N << "Already syncing plans. Skipping.";
        co_return;
    }
    ScopedExit clear_syncing{[this] {
        is_syncing_plans_.store(false);
    }};

    LOG_INFO << "Syncing plans with payment service.";

    payments::v1::GetPlansRequest req;
    req.set_product_id(config().product_id);
    auto response = co_await getPlans(std::move(req));

    auto db = co_await server_.db().getConnection();
    auto trx = co_await db.transaction();

    unordered_map<string, DbPlan> existing_plans;
    {
        auto res = co_await db.exec(R"(SELECT name, active, max_users, max_devices, max_nodes, nodes_monthly_growth,
            max_actions, actions_monthly_growth, max_worksessions, work_sessions_monthly_growth,
            max_time_blocks, time_blocks_monthly_growth, mobile_only FROM plan)");
        enum Cols {
            NAME,
            ACTIVE,
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

        for (const auto& row : res.rows()) {
            DbPlan plan;
            plan.name = row.at(NAME).as_string();
            plan.active = row.at(ACTIVE).as_int64() != 0;
            plan.max_users = row.at(MAX_USERS).as_int64();
            plan.max_devices = row.at(MAX_DEVICES).as_int64();
            plan.max_nodes = row.at(MAX_NODES).as_int64();
            plan.nodes_monthly_growth = row.at(NODES_MONTHLY_GROWTH).as_int64();
            plan.max_actions = row.at(MAX_ACTIONS).as_int64();
            plan.actions_monthly_growth = row.at(ACTIONS_MONTHLY_GROWTH).as_int64();
            plan.max_worksessions = row.at(MAX_WORKSESSIONS).as_int64();
            plan.work_sessions_monthly_growth = row.at(WORK_SESSIONS_MONTHLY_GROWTH).as_int64();
            plan.max_time_blocks = row.at(MAX_TIME_BLOCKS).as_int64();
            plan.time_blocks_monthly_growth = row.at(TIME_BLOCKS_MONTHLY_GROWTH).as_int64();
            plan.mobile_only = row.at(MOBILE_ONLY).as_int64() != 0;
            existing_plans.emplace(plan.name, std::move(plan));
        }
    }

    size_t added = 0;
    size_t updated = 0;
    size_t skipped_by_version = 0;
    bool changed = false;
    unordered_map<string, int32_t> processed_versions;

    for (const auto& remote_plan : response.plans()) {
        const auto plan_id = remote_plan.plan_id();
        if (plan_id.empty()) {
            throw runtime_error{"Payment service returned a plan without plan_id"};
        }

        if (auto it = synced_plan_versions_.find(plan_id);
            it != synced_plan_versions_.end() && it->second == remote_plan.version()) {
            ++skipped_by_version;
            continue;
        }

        const auto db_plan = toDbPlan(remote_plan);
        if (auto existing = existing_plans.find(db_plan.name); existing == existing_plans.end()) {
            co_await db.exec(R"(INSERT INTO plan
                (name, active, max_users, max_devices, max_nodes, nodes_monthly_growth, max_actions,
                 actions_monthly_growth, max_worksessions, work_sessions_monthly_growth, max_time_blocks,
                 time_blocks_monthly_growth, mobile_only)
              VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                db_plan.name,
                db_plan.active,
                db_plan.max_users,
                db_plan.max_devices,
                db_plan.max_nodes,
                db_plan.nodes_monthly_growth,
                db_plan.max_actions,
                db_plan.actions_monthly_growth,
                db_plan.max_worksessions,
                db_plan.work_sessions_monthly_growth,
                db_plan.max_time_blocks,
                db_plan.time_blocks_monthly_growth,
                db_plan.mobile_only);
            ++added;
            changed = true;
        } else if (existing->second != db_plan) {
            co_await db.exec(R"(UPDATE plan SET active=?, max_users=?, max_devices=?, max_nodes=?,
                nodes_monthly_growth=?, max_actions=?, actions_monthly_growth=?, max_worksessions=?,
                work_sessions_monthly_growth=?, max_time_blocks=?, time_blocks_monthly_growth=?,
                mobile_only=? WHERE name=?)",
                db_plan.active,
                db_plan.max_users,
                db_plan.max_devices,
                db_plan.max_nodes,
                db_plan.nodes_monthly_growth,
                db_plan.max_actions,
                db_plan.actions_monthly_growth,
                db_plan.max_worksessions,
                db_plan.work_sessions_monthly_growth,
                db_plan.max_time_blocks,
                db_plan.time_blocks_monthly_growth,
                db_plan.mobile_only,
                db_plan.name);
            ++updated;
            changed = true;
        }

        processed_versions[plan_id] = remote_plan.version();
    }

    size_t config_updates = 0;
    auto upsert_config = [&](string_view key, const string& value) -> asio::awaitable<void> {
        if (value.empty()) {
            co_return;
        }

        auto res = co_await db.exec("SELECT value FROM config WHERE name=?", key);
        if (res.rows().empty() || res.rows().front().front().as_string() != value) {
            co_await db.exec("INSERT INTO config (name, value) VALUES (?, ?) ON DUPLICATE KEY UPDATE value = ?",
                             key, value, value);
            LOG_TRACE_N << "Updated config '" << key << "' to '" << value << "'";
            ++config_updates;
        }
    };

    co_await upsert_config("trial_days", response.has_trial_days() ? to_string(response.trial_days()) : "0");
    co_await upsert_config("default_for_signup", response.default_for_signup());
    co_await upsert_config("default_for_free", response.default_for_free());

    co_await trx.commit();

    for (const auto& [plan_id, version] : processed_versions) {
        synced_plan_versions_[plan_id] = version;
    }

    if (changed || config_updates || !active_plans_.load()) {
        co_await loadActivePlans();
    }

    if (changed) {
        co_await server_.grpc().sessionManager().loadPlans();
    }

    LOG_INFO << "Payment plan sync completed: " << response.plans_size()
             << " plans received, " << added << " added, " << updated
             << " updated, " << skipped_by_version << " skipped by version, "
             << config_updates << " config values updated.";
}

std::tuple<std::string/* plan id*/, bool /* is trial */> Plans::getPlanForSignup() const
{
    if (const auto p = active_plans_.load()) {
        if (!p->default_for_signup.empty()) {
            return {p->default_for_signup, true};
        }
        if (!p->default_for_free.empty()) {
            return {p->default_for_free, false};
        }
    }

    throw runtime_error{"No default payment plan configured for new signups"};
}

asio::awaitable<payments::v1::CreateCheckoutContextResponse>
Plans::createCheckoutContext(payments::v1::CreateCheckoutContextRequest request)
{
    co_return co_await callRpc<payments::v1::CreateCheckoutContextResponse>(
        "CreateCheckoutContext",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::CreateCheckoutContext,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::EnsureTenantInitializedResponse>
Plans::ensureTenantInitialized(payments::v1::EnsureTenantInitializedRequest request)
{
    co_return co_await callRpc<payments::v1::EnsureTenantInitializedResponse>(
        "EnsureTenantInitialized",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::EnsureTenantInitialized,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::GetEntitlementResponse>
Plans::getEntitlement(payments::v1::GetEntitlementRequest request)
{
    co_return co_await callRpc<payments::v1::GetEntitlementResponse>(
        "GetEntitlement",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::GetEntitlement,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::ConfirmExternalPurchaseResponse>
Plans::confirmExternalPurchase(payments::v1::ConfirmExternalPurchaseRequest request)
{
    co_return co_await callRpc<payments::v1::ConfirmExternalPurchaseResponse>(
        "ConfirmExternalPurchase",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::ConfirmExternalPurchase,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::RegisterGooglePlayPurchaseResponse>
Plans::registerGooglePlayPurchase(payments::v1::RegisterGooglePlayPurchaseRequest request)
{
    co_return co_await callRpc<payments::v1::RegisterGooglePlayPurchaseResponse>(
        "RegisterGooglePlayPurchaseResponse",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::RegisterGooglePlayPurchase,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::GetPlansResponse>
Plans::getPlans(payments::v1::GetPlansRequest request)
{
    co_return co_await callRpc<payments::v1::GetPlansResponse>(
        "GetPlans",
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::GetPlans,
        asio::use_awaitable);
}

} // namespace nextapp
