
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
#include "cpp-push/cpp-push.h"
//#include "nextapp/logging.h"

namespace nextapp {

class Server;
class Publisher;


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

    /*! \brief Sends push notifications to a user on a specific device.
     *
     */
    class PushNotifications {
    public:
        PushNotifications(boost::uuids::uuid userid, boost::uuids::uuid deviceid, std::string token,
                          std::shared_ptr<jgaa::cpp_push::Pusher> pusher)
            : userid_{std::move(userid)}, deviceid_{std::move(deviceid)}
            , token_{std::move(token)}, pusher_{std::move(pusher)} {}

        PushNotifications(const PushNotifications&) = delete;
        PushNotifications& operator=(const PushNotifications&) = delete;

        /*! \brief Sends a push notification to the device.
         *
         *  In the current implementation, the push notifications is sent immediately.
         *  If it fails, there is no retry logic.
         *
         * @param pn The push notification to send.
         * @return true if the notification was sent successfully, false otherwise.
         */
        [[nodiscard]] boost::asio::awaitable<bool> sendNotification(std::shared_ptr<nextapp::pb::Update>& message);

        const boost::uuids::uuid& userId() const noexcept {
            return userid_;
        }

        const boost::uuids::uuid& deviceId() const noexcept {
            return deviceid_;
        }

    private:
        boost::uuids::uuid userid_;
        boost::uuids::uuid deviceid_;
        std::string token_;
        std::shared_ptr<jgaa::cpp_push::Pusher> pusher_;
    };

    class Device {
    public:
        using value_t = std::optional<uint32_t>;
        using values_t = ValueContainer<value_t, 10>;
        std::optional<values_t> last_request_id;
        std::shared_ptr<PushNotifications> push_notifications_;
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

        auto userPtr() const noexcept {
            return user_;
        }

        void addPublisher(const std::shared_ptr<Publisher>& publisher);

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

        void handlePushState(const nextapp::pb::UpdatesReq::WithPush& wp);

    private:
        boost::asio::awaitable<void> processPushState(nextapp::pb::UpdatesReq::WithPush wp);

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

    pb::UserGlobalSettings settings() const noexcept {
        std::shared_lock lock(mutex_);
        return settings_;
    }

    const bool autoStartNextWorkSession() const noexcept {
        std::shared_lock lock(mutex_);
        return settings_.autostartnextworksession();
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
        std::unique_lock lock(mutex_);
        std::erase_if(sessions_, [&sessionId](const auto& s) { return s->sessionId() == sessionId; });
        purgeExpiredPublishers();
    }

    bool hasNoSessions() const {
        std::shared_lock lock(mutex_);
        return sessions_.empty();
    }

    void setSettings(pb::UserGlobalSettings settings) {
        settings_ = std::move(settings);
    }

    void addPublisher(const std::shared_ptr<Publisher>& publisher);
    void removePublisher(const boost::uuids::uuid& uuid);
    [[nodiscard]] boost::asio::awaitable<void> publish(std::shared_ptr<pb::Update>& message);
    void publishUpdates(std::shared_ptr<pb::Update> &update, std::set<boost::uuids::uuid>* devices = {});
    [[nodiscard]] boost::asio::awaitable<bool> checkForReplay(const boost::uuids::uuid& deviceId, uint instanceId, uint reqId);
    [[nodiscard]] boost::asio::awaitable<void> resetReplay(const boost::uuids::uuid& deviceId, uint instanceId);
    void saveReplayStateForDevice(const boost::uuids::uuid& deviceId);
    [[nodiscard]] boost::asio::awaitable<Device::value_t>getLastReqId(const boost::uuids::uuid& deviceId,
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

    std::optional<uint32_t> getLastReadNotification() const noexcept {
        if (auto lrn = last_read_notification_id_.load(); lrn > 0) {
            return static_cast<uint32_t>(lrn);
        }
        return {};
    }

    void setLastReadNotification(uint32_t id) noexcept;

    bool valid() const noexcept {
        return valid_.load(std::memory_order::relaxed);
    }

    void setAsInvalid() {
        valid_ = false;
    }

    /*! Reloads the pushers for all the devices for this user.
     *
     *  Makes sure that the pusher's in memory corresponds with the
     *  database state.
     */
    boost::asio::awaitable<void> reloadPushers();

    // boost::asio::awaitable<void> addPusher(const boost::uuids::uuid& deviceId,
    //                                        const std::string& token,
    //                                        std::shared_ptr<jgaa::cpp_push::Pusher> pusher);
    // boost::asio::awaitable<void> removePusher(const boost::uuids::uuid& deviceId);

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
    std::atomic_int32_t last_read_notification_id_{-1};
    mutable std::shared_mutex mutex_;
    mutable std::mutex instance_mutex_;
    std::atomic_bool valid_{true}; // Indicates if the user context is still valid.
};

class Publisher {
public:
    virtual ~Publisher() = default;

    virtual void publish(const std::shared_ptr<pb::Update>& message) = 0;
    virtual void close() = 0;
    virtual std::weak_ptr<UserContext::Session>& getSessionWeakPtr() = 0;

    auto& uuid() const noexcept {
        return uuid_;
    }

private:
    const boost::uuids::uuid uuid_ = newUuid();
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

    boost::asio::awaitable<void> publishNotification(const nextapp::pb::Notification& notification);

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
