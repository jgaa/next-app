
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

class UserContext : public std::enable_shared_from_this<UserContext> {
public:
    class Session : public std::enable_shared_from_this<Session> {
    public:

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

    private:
        std::shared_ptr<UserContext> user_{}; // NB: Circular reference.
        const boost::uuids::uuid sessionid_;
        const boost::uuids::uuid deviceid_;
        const Metrics::gauge_scoped_t instance_scope_;
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
        return settings_;
    }

    bool isAdmin() const noexcept {
        return kind_ == pb::User::Kind::User_Kind_SUPER;
    }

    auto kind() const noexcept {
        return kind_;
    }

    void addSession(std::shared_ptr<Session> session) {
        sessions_.push_back(std::move(session));
    }

    void setSettings(pb::UserGlobalSettings settings) {
        settings_ = std::move(settings);
    }

    void addPublisher(const std::shared_ptr<Publisher>& publisher);
    void removePublisher(const boost::uuids::uuid& uuid);
    void publish(const std::shared_ptr<pb::Update>& message);

private:
    static boost::uuids::uuid newUuid();

    std::string user_uuid_;
    std::string tenant_uuid_;
    const std::chrono::time_zone* tz_{};
    jgaa::mysqlpool::Options db_options_;
    pb::UserGlobalSettings settings_;
    pb::User::Kind kind_{pb::User::Kind::User_Kind_REGULAR};
    std::vector<std::shared_ptr<Session>> sessions_; // NB: Circular reference.
    std::vector<std::weak_ptr<Publisher>> publishers_;
    std::shared_mutex mutex_;
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

private:
     boost::asio::awaitable<std::shared_ptr<UserContext>> getUserContext(const boost::uuids::uuid& deviceId, const ::grpc::ServerContextBase* context);

    std::shared_mutex mutex_;
    std::unordered_map<boost::uuids::uuid, UserContext::Session *, UuidHash> sessions_;
    std::unordered_map<boost::uuids::uuid, std::shared_ptr<UserContext>, UuidHash> users_;
    Server& server_;
    const bool skip_tls_auth_;
};

} // ns
