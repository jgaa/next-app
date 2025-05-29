#pragma once

#include <thread>
#include <optional>
#include <atomic>

#include <boost/asio.hpp>

#include "signup/config.h"
#include "nextapp/util.h"
#include "mysqlpool/mysqlpool.h"
#include "signup/Metrics.h"

#include "signup.pb.h"


namespace nextapp {

class GrpcServer;

class Server {
public:
    static constexpr uint latest_version = 2;

    struct Cluster {
        struct Region {
            enum class State {
                ACTIVE,
                INACTIVE
            };

            struct Instance {
                enum class State {
                    ACTIVE,
                    INACTIVE
                };

                boost::uuids::uuid uuid;
                std::atomic_int free_slots{-1}; // -1 = disabled
                std::string url;
                std::string pub_url;
                time_t created_at{};
                State state{State::ACTIVE};
                std::string x509_ca_cert;
                std::string x509_cert;
                std::string x509_key;
                std::string server_id;
                std::string metrics_url;
                std::atomic_bool is_online{false};
            };

            boost::uuids::uuid uuid;
            std::string name;
            std::string description;
            time_t created_at{};
            State state{State::ACTIVE};

            Instance& instance(const boost::uuids::uuid& uuid) {
                return *instances.at(uuid);
            }

            Instance& instance(const boost::uuids::uuid& uuid) const {
                return *instances.at(uuid);
            }

            std::map<boost::uuids::uuid, std::unique_ptr<Instance>> instances;
        };

        std::map<boost::uuids::uuid, Region> regions;
    };

    struct BootstrapOptions {
        bool drop_old_db = false;
        std::string db_root_user = getEnv("SIGNUP_ROOT_DBUSER", "root");
        std::string db_root_passwd = getEnv("SIGNUP_ROOT_DB_PASSWD", "");
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

    std::shared_ptr<Cluster> cluster() {
        return cluster_.load(std::memory_order_relaxed);
    }

    boost::asio::awaitable<::signup::pb::GetInfoResponse> getInfo(const ::signup::pb::GetInfoRequest& req);

    void bootstrap(const BootstrapOptions& opts);

    static std::string getPasswordHash(std::string_view password, std::string_view userUuid);

    struct AssignedInstance {
        boost::uuids::uuid region;
        boost::uuids::uuid instance;
        std::string pub_url;
    };

    boost::asio::awaitable<AssignedInstance> assignInstance(const boost::uuids::uuid& region);

    std::string getEmailHash(std::string_view email) const;

    boost::asio::awaitable<std::optional<boost::uuids::uuid>> getRegionFromUserEmail(std::string_view email);
    boost::asio::awaitable<std::optional<Server::AssignedInstance>> getInstanceFromUserEmail(std::string_view email);
    std::optional<Server::AssignedInstance> getInstanceFromUuid(const boost::uuids::uuid& uuid);

    auto& db() {
        assert(db_.has_value());
        return *db_;
    }

    auto& metrics() noexcept {
        return metrics_;
    }

    boost::asio::awaitable<bool> loadCluster(bool checkClusterWhenLoaded = true);
    std::string hashPasswd(std::string_view passwd);

private:
    void handleSignals();
    void initCtx(size_t numThreads);
    void runIoThread(size_t id);
    boost::asio::awaitable<std::shared_ptr<Cluster>> getClusterFromDb();

    std::shared_ptr<std::vector<::signup::pb::Region>> getRegions();
    boost::asio::awaitable<bool> checkDb();
    boost::asio::awaitable<void> createDb(const BootstrapOptions& opts);
    boost::asio::awaitable<void> upgradeDbTables(uint version);
    boost::asio::awaitable<void> createAdminUser();
    boost::asio::awaitable<void> createDefaultNextappInstance(); // For bootstrap
    boost::asio::awaitable<void> startGrpcService();
    boost::asio::awaitable<void> connectToInstances();
    boost::asio::awaitable<void> initializeInstance(const Cluster::Region& region, Cluster::Region::Instance& instance);
    void checkCluster();
    boost::asio::awaitable<void> checkCluster_();
    void startClusterTimer();
    boost::asio::awaitable<bool> loadCluster_();
    boost::asio::awaitable<void> prepareMetricsAuth();
    boost::asio::awaitable<void> resetMetricsPassword(jgaa::mysqlpool::Mysqlpool::Handle& handle);

    Metrics metrics_;
    boost::asio::io_context ctx_;
    std::optional<boost::asio::signal_set> signals_;
    std::vector <std::jthread> io_threads_;
    std::optional<jgaa::mysqlpool::Mysqlpool> db_;
    Config config_;
    std::optional<yahat::HttpServer> http_server_;
    std::atomic_size_t running_io_threads_{0};
    std::atomic_bool done_{false};
    std::shared_ptr<GrpcServer> grpc_service_;
    std::string welcome_text_;
    std::string eula_text_;
    std::string salt_;
    //std::atomic<std::shared_ptr<std::vector<::signup::pb::Region>>> regions_;
    std::atomic<std::shared_ptr<Cluster>> cluster_;
    boost::asio::strand<boost::asio::io_context::executor_type> cluster_strand_{ctx_.get_executor()};
    boost::asio::steady_timer cluster_timer_{ctx_};
    std::string metrics_auth_hash_;
};


} // ns
