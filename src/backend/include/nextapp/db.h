#pragma     once

#include <algorithm>
#include <queue>
#include <utility>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>

#include "nextapp/config.h"

namespace nextapp::db {

constexpr auto tuple_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);

/*! Database interface
 *
 * Normally a singleton
 *
 * Currently we will use mysql driver against mariadb.
 */

class Db {
public:
    Db(boost::asio::io_context& ctx, const DbConfig& config)
        : ctx_{ctx}, semaphore_{ctx}, config_{config}
    {
        init();
    }

    Db(const Db&) = delete;
    Db(Db&&) = delete;

    Db& operator = (const Db&) = delete;
    Db& operator = (Db&&) = delete;

    struct Connection {
        boost::mysql::tcp_connection connection_;
        bool available_{true};
    };

    class Handle {
    public:
        Handle() = default;
        Handle(const Handle &) = delete;

        Handle(Handle &&v) noexcept
            : parent_{std::exchange(v.parent_, {})}
            , connection_{std::exchange(v.connection_, {})}
        {
            int i = 0;
        }

        ~Handle() {
            if (parent_) {
                parent_->release(*this);
            }
        }

        Handle& operator = (const Handle) = delete;
        Handle& operator = (Handle && v) noexcept {
            parent_ = v.parent_;
            v.parent_ = {};
            connection_ = v.connection_;
            v.connection_ = {};
            return *this;
        }

        explicit Handle(Db* db, Connection* conn)
            : parent_{db}, connection_{conn} {}

        // Return the mysql connection
        auto& connection() {
            assert(connection_);
            return connection_->connection_;
        }

        bool empty() const noexcept {
            return connection_ != nullptr;
        }

        void reset() {
            parent_ = {};
            connection_ = {};
        }

        Db *parent_{};
        Connection *connection_{};
    };

    boost::asio::awaitable<Handle> get_connection(bool throwOnEmpty = true);

    // Execute a query or prepared, bound statement
    template <BOOST_MYSQL_EXECUTION_REQUEST T>
    boost::asio::awaitable<boost::mysql::results> exec(T query) {
        auto conn = co_await get_connection();
        boost::mysql::results result;
        log_query("static", query);
        co_await conn.connection().async_execute(query,
                                                 result,
                                                 boost::asio::use_awaitable);
        co_return std::move(result);
    }

    // Execute a statement. Arguments are bound before the query is executed.
    template<typename ...argsT>
    boost::asio::awaitable<boost::mysql::results> execs(std::string_view query, argsT ...args) {
        auto conn = co_await get_connection();
        boost::mysql::results result;
        log_query("statement", query);
        auto stmt = co_await conn.connection().async_prepare_statement(query, boost::asio::use_awaitable);
        co_await conn.connection().async_execute(stmt.bind("nextapp"),
                                                 result,
                                                 boost::asio::use_awaitable);
        co_return std::move(result);
    }

    boost::asio::awaitable<boost::mysql::results> close();

private:
    void init();
    void log_query(std::string_view type, std::string_view query);
    void release(Handle& h) noexcept;

    boost::asio::io_context& ctx_;
    mutable std::mutex mutex_;
    std::vector<Connection> connections_;
    boost::asio::deadline_timer semaphore_;
    const DbConfig& config_;
};

} // ns
