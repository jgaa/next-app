#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/uuid.hpp>

#include "nextapp/nextapp.h"
#include "nextapp/config.h"
#include "nextapp/util.h"
#include "nextapp/certs.h"
#include "mysqlpool/mysqlpool.h"
#include "yahat/HttpServer.h"
#include "nextapp/Metrics.h"

namespace nextapp {

namespace grpc {
class GrpcServer;
}

class Server {
public:
    static constexpr uint latest_version = 21;

    struct BootstrapOptions {
        bool drop_old_db = false;
        std::string db_root_user = getEnv("NEXTAPP_ROOT_DBUSER", "root");
        std::string db_root_passwd = getEnv("NEXTAPP_ROOT_DBPASSW");
    };

    Server(const Config& config);
    ~Server();

    void init();

    void run();

    void stop();

    void bootstrap(const BootstrapOptions& opts);

    void createClientCert(const std::string& fileName, boost::uuids::uuid user);

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

    auto& metrics() noexcept {
        return metrics_;
    }

    enum class WithMissingCert {
        FAIL,
        CREATE_SERVER
    };

    boost::asio::awaitable<CertData> getCert(std::string_view id, WithMissingCert what);

    static Server& instance() noexcept {
        assert(instance_);
        return *instance_;
    }

    /*! A presumably unique tag for this instance of the server.
     *
     *  This is used by clients to detect if they are connected to the same
     *  instance of the server as before.
     *
     *  Currently we only support one server instance, and that will not change in the
     *  foreseable future. So if the server restarts, the user can safely use this tag to
     *  detect it.
     */
    time_t instanceTag() const noexcept {
        return instance_tag_;
    }

    /*! A unique id for this server deployment.
     *
     *  The id is created when the server is deployed annd the database bootstrapped.
     */
    const std::string& serverId() const noexcept {
        return server_id_;
    }

    /*! Make a hash for the password, using the server-id as seed */
    std::string hashPassword(std::string_view passwd);

    // Called from main()
    void createGrpcCert();

private:
    void handleSignals();
    void initCtx(size_t numThreads);
    boost::asio::awaitable<void> initDb();
    void runIoThread(size_t id);
    boost::asio::awaitable<bool> checkDb();
    boost::asio::awaitable<void> createDb(const BootstrapOptions& opts);
    boost::asio::awaitable<void> upgradeDbTables(uint version);
    boost::asio::awaitable<void> loadCertAuthority();
    boost::asio::awaitable<void> startGrpcService();
    boost::asio::awaitable<void> resetMetricsPassword(jgaa::mysqlpool::Mysqlpool::Handle& handle);
    boost::asio::awaitable<void> prepareMetricsAuth();
    boost::asio::awaitable<void> loadServerId();
    boost::asio::awaitable<void> recreateServerCert(const std::vector<std::string>& fqdns);
    boost::asio::awaitable<boost::uuids::uuid> getAdminUserId();
    boost::asio::awaitable<void> onMetricsTimer();
    void startMetricsTimer();

    void createCa();
    void createServerCert();

    Metrics metrics_;
    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    std::optional<jgaa::mysqlpool::Mysqlpool> db_;
    Config config_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
    std::shared_ptr<grpc::GrpcServer> grpc_service_;
    std::optional<CertAuthority> ca_;
    std::optional<yahat::HttpServer> http_server_;
    static Server *instance_;
    const time_t instance_tag_{time({})};
    std::string server_id_;
    std::string metrics_auth_hash_;
};

template <ProtoMessage T>
std::string toJsonForLog(const T& obj) {
    return toJson(obj, Server::instance().config().options.log_protobuf_messages);
}


} // ns
