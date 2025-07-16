

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

boost::asio::awaitable<pb::UserSessions> SessionManager::listSessions()
{
    pb::UserSessions us;
    lock_guard lock{mutex_};
    for(const auto& [_, ux] : users_) {
        auto *u = us.add_sessions();
        assert(u);
        u->mutable_userid()->set_uuid(ux->userUuid());
        u->mutable_tenantid()->set_uuid(ux->tenantUuid());
        u->set_publishmessageid(ux->currentPublishId());
        u->set_kind(ux->isAdmin() ? pb::User_Kind::User_Kind_SUPER : pb::User_Kind::User_Kind_REGULAR);

        for(const auto& s : ux->sessions()) {
            auto *session = u->add_sessions();
            assert(session);
            session->mutable_deviceid()->set_uuid(to_string(s->deviceId()));
            session->mutable_sessionid()->set_uuid(to_string(s->sessionId()));
            session->set_durationinseconds(
                std::chrono::duration_cast<std::chrono::seconds>(s->currentDuration()).count());
            session->set_lastseensecsago(
                std::chrono::duration_cast<std::chrono::seconds>(s->durationSinceLastAccess()).count());

            if (auto d = ux->getDevice(s->deviceId()); d.has_value()) {
                if ( d->last_request_id) {
                    for(auto ix = 0u; ix < d->last_request_id->size(); ++ix) {
                        if (auto lri = d->last_request_id->get(ix)) {
                            session->add_lastreqids(*lri);
                        } else {
                            session->add_lastreqids(-1);
                        }
                    }
                }
            }
        };
    }

    co_return us;
}

boost::asio::awaitable<void> SessionManager::publishNotification(const pb::Notification &notification)
{
    // Create notification message
    auto msg = make_shared<pb::Update>();
    msg->set_op(pb::Update::Operation::Update_Operation_ADDED);
    auto * n = msg->mutable_notifications();
    n->add_notifications()->CopyFrom(notification);

    auto publish = [&](auto& user) -> boost::asio::awaitable<void> {
        // See if the user is in the cache
        shared_ptr<UserContext> u;

        {
            lock_guard lock{mutex_};
            if (auto it = users_.find(toUuid(notification.touser().uuid())); it != users_.end()) {
                u = it->second;
            }
        }

        if (u) {
            LOG_DEBUG_N << "Publishing notification #" << notification.id() << " to user " << u->userUuid();
            co_await u->publish(msg);
        } else {
            LOG_TRACE_N << "User " << notification.touser().uuid() << " not found in cache";
        };
        co_return;
    };

    if (notification.has_touser()) {
        co_await publish(notification.touser());
        co_return;
    }

    if (notification.has_totenant()) {

        vector<boost::uuids::uuid> users;

        {
            // TODO: Add indexing on tenant
            auto res = co_await server_.db().exec("SELECT id FROM user WHERE tenant=?", notification.totenant().uuid());

            users.reserve(res.rows().size());
            // Build a vector with user uuids
            for(const auto& row : res.rows()) {
                users.push_back(toUuid(row.front().as_string()));
            }
        }

        for(const auto& u : users) {
            co_await publish(u);
        }

        co_return;
    }

    // Publish to all users we have sessions for
    // Use the sessions instead of user_ to avoid overhead for offline users.
    set<shared_ptr<UserContext>> users;
    {
        unique_lock lock{mutex_};
        for(const auto& [_, ses] : sessions_) {
            users.emplace(ses->userPtr());
        }
    }

    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::steady_timer timer(executor);
    const auto ms = server_.config().options.notification_delay_ms;

    for(auto& u: users) {
        LOG_DEBUG_N << "Publishing mass-notification #" << notification.id() << " to user " << u->userUuid();
        co_await u->publish(msg);

        // co_await on a asio timer for a short period of time
        // to avoid server overload.
        if (ms) {
            timer.expires_after(std::chrono::milliseconds(ms));
            co_await timer.async_wait(boost::asio::use_awaitable);
        }
    }

    co_return;
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

void UserContext::publishUpdates(std::shared_ptr<pb::Update> &update, set<boost::uuids::uuid>* devices) {
    {
        update->set_messageid(++publish_message_id_);
        LOG_TRACE_N << "Publishing "
                    << pb::Update::Operation_Name(update->op())
                    << " update to " << publishers_.size() << " subscribers, Json: "
                    << toJsonForLog(*update);

        shared_lock lock{mutex_};
        purgeExpiredPublishers();
        for(auto& weak_pub: publishers_) {
            if (auto pub = weak_pub.lock()) {
                if (auto session = pub->getSessionWeakPtr().lock()) {
                    LOG_TRACE_N << "Publish #" << update->messageid() << " to " << pub->uuid();
                    if (pub->publish(update)) {
                        // The device-list will exclude the sessions device device from push notification for this update.
                        // We only add the device to the list if publish() succeeded and the update was
                        // accepted for delivery over gRPC.
                        if (devices) {
                            devices->emplace(session->deviceId());
                        }
                    }
                } else {
                    LOG_TRACE_N << "Publisher " << pub->uuid() << " has no valid session. Skipping.";
                }
            } else {
                LOG_WARN_N << "Failed to get a pointer to a publisher."
                           << " for user " << userUuid();
            }
        }
    } // unlock
}

boost::asio::awaitable<void> UserContext::publish(std::shared_ptr<pb::Update> &update)
{
    set<boost::uuids::uuid> devices;

    publishUpdates(update, &devices);

    {
        unique_lock lock{mutex_};
        // Remove all publishers that are no longer valid
        ranges::remove_if(publishers_, [&](const auto& weak_pub) {
            if (auto pub = weak_pub.lock()) {
                return false; // Keep valid publishers
            } else {
                LOG_TRACE_N << "Removing expired publisher for user " << userUuid();
                return true; // Remove expired weak_ptrs
            }
        });
    }

    // Filter out updates that are not for push notifications
    if (!isForPush(*update)) {
        co_return;
    }

    vector<std::shared_ptr<PushNotifications>> pn;

    // Briefly lock the instance and copy any push notification handlers
    // for devices that are not already published to in the `pn` set.
    {
        unique_lock lock{instance_mutex_};
        for(auto& [id, dev] : devices_) {
            if (dev.push_notifications_ && ! devices.contains(id)) {
                pn.emplace_back(dev.push_notifications_);
            }
        }
    }  // unlock

    // Publish to push notification handlers
    // Currently we do this on a "best effort" basis with no queuing or retry.
    // TODO: Apply rate limiting here
    for(auto handler : pn) {
        try {
            co_await handler->sendNotification(update);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to send push notification for update #" << update->messageid()
                       << " to device " << handler->deviceId() << ": " << e.what();
        }
    }
}

void UserContext::purgeExpiredPublishers()
{
    // Remove expired weak_ptrs
    auto num_purged = 0u;
    publishers_.erase(
        std::remove_if(
            publishers_.begin(), publishers_.end(),
            [&](const std::weak_ptr<Publisher>& weak_pub) {
                if (weak_pub.expired()) {
                    ++num_purged;
                    return true;
                }
                return false;
            }
            ),
        publishers_.end()
        );

    if (num_purged > 0) {
        LOG_DEBUG_N << "Purged " << num_purged << " expired publishers"
                                                  " for user " << userUuid();
    };
}

boost::asio::awaitable<bool>
UserContext::checkForReplay(const boost::uuids::uuid &deviceId, uint instanceId, uint reqId)
{
    validateInstanceId(instanceId);
    const auto index = instanceId - 1; // Instance start at 1

    bool rval = false;
    Device::value_t last_req_id;

    auto process = [&] {
        LOG_TRACE << "Replay check for device " << deviceId
                  << ", instanceId=" << instanceId
                  << ", reqId=" << reqId
                  << ", last_req_id=" << *last_req_id
                  << ", user=" << userUuid();

        if (*last_req_id >= reqId) {
            LOG_DEBUG << "Replay detected for device" << deviceId
                      << ", instanceId=" << instanceId
                      << ", reqId=" << reqId
                      << ", for user=" << userUuid();
            rval = true;
        }
    };

    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if ( device.last_request_id) {
            last_req_id = device.last_request_id->get(index);

            // If we have a value in memory, use it now. We don't want to aquire the
            // lock twice, but we need to do that if we access the db.
            if (last_req_id.has_value()) {
                process();
                device.last_request_id->set(index, max(*last_req_id, reqId));
                co_return rval;
            };
        }
    }

    // Get the value from the database
    if (auto v = co_await getLastReqId(deviceId, instanceId, true); v.has_value()) {
        last_req_id = *v;
    } else {
        last_req_id = 0; // Initialize it
    }

    assert(last_req_id.has_value());
    process();

    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if (!device.last_request_id) {
            device.last_request_id.emplace();
        }
        device.last_request_id->set(index, max(*last_req_id, reqId));
    }

    co_return rval;
}

boost::asio::awaitable<UserContext::Device::value_t>
UserContext::getLastReqId(const boost::uuids::uuid &deviceId, uint instanceId, bool lookupInDbOnly) {
    validateInstanceId(instanceId);
    const auto index = instanceId - 1; // Instance start at 1

    if (!lookupInDbOnly) {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        if (device.last_request_id) {
            if (auto value = device.last_request_id->get(index); value.has_value()) {
                co_return value;
            }
        }
    }

    // Fetch from database
    auto& db = Server::instance().db();
    auto res = co_await db.exec("SELECT request_id FROM request_state WHERE userid=? AND devid=? AND instance=?",
                                userUuid(), deviceId, instanceId);
    if (!res.rows().empty()) {
        const auto last_req_id = static_cast<uint32_t>(res.rows().front().at(0).as_int64());
        {
            lock_guard lock{instance_mutex_};
            auto& device = devices_[deviceId];
            if (!device.last_request_id) {
                device.last_request_id.emplace();
            }
            device.last_request_id->set(index, last_req_id);
        }

        co_return last_req_id;
    }

    co_return std::nullopt;
}

boost::asio::awaitable<void> UserContext::resetReplay(const boost::uuids::uuid &deviceId, uint instanceId)
{
    {
        lock_guard lock{instance_mutex_};
        auto& device = devices_[deviceId];
        validateInstanceId(instanceId);

        const auto index = instanceId - 1; // Instance start at 1
        if (!device.last_request_id) {
            device.last_request_id.emplace();
        }
        device.last_request_id->set(index, 0);
    }
    co_await saveLastReqIds(deviceId);
}

void UserContext::saveReplayStateForDevice(const boost::uuids::uuid &deviceId)
{
    boost::asio::co_spawn(Server::instance().ctx(), [self = shared_from_this(), deviceId] () -> boost::asio::awaitable<void> {
        co_await self->saveLastReqIds(deviceId);
    }, boost::asio::detached);
}

void UserContext::Session::setHasPush(bool enabled) {
    has_push_ = enabled;
    if (auto p = publisher_.lock()) {
        p->setHasPush(enabled);
    }
}

void UserContext::Session::push(const std::shared_ptr<pb::Update> &message)
{
    if (!hasPush()) {
        LOG_TRACE_N << "Skipping push notification for update #" << message->messageid()
                    << " for user " << user().userUuid() << " on device " << deviceId()
                    << " because push notifications are assumed disabled on this device.";
        return;
    }

    boost::asio::co_spawn(Server::instance().ctx(), [user = user_, msg=message, devid=deviceId()] () -> boost::asio::awaitable<void> {
        try {
            co_await user->push(msg, devid);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to push notification for update #" << msg->messageid()
                       << " for user " << user->userUuid() << " on device " << devid
                       << ": " << e.what();
        }
    }, boost::asio::detached);
}

boost::asio::awaitable<void> UserContext::push(const std::shared_ptr<pb::Update> &message, const boost::uuids::uuid deviceId)
{
    std::shared_ptr<PushNotifications> handler;

    // Briefly lock the instance and copy any push notification handlers
    // for devices that are not already published to in the `pn` set.
    {
        unique_lock lock{instance_mutex_};
        for(auto& [id, dev] : devices_) {
            if (dev.push_notifications_ && id == deviceId) {
                handler= dev.push_notifications_;
                break;
            }
        }
    }  // unlock

    if (handler) {
        try {
            // TODO: Apply rate limiting here
            co_await handler->sendNotification(message);
        } catch (const std::exception& e) {
            LOG_WARN_N << "Failed to send push notification for update #" << message->messageid()
            << " to device " << handler->deviceId() << ": " << e.what();
        }
    }
    co_return;
};

bool UserContext::isForPush(const pb::Update &update) {
    return update.has_calendarevents();
}

boost::asio::awaitable<void> UserContext::reloadPushers() {

    if (!Server::instance().config().push_enabled) {
        co_return;
    }

    auto db = co_await Server::instance().db().getConnection();
    const auto res = co_await db.exec("SELECT id, pushType, pushToken FROM device WHERE user=? AND pushType IS NOT NULL", userUuid());

    enum Cols {
        ID,
        TYPE,
        TOKEN
    };

    {
        unique_lock lock{instance_mutex_};

        // Remove all existing push handlers
        for(auto & [_, device] : devices_) {
            device.push_notifications_.reset();
        }

        // Add new push handlers
        for(const auto& row : res.rows()) {
            const auto deviceId = toUuid(row[ID].as_string());
            const auto type = row[TYPE].as_string();
            const auto token = row[TOKEN].as_string();

            if (type.empty() || token.empty()) {
                LOG_TRACE_N << "Skipping device " << deviceId << " with empty type or token";
                continue;
            }

            auto& device = devices_[deviceId];
            std::shared_ptr<jgaa::cpp_push::Pusher> pusher;
            if (type == "google") {
                pusher = Server::instance().getGooglePusher();
            }

            if (pusher) {
                LOG_TRACE_N << "Creating push notifications handler for device "
                            << deviceId << " with type " << type << " and token " << token.substr(0, 16) << "...";
                device.push_notifications_ = make_shared<PushNotifications>(toUuid(userUuid()), deviceId, token, pusher);
            } else {
                LOG_WARN_N << "No pusher found for device " << deviceId
                            << " with type " << type << " and token " << token;
            }
        }
    }
}

void UserContext::Session::handlePushState(const pb::PushNotificationConfig &wp)
{
    boost::asio::co_spawn(Server::instance().ctx(), [&]() -> boost::asio::awaitable<void> {
        co_await processPushState(wp);
    }, boost::asio::detached);
}

boost::asio::awaitable<void> UserContext::Session::processPushState(pb::PushNotificationConfig wp)
{
    bool enabled = true;
    {
        auto db = co_await Server::instance().db().getConnection();
        switch(wp.kind()) {
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_DISABLE:
    disable:
            enabled = false;
            LOG_DEBUG_N << "Disabling push notifications for device " << deviceid_ << " for user " << user().userUuid();
            co_await db.exec("UPDATE device SET pushType=NULL, pushToken=NULL WHERE id=? AND user=?",
                    deviceid_, user().userUuid());
            break;
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_GOOGLE:
            LOG_DEBUG_N << "Enabling Google push notifications for device " << deviceid_ << " for user " << user().userUuid();
            if (wp.token().empty()) {
                LOG_INFO_N << "Google push token is empty. Disabling push notifications for device " << deviceid_ << " for user " << user().userUuid();
                goto disable;
            }
            co_await db.exec("UPDATE device SET pushType='google', pushToken=? WHERE id=? AND user=?",
                    wp.token(), deviceid_, user().userUuid());
            break;
        case pb::PushNotificationConfig::Kind::PushNotificationConfig_Kind_APPLE:
            LOG_WARN_N << "Apple not supported for push notifications yet.";
            goto disable;
            break;
        default:
            LOG_WARN_N << "Unknown push notification kind: " << pb::PushNotificationConfig::Kind_Name(wp.kind());
            throw server_err{pb::Error::INVALID_ARGUMENT, "Unknown push notification kind"};
        }
    }

    co_await user_->reloadPushers();
    setHasPush(enabled);

    co_return;
}

void UserContext::setLastReadNotification(uint32_t id) noexcept {
    int id_int = static_cast<int>(id);
    if (id_int < 0) {
        LOG_WARN_N << "Invalid last read notification ID: " << id;
        return;
    }
    atomicSetIfGreater(last_read_notification_id_, id_int);
}

boost::asio::awaitable<void> UserContext::saveLastReqIds(const boost::uuids::uuid& deviceId) {
    try {
        Device::values_t values;
        {
            lock_guard lock{instance_mutex_};
            if (auto it = devices_.find(deviceId); it != devices_.end() && it->second.last_request_id.has_value()) {
                values = it->second.last_request_id.value();
            } else {
                co_return;
            }
        }
        const string devid_str = to_string(deviceId);
        auto db = co_await Server::instance().db().getConnection();
        for (size_t i = 0; i < values.size(); ++i) {
            auto v = values.get(i, 0);
            if (!v.has_value()) {
                continue;
            }
            co_await db.exec(R"(INSERT INTO request_state (userid, devid, instance, request_id)
                    VALUES (?, ?, ?, ?)
                    ON DUPLICATE KEY UPDATE
                        request_id = VALUES(request_id),
                        last_update = CURRENT_TIMESTAMP)",
                             userUuid(),  deviceId, i + 1, values.get(i, 0));
        }
    } catch(const std::exception& e) {
        LOG_WARN << "Failed to save replay state for device " << deviceId
                 << " for user " << userUuid() << ". Error: " << e.what();
    }
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
            if (!session.user().valid()) {
                LOG_DEBUG_N << "Session " << sid << " for device " << device_uuid
                            << " is not valid because the user context is not valid. Removing it.";
                // User context is not valid, remove the session.
                removeSession_(sid);
                throw server_err{pb::Error::AUTH_FAILED, "User context is no longer valid. Did you delete your user account?"};
            }
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

            // Store user UUID before erasing session
            const auto userUuid = toUuid(session->user().userUuid());
            sessions_.erase(it);
            session->user().removeSession(sessionId);
            
            // Check if UserContext has no more sessions and remove it from memory
            if (session->user().hasNoSessions()) {
                LOG_DEBUG_N << "Removing UserContext for user " << userUuid << " as it has no more sessions";
                users_.erase(userUuid);
            }
        }
    } else {
        LOG_WARN_N << "Session " << sessionId << " not found.";
    }
}

void UserContext::Session::cleanup() {
    LOG_TRACE_N << "Cleaning up session " << sessionid_ << " for user " << user_->userUuid() << " on device " << deviceid_;
    for(auto& c : cleanup_) {
        try {
            c();
        } catch(const std::exception& e) {
            LOG_WARN_N << "Exception in cleanup: " << e.what();
        }
    }

    user().saveReplayStateForDevice(deviceid_);
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

        co_await ucx->reloadPushers();

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

boost::asio::awaitable<bool> UserContext::PushNotifications::sendNotification(const std::shared_ptr<nextapp::pb::Update>& message)
{
    assert(pusher_);
    if (!pusher_->isReady()) {
        LOG_WARN_N << "Push notification pusher is closed. Cannot send notification.";
        co_return false;
    }

    enum DataSlots{
        KIND, MESSAGE, _NUM_SLOTS
    };

    // Note that the data is just a view of the values. They are not copied.
    array<pair<string_view, string_view>, _NUM_SLOTS> data;

    if (message->has_calendarevents()) {
        data[KIND] = {"kind", "calendar-event"};
    }

    if (data[KIND].first.empty()) {
        LOG_TRACE_N << "Push notification has no data to send.";
        co_return false;
    }

    // Serialize the message to protobuf binary and base-64 encode it.
    string serialized;
    if (!message->SerializeToString(&serialized)) {
        LOG_WARN_N << "Failed to serialize message";
        co_return false;
    }
    const auto value = Base64Encode(serialized);

    // TODO encrypt so Google can't snoop on this
    data[MESSAGE] = {"message", value};

    jgaa::cpp_push::PushMessage pm;
    pm.to = token_;
    pm.data = data;
    pm.type = jgaa::cpp_push::PushMessage::PushType::DATA;

    auto res = co_await pusher_->push(pm);
    if (!res) {
        LOG_WARN_N << "Failed to send push notification: " << res.message();
        co_return false;
    }

    co_return true;
}


} // ns
