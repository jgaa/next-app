
#include <algorithm>
#include <format>
#include <span>
#include <ranges>
#include <fcntl.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_with_diagnostics.hpp>
#include <boost/mysql/handshake_params.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/tcp.hpp>
#include <boost/mysql/throw_on_error.hpp>
#include <boost/mysql.hpp>

#include "nextapp/Server.h"
#include "nextapp/GrpcServer.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"

using namespace std;
using namespace jgaa;
using nextapp::logging::LogEvent;
namespace asio = boost::asio;
using jgaa::mysqlpool::tuple_awaitable;

namespace nextapp {

Server::Server(const Config& config)
    : config_(config), metrics_(*this)
{
}

Server::~Server()
{

}

Server * Server::instance_;

void Server::init()
{
    handleSignals();
    initCtx(config().svr.io_threads);

    assert(!instance_);
    instance_ = this;

    auto res = asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        co_await initDb();
    }, boost::asio::use_future);

    res.get();
}

boost::asio::awaitable<void> Server::initDb() {
    if (!db_) {
        db_.emplace(ctx_, config().db);
        co_await db_->init();
    }
}

void Server::run()
{
    assert(db_);
    try {
        asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            if (!co_await checkDb()) {
                LOG_ERROR << "The database version is wrong. Please upgrade before starting the server.";
                throw std::runtime_error("Database version is wrong");
            }

            co_await loadServerId();
            co_await loadCertAuthority();
            co_await startGrpcService();
            co_await prepareMetricsAuth();
            co_await onMetricsTimer();
        }, boost::asio::use_future).get();
    } catch (const std::exception& ex) {
        LOG_ERROR << "Caught exception during initialization: " << ex.what();
        stop();
        throw runtime_error{"Startup failed"};
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
                    const auto hash = hashPassword(base64);
                    if (hash == metrics_auth_hash_) {
                        LOG_TRACE << "Metrics request " << ar.req.uuid << " authenticated";
                        return yahat::Auth{"metrics", true};
                    }
                }
            }
            return yahat::Auth{"", false};
        }, metrics_.metrics(), "nextapp "s + NEXTAPP_VERSION);

        http_server_->start();
    }

    startMetricsTimer();

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
        LOG_DEBUG << "Closing database connections...";
        try {
            asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
                co_await db_->close();
            }, asio::use_future).get();
        } catch (const exception& ex) {
            LOG_WARN << "Caught exception while closing the database handles: " << ex.what();
        }
        LOG_DEBUG << "Database connections closed.";
    }
    LOG_DEBUG << "Shutting down the thread-pool.";
    ctx_.stop();
    LOG_DEBUG << "Server stopped.";
}

void Server::bootstrap(const BootstrapOptions& opts)
{
    LOG_INFO << "Bootstrapping the system...";

    auto res = asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        ScopedExit se{[this] {
            ctx_.stop();
        }};

        co_await createDb(opts);
        co_await initDb();
        co_await upgradeDbTables(0);
        co_await recreateServerCert(config().options.server_cert_dns_names);
    }, boost::asio::use_future);

    ctx_.run();
    res.get();
    LOG_INFO << "Bootstrapping is complete";
}

void Server::createGrpcCert()
{
    auto res = asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        ScopedExit se{[this] {
            ctx_.stop();
        }};
        co_await initDb();
        co_await recreateServerCert(config().options.server_cert_dns_names);
    }, boost::asio::use_future);

    ctx_.run();
    res.get();
}

void Server::createClientCert(const std::string &fileName, boost::uuids::uuid user)
{
    auto res = asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        ScopedExit se{[this] {
            ctx_.stop();
        }};

        co_await initDb();

        bool is_admin = false;
        if (user.is_nil()) {
            user = co_await getAdminUserId();
            LOG_DEBUG << "Using admin user id: " << user;
            is_admin = true;
        }

        filesystem::path cert_name = fileName + "-cert.pem";
        filesystem::path key_name = fileName + "-key.pem";
        filesystem::path ca_name = fileName + "-ca.pem";

        if (cert_name.has_parent_path() && !filesystem::exists(cert_name.parent_path())) {
            LOG_INFO << "Creating directories for cert files: " << cert_name.parent_path();
            filesystem::create_directories(cert_name.parent_path());
        }

        std::ofstream cert_file(cert_name);
        std::ofstream key_file(key_name);
        std::ofstream ca_file(ca_name);

        if (!cert_file.is_open() || !key_file.is_open()) {
            throw std::runtime_error("Failed to open cert or key file for writing");
        }
        if (!ca_file.is_open()) {
            throw std::runtime_error("Failed to open ca file for writing");
        }

        co_await loadCertAuthority();
        auto subject = newUuidStr();
        auto cert = ca().createClientCert(subject, to_string(user));
        co_await db().exec("INSERT INTO device (id, user, certHash, name) VALUES (?, ?, ?, '')",
                           subject, to_string(user), cert.hash);

        cert_file << cert.cert;
        key_file << cert.key;
        ca_file << ca().rootCert();

        LOG_INFO << "Created client cert for "
                 << (is_admin ? "**ADMIN**" : "")
                 << " user " << user << " with 'deviceid' " << subject
                 << " and saved the cert to " << cert_name << " and the key to " << key_name
                 << ". The CA cert is saved to " << ca_name;

    }, boost::asio::use_future);

    ctx_.run();
    res.get();
}

boost::asio::awaitable<CertData> Server::getCert(std::string_view id, WithMissingCert what)
{
    enum Cols {
        CERT,
        KEY
    };

    auto conn = co_await db().getConnection();
    auto trx = co_await conn.transaction(false, false);

    // See if we have a CA cert in the db.
    auto res = co_await conn.exec("SELECT cert, pkey FROM cert WHERE id=?", id);
    assert(!res.empty());
    CertData cd;
    if (res.rows().empty()) {
        switch (what) {
        case WithMissingCert::FAIL:
            co_return cd;
        case WithMissingCert::CREATE_SERVER: {
            auto res = co_await conn.exec("SELECT value FROM config WHERE name='grpc-fqdns'");
            if (res.rows().empty() || res.rows().front().empty()) {
                throw std::runtime_error("No FQDNs configured in the db nextapp.config:'grpc-fqdns'");
            }
            const auto fqdns = res.rows().front().at(0).as_string();
            const auto fqdns_list = split(fqdns, ',');

            LOG_INFO << "Creating server cert for " << fqdns;
            cd = ca().createServerCert(fqdns_list);
            } break;
        default:
            break;
        }

        assert(!id.empty());
        assert(!cd.cert.empty());
        assert(!cd.key.empty());

        co_await conn.exec("INSERT INTO cert (id, cert, pkey) VALUES (?, ?, ?)", id, cd.cert, cd.key);

    } else {
        cd.cert = res.rows().at(0).at(CERT).as_string();
        cd.key = res.rows().at(0).at(KEY).as_string();
    }

    co_await trx.commit();

    assert(!cd.cert.empty());
    assert(!cd.key.empty());

    co_return cd;
}

boost::asio::awaitable<boost::uuids::uuid> Server::getAdminUserId()
{
    co_await initDb();
    auto res = co_await db().exec("SELECT id FROM user WHERE kind='super' AND active=1 AND system_user=1 LIMIT 1");
    if (res.has_value() && !res.rows().empty()) {
        co_return toUuid(res.rows().front().front().as_string());
    }

    throw std::runtime_error("No active system/super-user found");
}

boost::asio::awaitable<void> Server::onMetricsTimer()
{
    if (config().options.metrics_timer_minutes == 0) {
        LOG_TRACE_N << "Metrics timer is disabled, skipping metrics update.";
        co_return;
    }

    auto conn = co_await db().getConnection();

    auto res = co_await conn.exec("SELECT COUNT(*) FROM user WHERE active=1");
    if (res.has_value() && !res.rows().empty()) {
        metrics().users().set(res.rows().front().front().as_int64());
    } else {
        LOG_WARN_N << "Failed to get the number of active users from the database, or there are no uses.";
    }

    res = co_await conn.exec("SELECT COUNT(*) FROM tenant WHERE state='active'");
    if (res.has_value() && !res.rows().empty()) {
        metrics().tenants().set(res.rows().front().front().as_int64());
    } else {
        LOG_WARN_N << "Failed to get the number of active tenants from the database, or there are no tenants.";
    }

    // device
    res = co_await conn.exec("SELECT COUNT(*) FROM device WHERE enabled=1");
    if (res.has_value() && !res.rows().empty()) {
        metrics().devices().set(res.rows().front().front().as_int64());
    } else {
        LOG_WARN_N << "Failed to get the number of active devices from the database, or there are no devices.";
    }
}

void Server::startMetricsTimer()
{
    if (config().options.metrics_timer_minutes == 0) {
        LOG_DEBUG << "Metrics timer is disabled.";
        return;
    }

    LOG_DEBUG << "Starting metrics timer with interval of "
              << config().options.metrics_timer_minutes << " minutes.";

    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        auto scope = metrics().asio_worker_threads().scoped();
        while (!ctx_.stopped()) {
            co_await asio::steady_timer{ctx_,
                std::chrono::minutes(config().options.metrics_timer_minutes)
            }.async_wait(asio::use_awaitable);

            try {
                co_await onMetricsTimer();
            } catch (const std::exception& ex) {
                LOG_WARN_N << "Caught exception during metrics timer: " << ex.what();
            }
        }
    }, asio::detached);
}

string Server::hashPassword(std::string_view passwd)
{
    assert(!server_id_.empty());
    const auto base = format("{}:{}", server_id_, passwd);
    return sha256(base, true);
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

boost::asio::awaitable<bool> Server::checkDb()
{
    LOG_TRACE_N << "Checking the database version...";
    auto res = co_await db_->exec("SELECT version FROM nextapp");
    if (res.has_value()) {
        const auto version = res.rows().front().front().as_int64();
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

boost::asio::awaitable<void> Server::createDb(const BootstrapOptions& opts)
{
    LOG_INFO << "Creating the database " << config_.db.database;

    // Create the database.
    auto cfg = config_.db;
    cfg.database = "mysql";
    cfg.username = opts.db_root_user;
    cfg.password = opts.db_root_passwd;
    cfg.max_connections = 1;
    cfg.timer_interval_ms = 0; // disable

    mysqlpool::Mysqlpool db{ctx_, cfg};

    co_await db.init();

    if (opts.drop_old_db) {
        LOG_INFO << "Dropping database " << config_.db.database ;

        try {
            co_await db.exec(format("REVOKE ALL PRIVILEGES ON {}.* FROM `{}`@`%`",
                                    config_.db.database, config_.db.username));
        } catch (const exception&) {};

        try {
            co_await db.exec(format("DROP USER '{}'@'%'",
                                    config_.db.username));
        } catch (const exception&) {};

        co_await db.exec("FLUSH PRIVILEGES");
        co_await db.exec(format("DROP DATABASE {}", config_.db.database));
    }

    {
        auto res = co_await db.exec(format("SELECT SCHEMA_NAME FROM information_schema.schemata WHERE SCHEMA_NAME = '{}'", config_.db.database));
        if (!res.rows().empty()) {
            LOG_INFO_N << "Database " << config_.db.database << " already exists. Skipping creation.";
            throw runtime_error{"Database already exist."};
        }
    }

    LOG_INFO_N << "Creating database " << config_.db.database;
    co_await db.exec(format("CREATE DATABASE {} CHARACTER SET = 'utf8'", config_.db.database));

    LOG_TRACE_N << "Creating database user " << config_.db.username;
    co_await db.exec(format("CREATE USER '{}'@'%' IDENTIFIED BY '{}'",
                            config_.db.username, config_.db.password));

    co_await db.exec(format("GRANT ALL PRIVILEGES ON {}.* TO '{}'@'%'",
                            config_.db.database, config_.db.username));

    co_await db.exec("FLUSH PRIVILEGES");

    co_await db.close();
}

boost::asio::awaitable<void> Server::upgradeDbTables(uint version)
{
    static constexpr auto v1_bootstrap = to_array<string_view>({
        "CREATE TABLE nextapp (id INTEGER NOT NULL, version INTEGER NOT NULL, serverid VARCHAR(37) NOT NULL DEFAULT UUID()) ",

        "INSERT INTO nextapp (id, version) values(1, 0)",

        R"(CREATE TABLE tenant (
              id UUID not NULL default UUID() PRIMARY KEY,
              name VARCHAR(128) NOT NULL,
              kind ENUM('super', 'regular') NOT NULL DEFAULT 'regular',
              descr TEXT,
              active TINYINT(1) NOT NULL DEFAULT 1,
              system_tenant TINYINT(1)))",

        R"(CREATE TABLE user (
              id UUID not NULL default UUID() PRIMARY KEY,
              tenant UUID NOT NULL,
              name VARCHAR(128) NOT NULL,
              kind ENUM('super', 'regular', 'guest') NOT NULL DEFAULT 'regular',
              descr TEXT,
              active TINYINT(1) NOT NULL DEFAULT 1,
              system_user TINYINT(1),
        FOREIGN KEY(tenant) REFERENCES tenant(id)))",

        R"(CREATE TABLE node (
              id UUID not NULL default UUID() PRIMARY KEY,
              user UUID NOT NULL,
              name VARCHAR(128) NOT NULL,
              kind INTEGER NOT NULL DEFAULT 0,
              status INTEGER NOT NULL DEFAULT 0,
              descr TEXT,
              active INTEGER NOT NULL DEFAULT 1,
              parent UUID,
        FOREIGN KEY(parent) REFERENCES node(id),
        FOREIGN KEY(user) REFERENCES user(id)))",

        R"(CREATE TABLE work(
              id UUID not NULL default UUID() PRIMARY KEY,
              node UUID NOT NULL,
              status INTEGER NOT NULL DEFAULT 0,
              start DATETIME NOT NULL,
              end DATETIME NOT NULL,
              used INTEGER NOT NULL,
              paused INTEGER NOT NULL DEFAULT 0,
              name TEXT NOT NULL,
              note TEXT,
          FOREIGN KEY(node) REFERENCES node(id)))",

        // If tenant is NULL, the row is a system-defined color
        R"(CREATE TABLE day_colors(
              id UUID not NULL default UUID() PRIMARY KEY,
              tenant UUID,
              score INTEGER NOT NULL DEFAULT 0,
              color varchar(32) NOT NULL,
              name varchar(255) NOT NULL,
          FOREIGN KEY(tenant) REFERENCES tenant(id)))",

        // color: see QML colors at https://doc.qt.io/qt-6/qml-color.html
        R"(INSERT INTO day_colors (id, name, color, score) VALUES
            ('965864e2-a95d-11ee-960b-87185e67f3da', 'Slow day', 'khaki', 2),
            ('a2547e0c-a95d-11ee-ad7b-f75675544037', 'Green Day', 'greenyellow', 3),
            ('b6abb366-a95d-11ee-b079-ef6165b96e9b', 'Awsome Day!', 'limegreen', 5),
            ('bb8aee74-a95d-11ee-b235-7f42126afd6d', 'Hollyday / Vacation / Day off', 'skyblue', 0),
            ('c0f7cb16-a95d-11ee-9da5-b3f4aed7f930', 'Failed Day', 'orange', 0),
            ('c5dfe53c-a95d-11ee-9465-73e10d6c4ad9', 'Disastorous Day!', 'fuchsia', 0),
            ('ca8a5edc-a95d-11ee-bae3-7b924c4b0414', 'Sick', 'lightseagreen', 0))",

        R"(CREATE TABLE day (
              date DATE NOT NULL DEFAULT CURDATE(),
              user UUID NOT NULL,
              color UUID,
              notes TEXT,
              report TEXT,
        PRIMARY KEY (user, date),
        FOREIGN KEY(color) REFERENCES day_colors(id),
        FOREIGN KEY(user) REFERENCES user(id)))",
    });

    static constexpr auto v2_upgrade = to_array<string_view>({
        "ALTER TABLE tenant ADD COLUMN properties JSON",
        "ALTER TABLE user ADD COLUMN email varchar(255) NOT NULL default 'jgaa@jgaa.com'",
        "ALTER TABLE user ADD COLUMN properties JSON",
        "ALTER TABLE node ADD COLUMN version INT NOT NULL DEFAULT 1",
        "UPDATE day_colors SET color = 'hotpink' WHERE id = 'c0f7cb16-a95d-11ee-9da5-b3f4aed7f930'",
        "UPDATE day_colors SET color = 'fuchsia' WHERE id = 'c5dfe53c-a95d-11ee-9465-73e10d6c4ad9'",
        "UPDATE day_colors SET color = 'yellow' WHERE id = '965864e2-a95d-11ee-960b-87185e67f3da'",
        "UPDATE day_colors SET color = 'orangered' WHERE id = 'ca8a5edc-a95d-11ee-bae3-7b924c4b0414'",
        "CREATE UNIQUE INDEX ix_tenant_name ON tenant(name)",
        "CREATE UNIQUE INDEX ix_user_email ON user(email)",
        "ALTER TABLE tenant CHANGE kind kind ENUM('super', 'regular', 'guest') NOT NULL DEFAULT 'guest'",
        "ALTER TABLE node DROP CONSTRAINT IF EXISTS node_ibfk_1",
        R"(ALTER TABLE node ADD CONSTRAINT node_parent_fk
            FOREIGN KEY(parent) REFERENCES node(id) ON DELETE CASCADE ON UPDATE RESTRICT)"
    });

    static constexpr array<string_view, 0> v3_upgrade;

    static constexpr auto v4_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(DROP TABLE IF EXISTS action2location)",

        R"(CREATE OR REPLACE TABLE location (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            name TEXT NOT NULL,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE OR REPLACE TABLE action (
            id UUID not NULL default UUID() PRIMARY KEY,
            node UUID NOT NULL,
            user UUID NOT NULL,
            origin UUID,
            priority ENUM ('pri_critical', 'pri_very_impornant', 'pri_higher', 'pri_high', 'pri_normal', 'pri_medium', 'pri_low', 'pri_insignificant') NOT NULL DEFAULT ('pri_normal'),
            status ENUM ('active', 'done', 'onhold') NOT NULL DEFAULT 'active',
            favorite BOOLEAN NOT NULL DEFAULT FALSE,
            name VARCHAR(128) NOT NULL,
            descr TEXT,
            created_date TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            due_kind ENUM('datetime', 'date', 'week', 'month', 'quarter', 'year', 'unset') NOT NULL DEFAULT 'unset',
            start_time DATETIME,
            due_by_time DATETIME,
            due_timezone VARCHAR(64),
            completed_time TIMESTAMP,
            time_estimate INTEGER,
            difficulty ENUM('trivial', 'easy', 'normal', 'hard', 'veryhard', 'inspired') NOT NULL DEFAULT 'normal',
            repeat_kind ENUM('never','completed', 'start_time', 'due_time'),
            repeat_unit ENUM('days', 'weeks', 'months', 'years'),
            repeat_when ENUM('at_date', 'at_dayspec'),
            repeat_after INTEGER,
            version INT NOT NULL DEFAULT 1,
        FOREIGN KEY(node) REFERENCES node(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY(origin) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX action_ix2 ON action (user, status, start_time, due_by_time))",
        R"(CREATE INDEX action_ix3 ON action (origin))",
        R"(CREATE INDEX action_ix4 ON action (node, status, start_time, due_by_time))",
        R"(CREATE INDEX action_ix5 ON action (user, status, favorite))",

        R"(CREATE OR REPLACE TABLE action2location (
            action UUID NOT NULL,
            location UUID NOT NULL,
            PRIMARY KEY (action, location),
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(location) REFERENCES location(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX action2location_ix2 ON action2location (location, action))",

        R"(CREATE OR REPLACE TABLE work_session (
            id UUID not NULL default UUID() PRIMARY KEY,
            action UUID NOT NULL,
            user UUID NOT NULL,
            state ENUM('active', 'paused', 'done') NOT NULL DEFAULT 'active',
            version INT NOT NULL DEFAULT 1,
            touch_time TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            start_time TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            end_time TIMESTAMP,
            duration INTEGER NOT NULL DEFAULT 0,
            paused INTEGER NOT NULL DEFAULT 0,
            name VARCHAR(256) NOT NULL DEFAULT '',
            note TEXT,
            events BLOB, -- The events for the session saved as a protobuf message
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX work_session_ix1 ON work_session (user, action, state, start_time))",
        R"(CREATE INDEX work_session_ix2 ON work_session (user, start_time, end_time))",
        R"(CREATE INDEX work_session_ix3 ON work_session (user, state, start_time, end_time))",
        R"(CREATE INDEX work_session_ix4 ON work_session (user, state, touch_time))",
        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v5_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",
        R"(CREATE OR REPLACE TABLE user_settings (
            user UUID not NULL default UUID() PRIMARY KEY,
            settings BLOB, -- The settings saved as a protobuf message
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v6_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE OR REPLACE TABLE action_category (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            name VARCHAR(128) NOT NULL,
            descr TEXT,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX action_category_ix1 ON action_category (user, name))",

        R"(ALTER TABLE action
            ADD COLUMN IF NOT EXISTS category UUID,
            ADD CONSTRAINT action_ibfk_category
                FOREIGN KEY IF NOT EXISTS (category) REFERENCES action_category(id) ON DELETE SET NULL ON UPDATE RESTRICT
        )",

        R"(CREATE OR REPLACE TABLE time_block (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            start_time DATETIME NOT NULL,
            end_time DATETIME NOT NULL,
            kind ENUM ('reservation', 'actions') NOT NULL DEFAULT 'reservation',
            category UUID,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(category) REFERENCES action_category(id) ON DELETE CASCADE ON UPDATE RESTRICT
        ))",

        R"(CREATE INDEX time_block_ix1 ON time_block (user, start_time, end_time))",

        R"(CREATE OR REPLACE TABLE time_block_actions (
            time_block UUID NOT NULL,
            action UUID NOT NULL,
            PRIMARY KEY (time_block, action),
            FOREIGN KEY(time_block) REFERENCES time_block(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT
        ))",

        R"(CREATE INDEX time_block_actions_ix1 ON time_block_actions (action))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v7_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE OR REPLACE TABLE action_category (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            name VARCHAR(128) NOT NULL,
            color VARCHAR(32) NOT NULL DEFAULT 'blue',
            descr TEXT,
            version INT NOT NULL DEFAULT 1,
            icon VARCHAR(128),
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX action_category_ix1 ON action_category (user, name))",

        "DROP TRIGGER IF EXISTS tr_before_update_action_category",

        R"(CREATE TRIGGER tr_before_update_action_category
          BEFORE UPDATE ON action_category
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
          END)",

        R"(CREATE OR REPLACE TABLE time_block (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            name VARCHAR(128) NOT NULL DEFAULT '',
            start_time DATETIME NOT NULL,
            end_time DATETIME NOT NULL,
            kind ENUM ('reservation', 'actions') NOT NULL DEFAULT 'reservation',
            category UUID,
            version INT NOT NULL DEFAULT 1,
            actions BLOB, -- repeatable string, saved as a protobuf message
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(category) REFERENCES action_category(id) ON DELETE RESTRICT ON UPDATE RESTRICT
        ))",

        R"(CREATE INDEX time_block_ix1 ON time_block (user, start_time, end_time))",

        "DROP TRIGGER IF EXISTS tr_before_update_time_block",

        R"(CREATE TRIGGER tr_before_update_time_block
          BEFORE UPDATE ON time_block
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
          END)",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v8_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE user_settings ADD COLUMN IF NOT EXISTS version INT NOT NULL DEFAULT 1",

        "UPDATE user_settings SET version = 1 WHERE version is NULL",

        "DROP TRIGGER IF EXISTS tr_before_update_user_settings",

        R"(CREATE TRIGGER tr_before_update_user_settings
          BEFORE UPDATE ON user_settings
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
          END)",

        R"(CREATE OR REPLACE TABLE notification (
            user UUID not NULL default UUID() PRIMARY KEY,
            device UUID,
            is_read BOOLEAN NOT NULL DEFAULT FALSE,
            created TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            content BLOB, -- The notification saved as a protobuf message
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        "CREATE INDEX notification_ix1 ON notification (user, created, is_read)",

        R"(CREATE OR REPLACE TABLE cert (
            id VARCHAR(42) NOT NULL PRIMARY KEY,
            created TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            expires TIMESTAMP,
            cert TEXT,
            pkey TEXT))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v9_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE tenant ADD COLUMN IF NOT EXISTS state ENUM('pending_activation', 'active', 'suspended') "
          "NOT NULL DEFAULT 'pending_activation'",

        "ALTER TABLE tenant DROP COLUMN IF EXISTS active",

        "UPDATE tenant SET state = 'active'",

        R"(CREATE OR REPLACE TABLE device (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            name VARCHAR(256) NOT NULL,
            created TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            hostName VARCHAR(256),
            os VARCHAR(128),
            osVersion VARCHAR(32),
            appVersion VARCHAR(32),
            productType VARCHAR(32),
            productVersion VARCHAR(32),
            arch VARCHAR(32),
            prettyName VARCHAR(256),
            certHash BLOB,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        "CREATE INDEX device_ix1 ON device (user, created)",

        // Hash is from string(id) + / + email + / + otp
        R"(CREATE OR REPLACE TABLE otp (
            id UUID not NULL default UUID() PRIMARY KEY,
            user UUID NOT NULL,
            otp_hash VARCHAR(256) NOT NULL,
            email VARCHAR(256) NOT NULL,
            kind ENUM ('new_device', 'new_user') NOT NULL,
            created TIMESTAMP NOT NULL DEFAULT UTC_TIMESTAMP,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        "CREATE INDEX otp_ix1 ON otp (user)",
        "CREATE INDEX otp_ix2 ON otp (email)",

        "ALTER TABLE user DROP FOREIGN KEY user_ibfk_1",
        "ALTER TABLE user ADD CONSTRAINT user_ibfk_1 FOREIGN KEY (tenant) REFERENCES tenant(id) ON DELETE CASCADE ON UPDATE RESTRICT",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v10_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE work_session MODIFY start_time TIMESTAMP DEFAULT NULL",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v11_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE day ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        "CREATE INDEX day_updated_ix ON day (user, updated)",
        "ALTER TABLE day ADD COLUMN IF NOT EXISTS deleted SMALLINT NOT NULL DEFAULT 0",
        "UPDATE day SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",
        "UPDATE day SET deleted = 0 WHERE deleted IS NULL",
        "ALTER TABLE day DROP FOREIGN KEY day_ibfk_1",
        "ALTER TABLE day DROP FOREIGN KEY day_ibfk_2",
        "ALTER TABLE day ADD CONSTRAINT day_ibfk_1 FOREIGN KEY (color) REFERENCES day_colors(id) ON DELETE SET NULL ON UPDATE RESTRICT",
        "ALTER TABLE day ADD CONSTRAINT day_ibfk_2 FOREIGN KEY (user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT",
        R"(CREATE TRIGGER updated_day_timestamp
            BEFORE UPDATE ON day
            FOR EACH ROW
            BEGIN
                SET NEW.updated = UTC_TIMESTAMP(6);
            END)",
        "ALTER TABLE day_colors ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        "CREATE INDEX day_colors_ix1 ON day_colors (tenant, updated)",
        "UPDATE day_colors SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",
        R"(CREATE TRIGGER updated_day_colors_timestamp
            BEFORE UPDATE ON day_colors
            FOR EACH ROW
            BEGIN
                SET NEW.updated = UTC_TIMESTAMP(6);
            END)",

        "ALTER TABLE node ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        "ALTER TABLE node ADD COLUMN IF NOT EXISTS deleted SMALLINT NOT NULL DEFAULT 0",
        "CREATE INDEX node_ix_updated ON node (user, updated)",
        "UPDATE node SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",
        "UPDATE node SET deleted = 0 WHERE deleted IS NULL",
        R"(CREATE TRIGGER tr_before_update_node
          BEFORE UPDATE ON node
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
            SET NEW.updated = UTC_TIMESTAMP(6);
          END)",

        "ALTER TABLE action ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        "ALTER TABLE action ADD COLUMN IF NOT EXISTS deleted SMALLINT NOT NULL DEFAULT 0",
        "CREATE INDEX action_ix_updated ON action (user, updated)",
        "UPDATE action SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",
        "UPDATE action SET deleted = 0 WHERE deleted IS NULL",
        R"(CREATE TRIGGER tr_before_update_action
          BEFORE UPDATE ON action
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
            SET NEW.updated = UTC_TIMESTAMP(6);
          END)",

        "ALTER TABLE action_category ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        "ALTER TABLE action_category ADD COLUMN IF NOT EXISTS deleted SMALLINT NOT NULL DEFAULT 0",
        "CREATE INDEX action_category_ix_updated ON action_category (user, updated)",
        "UPDATE action_category SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",
        "UPDATE action_category SET deleted = 0 WHERE deleted IS NULL",
        "DROP TRIGGER IF EXISTS tr_before_update_action_category",
        R"(CREATE TRIGGER tr_before_update_action_category
          BEFORE UPDATE ON action_category
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
            SET NEW.updated = UTC_TIMESTAMP(6);
          END)",

        R"(CREATE TABLE IF NOT EXISTS deleted (
            id UUID NOT NULL PRIMARY KEY,
            user UUID NOT NULL,
            kind enum ('action', 'action_category', 'node', 'day', 'day_colors') NOT NULL,
            deleted TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6),
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE TABLE IF NOT EXISTS versions (
            user UUID NOT NULL,
            kind ENUM ('action_category', 'settings', 'locations') NOT NULL,
            version INT NOT NULL DEFAULT 1,
            PRIMARY KEY (user, kind),
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE TRIGGER update_action_category_version
            AFTER UPDATE ON action_category
            FOR EACH ROW
            BEGIN
                -- Check if a version entry for this user and kind 'action_category' already exists
                IF EXISTS (SELECT 1 FROM versions WHERE user = NEW.user AND kind = 'action_category') THEN
                    -- If the entry exists, increment the version
                    UPDATE versions
                    SET version = version + 1
                    WHERE user = NEW.user AND kind = 'action_category';
                ELSE
                    -- If the entry does not exist, insert a new row with version 1
                    INSERT INTO versions (user, kind, version)
                    VALUES (NEW.user, 'action_category', 1);
                END IF;
            END)",

        R"(ALTER TABLE work_session
            MODIFY COLUMN state ENUM('active', 'paused', 'done', 'deleted') NOT NULL DEFAULT 'active';
        )",

        "ALTER TABLE work_session ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",

        "CREATE INDEX work_session_ix_updated ON work_session (user, updated)",

        R"(CREATE TRIGGER tr_before_update_work_session
          BEFORE UPDATE ON work_session
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
            SET NEW.updated = UTC_TIMESTAMP(6);
          END)",

        "UPDATE work_session SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",

        "ALTER TABLE time_block ADD COLUMN IF NOT EXISTS updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6)",
        R"(ALTER TABLE time_block
            MODIFY COLUMN kind ENUM('reservation','actions', 'deleted') NOT NULL DEFAULT 'reservation';
        )",

        "ALTER TABLE time_block MODIFY COLUMN start_time DATETIME NULL",
        "ALTER TABLE time_block MODIFY COLUMN end_time DATETIME NULL",

        "UPDATE time_block SET updated = UTC_TIMESTAMP(6) WHERE updated IS NULL",

        "DROP TRIGGER IF EXISTS tr_before_update_time_block",
        R"(CREATE TRIGGER tr_before_update_time_block
          BEFORE UPDATE ON time_block
          FOR EACH ROW
          BEGIN
            SET NEW.version = OLD.version + 1;
            SET NEW.updated = UTC_TIMESTAMP(6);
          END)",

        "CREATE INDEX time_block_ix_updated ON time_block (user, updated)",

        "ALTER TABLE action DROP FOREIGN KEY action_ibfk_1",
        "ALTER TABLE action DROP FOREIGN KEY action_ibfk_3",
        "ALTER TABLE work DROP FOREIGN KEY work_ibfk_1",
        "ALTER TABLE node DROP FOREIGN KEY node_parent_fk",
        "ALTER TABLE action2location DROP FOREIGN KEY action2location_ibfk_1",
        "ALTER TABLE work_session DROP FOREIGN KEY work_session_ibfk_1",

        // time_block_actions is not replicated. A simlar table is maintained locally by the client
        // based on the actions in the time_block.

        "ALTER TABLE action MODIFY COLUMN node UUID NULL",
        "ALTER TABLE action MODIFY name VARCHAR(256) NULL",

        R"(ALTER TABLE action
            MODIFY COLUMN status ENUM('active','done','onhold', 'deleted') NOT NULL DEFAULT 'active';
        )",

        "ALTER TABLE action DROP COLUMN deleted",

        "ALTER TABLE work_session MODIFY COLUMN action UUID NULL",
        "ALTER TABLE work_session MODIFY COLUMN name VARCHAR(256) NULL",

        "ALTER TABLE node MODIFY COLUMN name VARCHAR(256) NULL",

        R"(ALTER TABLE action
            ADD CONSTRAINT `action_ibfk_1` FOREIGN KEY (`node`) REFERENCES `node` (`id`) ON DELETE CASCADE ON UPDATE RESTRICT,
            ADD CONSTRAINT `action_ibfk_3` FOREIGN KEY (`origin`) REFERENCES `action` (`id`) ON DELETE CASCADE
        )",

        R"(ALTER TABLE work
            ADD CONSTRAINT `work_ibfk_1` FOREIGN KEY (`node`) REFERENCES `node` (`id`) ON DELETE CASCADE ON UPDATE RESTRICT
        )",

        R"(ALTER TABLE node
            ADD CONSTRAINT `node_parent_fk` FOREIGN KEY (`parent`) REFERENCES `node` (`id`) ON DELETE CASCADE ON UPDATE RESTRICT
        )",

        R"(ALTER TABLE action2location
            ADD CONSTRAINT `action2location_ibfk_1` FOREIGN KEY (`action`) REFERENCES `action` (`id`) ON DELETE CASCADE ON UPDATE RESTRICT
        )",

        R"(ALTER TABLE work_session
            ADD CONSTRAINT `work_session_ibfk_1` FOREIGN KEY (`action`) REFERENCES `action` (`id`) ON DELETE CASCADE ON UPDATE RESTRICT
        )",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v12_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(ALTER TABLE action MODIFY COLUMN due_kind
            ENUM('datetime', 'date', 'week', 'month', 'quarter', 'year', 'unset', 'span_hours', 'span_days')
            NOT NULL DEFAULT 'unset'
        )",
        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v13_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE device ADD COLUMN IF NOT EXISTS lastSeen TIMESTAMP",
        "ALTER TABLE device ADD COLUMN IF NOT EXISTS enabled TINYINT(1) NOT NULL DEFAULT TRUE",
        "ALTER TABLE device ADD COLUMN IF NOT EXISTS numSessions INT NOT NULL DEFAULT 0",
        "UPDATE device SET numSessions = 0",
        "UPDATE device SET enabled = TRUE",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v14_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE node ADD COLUMN IF NOT EXISTS exclude_from_wr TINYINT(1)",
        "ALTER TABLE node ADD COLUMN IF NOT EXISTS category UUID",
        R"(ALTER TABLE node
             ADD CONSTRAINT `node_category_fk` FOREIGN KEY (`category`) REFERENCES `action_category` (`id`) ON DELETE SET NULL ON UPDATE RESTRICT
        )",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v15_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE TABLE IF NOT EXISTS request_state  (
            userid UUID NOT NULL,
            devid UUID NOT NULL,
            instance INT NOT NULL,
            last_update TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            request_id INT NOT NULL DEFAULT 0,
            PRIMARY KEY (userid, devid, instance),
            CONSTRAINT fk_reqst_userid FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v16_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(ALTER TABLE tenant
            ADD COLUMN IF NOT EXISTS system_tenant TINYINT(1))",

        R"(ALTER TABLE user
            ADD COLUMN IF NOT EXISTS system_user TINYINT(1))",

        "UPDATE tenant set properties=NULL", // We are changing from json to protobuf binary format
        "UPDATE user set properties=NULL",
        "ALTER TABLE tenant MODIFY COLUMN properties BLOB",
        "ALTER TABLE user MODIFY COLUMN properties BLOB",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v17_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE TABLE IF NOT EXISTS config (
            name VARCHAR(128) NOT NULL PRIMARY KEY,
            value TEXT NOT NULL))",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v18_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(CREATE OR REPLACE TABLE notification (
            id INT NOT NULL AUTO_INCREMENT,
            created_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            updated TIMESTAMP(6) NOT NULL DEFAULT UTC_TIMESTAMP(6),
            valid_to TIMESTAMP NULL DEFAULT NULL,
            subject TEXT NOT NULL,
            message TEXT NULL,
            sender_type ENUM('admin','system','tenant','user') NOT NULL,
            sender_id VARCHAR(128) NULL,
            to_tenant UUID DEFAULT NULL,
            to_user UUID  DEFAULT NULL,
            uuid UUID NOT NULL DEFAULT UUID(),
            kind ENUM('info', 'warning', 'error', 'upgrade', 'outage', 'promotion', 'deleted') NOT NULL DEFAULT 'info',
            data TEXT DEFAULT NULL, -- depends on context; for example the latest version of the app
        PRIMARY KEY (id),
        UNIQUE KEY uniq_uuid (uuid),
        KEY idx_to_tenant_user (to_tenant, to_user),
        KEY idx_to_tenant_user_ts (updated, to_tenant, to_user),
        CONSTRAINT fk_notification_tenant FOREIGN KEY (to_tenant) REFERENCES tenant(id) ON DELETE CASCADE,
        CONSTRAINT fk_notification_user   FOREIGN KEY (to_user) REFERENCES user(id) ON DELETE CASCADE))",
        R"(CREATE TRIGGER updated_notification_timestamp
            BEFORE UPDATE ON notification
            FOR EACH ROW
            BEGIN
                SET NEW.updated = UTC_TIMESTAMP(6);
            END)",

        // We don't store each read notification, just the id of the last read notification for each user.
        R"(CREATE OR REPLACE TABLE notification_last_read (
            notification_id INT NOT NULL,
            user UUID NOT NULL,
            PRIMARY KEY (notification_id, user),
            KEY idx_user_notificatiion (user, notification_id),
            FOREIGN KEY(notification_id) REFERENCES notification(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        "SET FOREIGN_KEY_CHECKS=1"
    });


    static constexpr auto v19_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(ALTER TABLE action
            ADD COLUMN IF NOT EXISTS dyn_importance INT NULL,
            ADD COLUMN IF NOT EXISTS dyn_urgency INT NULL,
            ADD COLUMN IF NOT EXISTS time_spent INT NULL,
            MODIFY COLUMN priority ENUM('pri_critical','pri_very_impornant','pri_higher','pri_high','pri_normal','pri_medium','pri_low','pri_insignificant') NULL
        )",

        "DELETE FROM work_session AS s WHERE s.action NOT IN (SELECT action FROM action)",

        R"(UPDATE action AS a
            LEFT JOIN (
                SELECT s.action, SUM(s.duration) AS total_duration
                FROM work_session AS s
                WHERE s.state = 'done'
                GROUP BY s.action
            ) AS summary ON summary.action = a.id
            SET a.time_spent = IFNULL(summary.total_duration, 0);
            )",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    static constexpr auto v20_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        R"(ALTER TABLE action ADD COLUMN IF NOT EXISTS tags TEXT NULL)",

        "SET FOREIGN_KEY_CHECKS=1"
    });

    // make sure users and tenants can be deleted without leaving behind dangling references
    static constexpr auto v21_upgrade = to_array<string_view>({
        "SET FOREIGN_KEY_CHECKS=0",

        "ALTER TABLE node DROP FOREIGN KEY node_ibfk_2",

        R"(ALTER TABLE node
            ADD CONSTRAINT node_ibfk_2
            FOREIGN KEY (user) REFERENCES user (id)
            ON DELETE CASCADE ON UPDATE NO ACTION)",

        "ALTER TABLE day_colors DROP FOREIGN KEY day_colors_ibfk_1",

        R"(ALTER TABLE day_colors
            ADD CONSTRAINT day_colors_ibfk_1
            FOREIGN KEY (tenant) REFERENCES tenant (id)
            ON DELETE CASCADE ON UPDATE NO ACTION)",

        "ALTER TABLE time_block DROP FOREIGN KEY time_block_ibfk_2",

        R"(ALTER TABLE time_block
            ADD CONSTRAINT time_block_ibfk_2
            FOREIGN KEY (category) REFERENCES action_category (id)
                ON DELETE CASCADE ON UPDATE NO ACTION)",

        "ALTER TABLE action DROP FOREIGN KEY action_ibfk_3",

        R"(ALTER TABLE action ADD CONSTRAINT action_ibfk_3
          FOREIGN KEY (origin) REFERENCES action (id)
              ON DELETE SET NULL
              ON UPDATE NO ACTION)",

        "SET FOREIGN_KEY_CHECKS=1"
    });


    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
        v2_upgrade,
        v3_upgrade,
        v4_upgrade,
        v5_upgrade,
        v6_upgrade,
        v7_upgrade,
        v8_upgrade,
        v9_upgrade,
        v10_upgrade,
        v11_upgrade,
        v12_upgrade,
        v13_upgrade,
        v14_upgrade,
        v15_upgrade,
        v16_upgrade,
        v17_upgrade,
        v18_upgrade,
        v19_upgrade,
        v20_upgrade,
        v21_upgrade
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version;

    // auto cfg = config_.db;
    // cfg.max_connections = 1;

    // mysqlpool::Mysqlpool db{ctx_, cfg};

    // co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
    //     co_await db.init();
    //     {

    auto handle = co_await db().getConnection();
    auto trx = co_await handle.transaction();

    // Here we will run all SQL queries for upgrading from the specified version to the current version.
    auto relevant = ranges::drop_view(versions, version);
    for(string_view query : relevant | std::views::join) {
        co_await handle.exec(query);
    }

    if (version == 0) {
        // Create system tenant
        // Names and uuid's must be globally unique, so it can be used in a cluster.
        const auto tenant_id = newUuid();
        const auto user_id = newUuid();
        const auto tenant_name = format("system-{}", to_string(tenant_id));
        const auto user_name = format("admin-{}", to_string(user_id));

        // Add tenant
        co_await handle.exec("INSERT INTO tenant (id, name, kind, system_tenant) VALUES (?, ?, 'super', 1)", tenant_id, tenant_name);
        // Add user
        co_await handle.exec("INSERT INTO user (id, tenant, name, kind, system_user) VALUES (?, ?, ?, 'super', 1)", user_id, tenant_id, user_name);
    }

    co_await handle.exec("UPDATE nextapp SET VERSION = ? WHERE id = 1", latest_version);
    co_await trx.commit();
    //    }
    //     co_await db.close();

    // }, asio::use_awaitable);
}

boost::asio::awaitable<void> Server::loadCertAuthority()
{
    enum Cols {
        CERT,
        KEY
    };

    auto conn = co_await db().getConnection();
    auto trx = co_await conn.transaction(false, false);

    // See if we have a CA cert in the db.
    auto res = co_await conn.exec("SELECT cert, pkey FROM cert WHERE id='ca'");
    assert(!res.empty());
    CertData cd;
    if (res.rows().empty()) {
        LOG_INFO << "Creating CA cert: " << config_.ca.ca_name;
        cd = createCaCert(config_.ca.ca_name);
        co_await conn.exec("INSERT INTO cert (id, cert, pkey) VALUES ('ca', ?, ?)", cd.cert, cd.key);
    } else {
        cd.cert = res.rows().at(0).at(CERT).as_string();
        cd.key = res.rows().at(0).at(KEY).as_string();
    }

    co_await trx.commit();

    assert(!cd.cert.empty());
    assert(!cd.key.empty());
    ca_.emplace(cd, config_.ca);
    co_return;
}

boost::asio::awaitable<void> Server::startGrpcService()
{
    assert(!grpc_service_);
    grpc_service_ = make_shared<grpc::GrpcServer>(*this);
    grpc_service_->start();
    co_return;
}

boost::asio::awaitable<void> Server::resetMetricsPassword(jgaa::mysqlpool::Mysqlpool::Handle& handle)
{
    const auto passwd = getRandomStr(32);
    const auto auth = format("metrics:{}", passwd);
    const auto http_basic_auth = Base64Encode(auth);
    const auto hash = hashPassword(http_basic_auth);

    auto res = co_await handle.exec("INSERT INTO config (name, value) VALUES ('metric_auth_hash', ?) ON DUPLICATE KEY UPDATE value = ?", hash, hash);
    if (!res.affected_rows()) {
        LOG_ERROR << "Failed to insert metric_auth_hash into config table";
        co_return;
    }

    // Write the password to a file in users home directory
    const auto path = filesystem::path(getEnv("HOME", "/var/lib/nextapp")) / "nextapp_metrics_password.txt";
    auto content = format("user: metrics\npasswd: {}", passwd);

    if (auto fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR); fd != -1) {
        write(fd, content.c_str(), content.size());
        close(fd);
        LOG_INFO << "Metrics password saved to " << path;
    } else {
        LOG_ERROR << "Failed to write metrics password to " << path;
    }
}

boost::asio::awaitable<void> Server::prepareMetricsAuth()
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

boost::asio::awaitable<void> Server::loadServerId()
{
    auto res = co_await db().exec("SELECT serverid from nextapp where id=1");
    if (res.has_value() && !res.rows().empty()) {
        server_id_ = res.rows().front().front().as_string();
        LOG_INFO << "The server-id for this deployment is " << server_id_;
    } else {
        LOG_WARN << "No server-id found in nextapp table";
    }
}

boost::asio::awaitable<void> Server::recreateServerCert(const std::vector<std::string> &fqdns)
{
    assert(db_);

    if (fqdns.empty()) {
        LOG_DEBUG_N << "No FQDNs provided. Will not change the servers FQDNs.";
    } else {
        // Create a comma separated list of fqdn's from the list
        std::string fqdn_list;
        for (const auto& fqdn : fqdns) {
            if (!fqdn_list.empty()) {
                fqdn_list += ",";
            }
            fqdn_list += fqdn;
        }

        LOG_INFO << "(Re)creating the server's grpc TLS certificate for FQDNs: " << fqdn_list
                 << ". These FQDNs will be visible for all gRPC clients connecting to this server.";
        auto conn = co_await db().getConnection();
        co_await conn.exec("DELETE FROM cert WHERE id='grpc-server'");
        co_await conn.exec("DELETE FROM config where name='grpc-fqdns'");
        co_await conn.exec("INSERT INTO config (name, value) VALUES ('grpc-fqdns', ?)", fqdn_list);
    }

    co_await loadCertAuthority();
    co_await getCert("grpc-server", WithMissingCert::CREATE_SERVER);
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
