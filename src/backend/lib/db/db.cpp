
#include "nextapp/db.h"
#include "nextapp/logging.h"

using namespace std;
using ::nextapp::logging::LogEvent;
namespace asio = boost::asio;
namespace mysql = boost::mysql;

namespace nextapp::db {

asio::awaitable<Db::Handle> Db::get_connection() {
    while(true) {
        optional<Handle> handle;
        {
            std::scoped_lock lock{mutex_};
            if (auto it = std::ranges::find_if(connections_, [](const auto& c) {
                    return c.available_;
                } ); it != connections_.end()) {
                it->available_ = false;
                handle.emplace(this, &*it);
            }
        }

        if (handle) {
            co_return std::move(*handle);
        }

        assert(false);
        boost::system::error_code ec;
        co_await semaphore_.async_wait(asio::use_awaitable);
    }

    co_return Handle{};
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

    mysql::handshake_params params(
        config_.username,
        config_.password,
        config_.database
        );

    LOG_DEBUG_N << "Connecting to mysql compatible database at "
                << config_.host << ':' << config_.port
                << " as user " << config_.username << " with database "
                << config_.database;

    auto && connect = [&] {
        mysql::tcp_connection conn{ctx_.get_executor()};
        std::string why;

        for(auto ep : endpoints) {
            LOG_TRACE_N << "New db connection to " << ep.endpoint();
            try {
                conn.connect(ep.endpoint(), params);
                return std::move(conn);
            } catch (const std::exception& ex) {
                LOG_DEBUG_N << LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                          << "Failed to connect to to mysql compatible database at "
                          << ep.endpoint()
                          << " as user " << config_.username << " with database "
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
                  << " as user " << config_.username << " with database "
                  << config_.database
                  << ": " << why;

        throw runtime_error{"Failed to connect to database"};
    };

    connections_.reserve(config_.max_connections);
    for(size_t i = 0; i < config_.max_connections; ++i) {
        connections_.emplace_back(connect());
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



} // ns
