#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>

#include "signup/config.h"

#include "signup.pb.h"


namespace nextapp {

namespace grpc {
class GrpcServer;
}

class Server {
public:
    static constexpr uint latest_version = 8;

    Server(const Config& config);
    ~Server();

    void init();

    void run();

    void stop();

    const auto& config() const noexcept {
        return config_;
    }

    auto& ctx() noexcept {
        return ctx_;
    }

    bool is_done() const noexcept {
        return done_;
    }

    auto& grpc() noexcept {
        assert(grpc_service_);
        return *grpc_service_;
    }

    const std::string& getWelcomeText(const ::signup::pb::GetInfoRequest& /*req*/) const noexcept {
        return welcome_text;
    }

    const std::string& getEulaText(const ::signup::pb::GetInfoRequest& /*req*/) const noexcept {
        return eula_text;
    }

private:
    void handleSignals();
    void initCtx(size_t numThreads);
    void runIoThread(size_t id);
    boost::asio::awaitable<void> startGrpcService();

    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
    std::shared_ptr<grpc::GrpcServer> grpc_service_;
    std::string welcome_text;
    std::string eula_text;
};


} // ns
