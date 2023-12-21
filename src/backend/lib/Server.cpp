
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
using nextapp::logging::LogEvent;
namespace asio = boost::asio;
using nextapp::db::tuple_awaitable;

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
    ctx_.stop();
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
    try {
        ++running_io_threads_;
        ctx_.run();
        --running_io_threads_;
    } catch (const std::exception& ex) {
        --running_io_threads_;
        LOG_ERROR << LogEvent::LE_IOTHREAD_THREW
                  << "Caught exception from IO therad #" << id
                  << ": " << ex.what();

        LOG_ERROR << LogEvent::LE_IOTHREAD_THREW
                  << "I have " << running_io_threads_
                  << " remaining running IO threads.";

        if (running_io_threads_ <= 2) {
            LOG_ERROR << LogEvent::LE_IOTHREAD_THREW
                      << "*** FATAL **** Lower treashold for required IO threads is reached. Aborting.";

            // TODO: Shutdown, don't abort. terminate() only if all the IO threads are gone.
            std::terminate();
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
        LOG_DEBUG << "The existing database is at version " << version
                  << ". I need the database to be at version " << latest_version << '.';

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

    db::Db db{ctx_, cfg};

    co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {

        //boost::mysql::results result;

        if (opts.drop_old_db) {
            LOG_TRACE_N << "Dropping database " << config_.db.database ;

            try {
                co_await db.exec(format("REVOKE ALL PRIVILEGES FROM '{}'@'%'",
                                        config_.db.username));
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

        R"(CREATE TABLE action (
            id UUID not NULL default UUID() PRIMARY KEY,
            node UUID NOT NULL,
            list_id INTEGER NOT NULL,
            priority INTEGER NOT NULL DEFAULT (5),
            name VARCHAR(128) NOT NULL,
            descr TEXT,
            created_date TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
            due_type INTEGER NOT NULL DEFAULT (0),
            due_by_time DATETIME,
            completed_time TIMESTAMP NOT NULL DEFAULT(0),
            completed TINYINT(1) NOT NULL DEFAULT 1,
            time_estimate INTEGER,
            focus_needed INTEGER NOT NULL DEFAULT (3),
            repeat_type INTEGER NOT NULL DEFAULT(0),
            repeat_unit INTEGER NOT NULL DEFAULT(0),
            repeat_after INTEGER NOT NULL DEFAULT(0),
        FOREIGN KEY(node) REFERENCES node(id)))",

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
    });

    static constexpr auto versions = to_array<span<const string_view>>({
        v1_bootstrap,
    });

    LOG_INFO << "Will upgrade the database structure from version " << version
             << " to version " << latest_version;

    auto cfg = config_.db;
    cfg.max_connections = 1;

    db::Db db{ctx_, cfg};

    co_await asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {

        LOG_TRACE << "in coro...";

        // Here we will run all SQL queries for upgrading from the specified version to the current version.
        auto relevant = ranges::drop_view(versions, version);
        for(string_view query : relevant | std::views::join) {
            co_await db.exec(query);
        }

        co_await db.execs("UPDATE nextapp SET VERSION = ? WHERE id = 1", latest_version);
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
            return;
        } else {
            LOG_WARN_N << " Ignoring signal #" << signalNumber;
        }

        handleSignals();
    });
}

}
