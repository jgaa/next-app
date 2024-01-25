
#include <memory>
#include "nextapp/db.h"
#include "nextapp/logging.h"
#include "nextapp/errors.h"

using namespace std;
using ::nextapp::logging::LogEvent;
namespace asio = boost::asio;
namespace mysql = boost::mysql;

namespace nextapp::db {


asio::awaitable<Db::Handle> Db::getConnection(bool throwOnEmpty) {
    while(true) {
        optional<Handle> handle;
        {
            std::scoped_lock lock{mutex_};
            if (connections_.empty()) {
                if (throwOnEmpty) {
                    throw runtime_error{"No database connections are open. Is the server shutting down?"};
                }
                co_return Handle{};
            }
            if (auto it = std::ranges::find_if(connections_, [](const auto& c) {
                    return c.available_;
                } ); it != connections_.end()) {
                it->available_ = false;
                handle.emplace(this, &*it);
            }
        }

        if (handle) {
            LOG_TRACE_N << "Returning a DB connection.";
            co_return std::move(*handle);
        }

        LOG_TRACE_N << "Waiting for a DB connection to become available...";
        const auto [ec] = co_await semaphore_.async_wait(as_tuple(asio::use_awaitable));
        if (ec != boost::asio::error::operation_aborted) {
            LOG_DEBUG_N << "async_wait on semaphore failed: " << ec.message();
        }
        LOG_TRACE_N << "Done waiting";
    }

    co_return Handle{};
}

boost::asio::awaitable<void> Db::close()
{
    LOG_DEBUG_N << "Closing database connections...";
    while(true) {
        // Use this to get the connections while they are available
        auto conn = co_await getConnection(false);
        if (conn.empty()) {
            LOG_DEBUG_N << "Done closing database connections.";
            break; // done
        }

        try {
            LOG_TRACE_N << "Closing db connection.";
            co_await conn.connection().async_close(asio::use_awaitable);
            conn.reset();

            // Delete the Connection object
            std::scoped_lock lock{mutex_};
            if (auto it = find_if(connections_.begin(), connections_.end(), [&](const auto& v) {
                    return addressof(v) == conn.connection_;
                    }); it != connections_.end()) {
                connections_.erase(it);
            } else {
                LOG_ERROR << "Failed to lookup a connection I just closed!";
            }
        } catch(const exception&) {}
    }
}

boost::asio::awaitable<void> Db::init() {

    asio::ip::tcp::resolver resolver(ctx_.get_executor());
    auto endpoints = co_await resolver.async_resolve(config_.host,
                                                     std::to_string(config_.port),
                                                     boost::asio::use_awaitable);

    if (endpoints.empty()) {
        LOG_ERROR << LogEvent::LE_DATABASE_FAILED_TO_RESOLVE
                  << "Failed to resolve hostname "
                  << config_.host << " tor the database server: ";
        throw runtime_error{"Failed to resolve database hostname"};
    }

    LOG_DEBUG_N << "Connecting to mysql compatible database at "
                << config_.host << ':' << config_.port
                << " as user " << dbUser() << " with database "
                << config_.database;


    connections_.reserve(config_.max_connections);


    for(size_t i = 0; i < config_.max_connections; ++i) {
        mysql::tcp_connection conn{ctx_.get_executor()};
        co_await connect(conn, endpoints, 0, true);
        connections_.emplace_back(std::move(conn));
        co_return;
    }

    static constexpr auto one_hundred_years = 8766 * 100;
    semaphore_.expires_from_now(boost::posix_time::hours(one_hundred_years));

    co_return;
}

bool Db::handleError(const boost::system::error_code &ec, boost::mysql::diagnostics &diag)
{
    if (ec) {
        LOG_DEBUG << "Statement failed with error:  " << ec.message()
                  << " (" << ec.value()
                  << "). Client: " << diag.client_message()
                  << ". Server: " << diag.server_message();

        switch(ec.value()) {
            case static_cast<int>(mysql::common_server_errc::er_dup_entry):
                throw db_err{pb::Error::ALREADY_EXIST, ec.message()};

            case boost::asio::error::eof:
            case boost::asio::error::broken_pipe:
            case boost::system::errc::connection_reset:
            case boost::system::errc::connection_aborted:
            case boost::asio::error::operation_aborted:
                LOG_DEBUG << "The error is recoverable if we re-try the query it may succeed...";
                return false; // retry

            default:
                LOG_DEBUG << "The error is non-recoverable";
                throw db_err{pb::Error::DATABASE_REQUEST_FAILED, ec.message()};
        }
    }
    return true;
}

void Db::release(Handle &h) noexcept {
    if (h.connection_) {
        std::scoped_lock lock{mutex_};
        assert(h.connection_->available_ == false);
        h.connection_->available_ = true;
    }
    boost::system::error_code ec;
    semaphore_.cancel_one(ec);
}

string Db::dbUser() const
{
    return config_.username;
}

string Db::dbPasswd() const
{
    if (config_.password.empty()) {

        LOG_WARN << "Database password for " << dbUser() << " is not set.";
    }

    return config_.password;
}

boost::asio::awaitable<void> Db::Handle::reconnect()
{
    asio::ip::tcp::resolver resolver(parent_->ctx_.get_executor());
    auto endpoints = resolver.resolve(parent_->config_.host,
                                      std::to_string(parent_->config_.port));

    LOG_DEBUG << "Will try to re-connect to the database server at "
              << parent_->config_.host << ":" << parent_->config_.port;

    if (endpoints.empty()) {
        LOG_ERROR << LogEvent::LE_DATABASE_FAILED_TO_RESOLVE
                  << "Failed to resolve hostname "
                  << parent_->config_.host << " tor the database server: ";
        throw runtime_error{"Failed to resolve database hostname"};
    }

    co_await parent_->connect(connection_->connection_, endpoints, 0, true);
    co_return;
}

} // ns
