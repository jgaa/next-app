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
    Server(const Config& config);
    ~Server();

    void init();

    void run();

    void bootstrap();

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

private:
    void init_ctx(size_t numThreads);
    void run_io_thread(size_t id);

    boost::asio::io_context ctx_;
    std::vector <std::jthread> io_threads_;
    std::optional<db::Db> db_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
};

} // ns
