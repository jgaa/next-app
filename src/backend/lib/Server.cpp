
#include <algorithm>

#include <boost/asio/co_spawn.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/error_with_diagnostics.hpp>
#include <boost/mysql/handshake_params.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/tcp.hpp>
#include <boost/mysql/throw_on_error.hpp>
#include <boost/mysql.hpp>

#include "nextapp/Server.h"
#include "nextapp/logging.h"

using namespace std;
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
    init_ctx(config().svr.io_threads);

    db_.emplace(ctx_, config().db);
}

void Server::run()
{
    // TODO: Set up signal handler
    LOG_DEBUG_N << "Main thread joins the IO thread pool...";
    run_io_thread(0);
    LOG_DEBUG_N << "Main thread left the IO thread pool...";
}

void Server::bootstrap()
{
    // Create the database.
    constexpr auto tuple_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);

    auto cfg = config_.db;
    cfg.database = "mysql";
    cfg.max_connections = 1;

    db::Db db{ctx_, cfg};

    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {

        auto conn = co_await db.get_connection();
        boost::mysql::results result;
        boost::system::error_code ec;
        boost::mysql::diagnostics diag;

        LOG_TRACE_N << "Dropping database...";
        tie(ec) = co_await conn.connection().async_execute("DROP DATABASE nextapp", result, diag, tuple_awaitable);
        if (ec) {
            LOG_DEBUG_N << "Failed to drop the old database: " << diag.client_message() << ' ' << diag.server_message();
        }

        LOG_TRACE_N << "Creating database...";
        co_await conn.connection().async_execute("CREATE DATABASE nextapp", result, asio::use_awaitable);

    },
    [](std::exception_ptr ptr) {
        if (ptr) {
            std::rethrow_exception(ptr);
        }
    });

    ctx_.run();
}

void Server::init_ctx(size_t numThreads)
{
    io_threads_.reserve(numThreads);
    for(size_t i = 1; i < numThreads; ++i) {
        io_threads_.emplace_back([this, i]{
            run_io_thread(i);
        });
    }
}

void Server::run_io_thread(const size_t id)
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

}
