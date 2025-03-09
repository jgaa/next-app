
#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <shared_mutex>

#include <boost/asio.hpp>
#include <grpcpp/server_context.h>

#include "mysqlpool/mysqlpool.h"
#include "nextapp.pb.h"
#include "nextapp/util.h"
#include "nextapp/Metrics.h"
//#include "nextapp/logging.h"

namespace nextapp {

class Server;

class Publisher {
public:
    virtual ~Publisher() = default;

    virtual void publish(const std::shared_ptr<pb::Update>& message) = 0;
    virtual void close() = 0;

    auto& uuid() const noexcept {
        return uuid_;
    }

private:
    const boost::uuids::uuid uuid_ = newUuid();
};

/*! \brief A container that can hold either a single value or a vector of values.
 *
 * This is useful for storing values that may be either a single value or a list of values.
 * The container is optimized for the single value case, and will transition to a vector
 * when a second value is added.
 */
template <typename T, size_t maxT, bool reserveT = true>
class ValueContainer {
public:
    ValueContainer() : storage(T{}) {}

    size_t size() const {
        if (std::holds_alternative<T>(storage)) {
            return 1;
        } else {
            return std::get<std::vector<T>>(storage).size();
        }
    }

    T get(size_t index = 0, const std::optional<T>& emptyVal = {}) const {
        if (std::holds_alternative<T>(storage)) {
            if (index == 0) {
                return std::get<T>(storage);
            } else {
                if (emptyVal) {
                    return *emptyVal;
                }
                return std::nullopt;
            }
        } else {
            const auto& vec = std::get<std::vector<T>>(storage);
            if (index >= vec.size()) {
                if (emptyVal) {
                    return *emptyVal;
                }
                return std::nullopt;
            }
            return vec[index];
        }
    }

    void set(size_t index, const T& value) {
        if (index >= maxT) {
            throw std::out_of_range("Index out of range");
        }
        if (std::holds_alternative<T>(storage)) {
            if (index == 0) {
                storage = value;
            } else {
                // Transition to vector if index exceeds the single value
                std::vector<T> vec;
                if constexpr (reserveT) {
                    // Useful if the vector is expected to contains a small number of elements.
                    vec.reserve(maxT);
                }
                vec.resize(index + 1);
                vec[0] = std::get<T>(storage);
                vec[index] = value;
                storage = std::move(vec);
            }
        } else {
            auto& vec = std::get<std::vector<T>>(storage);
            if (index >= vec.size()) {
                vec.resize(index + 1);
            }
            vec[index] = value;
        }
    }

private:
    std::variant<T, std::vector<T>> storage;
};


class UserContext : public std::enable_shared_from_this<UserContext> {
public:
    class Device {
    public:
        using value_t = std::optional<uint32_t>;
        using values_t = ValueContainer<value_t, 10>;
        values_t last_request_id;
    };

    class Session : public std::enable_shared_from_this<Session> {
    public:
        using cleanup_t = std::function<void()>;

        Session(std::shared_ptr<UserContext>& user, boost::uuids::uuid devid, boost::uuids::uuid sessionid, Metrics::gauge_scoped_t scope)
            : user_(user), sessionid_{sessionid}, deviceid_{devid}, instance_scope_{std::move(scope)} {
        }

        const boost::uuids::uuid& sessionId() const noexcept {
            return sessionid_;
        }

        const boost::uuids::uuid& deviceId() const noexcept {
            return deviceid_;
        }

        UserContext& user() noexcept {
            assert(user_);
            return *user_;
        }

        void touch();

        std::chrono::steady_clock::time_point touched() const;

        // Call when the session is removed
        void cleanup();

        void addCleanup(cleanup_t cleanup) {
            cleanup_.push_back(std::move(cleanup));
        }

        auto currentDuration() const {
            return std::chrono::steady_clock::now() - created_;
        }

        auto durationSinceLastAccess() const {
            return std::chrono::steady_clock::now() - last_access_.load();
        }

        auto createdTime() const {
            return created_;
        }

        static auto duration(const std::chrono::steady_clock::time_point created) {
            return std::chrono::steady_clock::now() - created;
        };

    private:
        std::shared_ptr<UserContext> user_{}; // NB: Circular reference.
        const boost::uuids::uuid sessionid_;
        const boost::uuids::uuid deviceid_;
        const Metrics::gauge_scoped_t instance_scope_;
        std::vector<cleanup_t> cleanup_;
        std::atomic<std::chrono::steady_clock::time_point> last_access_{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point created_{std::chrono::steady_clock::now()};
    };


    UserContext() = default;

    UserContext(const std::string& tenantUuid, const std::string& userUuid, const std::string_view timeZone,
                bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options& dbOptions);

    UserContext(const std::string& tenantUuid, const std::string& userUuid,
                pb::User::Kind kind,
                const pb::UserGlobalSettings& settings);

    ~UserContext() = default;

    const std::string& userUuid() const noexcept {
        return user_uuid_;
    }

    const std::string& tenantUuid() const noexcept {
        return tenant_uuid_;
    }

    const std::chrono::time_zone& tz() const noexcept {
        assert(tz_ != nullptr);
        return *tz_;
    }

    bool sundayIsFirstWeekday() const noexcept {
        return !settings_.firstdayofweekismonday();
    }

    const jgaa::mysqlpool::Options& dbOptions() const noexcept {
        return db_options_;
    }

    const pb::UserGlobalSettings& settings() const noexcept {
        std::shared_lock lock(mutex_);
        return settings_;
    }

    bool isAdmin() const noexcept {
        return kind_ == pb::User::Kind::User_Kind_SUPER;
    }

    auto kind() const noexcept {
        return kind_;
    }

    auto currentPublishId() const {
        std::shared_lock lock(mutex_);
        return publish_message_id_;
    }

    void addSession(std::shared_ptr<Session> session) {
        std::shared_lock lock(mutex_);
        sessions_.push_back(std::move(session));
    }

    void removeSession(const boost::uuids::uuid& sessionId) {
        std::shared_lock lock(mutex_);
        std::erase_if(sessions_, [&sessionId](const auto& s) { return s->sessionId() == sessionId; });
        purgeExpiredPublishers();
    }

    void setSettings(pb::UserGlobalSettings settings) {
        settings_ = std::move(settings);
    }

    void addPublisher(const std::shared_ptr<Publisher>& publisher);
    void removePublisher(const boost::uuids::uuid& uuid);
    void publish(std::shared_ptr<pb::Update>& message);

    boost::asio::awaitable<bool> checkForReplay(const boost::uuids::uuid& deviceId, uint instanceId, uint reqId);
    boost::asio::awaitable<void> resetReplay(const boost::uuids::uuid& deviceId, uint instanceId);
    void saveReplayStateForDevice(const boost::uuids::uuid& deviceId);
    boost::asio::awaitable<Device::value_t>getLastReqId(const boost::uuids::uuid& deviceId,
                                                                        uint instanceId,
                                                                        bool lookupInDbOnly = false);
    // boost::asio::awaitable<void> setLastReqId(const boost::uuids::uuid& deviceId, uint instanceId, Device::value_t value);
    // boost::asio::awaitable<void> resetLastReqId(const boost::uuids::uuid& deviceId, uint instanceId);

    const auto& sessions() const {
        return sessions_;
    }

    std::optional<Device> getDevice(boost::uuids::uuid uuid) const {
        std::lock_guard lock(instance_mutex_);
        if (auto it = devices_.find(uuid); it != devices_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    static boost::uuids::uuid newUuid();
    void validateInstanceId(uint instanceId);
    boost::asio::awaitable<void> saveLastReqIds(const boost::uuids::uuid& deviceId);
    void purgeExpiredPublishers();

    std::string user_uuid_;
    std::string tenant_uuid_;
    uint32_t publish_message_id_{0};
    const std::chrono::time_zone* tz_{};
    jgaa::mysqlpool::Options db_options_;
    pb::UserGlobalSettings settings_;
    pb::User::Kind kind_{pb::User::Kind::User_Kind_REGULAR};
    std::vector<std::shared_ptr<Session>> sessions_; // NB: Circular reference.
    std::vector<std::weak_ptr<Publisher>> publishers_;
    std::map<boost::uuids::uuid, Device> devices_;
    mutable std::shared_mutex mutex_;
    mutable std::mutex instance_mutex_;
};


class SessionManager {
public:
    SessionManager(Server& server);

    ~SessionManager() = default;

    // Creates or fetches existing session.
    // Throws on error, or if the user or tenant is not active.
    boost::asio::awaitable<std::shared_ptr<UserContext::Session>> getSession(const ::grpc::ServerContextBase* context, bool allowNewSession = false);

    std::shared_ptr<UserContext::Session> getExistingSession(const ::grpc::ServerContextBase *context);

    void removeSession(const boost::uuids::uuid& sessionId);

    void setUserSettings(const boost::uuids::uuid& userUuid, pb::UserGlobalSettings settings);

    void shutdown();

    boost::asio::awaitable<pb::UserSessions> listSessions();

private:
    void removeSession_(const boost::uuids::uuid& sessionId);
    void startNextTimer();
    void onTimer();
    static boost::asio::io_context& ioContext() noexcept ;
    boost::asio::awaitable<std::shared_ptr<UserContext>> getUserContext(const boost::uuids::uuid& deviceId, const ::grpc::ServerContextBase* context);

    std::shared_mutex mutex_;
    std::unordered_map<boost::uuids::uuid, UserContext::Session *, UuidHash> sessions_;
    std::unordered_map<boost::uuids::uuid, std::shared_ptr<UserContext>, UuidHash> users_;
    Server& server_;
    boost::asio::steady_timer timer_{ioContext()};
    const bool skip_tls_auth_;
};

} // ns
