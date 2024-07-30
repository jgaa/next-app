
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

    eula_text = readFileToBuffer(config_.cluster.eula_path);
    LOG_DEBUG << "EULA text loaded from " << config_.cluster.eula_path;

    welcome_text = readFileToBuffer(config_.cluster.welcome_path);
    LOG_DEBUG << "Welcome text loaded from " << config_.cluster.welcome_path;
}

void Server::run()
{
    asio::co_spawn(ctx_, [&]() -> asio::awaitable<void> {
            try {
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


boost::asio::awaitable<void> Server::startGrpcService()
{
    LOG_DEBUG_N << "Starting gRPC services...";
    assert(!grpc_service_);
    grpc_service_ = make_shared<GrpcServer>(*this);
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
