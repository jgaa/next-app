
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

void Db::init() {

    asio::ip::tcp::resolver resolver(ctx_.get_executor());
    auto endpoints = resolver.resolve(config_.host,
                                      std::to_string(config_.port));

    if (endpoints.empty()) {
        LOG_ERROR << LogEvent::LE_DATABASE_FAILED_TO_RESOLVE
                  << "Failed to resolve hostname "
                  << config_.host << " tor the database server: ";
        throw runtime_error{"Failed to resolve database hostname"};
    }

    auto user = dbUser();
    auto passwd = dbPasswd();

    mysql::handshake_params params(
        user,
        passwd,
        config_.database
        );

    LOG_DEBUG_N << "Connecting to mysql compatible database at "
                << config_.host << ':' << config_.port
                << " as user " << dbUser() << " with database "
                << config_.database;

    auto && connect = [&](auto iteration) {
        mysql::tcp_connection conn{ctx_.get_executor()};
        std::string why;
        unsigned retries = 0;

        for(auto ep : endpoints) {
            LOG_TRACE_N << "New db connection to " << ep.endpoint();
again:
            if (ctx_.stopped()) {
                LOG_INFO << "Server is shutting down. Aborting connect to the database.";
                throw aborted{"Server is shutting down"};
            }
            try {
                conn.connect(ep.endpoint(), params);
                return std::move(conn);
            } catch (const boost::mysql::error_with_diagnostics& ex) {
                if (ex.code() == boost::system::errc::connection_refused
                    || ex.code() == boost::asio::error::eof) {
                    if (iteration == 0 && ++retries <= config_.retry_connect) {
                        LOG_INFO << "Failed to connect to the database server. Will retry "
                                 << retries << "/" << config_.retry_connect;
                        //std::this_thread::sleep_for(std::chrono::milliseconds{config_.retry_connect_delay_ms});
                        boost::asio::steady_timer timer(ctx_);
                        boost::system::error_code ec;
                        timer.expires_after(std::chrono::milliseconds{config_.retry_connect_delay_ms});
                        timer.wait(ec);
                        goto again;
                    }
                }

                retries = 0;
                LOG_DEBUG_N << LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                            << "Failed to connect to to mysql compatible database at "
                            << ep.endpoint()
                            << " as user " << dbUser() << " with database "
                            << config_.database
                            << ": " << ex.what();
                if (why.empty()) {
                    why = ex.what();
                }
            } catch (const std::exception& ex) {
                LOG_DEBUG_N << LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                          << "Failed to connect to to mysql compatible database at "
                          << ep.endpoint()
                          << " as user " << dbUser() << " with database "
                          << config_.database
                          << ": " << ex.what();

                if (why.empty()) {
                    why = ex.what();
                }
            }
        }

        LOG_ERROR << LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                  << "Failed to connect to to mysql compatible database at "
                  << config_.host << ':' << config_.port
                  << " as user " << dbUser() << " with database "
                  << config_.database
                  << ": " << why;

        throw runtime_error{"Failed to connect to database"};
    };

    connections_.reserve(config_.max_connections);

    for(size_t i = 0; i < config_.max_connections; ++i) {
        connections_.emplace_back(connect(i));
    }

    static constexpr auto one_hundred_years = 8766 * 100;
    semaphore_.expires_from_now(boost::posix_time::hours(one_hundred_years));
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

} // ns
