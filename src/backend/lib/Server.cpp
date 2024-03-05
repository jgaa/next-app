
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

#include "nextapp/Server.h"
#include "nextapp/GrpcServer.h"
#include "nextapp/logging.h"

using namespace std;
using namespace jgaa;
using nextapp::logging::LogEvent;
namespace asio = boost::asio;
using jgaa::mysqlpool::tuple_awaitable;

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

    db_.emplace(ctx_, config().db);
}

void Server::run()
{
    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            co_await db().init();
            if (!co_await checkDb()) {
                LOG_ERROR << "The database version is wrong. Please upgrade before starting the server.";
                stop();
            }

            co_await startGrpcService();
        },
        [](std::exception_ptr ptr) {
            if (ptr) {
                std::rethrow_exception(ptr);
            }
        });

    // TODO: Set up signal handler
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

    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        co_await createDb(opts);
        co_await upgradeDbTables(0);
    },
    [](std::exception_ptr ptr) {
        if (ptr) {
            std::rethrow_exception(ptr);
        }
    });

    ctx_.run();

    LOG_INFO << "Bootstrapping is complete";
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

    mysqlpool::Mysqlpool db{ctx_, cfg};

    co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {

        co_await db.init();

        if (opts.drop_old_db) {
            LOG_TRACE_N << "Dropping database " << config_.db.database ;

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
        "CREATE TABLE nextapp (id INTEGER NOT NULL, version INTEGER NOT NULL, serverid VARCHAR(37) NOT NULL DEFAULT UUID()) ",

        "INSERT INTO nextapp (id, version) values(1, 0)",

        R"(CREATE TABLE tenant (
              id UUID not NULL default UUID() PRIMARY KEY,
              name VARCHAR(128) NOT NULL,
              kind ENUM('super', 'regular') NOT NULL DEFAULT 'regular',
              descr TEXT,
              active TINYINT(1) NOT NULL DEFAULT 1))",

        "INSERT INTO tenant (id, name, kind) VALUES ('a5e7bafc-9cba-11ee-a971-978657e51f0c', 'nextapp', 'super')",

        R"(CREATE TABLE user (
              id UUID not NULL default UUID() PRIMARY KEY,
              tenant UUID NOT NULL,
              name VARCHAR(128) NOT NULL,
              kind ENUM('super', 'regular', 'guest') NOT NULL DEFAULT 'regular',
              descr TEXT,
              active TINYINT(1) NOT NULL DEFAULT 1,
        FOREIGN KEY(tenant) REFERENCES tenant(id)))",

        "INSERT INTO user (id, tenant, name, kind) VALUES ('dd2068f6-9cbb-11ee-bfc9-f78040cadf6b', 'a5e7bafc-9cba-11ee-a971-978657e51f0c', 'admin', 'super')",

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
            name VARCHAR(128) NOT NULL,
            descr TEXT,
            created_date TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            due_kind ENUM('datetime', 'date', 'week', 'month', 'quarter', 'year', 'unset') NOT NULL DEFAULT 'unset',
            start_time DATETIME,
            due_by_time DATETIME,
            due_timezone VARCHAR(64),
            completed_time TIMESTAMP,
            time_estimate INTEGER,
            difficulty ENUM('trivial', 'easy', 'normal', 'hard', 'veryhard', 'inspired') NOT NULL DEFAULT 'normal',
            repeat_kind ENUM('never','scheduled', 'completed'),
            repeat_unit ENUM('days', 'weeks', 'months', 'years'),
            repeat_when ENUM('at_date', 'at_dayspec'),
            repeat_after INTEGER,
            version INT NOT NULL DEFAULT 1,
        FOREIGN KEY(node) REFERENCES node(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY(user) REFERENCES user(id) ON DELETE CASCADE ON UPDATE RESTRICT,
        FOREIGN KEY(origin) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

        R"(CREATE INDEX action_ix2 ON action (user, status, due_by_time))",
        R"(CREATE INDEX action_ix3 ON action (origin))",
        R"(CREATE INDEX action_ix4 ON action (node, status, due_by_time))",

        R"(CREATE OR REPLACE TABLE action2location (
            action UUID NOT NULL,
            location UUID NOT NULL,
            PRIMARY KEY (action, location),
            FOREIGN KEY(action) REFERENCES action(id) ON DELETE CASCADE ON UPDATE RESTRICT,
            FOREIGN KEY(location) REFERENCES location(id) ON DELETE CASCADE ON UPDATE RESTRICT))",

         R"(CREATE INDEX action2location_ix2 ON action2location (location, action))",
    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
        v2_upgrade,
        v3_upgrade,
        v4_upgrade,
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version;

    auto cfg = config_.db;
    cfg.max_connections = 1;

    mysqlpool::Mysqlpool db{ctx_, cfg};

    co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
        co_await db.init();

        // Here we will run all SQL queries for upgrading from the specified version to the current version.
        auto relevant = ranges::drop_view(versions, version);
        for(string_view query : relevant | std::views::join) {
            co_await db.exec(query);
        }

        co_await db.exec("UPDATE nextapp SET VERSION = ? WHERE id = 1", latest_version);
        co_await db.close();

    }, asio::use_awaitable);
}

boost::asio::awaitable<void> Server::startGrpcService()
{
    assert(!grpc_service_);
    grpc_service_ = make_shared<grpc::GrpcServer>(*this);
    grpc_service_->start();
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
