#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>

#include "nextapp/nextapp.h"
#include "nextapp/config.h"
#include "nextapp/db.h"

namespace nextapp {

class Server {
public:
    static constexpr uint latest_version = 1;

    struct BootstrapOptions {
        bool drop_old_db = false;
        std::string db_root_user = "root";
        std::string db_root_passwd;
    };

    Server(const Config& config);
    ~Server();

    void init();

    void run();

    void stop();

    void bootstrap(const BootstrapOptions& opts);

    const auto& config() const noexcept {
        return config_;
    }

    auto& ctx() noexcept {
        return ctx_;
    }

    auto& db() noexcept {
        assert(db_.has_value());
        return *db_;
    }

    bool is_done() const noexcept {
        return done_;
    }

private:
    void handle_signals();
    void init_ctx(size_t numThreads);
    void run_io_thread(size_t id);
    boost::asio::awaitable<bool> check_db();
    boost::asio::awaitable<void> create_db(const BootstrapOptions& opts);
    boost::asio::awaitable<void> upgrade_db_tables(uint version);

    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    std::optional<db::Db> db_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
};

} // ns
