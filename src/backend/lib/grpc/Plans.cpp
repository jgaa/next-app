#include "nextapp/Plans.h"

#include <format>
#include <stdexcept>

#include <grpcpp/security/credentials.h>

#include "nextapp/Server.h"
#include "nextapp/GrpcServer.h"
#include "nextapp/error_mapping.h"
#include "nextapp/logging.h"

using namespace std;
namespace asio = boost::asio;

namespace nextapp {

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

    ::grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, server_.config().grpc.keepalive_time_sec * 1000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, server_.config().grpc.keepalive_timeout_sec * 1000);

    channel_ = ::grpc::CreateCustomChannel(grpcServerAddress(), createCredentials(), args);
    stub_ = payments::v1::PaymentsService::NewStub(channel_);

    if (!stub_) {
        throw runtime_error{"Failed to create payment service gRPC client"};
    }

    co_return;
}

void Plans::shutdown()
{
    stub_.reset();
    channel_.reset();
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
        auto res = co_await db.exec(R"(SELECT name, max_users, max_devices, max_nodes, max_actions,
            max_worksessions, max_time_blocks, mobile_only FROM plan WHERE active=TRUE ORDER BY name)");
        enum Cols {
            NAME,
            MAX_USERS,
            MAX_DEVICES,
            MAX_NODES,
            MAX_ACTIONS,
            MAX_WORKSESSIONS,
            MAX_TIME_BLOCKS,
            MOBILE_ONLY
        };

        for (const auto& row : res.rows()) {
            PlanProperties plan;
            plan.max_users = row.at(MAX_USERS).as_int64();
            plan.max_devices = row.at(MAX_DEVICES).as_int64();
            plan.max_nodes = row.at(MAX_NODES).as_int64();
            plan.max_actions = row.at(MAX_ACTIONS).as_int64();
            plan.max_worksessions = row.at(MAX_WORKSESSIONS).as_int64();
            plan.max_time_blocks = row.at(MAX_TIME_BLOCKS).as_int64();
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
                    throw runtime_error{format("Invalid trial_days value '{}' in config table", value)};
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
    int64_t max_actions = 1240;
    int64_t max_worksessions = 1240;
    int64_t max_time_blocks = 1240;
    bool mobile_only = false;

    bool operator==(const DbPlan&) const = default;
};

int64_t getIntValue(const payments::v1::Plan& plan, string_view key)
{
    if (auto it = plan.values().find(string{key}); it != plan.values().end()) {
        size_t pos = 0;
        auto value = stoll(it->second, &pos);
        if (pos != it->second.size()) {
            throw runtime_error{format("Invalid integer value '{}' for payment plan '{}' field '{}'",
                                      it->second, plan.plan_id(), key)};
        }
        return value;
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
    out.max_users = getIntValue(plan, "max_users");
    out.max_devices = getIntValue(plan, "max_devices");
    out.max_nodes = getIntValue(plan, "max_nodes");
    out.max_actions = getIntValue(plan, "max_actions");
    out.max_worksessions = getIntValue(plan, "max_worksessions");
    out.max_time_blocks = getIntValue(plan, "max_time_blocks");
    out.mobile_only = getBoolValue(plan, "mobile_only");
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
        auto res = co_await db.exec(R"(SELECT name, active, max_users, max_devices, max_nodes, max_actions,
            max_worksessions, max_time_blocks, mobile_only FROM plan)");
        enum Cols {
            NAME,
            ACTIVE,
            MAX_USERS,
            MAX_DEVICES,
            MAX_NODES,
            MAX_ACTIONS,
            MAX_WORKSESSIONS,
            MAX_TIME_BLOCKS,
            MOBILE_ONLY
        };

        for (const auto& row : res.rows()) {
            DbPlan plan;
            plan.name = row.at(NAME).as_string();
            plan.active = row.at(ACTIVE).as_int64() != 0;
            plan.max_users = row.at(MAX_USERS).as_int64();
            plan.max_devices = row.at(MAX_DEVICES).as_int64();
            plan.max_nodes = row.at(MAX_NODES).as_int64();
            plan.max_actions = row.at(MAX_ACTIONS).as_int64();
            plan.max_worksessions = row.at(MAX_WORKSESSIONS).as_int64();
            plan.max_time_blocks = row.at(MAX_TIME_BLOCKS).as_int64();
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
                (name, active, max_users, max_devices, max_nodes, max_actions, max_worksessions, max_time_blocks, mobile_only)
              VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?))",
                db_plan.name,
                db_plan.active,
                db_plan.max_users,
                db_plan.max_devices,
                db_plan.max_nodes,
                db_plan.max_actions,
                db_plan.max_worksessions,
                db_plan.max_time_blocks,
                db_plan.mobile_only);
            ++added;
            changed = true;
        } else if (existing->second != db_plan) {
            co_await db.exec(R"(UPDATE plan SET active=?, max_users=?, max_devices=?, max_nodes=?,
                max_actions=?, max_worksessions=?, max_time_blocks=?, mobile_only=? WHERE name=?)",
                db_plan.active,
                db_plan.max_users,
                db_plan.max_devices,
                db_plan.max_nodes,
                db_plan.max_actions,
                db_plan.max_worksessions,
                db_plan.max_time_blocks,
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
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::CreateCheckoutContext,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::GetEntitlementResponse>
Plans::getEntitlement(payments::v1::GetEntitlementRequest request)
{
    co_return co_await callRpc<payments::v1::GetEntitlementResponse>(
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::GetEntitlement,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::GetEntitlementsResponse>
Plans::getEntitlements(payments::v1::GetEntitlementsRequest request)
{
    co_return co_await callRpc<payments::v1::GetEntitlementsResponse>(
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::GetEntitlements,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::ConfirmExternalPurchaseResponse>
Plans::confirmExternalPurchase(payments::v1::ConfirmExternalPurchaseRequest request)
{
    co_return co_await callRpc<payments::v1::ConfirmExternalPurchaseResponse>(
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::ConfirmExternalPurchase,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::RegisterGooglePlayPurchaseResponse>
Plans::registerGooglePlayPurchase(payments::v1::RegisterGooglePlayPurchaseRequest request)
{
    co_return co_await callRpc<payments::v1::RegisterGooglePlayPurchaseResponse>(
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::RegisterGooglePlayPurchase,
        asio::use_awaitable);
}

asio::awaitable<payments::v1::GetPlansResponse>
Plans::getPlans(payments::v1::GetPlansRequest request)
{
    co_return co_await callRpc<payments::v1::GetPlansResponse>(
        std::move(request),
        &payments::v1::PaymentsService::Stub::async::GetPlans,
        asio::use_awaitable);
}

} // namespace nextapp
