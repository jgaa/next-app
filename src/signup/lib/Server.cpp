
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
    : config_(config)
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
}

void Server::run()
{
    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            try {
                co_await loadCluster();
                co_await connectToInstances();
                co_await startGrpcService();
            } catch (const std::exception& ex) {
                LOG_ERROR << "Failed to start gRPC service: " << ex.what();
                co_return;
            }
    }, asio::use_future).get();

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

boost::asio::awaitable<bool> Server::loadCluster()
{
    LOG_DEBUG_N << "Loading regions...";
    auto cfg = config_.db;
    cfg.max_connections = 1;
    mysqlpool::Mysqlpool db{ctx_, cfg};

    auto cluster = make_shared<Cluster>();

    auto conn = co_await db.getConnection();

    {
        auto res = co_await conn.exec("SELECT id, name, description, created, state FROM region");
        enum Cols { ID, NAME, DESC, CREATED, STATE };

        for(const auto& row : res.rows()) {
            Cluster::Region r;
            r.uuid = toUuid(row[ID].as_string());
            r.name = row[NAME].as_string();
            r.description = row[DESC].as_string();
            r.created_at = chrono::system_clock::to_time_t(row[CREATED].as_datetime().get_time_point());
            r.state = row[STATE].as_string() == "active" ? Cluster::Region::State::ACTIVE
                                                         : Cluster::Region::State::INACTIVE;

            cluster->regions_.emplace(r.uuid, std::move(r));
        }
    }

    {
        auto res = co_await conn.exec(
            "SELECT id, region, grpc_url, grpc_public_url, metrics_url, created, ca_cert, grpc_key, grpc_cert, "
            "grpc_pam_cert, grpc_pam_key, server_id, state, free_slots FROM instance");
        enum Cols { ID, REGION, GRPC_URL, GRPC_PUBLIC_URL, METRICS_URL, CREATED, CA_CERT, GRPC_KEY, GRPC_CERT, GRPC_PAM_CERT, GRPC_PAM_KEY, SERVER_ID, STATE, FREE_SLOTS };

        for(const auto& row : res.rows()) {
            auto region = toUuid(row[REGION].as_string());
            Cluster::Region::Instance i;
            i.uuid = toUuid(row[ID].as_string());
            i.url = row[GRPC_URL].as_string();
            i.pub_url = row[GRPC_PUBLIC_URL].as_string();
            //i.metrics_url = row[METRICS_URL].as_string();
            i.created_at = chrono::system_clock::to_time_t(row[CREATED].as_datetime().get_time_point());
            i.x509_ca_cert = row[CA_CERT].as_string();
            i.x509_cert = row[GRPC_CERT].as_string();
            i.x509_key = row[GRPC_KEY].as_string();
            i.server_id = row[SERVER_ID].as_string();
            i.state = row[STATE].as_string() == "active" ? Cluster::Region::Instance::State::ACTIVE
                                                         : Cluster::Region::Instance::State::INACTIVE;
            i.free_slots = row[FREE_SLOTS].as_int64();

            if (auto it = cluster->regions_.find(region); it != cluster->regions_.end()) {
                it->second.instances_.emplace(i.uuid, std::move(i));
            } else {
                LOG_ERROR << "Instance " << i.uuid << " references unknown region " << region;
            }
        }
    }

    cluster_.store(std::move(cluster));
    co_return true;
}

std::shared_ptr<std::vector<signup::pb::Region> > Server::getRegions()
{
    auto regions = make_shared<std::vector<signup::pb::Region>>();
    if (auto cluster = cluster_.load()) {
        for(const auto& [_, region] : cluster->regions_) {
            signup::pb::Region r;
            r.set_uuid(to_string(region.uuid));
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
    assert(!grpc_service_);
    grpc_service_ = make_shared<GrpcServer>(*this);
    grpc_service_->start();
    co_return;
}


// This creates and connects to all active instances
boost::asio::awaitable<void> Server::connectToInstances()
{
    assert(grpc_service_); // Must be initialized
    if (auto cluster = cluster_.load()) {
        for(auto& [_, region] : cluster->regions_) {
            LOG_INFO << "Connecting to instances in region " << region.name;
            for(auto& [_, instance] : region.instances_) {
                if (instance.state == Cluster::Region::Instance::State::ACTIVE) {
                    LOG_INFO << "  -- Connecting to instance " << instance.uuid << " at " << instance.url;
                    GrpcServer::InstanceCommn::InstanceInfo i;
                    i.url = instance.url;
                    i.x509_ca_cert = instance.x509_ca_cert;
                    i.x509_cert = instance.x509_cert;
                    i.x509_key = instance.x509_key;
                    instance.is_online = co_await grpc_service_->connectToInstance(instance.uuid, i);
                    // TODO: Schedule retry if not online
                    // TODO: Update the online status from grpc server if it loose the connection
                }
            }
        }
    }
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

}


void nextapp::Server::bootstrap(const BootstrapOptions &opts)
{
    LOG_INFO << "Bootstrapping the system...";

    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        co_await createDb(opts);
        co_await upgradeDbTables(0);
        co_await createAdminUser();
        co_await createDefaultNextappInstance();
    },
        [](std::exception_ptr ptr) {
           if (ptr) {
               std::rethrow_exception(ptr);
           }
        });

    ctx_.run();

    LOG_INFO << "Bootstrapping is complete";
}

boost::asio::awaitable<nextapp::Server::AssignedInstance> nextapp::Server::assignInstance(const boost::uuids::uuid &region)
{
    if (auto cluster = cluster_.load()) {
        if (auto it = cluster->regions_.find(region); it != cluster->regions_.end()) {
            vector<const Cluster::Region::Instance *> alternatives;
            for(const auto& [_, instance] : it->second.instances_) {
                if (instance.is_online && instance.state == Cluster::Region::Instance::State::ACTIVE) {
                    alternatives.push_back(&instance);
                }
            }

            if (!alternatives.empty()) {
                // TODO: Implement load balancing
                const auto& use_instance = *alternatives.at(getRandomNumber32() % alternatives.size());
                co_return AssignedInstance{
                    .region = region,
                    .instance = use_instance.uuid,
                    .pub_url = use_instance.pub_url,
                };
            }
        }
    }

    co_return AssignedInstance{};
}

string nextapp::Server::getPasswordHash(std::string_view password, std::string_view userUuid)
{
    const auto v = format("{}:{}", password, userUuid);
    return sha256(v, true);
}



boost::asio::awaitable<bool> nextapp::Server::checkDb()
{
    // TODO: Implement
}



boost::asio::awaitable<void> nextapp::Server::createDb(const BootstrapOptions &opts)
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



boost::asio::awaitable<void> nextapp::Server::upgradeDbTables(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        R"(CREATE TABLE signup (
            id INTEGER NOT NULL PRIMARY KEY,
            version INTEGER NOT NULL))",
        "INSERT INTO signup (id, version) VALUES (1, 0)",
            R"(CREATE TABLE region (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            name VARCHAR(255) NOT NULL,
            description TEXT,
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
        UNIQUE KEY (name)))",
        R"(CREATE TABLE instance (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(), -- Our UUID.
            region VARCHAR(36) NOT NULL,
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
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            state ENUM('active', 'inactive', 'migrating') NOT NULL DEFAULT 'active',
            instance VARCHAR(36) NOT NULL,
            region VARCHAR(36) NOT NULL,
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        FOREIGN KEY (instance) REFERENCES instance(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY (region) REFERENCES region(id) ON DELETE CASCADE ON UPDATE RESTRICT))",
        R"(CREATE TABLE voucher (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            comment TEXT, -- optional
            region VARCHAR(36), -- optional
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            ttl INTEGER NOT NULL DEFAULT 604800, -- one week
        FOREIGN KEY (region) REFERENCES region(id) ON DELETE CASCADE ON UPDATE RESTRICT))",
        R"(CREATE TABLE user (
            id VARCHAR(36) NOT NULL PRIMARY KEY DEFAULT UUID(),
            nickname VARCHAR(255) NOT NULL, -- unix name, email, whatever the admin likes
            created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            state ENUM('active', 'inactive') NOT NULL DEFAULT 'active',
            type ENUM('admin', 'user', 'metrics') NOT NULL DEFAULT 'user',
            auth_token varchar(255),
            auth_type enum('password') NOT NULL DEFAULT 'password',
            UNIQUE KEY (nickname)))",
    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version
             << " on database " << config_.db.database;

    auto cfg = config_.db;
    cfg.max_connections = 1;

    mysqlpool::Mysqlpool db{ctx_, cfg};

    //co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        co_await db.init();
        {
            auto handle = co_await db.getConnection();
            auto trx = co_await handle.transaction();

            // Here we will run all SQL queries for upgrading from the specified version to the current version.
            auto relevant = ranges::drop_view(versions, version);
            for(string_view query : relevant | std::views::join) {
                co_await handle.exec(query);
            }


            co_await handle.exec("UPDATE signup SET VERSION = ? WHERE id = 1", latest_version);
            co_await trx.commit();
        }
        co_await db.close();

    //}, asio::use_awaitable);
}

boost::asio::awaitable<void> nextapp::Server::createAdminUser()
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
        "INSERT INTO user (id, nickname, type, auth_token) VALUES (?, ?, ?, ?)",
        id, admin, "admin", hash);
}

boost::asio::awaitable<void> nextapp::Server::createDefaultNextappInstance()
{
    if (config_.grpc_nextapp.ca_cert.empty()
        || config_.grpc_nextapp.address.empty()
        || config_.grpc_nextapp.server_key.empty()) {
        LOG_INFO << "Nextapp instance is not configured. Skipping...";
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

