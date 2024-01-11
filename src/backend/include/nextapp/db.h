#pragma     once

#include <algorithm>
#include <queue>
#include <utility>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>

#include "nextapp/config.h"
#include "nextapp/logging.h"

namespace nextapp::db {

using results = boost::mysql::results;

constexpr auto tuple_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);

/*! Database interface
 *
 * Normally a singleton
 *
 * Currently we will use mysql driver against mariadb.
 */

class Db {
public:
    // enum class DbPrecence {
    //     OK,
    //     LOWER_VERSION,
    //     NO_SERVER,
    //     NO_DATABASE,
    //     UNKNOWN_DB_VERSION // Database is for a newer version of nextappd
    // };

    // static DbPrecence checkDbPrecence(const DbConfig& config);

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

    [[nodiscard]] boost::asio::awaitable<Handle> getConnection(bool throwOnEmpty = true);

    // Execute a query or prepared, bound statement
    template <BOOST_MYSQL_EXECUTION_REQUEST T>
    boost::asio::awaitable<results> exec(T query) {
        auto conn = co_await getConnection();
        logQuery("static", query);
        results res;
        co_await conn.connection().async_execute(query,
                                                 res,
                                                 boost::asio::use_awaitable);
        co_return std::move(res);
    }

    template<typename ...argsT>
    boost::asio::awaitable<results> exec(std::string_view query, argsT ...args) {
        auto conn = co_await getConnection();
        logQuery("statement", query, args...);
        results res;
        boost::mysql::diagnostics diag;

        if constexpr (sizeof...(argsT) == 0) {
            auto [ec] = co_await conn.connection().async_execute(query, res, diag, tuple_awaitable);
            handleError(ec, diag);
        } else {
            auto [ec, stmt] = co_await conn.connection().async_prepare_statement(query, diag, tuple_awaitable);
            handleError(ec, diag);
            std::tie(ec) = co_await conn.connection().async_execute(stmt.bind(args...), res, diag, tuple_awaitable);\
            handleError(ec, diag);
        }

        co_return std::move(res);
    }

    boost::asio::awaitable<void> close();

private:
    void init();
    void handleError(const boost::system::error_code& ec, boost::mysql::diagnostics& diag);
    template <typename... T>
    std::string logArgs(const T... args) {
        if constexpr (sizeof...(T)) {
            std::stringstream out;
            out << " args: ";
            auto col = 0;
            ((out << (++col == 1 ? "" : ", ")  << args), ...);
            return out.str();
        }
        return {};
    }

    template <typename... T>
    void logQuery(std::string_view type, std::string_view query, T... bound) {
        LOG_TRACE << "Exceuting " << type << " SQL query: " << query << logArgs(bound...);
    }

    void release(Handle& h) noexcept;
    std::string dbUser() const;
    std::string dbPasswd() const;

    boost::asio::io_context& ctx_;
    mutable std::mutex mutex_;
    std::vector<Connection> connections_;
    boost::asio::deadline_timer semaphore_;
    const DbConfig& config_;
};

} // ns
