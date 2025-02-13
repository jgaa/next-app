#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>

#include "signup/config.h"
#include "nextapp/util.h"

#include "signup.pb.h"


namespace nextapp {

class GrpcServer;

class Server {
public:
    static constexpr uint latest_version = 1;

    struct BootstrapOptions {
        bool drop_old_db = false;
        std::string db_root_user = getEnv("NA_ROOT_DBUSER", "root");
        std::string db_root_passwd = getEnv("NA_ROOT_DBPASSWD");
    };

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

    void bootstrap(const BootstrapOptions& opts);

    static std::string getPasswordHash(std::string_view password, std::string_view userUuid);

private:
    void handleSignals();
    void initCtx(size_t numThreads);
    void runIoThread(size_t id);
    boost::asio::awaitable<bool> checkDb();
    boost::asio::awaitable<void> createDb(const BootstrapOptions& opts);
    boost::asio::awaitable<void> upgradeDbTables(uint version);
    boost::asio::awaitable<void> createAdminUser();
    boost::asio::awaitable<void> createDefaultNextappInstance(); // For bootstrap
    boost::asio::awaitable<void> startGrpcService();

    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
    std::shared_ptr<GrpcServer> grpc_service_;
    std::string welcome_text;
    std::string eula_text;
};


} // ns
