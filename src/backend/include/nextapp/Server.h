#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>

#include "nextapp/nextapp.h"
#include "nextapp/config.h"
#include "nextapp/util.h"
#include "nextapp/certs.h"
#include "mysqlpool/mysqlpool.h"

namespace nextapp {

namespace grpc {
class GrpcServer;
}

class Server {
public:
    static constexpr uint latest_version = 8;

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

    void bootstrap(const BootstrapOptions& opts);

    void bootstrapCa();

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

    auto& grpc() noexcept {
        assert(grpc_service_);
        return *grpc_service_;
    }

    auto& ca() noexcept {
        assert(ca_);
        return *ca_;
    }

    enum class WithMissingCert {
        FAIL,
        CREATE_SERVER,
        CREATE_CLIENT
    };

    boost::asio::awaitable<CertData> getCert(std::string_view id, WithMissingCert what);

private:
    void handleSignals();
    void initCtx(size_t numThreads);
    void runIoThread(size_t id);
    boost::asio::awaitable<bool> checkDb();
    boost::asio::awaitable<void> createDb(const BootstrapOptions& opts);
    boost::asio::awaitable<void> upgradeDbTables(uint version);
    boost::asio::awaitable<void> loadCertAuthority();
    boost::asio::awaitable<void> startGrpcService();
    void createCa();
    void createServerCert();

    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    std::optional<jgaa::mysqlpool::Mysqlpool> db_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
    std::shared_ptr<grpc::GrpcServer> grpc_service_;
    std::optional<CertAuthority> ca_;
};

} // ns
