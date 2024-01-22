#pragma     once

#include <iostream>
#include <algorithm>
#include <queue>
#include <utility>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>

#include "nextapp/config.h"
#include "nextapp/logging.h"
#include "nextapp/errors.h"

template <typename T>
concept OptionalPrintable = requires {
    std::is_same_v<std::remove_cv<T>, std::optional<typename T::value_type>>;
    std::cout << *std::declval<T>();
};

template <OptionalPrintable T>
std::ostream& operator << (std::ostream& out, const T& val) {
    if (val) {
        return out << *val;
    }
    return out << "(null)";
}

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
    Db(boost::asio::io_context& ctx, const DbConfig& config)
        : ctx_{ctx}, semaphore_{ctx}, config_{config}
    {
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

        boost::asio::awaitable<void> reconnect();
    };

    [[nodiscard]] boost::asio::awaitable<Handle> getConnection(bool throwOnEmpty = true);

    // Execute a query or prepared, bound statement
    template <BOOST_MYSQL_EXECUTION_REQUEST T>
    boost::asio::awaitable<results> exec(T query) {
        auto conn = co_await getConnection();
        logQuery("static", query);
        results res;
        boost::mysql::diagnostics diag;
        auto [ec] = co_await conn.connection().async_execute(query,
                                                             res, diag,
                                                             tuple_awaitable);
        if (ec) {
            handleError(ec, diag);
        }
        co_return std::move(res);
    }

    template<typename ...argsT>
    boost::asio::awaitable<results> exec(std::string_view query, argsT ...args) {
        auto conn = co_await getConnection();
        logQuery("statement", query, args...);
        results res;
        boost::mysql::diagnostics diag;

        again:

        if constexpr (sizeof...(argsT) == 0) {
            auto [ec] = co_await conn.connection().async_execute(query, res, diag, tuple_awaitable);
            if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
        } else {
            auto [ec, stmt] = co_await conn.connection().async_prepare_statement(query, diag, tuple_awaitable);
            if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
            std::tie(ec) = co_await conn.connection().async_execute(stmt.bind(args...), res, diag, tuple_awaitable);\
            if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
        }

        co_return std::move(res);
    }

    boost::asio::awaitable<void> init();
    boost::asio::awaitable<void> close();

private:

    template <typename epT, typename connT = boost::mysql::tcp_connection>
    boost::asio::awaitable<void> connect(connT& conn, epT& endpoints, unsigned iteration, bool retry) {

        const auto user = dbUser();
        const auto pwd = dbPasswd();
        boost::mysql::handshake_params params(
            user,
            pwd,
            config_.database
            );

        std::string why;
        unsigned retries = 0;

        for(auto ep : endpoints) {
            LOG_TRACE_N << "New db connection to " << ep.endpoint();
        again:
            if (ctx_.stopped()) {
                LOG_INFO << "Server is shutting down. Aborting connect to the database.";
                throw aborted{"Server is shutting down"};
            }

            boost::mysql::diagnostics diag;
            boost::system::error_code ec;
            std::tie(ec) = co_await conn.async_connect(ep.endpoint(), params, diag, tuple_awaitable);
            if (ec) {
                if (ec == boost::system::errc::connection_refused
                    || ec == boost::asio::error::broken_pipe
                    || ec == boost::asio::error::eof) {
                    if (retry && iteration == 0 && ++retries <= config_.retry_connect) {
                        LOG_INFO << "Failed to connect to the database server. Will retry "
                                 << retries << "/" << config_.retry_connect;
                        boost::asio::steady_timer timer(ctx_);
                        boost::system::error_code ec;
                        timer.expires_after(std::chrono::milliseconds{config_.retry_connect_delay_ms});
                        timer.wait(ec);
                        goto again;
                    }
                }

                retries = 0;
                LOG_DEBUG_N << ::nextapp::logging::LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                            << "Failed to connect to to mysql compatible database at "
                            << ep.endpoint()
                            << " as user " << dbUser() << " with database "
                            << config_.database
                            << ": " << ec.message();
                if (why.empty()) {
                    why = ec.message();
                }

            } else {
                //co_return std::move(conn);
                co_return;
            }
        }

        LOG_ERROR << ::nextapp::logging::LogEvent::LE_DATABASE_FAILED_TO_CONNECT
                  << "Failed to connect to to mysql compatible database at "
                  << config_.host << ':' << config_.port
                  << " as user " << dbUser() << " with database "
                  << config_.database
                  << ": " << why;

        throw std::runtime_error{"Failed to connect to database"};
    }

    // If it returns false, connection to server is closed
    bool handleError(const boost::system::error_code& ec, boost::mysql::diagnostics& diag);
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
