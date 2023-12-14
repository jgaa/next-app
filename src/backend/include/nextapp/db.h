#pragma     once

#include <algorithm>
#include <queue>
#include <utility>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>

#include "nextapp/config.h"

namespace nextapp::db {

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

        Db *parent_{};
        Connection *connection_{};
    };

    boost::asio::awaitable<Handle> get_connection();;

private:
    void init();

    void release(Handle& h) noexcept;

    boost::asio::io_context& ctx_;
    mutable std::mutex mutex_;
    std::vector<Connection> connections_;
    boost::asio::deadline_timer semaphore_;
    const DbConfig& config_;
};

} // ns
