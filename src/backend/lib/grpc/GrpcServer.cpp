
#include "shared_grpc_server.h"
#include "nextapp/util.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

#include <grpcpp/support/server_interceptor.h>

std::ostream& operator << (std::ostream& out, const nextapp::grpc::RequestCtx& ctx) {
    return out << "RequestCtx{"
               << "session=" << ctx.session().sessionId()
               << ", user=" << ctx.uctx->userUuid()
               << ", tenant=" << ctx.uctx->tenantUuid()
               << ", device=" << ctx.session().deviceId()
               << "}";
}


namespace logfault {
std::pair<bool /* json */, std::string /* content or json */> toLog(const nextapp::grpc::RequestCtx& ctx, bool json) {

    if (json) {
        return make_pair(true, format(R"("session":"{}", "user":"{}", "tenant":"{}", "device":"{}")",
                                      boost::uuids::to_string(ctx.session().sessionId()),
                                      ctx.uctx->userUuid(),
                                      ctx.uctx->tenantUuid(),
                                      boost::uuids::to_string(ctx.session().deviceId())));
    }

    return make_pair(false, format("RequestCtx{{session={}, user={}, tenant={}, device={}}}",
                                   boost::uuids::to_string(ctx.session().sessionId()),
                                   ctx.uctx->userUuid(),
                                   ctx.uctx->tenantUuid(),
                                   boost::uuids::to_string(ctx.session().deviceId())));
}

} // ns


namespace nextapp::grpc {
namespace {
struct ToDevice {
    enum Cols { ID, USER, NAME, CREATED, HOSTNAME, OS, OSVERSION, APPVERSION, PRODUCTTYPE, PRODUCTVERSION,
                ARCH, PRETTYNAME, LASTSEEN, ENABLED, NUM_SESSIONS };

    static constexpr string_view columns = "id, user, name, created, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, lastSeen, enabled, numSessions";

    static void assign(const boost::mysql::row_view& row, pb::Device &device, const chrono::time_zone& tz) {
        device.set_id(row[ID].as_string());
        device.set_user(row[USER].as_string());
        device.set_name(row[NAME].as_string());
        device.mutable_created()->set_seconds(toTimeT(row[CREATED].as_datetime(), tz));
        if (!row[HOSTNAME].is_null()) device.set_hostname(row[HOSTNAME].as_string());
        if (!row[OS].is_null()) device.set_os(row[OS].as_string());
        if (!row[OSVERSION].is_null()) device.set_osversion(row[OSVERSION].as_string());
        if (!row[APPVERSION].is_null()) device.set_appversion(row[APPVERSION].as_string());
        if (!row[PRODUCTTYPE].is_null()) device.set_producttype(row[PRODUCTTYPE].as_string());
        if (!row[PRODUCTVERSION].is_null()) device.set_productversion(row[PRODUCTVERSION].as_string());
        if (!row[ARCH].is_null()) device.set_arch(row[ARCH].as_string());
        if (!row[PRETTYNAME].is_null()) device.set_prettyname(row[PRETTYNAME].as_string());
        if (!row[LASTSEEN].is_null()) device.mutable_lastseen()->set_seconds(toTimeT(row[LASTSEEN].as_datetime(), tz));
        device.set_enabled(row[ENABLED].as_int64() == 1);
        device.set_numsessions(row[NUM_SESSIONS].as_int64());
    }
};

    struct ToNotification {
    static constexpr string_view fields = "id, created_time, updated, valid_to, subject, message, sender_type, sender_id, to_tenant, to_user, uuid, kind, data";
    enum Cols { ID, CREATED_TIME, UPDATED, VALID_TO, SUBJECT, MESSAGE, SENDER_TYPE, SENDER_ID, TO_TENANT, TO_USER, UUID, KIND, DATA };

    static void assign(const boost::mysql::row_view& row, pb::Notification &notification, const chrono::time_zone& tz) {
        notification.set_id(row[ID].as_int64());
        notification.mutable_createdtime()->set_unixtime(toTimeT(row[CREATED_TIME].as_datetime(), tz));
        notification.set_updated(toMsTimestamp(row[UPDATED].as_datetime(), tz));
        if (!row[VALID_TO].is_null()) {
            notification.mutable_validto()->set_unixtime(toTimeT(row[VALID_TO].as_datetime(), tz));
        }
        notification.set_subject(row[SUBJECT].as_string());
        if (!row[MESSAGE].is_null()) {
            notification.set_message(row[MESSAGE].as_string());
        }
        notification.set_senderid(row[SENDER_ID].as_string());
        if (!row[TO_TENANT].is_null()) {
            notification.mutable_totenant()->set_uuid(row[TO_TENANT].as_string());
        }
        if (!row[TO_USER].is_null()) {
            notification.mutable_touser()->set_uuid(row[TO_USER].as_string());
        }
        notification.mutable_uuid()->set_uuid(row[UUID].as_string());
        if (!row[KIND].is_null()) {
            auto k = toUpper(row[KIND].as_string());
            pb::Notification_Kind kind{};
            if (pb::Notification::Kind_Parse(k, &kind)) {
                notification.set_kind(kind);
            }
        }
        if (!row[DATA].is_null()) {
            notification.set_data(row[DATA].as_string());
        }
    }
};

} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::Hello(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Hello from session " << rctx.session().sessionId() << " for user " << rctx.uctx->userUuid() << " at " << ctx->peer();
        if (auto hello = reply->mutable_hello()) {
            hello->set_sessionid(to_string(rctx.session().sessionId()));
            hello->set_serverid(Server::instance().serverId());
            hello->set_serverinstancetag(Server::instance().instanceTag());
            hello->set_lastpublishid(rctx.uctx->currentPublishId());
            hello->set_lastnotification(owner_.getLastNotificationUpdated());
        };

        co_return;
    }, __func__, true /* allow new session */);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::Ping(::grpc::CallbackServerContext *ctx, const pb::PingReq *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Ping from session " << rctx.session().sessionId() << " for user "  << rctx.uctx->userUuid() << " at " << ctx->peer();
        co_return;
    }, __func__);
}

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetServerInfo(::grpc::CallbackServerContext *ctx,
                                       const pb::Empty *req,
                                       pb::Status *reply)
{
    assert(ctx);
    assert(reply);

    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        auto *si = reply->mutable_serverinfo();

        if (!si) {
            throw runtime_error{"Could not allocate serverinfo"};
        }

        auto* prop = si->mutable_properties()->mutable_kv();
        prop->emplace("version", NEXTAPP_VERSION);
        prop->emplace("server-id", Server::instance().serverId());

        co_return;
    }, __func__);
}

::grpc::ServerWriteReactor<pb::Update> *GrpcServer::NextappImpl::SubscribeToUpdates(::grpc::CallbackServerContext *context,
                                                                                    const pb::UpdatesReq *request)
{
    if (!owner_.active()) {
        LOG_WARN << "Rejecting subscription. We are shutting down.";
        return {};
    }

    class ServerWriteReactorImpl
        : public std::enable_shared_from_this<ServerWriteReactorImpl>
        , public Publisher
        , public ::grpc::ServerWriteReactor<pb::Update> {
    public:
        enum class State {
            READY,
            WAITING_ON_WRITE,
            DONE
        };

        ServerWriteReactorImpl(GrpcServer& owner, ::grpc::CallbackServerContext *context)
            : owner_{owner}, context_{context} {
            LOG_DEBUG_N << "Remote client " << uuid() << " is subscribing to updates.";
        }

        ~ServerWriteReactorImpl() {
            LOG_DEBUG_N << "Remote client " << uuid() << " is going...";
        }

        void start(const pb::UpdatesReq *req) {
            self_ = shared_from_this();

            try {
                auto session = owner_.sessionManager().getExistingSession(context_);
                assert(session);
                session->addCleanup([w=weak_from_this(), sid=session->sessionId(), when=session->createdTime()] {
                    if (auto self = w.lock()) {
                        LOG_TRACE << "Session " << sid
                                  << " was closed after "
                                  << formatDuration(UserContext::Session::duration(when))
                                  << ". Closing subscription.";
                        self->close();
                    }
                });
                session->user().addPublisher(self_);
                if (req && req->has_withpush()) {
                    LOG_TRACE_N << "Remote client " << context_->peer()
                              << " is subscribing to updates with push: "
                                << owner_.toJsonForLog(*req);
                    session->handlePushState(req->withpush());
                }
                session_ = session;
                LOG_DEBUG << "Remote client " << context_->peer() << " is subscribing to updates as subscriber " << uuid()
                          << " from session " << session->sessionId()
                          << " for user " << session->user().userUuid();
            } catch (const server_err& err) {
                LOG_WARN << "Caught server error when fetching session for connection from "
                         << context_->peer()
                         << ": " << err.what();
                close();
            }

            reply();
        }

        /*! Callback event when the RPC is completed */
        void OnDone() override {
            LOG_TRACE_N << "OnDone called for subscriber " << uuid();
            {
                scoped_lock lock{mutex_};
                state_ = State::DONE;
            }

            if (auto session = session_.lock()) {
                session->user().removePublisher(uuid());
                owner_.sessionManager().removeSession(session->sessionId());
            } else {
                LOG_TRACE_N << "Session was gone for subscriber " << uuid();
            }

            self_.reset();
        }


        /*! Callback event when a write operation is complete */
        void OnWriteDone(bool ok) override {
            if (!ok) [[unlikely]] {
                LOG_WARN << "The write-operation failed.";

                // We still need to call Finish or the request will remain stuck!
                Finish({::grpc::StatusCode::UNKNOWN, "stream write failed"});
                scoped_lock lock{mutex_};
                state_ = State::DONE;
                return;
            }

            {
                scoped_lock lock{mutex_};
                state_ = State::READY;
                updates_.pop();
            }

            reply();
        }

        void publish(const std::shared_ptr<pb::Update>& message) override {
            if (auto session = session_.lock()) {
                LOG_TRACE_N << "Queued message for subscriber " << uuid() << " from session " << session->sessionId() << " for user " << session->user().userUuid();
                scoped_lock lock{mutex_};
                updates_.emplace(message);
                session->touch();
            } else {
                LOG_DEBUG_N << "Session is gone for subscriber " << uuid() << ". Closing subscription.";
                close();
                return;
            }

            reply();
        }

        void close() override {
            LOG_TRACE << "Closing subscription " << uuid();
            Finish(::grpc::Status::OK);
        }

        std::weak_ptr<UserContext::Session>& getSessionWeakPtr() override {
            return session_;
        }

    private:
        void reply() {
            scoped_lock lock{mutex_};
            if (state_ != State::READY || updates_.empty()) {
                return;
            }

            if (!owner_.active()) {
                close();
                return;
            }

            StartWrite(updates_.front().get());
            state_ = State::WAITING_ON_WRITE;

            if (auto session = session_.lock()) {
                session->touch();
            }

            // TODO: Implement finish if the server shuts down.
            //Finish(::grpc::Status::OK);
        }

        GrpcServer& owner_;
        State state_{State::READY};
        std::queue<std::shared_ptr<pb::Update>> updates_;
        std::mutex mutex_;
        std::shared_ptr<ServerWriteReactorImpl> self_;
        ::grpc::CallbackServerContext *context_;
        std::weak_ptr<UserContext::Session> session_;
        Metrics::gauge_scoped_t session_subscriptions_{owner_.server().metrics().session_subscriptions().scoped()};
    };

    try {
        auto handler = make_shared<ServerWriteReactorImpl>(owner_, context);
        handler->start(request);
        return handler.get(); // The object maintains ownership over itself
    } catch (const exception& ex) {
        LOG_ERROR_N << "Caught exception while adding subscriber to update: " << ex.what();
    }

    return {};
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetDevices(::grpc::CallbackServerContext *ctx,
                                                                const pb::Empty *req,
                                                                pb::Status *reply) {
    assert(ctx);
    assert(reply);

    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

    auto res = co_await rctx.dbh->exec(
        format(R"(SELECT {}
          FROM device WHERE user=?
          ORDER BY lastSeen DESC, name)", ToDevice::columns),
        rctx.uctx->dbOptions(), rctx.uctx->userUuid());

    if (res.has_value()) {
        auto *devices = reply->mutable_devices();
        assert(devices);

        for(const auto& row : res.rows()) {
            auto *device = devices->add_devices();
            ToDevice::assign(row, *device, rctx.uctx->tz());
        }
    }
        co_return;
    }, __func__);
}
::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::UpdateDevice(::grpc::CallbackServerContext *ctx,
                                                                  const pb::DeviceUpdateReq *req,
                                                                  pb::Status *reply) {
    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        // Check if the device exists. Get id, name and enabled.
        auto res = co_await rctx.dbh->exec(
            "SELECT id, name, enabled FROM device WHERE user=? AND id=?",
            rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->id());
        if (res.has_value() && res.rows().size() == 1) {

            if (req->has_name()) {
                co_await rctx.dbh->exec("UPDATE device SET name=? WHERE id=? AND user=?",
                    rctx.uctx->dbOptions(), req->name(), req->id(), rctx.uctx->userUuid());
            };

            if (req->has_enabled()) {
                co_await rctx.dbh->exec("UPDATE device SET enabled=? WHERE id=? AND user=?",
                    rctx.uctx->dbOptions(), req->enabled(), req->id(), rctx.uctx->userUuid());
            }

            // Get the device from the db
            auto res = co_await rctx.dbh->exec(
                format(R"(SELECT {}
                  FROM device WHERE user=? AND id=?)", ToDevice::columns),
                rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->id());

            if (res.has_value() && res.rows().size() == 1) {
                const auto& row = res.rows().front();
                auto& pub = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                ToDevice::assign(row, *pub.mutable_device(), rctx.uctx->tz());
            }


        } else {
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteDevice(::grpc::CallbackServerContext *ctx,
                                                                  const common::Uuid *req,
                                                                  pb::Status *reply) {

    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        const auto &uuid = toUuid(req->uuid());
        if (uuid == rctx.session().deviceId()) {
            throw server_err{pb::Error::INVALID_ACTION, "Cannot delete the user-sessions current device"};
        };

        // Get the device from the db
        const auto fetch_res = co_await rctx.dbh->exec(
            format(R"(SELECT {}
              FROM device WHERE user=? AND id=?)", ToDevice::columns),
            rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->uuid());

        if (fetch_res.has_value() && fetch_res.rows().size() == 1) {
            LOG_INFO << "Deleting device " << req->uuid() << " for user " << rctx.uctx->userUuid();
            const auto delete_res = co_await rctx.dbh->exec("DELETE FROM device WHERE user=? AND id=?",
                rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->uuid());
            if (delete_res.has_value() && delete_res.affected_rows() == 1) {
                auto& pub = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
                ToDevice::assign(fetch_res.rows().front(), *pub.mutable_device(), rctx.uctx->tz());
            } else {
                LOG_WARN << "Failed to delete device " << req->uuid() << " for user " << rctx.uctx->userUuid();
                throw server_err{pb::Error::GENERIC_ERROR, format("Failed to delete the device {}", req->uuid())};
            }
        } else {
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::ResetPlayback(::grpc::CallbackServerContext *ctx,
                                                                   const pb::ResetPlaybackReq *req,
                                                                   pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        const auto& device_id = rctx.session().deviceId();
        LOG_TRACE_N << "Resetting replay for device " << device_id << " for instance " << req->instanceid()
                    << " from session " << rctx.session().sessionId() << " for user " << rctx.uctx->userUuid();
        co_await rctx.uctx->resetReplay(device_id, req->instanceid());
        co_return;
    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::ListCurrentSessions(::grpc::CallbackServerContext *ctx,
                                                                         const pb::Empty *req,
                                                                         pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            auto sessions = co_await owner_.sessionManager().listSessions();

            auto fetch = [&](const std::string& query, const std::string& uuid) -> boost::asio::awaitable<string> {
                auto res = co_await rctx.dbh->exec(query, rctx.uctx->dbOptions(), uuid);
                if (!res.rows().empty()) {
                    co_return res.rows().front().front().as_string();
                }

                co_return ""s;
            };

            // Fill inn the missing parts
            for(auto& us : *sessions.mutable_sessions()) {
                us.set_tenantname(co_await fetch("SELECT name FROM tenant WHERE id=?", us.tenantid().uuid()));
                us.set_useremail(co_await fetch("SELECT email FROM user WHERE id=?", us.userid().uuid()));

                for(auto& s : *us.mutable_sessions()) {
                    s.set_devicename(co_await fetch("SELECT name FROM device WHERE id=?", s.deviceid().uuid()));
                }
            }

            auto *session_list = reply->mutable_usersessions();
            *session_list = std::move(sessions);
            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteNotification(::grpc::CallbackServerContext *ctx,
                                                                        const pb::DeleteNotificationReq *req,
                                                                        pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            assert(req);

            notificatation_req_t notification_req;
            if (req->has_id()) {
                notification_req = req->id();
            } else if (req->has_uuid()) {
                notification_req = toUuid(req->uuid().uuid());
            } else {
                throw server_err{pb::Error::INVALID_ACTION, "Invalid request"};
            }

            auto notification = co_await owner_.getNotification(rctx, notification_req);
            if (notification.kind() == pb::Notification::Kind::Notification_Kind_DELETED) {
                LOG_TRACE_N << "Notification " << notification.id() << " is already deleted.";
                co_return;
            }

            auto res = co_await rctx.dbh->exec(R"(UPDATE notification
            SET subject="", message=NULL, sender_id=NULL, kind="deleted", data=NULL, valid_to=NULL
            WHERE id=?)", notification.id());

            auto deleted_n = co_await owner_.getNotification(rctx, notification_req);
            assert(deleted_n.kind() == pb::Notification::Kind::Notification_Kind_DELETED);

            enum Cols { UPDATED };
            auto updated_res = co_await rctx.dbh->exec("SELECT updated FROM notification WHERE id=?",
                notification.id());
            if (updated_res.has_value() && updated_res.rows().size() == 1) {
                const auto when = toMsTimestamp(updated_res.rows().front().at(UPDATED).as_datetime(), rctx.uctx->tz());
                owner_.setLastNotificationUpdated(when);
            }

            co_await owner_.sessionManager().publishNotification(deleted_n);

            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetLastReadNotification(::grpc::CallbackServerContext *ctx, const
                                                                             pb::Empty *req,
                                                                             pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            if (auto lrn = rctx.uctx->getLastReadNotification()) {
                reply->set_lastreadnotificationid(*lrn);
                co_return;
            }

            auto res = co_await rctx.dbh->exec(R"(SELECT notification_id FROM notification_last_read
                WHERE user=?)", rctx.uctx->dbOptions(), rctx.uctx->userUuid());
            enum Cols { NOTIFICATION_ID };

            if (res.has_value() && !res.rows().empty()) {
                auto id = res.rows().front().at(NOTIFICATION_ID).as_int64();
                reply->set_lastreadnotificationid(static_cast<uint32_t>(id));
            } else {
                reply->set_lastreadnotificationid(0);
            }

            rctx.uctx->setLastReadNotification(reply->lastreadnotificationid());

            co_return;
        }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SetLastReadNotification(::grpc::CallbackServerContext *ctx,
                                                                             const pb::SetReadNotificationReq *req,
                                                                             pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
            // TODO: Cache the last read notification in the user context

            const auto id = req->notificationid();

            co_await rctx.dbh->exec(R"(INSERT INTO notification_last_read
                (user, notification_id) VALUES (?, ?)
                ON DUPLICATE KEY UPDATE notification_id=?)",
                rctx.uctx->dbOptions(), rctx.uctx->userUuid(), id, id);

            auto& update = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
            update.set_lastreadnotificationid(id);

            co_return;
        }, __func__);
}

::grpc::ServerWriteReactor<pb::Status> *GrpcServer::NextappImpl::GetNewNotifications(::grpc::CallbackServerContext *ctx,
                                                                                     const pb::GetNewReq *req)
{
    return writeStreamHandler(ctx, req,
    [this, req, ctx] (auto stream, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        const auto stream_scope = owner_.server().metrics().data_streams_actions().scoped();
        const auto uctx = rctx.uctx;
        const auto& cuser = uctx->userUuid();
        const auto batch_size = owner_.server().config().options.stream_batch_size;

        // Use batched reading from the database, so that we can get all the data, but
        // without running out of memory.
        // TODO: Set a timeout or constraints on how many db-connections we can keep open for batches.
        assert(rctx.dbh);
        const auto sql = format(R"(SELECT {}
            FROM notification WHERE updated > ?
            AND (to_user=? || (to_tenant=? && to_user IS NULL) || (to_user IS NULL && to_tenant IS NULL))
            AND (valid_to IS NULL OR valid_to > NOW())
            ORDER BY updated)", ToNotification::fields);
        co_await  rctx.dbh->start_exec(sql,
          uctx->dbOptions(), toMsDateTime(req->since(), uctx->tz()), cuser, rctx.uctx->tenantUuid());

        nextapp::pb::Status reply;

        auto *notifications = reply.mutable_notifications();
        auto num_rows_in_batch = 0u;
        auto total_rows = 0u;
        auto batch_num = 0u;

        auto flush = [&]() -> boost::asio::awaitable<void> {
          reply.set_error(::nextapp::pb::Error::OK);
          assert(reply.has_notifications());
          ++batch_num;
          reply.set_message(format("Fetched {} notifications in batch {}", reply.notifications().notifications_size(), batch_num));
          co_await stream->sendMessage(std::move(reply), boost::asio::use_awaitable);
          reply.Clear();
          notifications = reply.mutable_notifications();
          num_rows_in_batch = {};
        };

        bool read_more = true;
        for(auto rows = co_await rctx.dbh->readSome()
           ; read_more
           ; rows = co_await rctx.dbh->readSome()) {

          read_more = rctx.dbh->shouldReadMore(); // For next iteration

          if (rows.empty()) {
              LOG_TRACE_N << "Out of rows to iterate... num_rows_in_batch=" << num_rows_in_batch;
              break;
          }

          for(const auto& row : rows) {
              auto * n = notifications->add_notifications();
              assert(n);
              ToNotification::assign(row, *n, rctx.uctx->tz());
              ++total_rows;
              // Do we need to flush?
              if (++num_rows_in_batch >= batch_size) {
                  co_await flush();
              }
          }

        } // read more from db loop

        co_await flush();

        LOG_DEBUG_N << "Sent " << total_rows << " notifications to client.";
        co_return;

    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::SendNotification(::grpc::CallbackServerContext *ctx,
                                                                     const pb::Notification *req,
                                                                     pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

            assert(req);
            auto notification = *req;
            if (!notification.has_uuid()) {
                notification.mutable_uuid()->set_uuid(newUuidStr());
            }
            if (notification.kind() == pb::Notification::Kind::Notification_Kind_DELETED) {
                throw server_err{pb::Error::INVALID_ARGUMENT, "Invalid notification kind (deleted)"};
            }

            // Save to db
            optional<string> valid_to;
            if (notification.has_validto()) {
                valid_to = toAnsiTime(notification.validto().unixtime(), rctx.uctx->tz());
            }
            optional<string> user_id;
            if (notification.has_touser()) {
                user_id = notification.touser().uuid();
            }
            optional<string> tenant_id;
            if (notification.has_totenant()) {
                tenant_id = notification.totenant().uuid();
            }

            const string sender_type = toLower(pb::Notification::SenderType_Name(notification.sendertype()));
            const string kind = toLower(pb::Notification::Kind_Name(notification.kind()));

            auto trx = co_await rctx.dbh->transaction();

            auto res = co_await rctx.dbh->exec(R"(INSERT INTO notification
                (valid_to, subject, message, sender_type, sender_id, to_tenant, to_user, uuid, kind, data)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?))",
                rctx.uctx->dbOptions(),
                valid_to,
                notification.subject(),
                toStringOrNull(notification.message()),
                sender_type,
                notification.senderid(),
                tenant_id,
                user_id,
                notification.uuid().uuid(),
                kind,
                notification.data());

            // Get the id and updated_time
            assert(res.last_insert_id() > 0);

            notification.set_id(res.last_insert_id());
            enum Cols { UPDATED, CREATED_TIME };
            auto updated_res = co_await rctx.dbh->exec("SELECT updated, created_time FROM notification WHERE id=?",
                                                        rctx.uctx->dbOptions(), notification.id());
            notification.set_updated(toMsTimestamp(updated_res.rows().front().at(UPDATED).as_datetime(), rctx.uctx->tz()));
            notification.mutable_createdtime()->set_unixtime(toTimeT(updated_res.rows().front().at(CREATED_TIME).as_datetime(), rctx.uctx->tz()));
            co_await trx.commit();

            owner_.setLastNotificationUpdated(notification.updated());
            co_await owner_.sessionManager().publishNotification(notification);

            reply->set_uuid(notification.uuid().uuid());

            co_return;
        }, __func__, true /* allow new session */, true /* admin only */);
}


boost::asio::awaitable<pb::Notification> GrpcServer::getNotification(RequestCtx& rctx, notificatation_req_t req)
{
    assert(rctx.dbh);

    string sql;
    std::vector<boost::mysql::field_view> params;
    if (holds_alternative<uint32_t>(req)) {
        sql = format("SELECT {} FROM notification WHERE id=?", ToNotification::fields);
        params.emplace_back(get<uint32_t>(req));
    } else {
        assert(holds_alternative<boost::uuids::uuid>(req));
        sql = format("SELECT {} FROM notification WHERE uuid=?", ToNotification::fields);
        params.emplace_back(get<boost::uuids::uuid>(req));
    }

    auto res = co_await rctx.dbh->exec(sql, params);
    if (res.rows().size() == 1) {
        const auto& row = res.rows().front();
        pb::Notification notification;
        ToNotification::assign(row, notification, rctx.uctx->tz());
        co_return notification;
    }

    throw server_err{pb::Error::NOT_FOUND, "Notification not found"};
}

GrpcServer::GrpcServer(Server &server)
    : server_{server}, sessionManager_{server}
{
}

void GrpcServer::start() {

    // Load the certificate and key for the gRPC server.
    boost::asio::co_spawn(server_.ctx(), [&]() -> boost::asio::awaitable<void> {
        co_await loadCert();
        co_await initNotifications();
    }, boost::asio::use_future).get();

    ::grpc::ServerBuilder builder;

    if (config().tls_mode == "none") {
        LOG_WARN << "No TLS on gRPC endpoint. Use only in a safe environment!";
        builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());
    } else if (config().tls_mode == "ca") {
        LOG_INFO << "Using CA mode for TLS on gRPC endpoint";
        ::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY};
        ssl_opts.pem_root_certs = server_.ca().rootCert();
        ssl_opts.pem_key_cert_pairs.push_back({cert_.key, cert_.cert});
        builder.AddListeningPort(config().address, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        throw runtime_error{"Unknown TLS mode: " + config().tls_mode};
    }

    // Set up keepalive options
    if (!config().disable_keepalive) {
        LOG_DEBUG << "Setting up gRPC keepalive options";
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, config().keepalive_time_sec * 1000);
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, config().keepalive_timeout_sec * 1000);
        builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
        builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, config().min_recv_ping_interval_without_data_sec * 1000);
        builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
        builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, config().max_ping_strikes);
    } else {
        LOG_WARN_N << "gRPC keepalive is disabled.";
    }

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<NextappImpl>(*this);
    builder.RegisterService(service_.get());

    // Finally assemble the server.
    grpc_server_ = builder.BuildAndStart();
    LOG_INFO
        // Fancy way to print the class-name.
        // Useful when I copy/paste this code around ;)
        << boost::typeindex::type_id_runtime(*this).pretty_name()

        // The useful information
        << " listening on " << config().address;

    active_ = true;
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down GrpcServer.";
    active_ = false;

    sessionManager_.shutdown();

    for(auto& [_, wp] : publishers_) {
        if (auto pub = wp.lock()) {
            pub->close();
        }
    }
    auto deadline = std::chrono::system_clock::now() + 6s;
    grpc_server_->Shutdown(deadline);
    publishers_.clear();
    //grpc_server_->Wait();
    grpc_server_.reset();
    LOG_DEBUG << "GrpcServer is done.";
}

boost::asio::awaitable<void> GrpcServer::loadCert()
{
    cert_ = co_await server_.getCert("grpc-server", Server::WithMissingCert::CREATE_SERVER);
    assert(!cert_.cert.empty());
    assert(!cert_.key.empty());
    co_return;
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::GetDataVersions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        auto res = co_await rctx.dbh->exec("SELECT version, kind from versions WHERE user=?",
                                  rctx.uctx->dbOptions(), rctx.uctx->userUuid());

        enum Cols { VERSION, KIND };

        if (res.has_value()) {
            auto versions = reply->mutable_dataversions();

            for(const auto& row : res.rows()) {
                const auto& kind = row[KIND].as_string();
                if (kind == "action_category") {
                    assert(row[VERSION].is_int64());
                    versions->set_actioncategoryversion(row[VERSION].as_int64());
                }
            }
        }

    }, __func__);
}

boost::asio::awaitable<bool> GrpcServer::isReplay(::grpc::CallbackServerContext *ctx, RequestCtx &rctx)
{
    // Check if the request is protected against replay
    if (const auto it = ctx->client_metadata().find("req_id"); it != ctx->client_metadata().end()) {
        if (const uint req_id = stoul(it->second.data()); req_id > 0) {
            // Validate

            const auto instance_id = getInstanceId(ctx);
            LOG_TRACE_N << "Checking replay for instance " << instance_id << " and req " << req_id
                        << " from session " << rctx.session().sessionId() << " for user " << rctx.uctx->userUuid();

            const auto& device_id = rctx.session().deviceId();
            co_return co_await rctx.uctx->checkForReplay(device_id, instance_id, req_id);
        }
    }

    co_return false;
}

uint GrpcServer::getInstanceId(::grpc::CallbackServerContext *ctx)
{
    // The client only sends instance_id if the instance_id is > 1.
    if (const auto iit = ctx->client_metadata().find("instance_id"); iit != ctx->client_metadata().end()) {
        return stoul(iit->second.data());
    }
    return 1;
}

boost::asio::awaitable<void> GrpcServer::initNotifications()
{
    jgaa::mysqlpool::Options opt;
    opt.time_zone = "UTC";
    auto res = co_await server_.db().exec("SELECT MAX(updated) FROM notification", opt);
    enum Cols { UPDATED };
    uint64_t when{};
    if (!res.rows().empty() && res.rows().front().at(UPDATED).is_datetime()) {
        const auto *utc_zone = chrono::get_tzdb().locate_zone("UTC");
        assert(utc_zone);
        if (utc_zone) {
            when = toMsTimestamp(res.rows().front().at(UPDATED).as_datetime(), *utc_zone);
        } else {
            auto dt = res.rows().front().at(UPDATED).as_datetime();
            LOG_WARN_N << "Could not find UTC timezone. Manually converting the notification update ts.";
            when = std::chrono::duration_cast<std::chrono::milliseconds>(dt.as_time_point().time_since_epoch()).count();
            when += std::chrono::duration_cast<std::chrono::milliseconds>(chrono::microseconds{dt.microsecond()}).count();
        }
    }

    if (when) {
        setLastNotificationUpdated(when);
    }
}

void GrpcServer::setLastNotificationUpdated(uint64_t lastNotificationUpdated) noexcept {
    atomicSetIfGreater(last_notification_udated_, lastNotificationUpdated);
    LOG_TRACE_N << "Last notification updated set to " << lastNotificationUpdated;
}


} // ns


