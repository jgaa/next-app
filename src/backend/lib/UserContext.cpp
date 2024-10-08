

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

boost::uuids::uuid UserContext::newUuid()
{
    return ::nextapp::newUuid();
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

    if (!context->auth_context()->IsPeerAuthenticated()) {
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }

    string_view device_id;
    {
        auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
        if (v.empty()) {
            throw server_err{pb::Error::NOT_FOUND, "Missing device ID in client cert"};
        }
        device_id = to_string_view(v.front());
    }

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

    auto session = make_shared<UserContext::Session>(ucx, device_uuid, new_sid);
    {
        unique_lock lock{mutex_};
        sessions_[session->sessionId()] = session.get();
        ucx->addSession(session);
    }
    LOG_INFO << "Added user-session << " << session->sessionId()
             << " for device " << session->deviceId() << " for user " << ucx->userUuid()
             << " from IP " << context->peer();

    // TODO: Add entry in session-table
    // TODO: Check sessions limit for user
    // TODO: Check concurrent devices limit for user

    co_return session;
}

std::shared_ptr<UserContext::Session> SessionManager::getExistingSession(const ::grpc::ServerContextBase *context)
{
    if (!context->auth_context()->IsPeerAuthenticated()) {
        throw server_err{pb::Error::AUTH_FAILED, "Not authenticated by gRPC!"};
    }
    auto v = context->auth_context()->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
    auto device_id = to_string_view(v.front());
    const auto device_uuid = toUuid(device_id);

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

void SessionManager::setUserSettings(const boost::uuids::uuid &userUuid, pb::UserGlobalSettings settings)
{
    unique_lock lock{mutex_};
    if (auto it = users_.find(userUuid); it != users_.end()) {
        it->second->setSettings(settings);
    } else {
        LOG_WARN_N << "User " << userUuid << " not found in cache";
    }
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
            "SELECT user, certHash FROM device where id=? ", devid);
        if (res.rows().empty()) {
            LOG_WARN << "Failed to lookup device " << devid << " in the database, although the user appears to have a signed cert with that ID.";
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
        enum Cols { USER, CERT_HASH };

        const auto& row = res.rows().front();
        uid = row.at(USER).as_string();

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



} // ns
