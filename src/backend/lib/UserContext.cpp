

#include "nextapp/UserContext.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"

#include "nextapp/Server.h"
#include "nextapp/errors.h"
#include "grpc/grpc_security_constants.h"

using namespace std;

namespace nextapp {

namespace {

auto to_string_view(::grpc::string_ref ref) {
    return string_view{ref.data(), ref.size()};
}

auto to_string_view(const boost::mysql::blob_view& blob) {
    return string_view{reinterpret_cast<const char *>(blob.data()), blob.size()};
}

} // anon ns


SessionManager::SessionManager(Server &server)
    : server_{server}
    , skip_tls_auth_{server.config().grpc.tls_mode == "none"}
{
    startNextTimer();
}

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid, const std::string_view timeZone, bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options &dbOptions)
    : user_uuid_{userUuid},
    tenant_uuid_{tenantUuid},
    db_options_{dbOptions} {

    if (timeZone.empty()) {
        tz_ = std::chrono::current_zone();
    } else {
        tz_ = std::chrono::locate_zone(timeZone);
        settings_.set_timezone(string{tz_->name()});
        settings_.set_firstdayofweekismonday(!sundayIsFirstWeekday);
        if (tz_ == nullptr) {
            LOG_DEBUG << "UserContext: Invalid timezone: " << timeZone;
            throw std::invalid_argument("Invalid timezone: " + std::string{timeZone});
        }
    }
}

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid,
                         pb::User::Kind kind,
                         const pb::UserGlobalSettings &settings)
    : user_uuid_{userUuid}, tenant_uuid_{tenantUuid}, settings_(settings)
    , kind_{kind} {

    try {
        if (settings_.timezone().empty()) [[unlikely]] {
            tz_ = std::chrono::current_zone();
            LOG_WARN << "User has no time-zone set. Using " << tz_->name() << ".";
        } else {
            tz_ = std::chrono::locate_zone(settings_.timezone());
        }
    } catch (const std::exception& e) {
        tz_ = std::chrono::current_zone();
        LOG_WARN << "Failed to locate timezone " << settings_.timezone()
                 << ". Using " << tz_->name() << " instead.";

        // TODO: Send a notice to the user about their time zone setting.
    }

    assert(tz_ != nullptr);
    db_options_.time_zone = tz_->name();
    db_options_.reconnect_and_retry_query = true;
}

void UserContext::addPublisher(const std::shared_ptr<Publisher> &publisher)
{
    LOG_TRACE_N << "Adding publisher " << publisher->uuid() << " to user context for user " << userUuid();
    unique_lock lock{mutex_};
    publishers_.push_back(publisher);
}

void UserContext::removePublisher(const boost::uuids::uuid &uuid)
{
    LOG_TRACE_N << "Removing publisher " << uuid << " from user context for user " << userUuid();
    unique_lock lock{mutex_};
    ranges::remove_if(publishers_, [uuid](const auto& p) {
        if (auto pub = p.lock()) {
            return pub->uuid() == uuid;
        } else {
            return true; // Always remove dangling pointers
        }
    });
}

void UserContext::publish(const std::shared_ptr<pb::Update> &update)
{
    LOG_TRACE_N << "Publishing "
                << pb::Update::Operation_Name(update->op())
                << " update to " << publishers_.size() << " subscribers, Json: "
                << toJsonForLog(*update);

    shared_lock lock{mutex_};

    for(auto& weak_pub: publishers_) {
        if (auto pub = weak_pub.lock()) {
            LOG_TRACE_N << "Publish to " << pub->uuid();
            pub->publish(update);
        } else {
            LOG_WARN_N << "Failed to get a pointer to a publisher ";
        }
    }
}

bool UserContext::checkForReplay(const boost::uuids::uuid &deviceId, uint instanceId, uint reqId)
{
    lock_guard lock{instance_mutex_};
    auto& device = devices_[deviceId];
    validateInstanceId(instanceId);

    const auto index = instanceId - 1; // Instance start at 1
    if (reqId <= device.last_request_id.get(index, 0)) {
        LOG_DEBUG_N << "Replay detected for device" << deviceId
                    << ", instanceId=" << instanceId
                    << ", reqId=" << reqId
                    << ", for user=" << userUuid();
        return true;
    }
    device.last_request_id.set(index, reqId);
    return false;
}

void UserContext::resetReplay(const boost::uuids::uuid &deviceId, uint instanceId)
{
    lock_guard lock{instance_mutex_};
    auto& device = devices_[deviceId];
    validateInstanceId(instanceId);

    const auto index = instanceId - 1; // Instance start at 1
    device.last_request_id.set(index, 0);
}

boost::uuids::uuid UserContext::newUuid()
{
    return ::nextapp::newUuid();
}

void UserContext::validateInstanceId(uint instanceId)
{
    if (instanceId < 1 || instanceId > 10) {
        throw server_err{pb::Error::INVALID_ARGUMENT, "Invalid instanceId"};
    }
}

string mapToString(const auto &map) {
    ostringstream out;
    unsigned count = 0;
    for(const auto& [key, value] : map) {
        if (++count > 1) {
            out << ", ";
        }
        out << key << ": " << value;
    }
    return out.str();
}

boost::asio::awaitable<std::shared_ptr<UserContext::Session> > SessionManager::getSession(
    const ::grpc::ServerContextBase* context, bool allowNewSession)
{
    LOG_TRACE << "Getting session for peer at " << context->peer()
              << " context: " << mapToString(context->client_metadata());

    string_view device_id;

    if (!context->auth_context()->IsPeerAuthenticated()) {
        if (skip_tls_auth_) {
            LOG_WARN << "TLS is disabled. Skipping authentication via cert.";
            if (auto it = context->client_metadata().find("did"); it != context->client_metadata().end()) {
                device_id = to_string_view(it->second);
                goto initial_auth_ok;
            }
        }
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }

    {
        auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
        if (v.empty()) {
            throw server_err{pb::Error::NOT_FOUND, "Missing device ID in client cert"};
        }
        device_id = to_string_view(v.front());
    }

initial_auth_ok:

    const auto device_uuid = toUuid(device_id);
    boost::uuids::uuid new_sid = newUuid();

    // Happy path. Just return an existing session.
    if (auto it = context->client_metadata().find("sid"); it != context->client_metadata().end()) {
        auto sid = toUuid(to_string_view(it->second));
        shared_lock lock{mutex_};
        if (auto it = sessions_.find(sid); it != sessions_.end()) {
            auto& session = *it->second;
            if (session.deviceId() == device_uuid) [[likely]] {
                co_return session.shared_from_this();
            }
            LOG_WARN << "Session " << sid << " is not for device " << device_uuid << ". Closing the session.";
            throw server_err{pb::Error::AUTH_FAILED, "Session is not for the connected device"};
        }

        // TODO: Enable when the client can handle a server-assigned session-id.
        //throw server_err{pb::Error::AUTH_FAILED, "Session not found"};
        new_sid = sid;
    } else if (!allowNewSession) {
        throw server_err{pb::Error::AUTH_FAILED, "Session-id 'sid' not found in the gRPC meta-data."};
    }

    // Fetch or create a user context.
    auto ucx = co_await getUserContext(device_uuid, context);
    assert(ucx);

    auto scope = ucx->isAdmin() ? server_.metrics().sessions_admin().scoped() : server_.metrics().sessions_user().scoped();

    auto session = make_shared<UserContext::Session>(ucx, device_uuid, new_sid, std::move(scope));
    {
        unique_lock lock{mutex_};
        sessions_[session->sessionId()] = session.get();
        ucx->addSession(session);
    }
    LOG_INFO << "Added user-session << " << session->sessionId()
             << " for device " << session->deviceId() << " for user " << ucx->userUuid()
             << " from IP " << context->peer();

    // User logged on with a device
    auto db = co_await server_.db().getConnection();
    co_await db.exec("UPDATE device SET lastSeen=NOW(), numSessions=numSessions+1 WHERE id=? AND user=?",
                     device_id,
                     session->user().userUuid());

    // TODO: Add entry in session-table
    // TODO: Check sessions limit for user
    // TODO: Check concurrent devices limit for user

    co_return session;
}

std::shared_ptr<UserContext::Session> SessionManager::getExistingSession(const ::grpc::ServerContextBase *context)
{
    boost::uuids::uuid device_uuid;
    if (!context->auth_context()->IsPeerAuthenticated()) {
        if (skip_tls_auth_) {
            if (auto it = context->client_metadata().find("did"); it != context->client_metadata().end()) {
                device_uuid = toUuid(to_string_view(it->second));
                LOG_WARN << "TLS is disabled. Skipping authentication via cert.";
                goto initial_auth_ok;
            }
        }
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }

    {
        auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
        device_uuid = toUuid(to_string_view(v.front()));
    }

initial_auth_ok:
    assert(device_uuid != boost::uuids::nil_uuid());

    if (auto it = context->client_metadata().find("sid"); it != context->client_metadata().end()) {
        auto sid = toUuid(to_string_view(it->second));
        shared_lock lock{mutex_};
        if (auto it = sessions_.find(sid); it != sessions_.end()) {
            auto& session = *it->second;
            if (session.deviceId() == device_uuid) [[likely]] {
                return session.shared_from_this();
            }
            LOG_WARN << "Session " << sid << " is not for device " << device_uuid << ". Closing the session.";
            throw server_err{pb::Error::AUTH_FAILED, "Session is not for the connected device"};
        }
    } else {
        throw server_err{pb::Error::AUTH_FAILED, "Session-id 'sid' not found in the gRPC meta-data."};
    }

    throw server_err{pb::Error::AUTH_FAILED, "Session not found"};
}

void SessionManager::removeSession(const boost::uuids::uuid &sessionId)
{
    unique_lock lock{mutex_};
    removeSession_(sessionId);
}

void SessionManager::removeSession_(const boost::uuids::uuid &sessionId)
{
    LOG_TRACE_N << "Removing session " << sessionId;
    if (auto it = sessions_.find(sessionId); it != sessions_.end()) {
        auto *session = it->second;
        assert(session);
        if (session) {
            boost::asio::co_spawn(ioContext(), [uid=session->user().userUuid(), devid=session->deviceId(), sessionId] () -> boost::asio::awaitable<void>  {
                LOG_DEBUG_N << "Session " << sessionId << " from device " << devid << " from user " << uid << " is done.";
                co_await Server::instance().db().exec("UPDATE device SET lastSeen=NOW() WHERE id=?", devid);
            }, boost::asio::detached);

            sessions_.erase(it);
            session->user().removeSession(sessionId);
        }
    } else {
        LOG_WARN_N << "Session " << sessionId << " not found.";
    }
}

void SessionManager::setUserSettings(const boost::uuids::uuid &userUuid, pb::UserGlobalSettings settings)
{
    unique_lock lock{mutex_};
    if (auto it = users_.find(userUuid); it != users_.end()) {
        it->second->setSettings(settings);
    } else {
        LOG_WARN_N << "User " << userUuid << " not found in cache";
    }
}

void SessionManager::shutdown()
{

    timer_.cancel();
    std::vector<std::shared_ptr<UserContext::Session>> to_clean;
    bool has_more = false;

    do {
        {
            unique_lock lock{mutex_};
            while(!sessions_.empty()) {
                LOG_DEBUG_N << "Removing session " << sessions_.begin()->first << " due to shutdown.";
                auto session = sessions_.begin()->second;
                to_clean.push_back(session->shared_from_this());
                removeSession_(session->sessionId());
            }
        }

        while(!to_clean.empty()) {
            auto session = to_clean.back();
            to_clean.pop_back();
            session->cleanup();
        }

        {
            unique_lock lock{mutex_};
            has_more = !sessions_.empty();
        }

    } while(has_more);
}

void SessionManager::startNextTimer()
{
    timer_.expires_after(chrono::seconds{server_.config().svr.session_timer_interval_sec});
    timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            LOG_WARN << "Timer error: " << ec.message();
            return;
        }

        if (!server_.is_done()) {
            onTimer();
            startNextTimer();
        }
    });
}

void SessionManager::onTimer()
{
    LOG_TRACE_N << "Checking sessions...";

    const auto secs = server_.config().svr.session_timeout_sec;
    const auto now = chrono::steady_clock::now();

    std::vector<std::shared_ptr<UserContext::Session>> to_clean;

    {
        unique_lock lock{mutex_};
        for(auto it = sessions_.begin(); it != sessions_.end();) {
            auto& session = it->second;
            ++it;

            const auto expieres = session->touched() + chrono::seconds{secs};

            // Show the difference between the two time points in seconds
            auto diff = chrono::duration_cast<chrono::seconds>(now - session->touched());
            LOG_TRACE_N << "Session " << session->sessionId() << " touched " << diff.count() << " seconds ago.";

            if (expieres < now) {
                to_clean.push_back(session->shared_from_this());
                LOG_DEBUG_N << "Session " << session->sessionId() << " expired.";
                removeSession_(session->sessionId());
            }
        }
    }

    while(!to_clean.empty()) {
        LOG_INFO << "Session " << to_clean.back()->sessionId() << " is done.";
        auto session = to_clean.back();
        to_clean.pop_back();
        session->cleanup();
    }
}

boost::asio::io_context &SessionManager::ioContext() noexcept
{
    return Server::instance().ctx();
}

boost::asio::awaitable<std::shared_ptr<UserContext> > SessionManager::getUserContext(
    const boost::uuids::uuid &deviceId,
    const ::grpc::ServerContextBase *context)
{

    auto db = co_await server_.db().getConnection();

    const auto devid = to_string(deviceId);

    string uid;

    {
        auto res = co_await db.exec(
            "SELECT user, certHash, enabled FROM device where id=? ", devid);
        if (res.rows().empty()) {
            LOG_WARN << "Failed to lookup device " << devid << " in the database, although the user appears to have a signed cert with that ID.";
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
        enum Cols { USER, CERT_HASH, ENABLED };

        const auto& row = res.rows().front();
        uid = row.at(USER).as_string();

        if (row.at(ENABLED).as_int64() == 0) {
            throw server_err{pb::Error::DEVICE_DISABLED, "Device is disabled"};
        }

        {
            // Happy path
            shared_lock lock{mutex_};
            if (auto id = users_.find(toUuid(uid)) ; id != users_.end()) {
                LOG_TRACE << "UserContext: Found user " << uid << " in cache";
                co_return id->second;
            }
        }

        // TODO MAYBE: Validate the cert hash in the database aginst the presented certificate.
        //             gRPC just validates the that the cert is signed. We could check the hash
        //             to make 100% sure that the device match the presented cert.
    }

    auto res = co_await db.exec(
        "SELECT u.tenant, t.kind, u.kind, t.state, u.active, s.settings FROM user u "
        "JOIN tenant t on t.id=u.tenant "
        "LEFT JOIN user_settings s on s.user=u.id "
        "WHERE u.id=? ", uid);

    enum Cols { TENANT, TENANT_KIND, USER_KIND, TENANT_STATE, USER_ACTIVE, SETTINGS };
    if (res.rows().empty()) [[unlikely]] {
        LOG_ERROR << "Database inconsistency found. A device " << devid
                  << " is linked to a user " << uid << " that does not exist.";
        throw server_err{pb::Error::AUTH_FAILED, "User not found"};
    } else {
        const auto& row = res.rows().front();
        const auto tenant = row.at(TENANT).as_string();
        pb::Tenant::State tenant_state;
        if (!pb::Tenant::State_Parse(toUpper(row.at(TENANT_STATE).as_string()), &tenant_state)) {
            LOG_WARN_N << "Failed to parse Tenant::State from " << row.at(TENANT_STATE).as_string();
            throw runtime_error{"Failed to parse Tenant::State"};
        }
        if (tenant_state == pb::Tenant::State::Tenant_State_SUSPENDED) {
            throw server_err{pb::Error::TENANT_SUSPENDED, "Tenant account is suspended"};
        }
        bool active = row.at(USER_ACTIVE).as_int64() != 0;
        if (!active) {
            throw server_err{pb::Error::USER_SUSPENDED, "User account is inactive"};
        }
        pb::UserGlobalSettings tmp_settings;
        if (row.at(SETTINGS).is_blob()) {
            const auto blob = row.at(SETTINGS).as_blob();
            if (!tmp_settings.ParseFromArray(blob.data(), blob.size())) {
                LOG_WARN_N << "Failed to parse UserGlobalSettings for user " << uid;
                throw runtime_error{"Failed to parse UserGlobalSettings"};
            }
        }

        pb::User::Kind ukind;
        if (!pb::User::Kind_Parse(toUpper(row.at(USER_KIND).as_string()), &ukind)) {
            LOG_WARN_N << "Failed to parse User::Kind from " << row.at(USER_KIND).as_string();
            throw runtime_error{"Failed to parse User::Kind"};
        }

        if (tenant_state == pb::Tenant::State::Tenant_State_PENDING_ACTIVATION) {
            co_await db.exec("UPDATE tenant SET state='active' WHERE id=?", tenant);
            LOG_INFO << "Activated pending tenant " << tenant << " because user " << uid << " logged in.";
        }

        auto ucx = make_shared<UserContext>(tenant, uid, ukind, tmp_settings);
        {
            unique_lock lock{mutex_};
            users_[toUuid(uid)] = ucx;
        }

        co_return ucx;
    }

    assert(false);
    throw runtime_error{"Failed to create UserContext"};
}

void UserContext::Session::touch() {
    //last_access_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
    last_access_ = chrono::steady_clock::now();
    LOG_TRACE_N << "Touching session " << sessionid_ << " for user " << user_->userUuid() << " on device " << deviceid_
                << " lifetime="
                << std::chrono::duration_cast<chrono::seconds>(last_access_.load() - created_) << "s";
}

std::chrono::steady_clock::time_point UserContext::Session::touched() const {
    //return last_access_.load(std::memory_order_relaxed);
    return last_access_;
}


} // ns
