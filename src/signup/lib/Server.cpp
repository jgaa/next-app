
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

    db_.emplace(ctx_, config().db);

    assert(!grpc_service_);
    grpc_service_ = make_shared<GrpcServer>(*this);
}

void Server::run()
{
    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            try {
                co_await db().init();
                co_await checkDb();
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
            instance->free_slots = toIntIfValue(row, SERVER_ID);

            if (auto it = cluster->regions_.find(region); it != cluster->regions_.end()) {
                auto& region = it->second;
                const auto id = instance->uuid;
                it->second.instances_.emplace(id, std::move(instance));
            } else {
                LOG_ERROR << "Instance " << instance->uuid << " references unknown region " << region;
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
    grpc_service_->start();
    co_return;
}


// This creates and connects to all active instances
boost::asio::awaitable<void> Server::connectToInstances()
{
    const auto max_wait = chrono::steady_clock::now() + chrono::seconds(config().options.max_retry_time_to_nextapp_secs);
    assert(grpc_service_); // Must be initialized
    while(!is_done()) {
        if (auto cluster = cluster_.load()) {
            auto num_instances = 0u;
            auto num_connected = 0u;

            for(auto& [_, region] : cluster->regions_) {
                LOG_INFO << "Connecting to instances in region " << region.name;
                num_instances += region.instances_.size();
                for(auto& [_, instance] : region.instances_) {
                    if (instance->is_online) {
                        ++num_connected;
                        continue;
                    }

                    if (instance->state == Cluster::Region::Instance::State::ACTIVE) {
                        LOG_INFO << "  -- Connecting to instance " << instance->uuid << " at " << instance->url;
                        GrpcServer::InstanceCommn::InstanceInfo i;
                        i.url = instance->url;
                        i.x509_ca_cert = instance->x509_ca_cert;
                        i.x509_cert = instance->x509_cert;
                        i.x509_key = instance->x509_key;
                        auto info = co_await grpc_service_->connectToInstance(instance->uuid, i);
                        if (info) {
                            instance->is_online = true;
                            ++num_connected;

                            // Is this the first time we connect to this instance?
                            if (instance->server_id.empty()) {
                                if (auto server_id = getValueFromKey(info->properties(), "server-id")) {
                                    instance->server_id = *server_id;
                                    co_await initializeInstance(*instance);
                                } else {
                                    LOG_ERROR << "Failed to get server-id from the instance " << instance->uuid;
                                }
                            }

                        }
                        // TODO: Schedule retry if not online
                        // TODO: Update the online status from grpc server if it loose the connection
                    }
                }
            }

            if (num_instances == num_connected) {
                LOG_INFO << "All instances are connected.";
                break;
            }

            if (chrono::steady_clock::now() > max_wait) {
                LOG_ERROR << "Failed to connect to all instances in time."
                          << ". Connected to " << num_connected << " out of " << num_instances;
                break;
            };

            LOG_INFO << "Connected to " << num_connected << " out of " << num_instances << " instances."
                     << " Retrying in " << config().options.retry_connect_to_nextappd_secs << " seconds.";

            auto timer = asio::steady_timer(ctx_, chrono::seconds(config().options.retry_connect_to_nextappd_secs));
            co_await timer.async_wait(asio::use_awaitable);
        }
    }
}

boost::asio::awaitable<void> Server::initializeInstance(Cluster::Region::Instance &instance)
{
    // TODO: Do initial sync to get any tenants and users
    // auto conn = grpc_service_->getInstance(instance.uuid);
    // if (!conn) {
    //     throw runtime_error("Failed to get connection to the instance " + to_string(instance.uuid));
    // }

    co_await db().exec("UPDATE instance SET server_id = ? WHERE id = ?", instance.server_id, instance.uuid);

    co_return;
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
    LOG_TRACE_N << "Checking the database version...";
    auto res = co_await db_->exec("SELECT version, salt FROM signup where id = 1");

    enum Cols { VERSION, SALT };

    if (res.has_value()) {
        const auto version = res.rows().front().at(VERSION).as_int64();
        salt_ = res.rows().front().at(SALT).as_string();
        if (salt_.empty()) {
            LOG_ERROR << "Salt is empty in the 'signup' table.!";
            co_return false;
        }
        LOG_DEBUG << "I need the database to be at version " << latest_version
                  << ". The existing database is at version " << version << '.';

        if (latest_version > version) {
            co_await upgradeDbTables(version);
            co_return true;
        }
        co_return version == latest_version;
    }

    co_return false;
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

            if (latest_version == 1) {
                auto salt = getRandomStr(32);
                co_await handle.exec("INSERT INTO signup (id, version, salt) VALUES (1, 1, ?)", salt);
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
        "INSERT INTO localuser (id, nickname, kind, auth_token) VALUES (?, ?, ?, ?)",
        id, admin, "admin", hash);
}

boost::asio::awaitable<void> nextapp::Server::createDefaultNextappInstance()
{
    if (config_.grpc_nextapp.ca_cert.empty()
        || config_.grpc_nextapp.address.empty()
        || config_.grpc_nextapp.server_key.empty()
        || config_.grpc_nextapp.server_cert.empty()) {
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

boost::asio::awaitable<std::optional<boost::uuids::uuid> > nextapp::Server::getRegionFromUserEmail(std::string_view email)
{
    auto hash = getEmailHash(email);;

    auto conn = co_await db().getConnection();
    auto res = co_await conn.exec("SELECT t.region FROM tenant t JOIN user u ON u.tenant = t.id WHERE u.email_hash = ?", hash);
    if (res.rows().empty()) {
        co_return std::nullopt;
    }

    co_return toUuid(res.rows().front()[0].as_string());
}

boost::asio::awaitable<std::optional<boost::uuids::uuid>> nextapp::Server::getInstanceFromUserEmail(std::string_view email) {
    auto hash = getEmailHash(email);;

    auto conn = co_await db().getConnection();
    auto res = co_await conn.exec("SELECT t.instance FROM tenant t JOIN user u ON u.tenant = t.id WHERE u.email_hash = ?", hash);
    if (res.rows().empty()) {
        co_return std::nullopt;
    }

    co_return toUuid(res.rows().front()[0].as_string());
}

string nextapp::Server::getEmailHash(std::string_view email) const
{
    return  sha256(format("{}:{}", toLower(email), salt_), true);
}
