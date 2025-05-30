
#include <algorithm>
#include <format>
#include <span>
#include <ranges>

#include <boost/asio/co_spawn.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_with_diagnostics.hpp>
#include <boost/mysql/handshake_params.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/tcp.hpp>
#include <boost/mysql/throw_on_error.hpp>
#include <boost/mysql.hpp>

#include "signup/Server.h"
#include "signup/GrpcServer.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"
#include "mysqlpool/mysqlpool.h"

using namespace std;
using namespace jgaa;
using nextapp::logging::LogEvent;
namespace asio = boost::asio;

namespace nextapp {

Server::Server(const Config& config)
    : config_(config), metrics_(*this)
{
}

Server::~Server()
{

}

void Server::init()
{
    handleSignals();
    initCtx(config().svr.io_threads);

    if (config_.cluster.eula_path.empty()) {
        LOG_ERROR << "Missing EULA path!";
        throw runtime_error{"Missing EULA path"};
    }

    if (config_.cluster.welcome_path.empty()) {
        LOG_ERROR << "Missing welcome path!";
        throw runtime_error{"Missing welcome path"};
    }

    eula_text_ = readFileToBuffer(config_.cluster.eula_path);
    LOG_DEBUG << "EULA text loaded from " << config_.cluster.eula_path;

    welcome_text_ = readFileToBuffer(config_.cluster.welcome_path);
    LOG_DEBUG << "Welcome text loaded from " << config_.cluster.welcome_path;

    db_.emplace(ctx_, config().db);

    assert(!grpc_service_);
    grpc_service_ = make_shared<GrpcServer>(*this);
}

void Server::run()
{
    bool ok = false;
    try {
        asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
                co_await db().init();
                co_await checkDb();
                co_await loadCluster(false);
                co_await connectToInstances();
                co_await startGrpcService();
                startClusterTimer();
                ok = true;
        }, asio::use_future).get();
    } catch (const std::exception& ex) {
        LOG_ERROR << "Failed to initialize the server: " << ex.what();
    }

    if (!ok) {
        stop();
        throw runtime_error{"Failed to initialize the server"};
    }

    if (!config().options.enable_http) {
        LOG_INFO << "HTTP server (metrics) is disabled.";
    } else {
        LOG_INFO << "Starting HTTP server (metrics).";
        http_server_.emplace(config().http, [this](const yahat::AuthReq& ar) {
            if (ar.req.target == "/metrics" && ar.req.type == yahat::Request::Type::GET) {
                if (config().options.no_metrics_password) {
                    LOG_TRACE << "Metrics request " << ar.req.uuid << " authenticated (no password required)";
                    return yahat::Auth{"metrics", true};
                }
                // Need authentication here
                const auto lower = toLower(ar.auth_header);
                if (lower.starts_with("basic ")) {
                    const auto base64 = ar.auth_header.substr(6);
                    const auto hash = hashPasswd(base64);
                    if (hash == metrics_auth_hash_) {
                        LOG_TRACE << "Metrics request " << ar.req.uuid << " authenticated";
                        return yahat::Auth{"metrics", true};
                    }
                }
            }
            return yahat::Auth{"", false};
        }, metrics_.metrics(), "signup "s + NEXTAPP_VERSION);

        http_server_->start();
    }


    LOG_DEBUG_N << "Main thread joins the IO thread pool...";
    runIoThread(0);
    LOG_DEBUG_N << "Main thread left the IO thread pool...";
}

void Server::stop()
{
    LOG_DEBUG << "Server stopping...";
    done_ = true;
    if (grpc_service_) {
        grpc_service_->stop();
    }

    if (db_) {
        asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            LOG_DEBUG << "Closing the database connection...";
            co_await db().close();
        }, asio::use_future).get();
    }

    LOG_DEBUG << "Shutting down the thread-pool.";
    ctx_.stop();
    LOG_DEBUG << "Server stopped.";
}

boost::asio::awaitable<signup::pb::GetInfoResponse> Server::getInfo(const signup::pb::GetInfoRequest &req)
{
    signup::pb::GetInfoResponse resp;
    resp.set_eula(eula_text_);
    resp.set_greeting(welcome_text_);

    if (auto regions = getRegions()) {
        for(const auto& r : *regions) {
            resp.add_regions()->CopyFrom(r);
        }
    }

    co_return resp;
}


void Server::initCtx(size_t numThreads)
{
    io_threads_.reserve(numThreads);
    for(size_t i = 1; i < numThreads; ++i) {
        io_threads_.emplace_back([this, i]{
            runIoThread(i);
        });
    }
}

void Server::runIoThread(const size_t id)
{
    auto scope = metrics().asio_worker_threads().scoped();

    LOG_DEBUG_N << "starting io-thread " << id;
    while(!ctx_.stopped()) {
        try {
            ++running_io_threads_;
            ctx_.run();
            --running_io_threads_;
        } catch (const std::exception& ex) {
            --running_io_threads_;
            LOG_ERROR << LogEvent::LE_IOTHREAD_THREW
                      << "Caught exception from IO therad #" << id
                      << ": " << ex.what();
            assert(false);
        }
    }

    LOG_DEBUG_N << "Io-thread " << id << " is done.";
}

boost::asio::awaitable<std::shared_ptr<Server::Cluster> > Server::getClusterFromDb()
{
    LOG_DEBUG_N << "Loading regions from db...";

    auto cluster = make_shared<Cluster>();

    auto conn = co_await db().getConnection();

    {
        auto res = co_await conn.exec("SELECT id, name, description, created, state FROM region");
        enum Cols { ID, NAME, DESC, CREATED, STATE };

        for(const auto& row : res.rows()) {
            Cluster::Region r;
            r.uuid = toUuid(row[ID].as_string());
            r.name = row[NAME].as_string();
            r.description = toStringIfValue(row, DESC);
            r.created_at = chrono::system_clock::to_time_t(row[CREATED].as_datetime().get_time_point());
            r.state = row[STATE].as_string() == "active" ? Cluster::Region::State::ACTIVE
                                                         : Cluster::Region::State::INACTIVE;
            cluster->regions.emplace(r.uuid, std::move(r));
        }
    }

    {
        auto res = co_await conn.exec(
            "SELECT id, region, grpc_url, grpc_public_url, metrics_url, created, ca_cert, grpc_key, grpc_cert, "
            "grpc_pam_cert, grpc_pam_key, server_id, state, free_slots FROM instance");
        enum Cols { ID, REGION, GRPC_URL, GRPC_PUBLIC_URL, METRICS_URL, CREATED, CA_CERT, GRPC_KEY, GRPC_CERT, GRPC_PAM_CERT, GRPC_PAM_KEY, SERVER_ID, STATE, FREE_SLOTS };

        for(const auto& row : res.rows()) {
            auto region = toUuid(row[REGION].as_string());
            auto instance = make_unique<Cluster::Region::Instance>();
            instance->uuid = toUuid(row[ID].as_string());
            instance->url = row[GRPC_URL].as_string();
            instance->pub_url = row[GRPC_PUBLIC_URL].as_string();
            instance->metrics_url = toStringIfValue(row, METRICS_URL);
            instance->created_at = chrono::system_clock::to_time_t(row[CREATED].as_datetime().get_time_point());
            instance->x509_ca_cert = toStringIfValue(row, CA_CERT);
            instance->x509_cert = toStringIfValue(row, GRPC_CERT);
            instance->x509_key = toStringIfValue(row, GRPC_KEY);
            instance->server_id = toStringIfValue(row, SERVER_ID);
            instance->state = row[STATE].as_string() == "active" ? Cluster::Region::Instance::State::ACTIVE
                                                                 : Cluster::Region::Instance::State::INACTIVE;
            instance->free_slots = toIntIfValue(row, FREE_SLOTS);

            if (auto it = cluster->regions.find(region); it != cluster->regions.end()) {
                auto& region = it->second;
                const auto id = instance->uuid;
                it->second.instances.emplace(id, std::move(instance));
            } else {
                LOG_ERROR << "Instance " << instance->uuid << " references unknown region " << region;
            }
        }
    }

    co_return cluster;
}

std::shared_ptr<std::vector<signup::pb::Region> > Server::getRegions()
{
    auto regions = make_shared<std::vector<signup::pb::Region>>();
    if (auto cluster = cluster_.load()) {
        for(const auto& [_, region] : cluster->regions) {
            signup::pb::Region r;
            r.mutable_uuid()->set_uuid(to_string(region.uuid));
            r.set_name(region.name);
            r.set_description(region.description);
            regions->push_back(r);
        };
    }

    return regions;
}

boost::asio::awaitable<void> Server::startGrpcService()
{
    LOG_DEBUG_N << "Starting gRPC services...";
    grpc_service_->start();
    co_return;
}


// This creates and connects to all active instances
boost::asio::awaitable<void> Server::connectToInstances()
{
    const auto max_wait = chrono::steady_clock::now() + chrono::seconds(config().options.max_retry_time_to_nextapp_secs);
    assert(grpc_service_); // Must be initialized
    auto num_instances = 0u;
    auto num_peered = 0u;
    auto num_regions = 0u;
    auto num_reachable = 0u;

    while(!is_done()) {
        if (auto cluster = cluster_.load()) {
            num_instances = {};
            num_peered = {};
            num_regions = {};

            for(auto& [_, region] : cluster->regions) {
                ++num_regions;
                LOG_INFO << "Connecting to instances in region " << region.name;
                num_instances += region.instances.size();
                for(auto& [_, instance] : region.instances) {
                    if (instance->is_online) {
                        ++num_peered; // Assume for now that grpc channelse will self-heal once they have been connected.

                        if (auto inst = grpc_service_->getInstance(instance->uuid)) {
                            if (co_await inst->isReachable()) {
                                ++num_reachable;
                            } else {
                                LOG_INFO << "  -- Instance " << instance->uuid << " at " << instance->url
                                         << " was connected but is currently unreachable. ";
                            }
                        };
                        continue;
                    }

                    LOG_INFO << "  -- Connecting to instance " << instance->uuid << " at " << instance->url;
                    GrpcServer::InstanceCommn::InstanceInfo i;
                    i.url = instance->url;
                    i.x509_ca_cert = instance->x509_ca_cert;
                    i.x509_cert = instance->x509_cert;
                    i.x509_key = instance->x509_key;
                    auto info = co_await grpc_service_->connectToInstance(instance->uuid, i);
                    if (info) {
                        instance->is_online = true;
                        ++num_peered;
                        ++num_reachable;

                        // Is this the first time we connect to this instance?
                        if (instance->server_id.empty()) {
                            auto server_id = info->properties().kv().at("server-id");
                            if (server_id.empty()) {
                                LOG_ERROR << "Failed to get server-id from the instance " << instance->uuid;
                            } else {
                                instance->server_id = server_id;
                                co_await initializeInstance(region, *instance);
                            }
                        }
                    }
                }
            }

            if (num_instances == 0) {
                LOG_WARN << "No instances are added to the server.";
                break;
            }

            if (num_instances == num_reachable && num_instances == num_peered) {
                LOG_INFO << "All instances are connected.";
                break;

            } else {
                LOG_INFO << "Peered with " << num_peered << " out of " << num_instances
                         << " instances in the cluster. "
                         << num_peered - num_reachable << " instances are currently unreachable.";

                if (num_instances == num_peered) {
                    LOG_DEBUG_N << "All instances are initialized. The one(s) that are down should heal automatically...";
                    break;
                }
            }

            if (chrono::steady_clock::now() > max_wait) {
                LOG_ERROR << "Failed to connect to all instances in time."
                          << ". Connected to " << num_peered << " out of " << num_instances;
                break;
            };

            LOG_INFO << "Peered with " << num_peered << " out of " << num_instances << " instances."
                     << " Retrying in " << config().options.retry_connect_to_nextappd_secs << " seconds.";

            auto timer = asio::steady_timer(ctx_, chrono::seconds(config().options.retry_connect_to_nextappd_secs));
            co_await timer.async_wait(asio::use_awaitable);
        }
    }

    metrics().regions().set(num_regions);
    metrics().nextapp_connections().set(num_reachable);
    metrics().nextapp_servers().set(num_instances);
}

boost::asio::awaitable<void> Server::initializeInstance(const Cluster::Region& region, Cluster::Region::Instance &instance)
{
    LOG_DEBUG_N << "Initializing instance " << instance.uuid << " in region " << region.uuid;
    auto conn = grpc_service_->getInstance(instance.uuid);
    if (!conn) {
        throw runtime_error("Failed to get connection to the instance " + to_string(instance.uuid));
    }

    ::grpc::ClientContext ctx;
    conn->prepareMetadata(ctx);
    auto stream = conn->stub().ListTenants(&ctx, {});
    nextapp::pb::Status status;
    unsigned tenants{}, users{};
    while(stream->Read(&status)) {
        if (status.error() != nextapp::pb::Error::OK) {
            LOG_ERROR_N << "Failed to get tenants from the instance " << instance.uuid
                        << ". Error: " << status.error();
            co_return;
        }

        if (!status.has_tenant()) {
            LOG_ERROR_N << "Instance " << instance.uuid
                        << " did not return the expected stream of Status.Tenant";
            co_return;
        };
        const auto& tenant = status.tenant();

        ++tenants;
        LOG_TRACE << "Adding tenant " << tenant.uuid() << " from instance " << instance.uuid;
        co_await db().exec( R"(INSERT INTO tenant (id, state, instance, region)
     VALUES (?, ?, ?, ?)
     ON DUPLICATE KEY UPDATE
       state    = VALUES(state),
       instance = VALUES(instance),
       region   = VALUES(region))",
                           tenant.uuid(), tenant.state(), instance.uuid, region.uuid);

        for(const auto& user : tenant.users()) {
            ++users;
            LOG_TRACE << "Adding user " << user.uuid() << " from tenant " << tenant.uuid();
            const auto email_hash = getEmailHash(user.email());
            const string_view state = user.active() ? "active" : "inactive";

            co_await db().exec(R"(INSERT INTO `user` (id, email_hash, tenant, state)
      VALUES (?, ?, ?, ?)
    ON DUPLICATE KEY UPDATE
      email_hash = VALUES(email_hash),
      tenant     = VALUES(tenant),
      state      = VALUES(state))",
                               user.uuid(), email_hash, tenant.uuid(), state);
        }
    }

    LOG_INFO << "Added " << tenants << " tenants and " << users << " users from instance "
             << instance.uuid << " in region " << region.uuid << ' ' << region.name;
    co_await db().exec("UPDATE instance SET server_id = ? WHERE id = ?", instance.server_id, instance.uuid);

    co_return;
}

void Server::startClusterTimer()
{
    if (done_) {
        LOG_DEBUG_N << "Server is done. Cluster timer will not be started.";
        return;
    }

    cluster_timer_.expires_after(chrono::seconds(config().cluster.timer_interval_sec));
    cluster_timer_.async_wait([this](const boost::system::error_code& ec) mutable {
        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE_N << "Cluster timer was aborted.";
                return;
            }
            LOG_WARN_N << "Cluster timer failed: " << ec.message();
            return;
        }
        LOG_DEBUG_N << "Cluster timer fired.";
        checkCluster();
    });
}

void Server::checkCluster() {
    boost::asio::co_spawn(cluster_strand_, [this]() -> boost::asio::awaitable<void> {
        try {
            co_await checkCluster_();
        } catch (const std::exception& ex) {
            LOG_ERROR << "Failed to connect to instances: " << ex.what();
        }
        startClusterTimer();
    }, boost::asio::detached);
}

boost::asio::awaitable<void> Server::checkCluster_() {
    LOG_DEBUG_N << "Checking the cluster state...";

    // TODO: add other cluster state checks here
    co_await connectToInstances();
}

void Server::handleSignals()
{
    if (is_done()) {
        return;
    }

    static unsigned count = 0;

    if (!signals_) {
        signals_.emplace(ctx(), SIGINT, SIGQUIT);
        signals_->add(SIGUSR1);
        signals_->add(SIGHUP);
    }

    signals_->async_wait([this](const boost::system::error_code& ec, int signalNumber) {

        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE_N << "Server::handleSignals: Handler aborted.";
                return;
            }
            LOG_WARN_N << "Server::handleSignals Received error: " << ec.message();
            return;
        }

        LOG_INFO << "Server::handleSignals: Received signal #" << signalNumber;
        if (signalNumber == SIGHUP) {
            LOG_WARN << "Server::handleSignals: Ignoring SIGHUP. Note - config is not re-loaded.";
        } else if (signalNumber == SIGQUIT || signalNumber == SIGINT) {
            if (!is_done()) {
                LOG_INFO_N << "Stopping the services.";
                stop();
            }
        } else {
            LOG_WARN_N << " Ignoring signal #" << signalNumber;
        }

        handleSignals();
    });
}

void Server::bootstrap(const BootstrapOptions &opts)
{
    LOG_INFO << "Bootstrapping the system...";

    auto res = asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        ScopedExit se{[this] {
            ctx_.stop();
        }};

        co_await createDb(opts);
        co_await upgradeDbTables(0);
        co_await createAdminUser();
        co_await createDefaultNextappInstance();
    }, boost::asio::use_future);

    ctx_.run();
    res.get();
    LOG_INFO << "Bootstrapping is complete";
}

boost::asio::awaitable<Server::AssignedInstance> Server::assignInstance(const boost::uuids::uuid &region)
{
    string region_name;
    if (auto cluster = cluster_.load()) {
        if (auto it = cluster->regions.find(region); it != cluster->regions.end()) {
            region_name = it->second.name;
            vector<const Cluster::Region::Instance *> alternatives;
            for(const auto& [_, instance] : it->second.instances) {
                if (instance->is_online && instance->state == Cluster::Region::Instance::State::ACTIVE) {
                    alternatives.push_back(instance.get());
                }
            }

            if (!alternatives.empty()) {
                // TODO: Implement load balancing
                const auto *use_instance = alternatives.at(getRandomNumber32() % alternatives.size());
                co_return AssignedInstance{
                    .region = region,
                    .instance = use_instance->uuid,
                    .pub_url = use_instance->pub_url,
                };
            }
        } else {
            throw runtime_error("Region " + to_string(region) + " not found in the cluster.");
        }
    }

    throw runtime_error("No available instances found in region "s + region_name);
}

string Server::getPasswordHash(std::string_view password, std::string_view userUuid)
{
    const auto v = format("{}:{}", password, userUuid);
    return sha256(v, true);
}

boost::asio::awaitable<bool> Server::checkDb()
{
    LOG_TRACE_N << "Checking the database version...";
    auto res = co_await db_->exec("SELECT version, salt FROM signup where id = 1");

    enum Cols { VERSION, SALT };

    if (res.rows().empty()) {
        LOG_ERROR_N << "The 'signup' table is empty. Is the database initialized?";
        throw runtime_error{"The 'signup' table is empty"};
    } else {
        const auto version = res.rows().front().at(VERSION).as_int64();
        salt_ = res.rows().front().at(SALT).as_string();
        if (salt_.empty()) {
            LOG_ERROR_N << "Salt is empty in the 'signup' table.!";
            co_return false;
        }
        LOG_DEBUG_N << "I need the database to be at version " << latest_version
                  << ". The existing database is at version " << version << '.';

        if (latest_version > version) {
            co_await upgradeDbTables(version);
            co_return true;
        }
        co_return version == latest_version;
    }

    co_return false;
}

boost::asio::awaitable<void> Server::createDb(const BootstrapOptions &opts)
{
    LOG_INFO << "Creating the database " << config_.db.database;

    // Create the database.
    auto cfg = config_.db;
    cfg.database = "mysql";
    cfg.username = opts.db_root_user;
    cfg.password = opts.db_root_passwd;
    cfg.max_connections = 1;

    mysqlpool::Mysqlpool db{ctx_, cfg};

    co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {

        co_await db.init();

        if (opts.drop_old_db) {
            LOG_TRACE_N << "Dropping database " << config_.db.database ;

            try {
                co_await db.exec(format("REVOKE ALL PRIVILEGES ON {}.* FROM `{}`@`%`",
                                        config_.db.database, config_.db.username));
            } catch (const exception &ex) {
                LOG_WARN_N << "Failed to revoke privileges from user " << config_.db.username
                           << " on database " << config_.db.database << ": " << ex.what();
            };

            try {
                co_await db.exec(format("DROP USER '{}'@'%'",
                                        config_.db.username));
            } catch (const exception& ex) {
                LOG_WARN_N << "Failed to drop user " << config_.db.username << ": " << ex.what();
            };

            try {
                co_await db.exec("FLUSH PRIVILEGES");
            } catch (const exception& ex) {
                LOG_WARN_N << "Failed to flush privileges: " << ex.what();
            };

            try {
                co_await db.exec(format("DROP DATABASE {}", config_.db.database));
            } catch (const exception& ex) {
                LOG_WARN_N << "Failed to drop database " << config_.db.database << ": " << ex.what();
            };
        }

        {
            auto res = co_await db.exec(format("SELECT SCHEMA_NAME FROM information_schema.schemata WHERE SCHEMA_NAME = '{}'", config_.db.database));
            if (!res.rows().empty()) {
                LOG_INFO_N << "Database " << config_.db.database << " already exists. Skipping creation.";
                throw runtime_error{"Database already exist."};
            }
        }

        LOG_TRACE_N << "Creating database...";
        co_await db.exec(format("CREATE DATABASE {} CHARACTER SET = 'utf8'", config_.db.database));

        LOG_TRACE_N << "Creating database user " << config_.db.username;
        co_await db.exec(format("CREATE USER '{}'@'%' IDENTIFIED BY '{}'",
                                config_.db.username, config_.db.password));

        co_await db.exec(format("GRANT ALL PRIVILEGES ON {}.* TO '{}'@'%'",
                                config_.db.database, config_.db.username));

        co_await db.exec("FLUSH PRIVILEGES");

        co_await db.close();

    }, asio::use_awaitable);
}



boost::asio::awaitable<void> Server::upgradeDbTables(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        R"(CREATE TABLE signup (
            id INTEGER NOT NULL PRIMARY KEY,
            version INTEGER NOT NULL,
            salt VARCHAR(255) NOT NULL
        ))",

        R"(CREATE TABLE region (
            id UUID NOT NULL PRIMARY KEY DEFAULT UUID(),
            name VARCHAR(255) NOT NULL,
            description TEXT,
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
        UNIQUE KEY (name)
        ))",

        R"(CREATE TABLE instance (
            id UUID NOT NULL PRIMARY KEY DEFAULT UUID(), -- Our UUID.
            region UUID NOT NULL,
            grpc_url VARCHAR(255) NOT NULL,
            grpc_public_url VARCHAR(255) NOT NULL,
            metrics_url VARCHAR(255), -- Fixed missing data type
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            ca_cert TEXT,
            grpc_key TEXT,
            grpc_cert TEXT,
            grpc_pam_cert VARCHAR(255),
            grpc_pam_key VARCHAR(255),
            server_id VARCHAR(255), -- The servers UUID
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
            free_slots INTEGER,
            cpu_load INTEGER,
            users INTEGER,
            db_size INTEGER,
            free_dispspace INTEGER,
        FOREIGN KEY (region) REFERENCES region(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE TABLE tenant (
            id UUID NOT NULL PRIMARY KEY DEFAULT UUID(),
            state ENUM('active', 'inactive', 'migrating') NOT NULL DEFAULT 'active',
            instance UUID NOT NULL,
            region UUID NOT NULL,
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        FOREIGN KEY (instance) REFERENCES instance(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY (region) REFERENCES region(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE TABLE user (
            id UUID NOT NULL PRIMARY KEY DEFAULT UUID(),
            email_hash VARCHAR(255) NOT NULL COMMENT 'SHA256(email:signup.salt)',
            tenant UUID NOT NULL,
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
            UNIQUE KEY (email_hash),
        FOREIGN KEY (tenant) REFERENCES tenant(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE TABLE voucher (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            comment TEXT, -- optional
            region UUID, -- optional
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            ttl INTEGER NOT NULL DEFAULT 604800, -- one week
        FOREIGN KEY (region) REFERENCES region(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        // Table for authentication for the signup service itself.
        R"(CREATE TABLE localuser (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            nickname VARCHAR(255) NOT NULL COMMENT 'unix name, email, whatever the admin likes.',
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
            kind ENUM('admin', 'user', 'metrics') NOT NULL DEFAULT 'user',
            auth_token varchar(255),
            auth_type enum('password') NOT NULL DEFAULT 'password',
            UNIQUE KEY (nickname)))",
    });

    static constexpr auto v2_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE TABLE IF NOT EXISTS config (
            name VARCHAR(128) NOT NULL PRIMARY KEY,
            value TEXT NOT NULL))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
        v2_upgrade,
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version
             << " on database " << config_.db.database;

    auto cfg = config_.db;
    cfg.max_connections = 1;

    mysqlpool::Mysqlpool db{ctx_, cfg};

    co_await db.init();
    {
        auto handle = co_await db.getConnection();
        auto trx = co_await handle.transaction();

        // Here we will run all SQL queries for upgrading from the specified version to the current version.
        auto relevant = ranges::drop_view(versions, version);
        for(string_view query : relevant | std::views::join) {
            co_await handle.exec(query);
        }

        if (version == 0) {
            auto salt = getRandomStr(32);
            LOG_DEBUG_N << "Initializing the signup table";
            co_await handle.exec("INSERT INTO signup (id, version, salt) VALUES (1, 1, ?)", salt);
        }

        co_await handle.exec("UPDATE signup SET VERSION = ? WHERE id = 1", latest_version);
        co_await trx.commit();
    }
    co_await db.close();
}

boost::asio::awaitable<void> Server::createAdminUser()
{
    const auto admin = getEnv("SIGNUP_ADMIN", "admin");
    auto password = getEnv("SIGNUP_ADMIN_PASSWORD");
    if (password.empty()) {
        LOG_DEBUG << "Missing password for the admin user. Generating random password.";
        password = getRandomStr(48);
        LOG_INFO << "Generated random password for the admin user: " << password;
    } else {
        LOG_INFO << "Setting admin password to the value of SIGNUP_ADMIN_PASSWORD";
    }

    LOG_INFO << "Creating admin user: " << admin;
    auto cfg = config_.db;
    cfg.max_connections = 1;
    mysqlpool::Mysqlpool db{ctx_, cfg};

    const auto id = newUuidStr();
    const auto hash = getPasswordHash(password, id);

    auto conn = co_await db.getConnection();
    auto res = co_await conn.exec(
        "INSERT INTO localuser (id, nickname, kind, auth_token) VALUES (?, ?, ?, ?)",
        id, admin, "admin", hash);
}

boost::asio::awaitable<void> Server::createDefaultNextappInstance()
{
    if (config_.grpc_nextapp.ca_cert.empty()
        || config_.grpc_nextapp.address.empty()
        || config_.grpc_nextapp.server_key.empty()
        || config_.grpc_nextapp.server_cert.empty()) {
        LOG_INFO << "Nextapp instance is not configured. Skipping configuration.";
        co_return;
    }

    LOG_INFO << "Adding nextapp instance at " << config_.grpc_nextapp.address;
    auto cfg = config_.db;
    cfg.max_connections = 1;
    mysqlpool::Mysqlpool db{ctx_, cfg};

    auto conn = co_await db.getConnection();

    // query for the first region's id
    string region_id;
    {
        auto res = co_await conn.exec("SELECT id FROM region LIMIT 1");
        if (res.rows().empty()) {
            region_id = newUuidStr();
            LOG_INFO << "No regions found in the database. Creating 'Default' region " << region_id;
            co_await conn.exec("INSERT INTO region (id, name) VALUES (?, 'Default')", region_id);
        }
    }

    const auto pub_url = config_.cluster.nextapp_public_url.empty()
        ? config_.grpc_nextapp.address
        : config_.cluster.nextapp_public_url;
    auto res = co_await conn.exec(
        "INSERT INTO instance (region, grpc_url, grpc_public_url, ca_cert, grpc_key, grpc_cert) VALUES (?, ?, ?, ?, ?, ?)",
        region_id,
        config_.grpc_nextapp.address,
        pub_url,
        readFileToBuffer(config_.grpc_nextapp.ca_cert),
        readFileToBuffer(config_.grpc_nextapp.server_key),
        readFileToBuffer(config_.grpc_nextapp.server_cert));
}

boost::asio::awaitable<std::optional<boost::uuids::uuid> > Server::getRegionFromUserEmail(std::string_view email)
{
    auto hash = getEmailHash(email);;

    auto conn = co_await db().getConnection();
    auto res = co_await conn.exec("SELECT t.region FROM tenant t JOIN user u ON u.tenant = t.id WHERE u.email_hash = ?", hash);
    if (res.rows().empty()) {
        co_return std::nullopt;
    }

    co_return toUuid(res.rows().front()[0].as_string());
}

boost::asio::awaitable<std::optional<Server::AssignedInstance>> Server::getInstanceFromUserEmail(std::string_view email) {
    auto hash = getEmailHash(email);;

    auto conn = co_await db().getConnection();
    auto res = co_await conn.exec("SELECT t.instance FROM tenant t JOIN user u ON u.tenant = t.id WHERE u.email_hash = ?", hash);
    if (res.rows().empty()) {
        co_return std::nullopt;
    }

    co_return getInstanceFromUuid(toUuid(res.rows().front()[0].as_string()));
}

std::optional<Server::AssignedInstance> Server::getInstanceFromUuid(const boost::uuids::uuid &uuid)
{
    if (auto cluster = cluster_.load()) {
        // Find the instance
        for(const auto& [_, region] : cluster->regions) {
            for(const auto& [_, instance] : region.instances) {
                if (instance->uuid == uuid) {
                    return AssignedInstance{
                        .region = region.uuid,
                        .instance = instance->uuid,
                        .pub_url = instance->pub_url,
                    };
                }
            }
        }
    }
    return {};
}

boost::asio::awaitable<bool> Server::loadCluster(bool checkClusterWhenLoaded) {
    // Load the cluster from it's strand to prevent paralell loading form multiple threads
    const auto result = co_await boost::asio::co_spawn(cluster_strand_, [this]() -> boost::asio::awaitable<bool> {
            co_return co_await loadCluster_();
    }, boost::asio::use_awaitable);

    if (result && checkClusterWhenLoaded) {
        checkCluster();
    }
    co_return result;
}

string Server::hashPasswd(std::string_view passwd)
{
    const auto base = format("{}:{}", salt_, passwd);
    return sha256(base, true);
}

boost::asio::awaitable<bool> Server::loadCluster_()
{
    auto new_cluster = co_await getClusterFromDb();
    auto current_cluster = cluster_.load();

    if (current_cluster) {
        // sync the volatile state in is_online to the new cluster
        // This is actually a potential race-condition, as the state could
        // be changed again before the new cluster instance takes over.
        // However, it will be corrected when we iterate over the instances later on.
        std::unordered_map<boost::uuids::uuid, Cluster::Region::Instance *> current;
        for(auto& [_, region] : current_cluster->regions) {
            for(auto& [_, instance] : region.instances) {
                current[instance->uuid] = instance.get();
            }
        }

        for(auto& [_, region] : new_cluster->regions) {
            for(auto& [_, instance] : region.instances) {
                if (auto it = current.find(instance->uuid); it != current.end()) {
                    instance->is_online.store(it->second->is_online.load());
                }
            }
        }
    }

    // Set the new cluster as the current cluster
    LOG_INFO << "Loaded cluster with " << new_cluster->regions.size() << " regions.";

    // Log regions and instances
    for(const auto& [_, region] : new_cluster->regions) {
        LOG_INFO << "Region " << region.name << " has " << region.instances.size() << " instance(s).";
        for(const auto& [_, instance] : region.instances) {
            LOG_INFO << "  - Instance " << instance->uuid << " at " << instance->url
                     << ' ' << (instance->is_online ? "[online]" : "[offline]");
        }
    }
    cluster_.store(std::move(new_cluster));
    co_return true;
}

string Server::getEmailHash(std::string_view email) const
{
    return  sha256(format("{}:{}", toLower(email), salt_), true);
}


} // ns


boost::asio::awaitable<void> nextapp::Server::prepareMetricsAuth()
{
    if (!config_.options.enable_http) {
        LOG_DEBUG << "Metrics HTTP server is disabled";
        co_return;
    }

    if (config_.options.no_metrics_password) {
        LOG_INFO << "Metrics password is disabled";
    } else {
        for(auto i = 0u; i < 2; ++i) {
            auto res = co_await db().exec("SELECT value FROM config WHERE name='metric_auth_hash'");
            if (!res.rows().empty()) {
                metrics_auth_hash_ = res.rows().at(0).at(0).as_string();
            } else {
                if (i == 0) {
                    auto conn = co_await db().getConnection();
                    co_await resetMetricsPassword(conn);
                } else {
                    LOG_WARN_N << "No metrics password found in config table, even after I tried to reset it.";
                }
            }
        }
    }
    co_return;
}



boost::asio::awaitable<void> nextapp::Server::resetMetricsPassword(jgaa::mysqlpool::Mysqlpool::Handle &handle)
{
    const auto passwd = getRandomStr(32);
    const auto auth = format("metrics:{}", passwd);
    const auto http_basic_auth = Base64Encode(auth);
    const auto hash = hashPasswd(http_basic_auth);

    auto res = co_await handle.exec("INSERT INTO config (name, value) VALUES ('metric_auth_hash', ?) ON DUPLICATE KEY UPDATE value = ?", hash, hash);
    if (!res.affected_rows()) {
        LOG_ERROR << "Failed to insert metric_auth_hash into config table";
        co_return;
    }

    // Write the password to a file in users home directory
    const auto path = filesystem::path(getEnv("HOME", "/var/lib/nextapp")) / "signup_metrics_password.txt";
    auto content = format("user: metrics\npasswd: {}", passwd);

    if (auto fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR); fd != -1) {
        write(fd, content.c_str(), content.size());
        close(fd);
        LOG_INFO << "Metrics password saved to " << path;
    } else {
        LOG_ERROR << "Failed to write metrics password to " << path;
    }
}

