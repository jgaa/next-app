
#include "shared_grpc_server.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

#include <grpcpp/support/server_interceptor.h>

namespace nextapp::grpc {

namespace {
struct ToDevice {
    enum Cols { ID, USER, NAME, CREATED, HOSTNAME, OS, OSVERSION, APPVERSION, PRODUCTTYPE, PRODUCTVERSION,
                ARCH, PRETTYNAME, LASTSEEN, ENABLED };

    static constexpr auto columns = "id, user, name, created, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, lastSeen, enabled";

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
    }
};
} // anon ns

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::Hello(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply)
{
    return unaryHandler(ctx, req, reply,
        [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Hello from session " << rctx.session().sessionId() << " for user " << rctx.uctx->userUuid() << " at " << ctx->peer();
        reply->set_sessionid(to_string(rctx.session().sessionId()));
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


        auto add = [&reply, si](string key, string value) {
            auto prop = si->mutable_properties()->Add();
            prop->set_key(key);
            prop->set_value(value);
        };

        add("version", NEXTAPP_VERSION);
        co_return;
    }, __func__);
}

::grpc::ServerWriteReactor<pb::Update> *GrpcServer::NextappImpl::SubscribeToUpdates(::grpc::CallbackServerContext *context, const pb::UpdatesReq *request)
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

        void start() {
            self_ = shared_from_this();

            try {
                auto session = owner_.sessionManager().getExistingSession(context_);
                assert(session);
                session->addCleanup([w=weak_from_this(), sid=session->sessionId()] {
                    if (auto self = w.lock()) {
                        LOG_TRACE << "Session " << sid << " was closed. Closing subscription.";
                        self->close();
                    }
                });
                session->user().addPublisher(self_);
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
        handler->start();
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
        format(R"(SELECT id, user, name, created, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, lastSeen, enabled
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
                format(R"(SELECT id, user, name, created, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, lastSeen, enabled
                  FROM device WHERE user=? AND id=?)", ToDevice::columns),
                rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->id());

            if (res.has_value() && res.rows().size() == 1) {
                const auto& row = res.rows().front();
                auto pub = rctx.publishLater(pb::Update::Operation::Update_Operation_UPDATED);
                ToDevice::assign(row, *pub.mutable_device(), rctx.uctx->tz());
            }


        } else {
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
    });
}

::grpc::ServerUnaryReactor *GrpcServer::NextappImpl::DeleteDevice(::grpc::CallbackServerContext *ctx,
                                                                  const pb::Uuid *req,
                                                                  pb::Status *reply) {

    return unaryHandler(ctx, req, reply,
    [this, req, ctx] (pb::Status *reply, RequestCtx& rctx) -> boost::asio::awaitable<void> {

        const auto &uuid = toUuid(req->uuid());
        if (uuid == rctx.session().deviceId()) {
            throw server_err{pb::Error::INVALID_ACTION, "Cannot delete the user-sessions current device"};
        };

        // Get the device from the db
        auto res = co_await rctx.dbh->exec(
            format(R"(SELECT id, user, name, created, hostName, os, osVersion, appVersion, productType, productVersion, arch, prettyName, lastSeen, enabled
              FROM device WHERE user=? AND id=?)", ToDevice::columns),
            rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->uuid());

        if (res.has_value() && res.rows().size() == 1) {
            LOG_INFO << "Deleting device " << req->uuid() << " for user " << rctx.uctx->userUuid();
            res = co_await rctx.dbh->exec("DELETE FROM device WHERE user=? AND id=?",
                rctx.uctx->dbOptions(), rctx.uctx->userUuid(), req->uuid());
            if (res.has_value() && res.affected_rows() == 1) {
                auto pub = rctx.publishLater(pb::Update::Operation::Update_Operation_DELETED);
                ToDevice::assign(res.rows().front(), *pub.mutable_device(), rctx.uctx->tz());
            } else {
                LOG_WARN << "Failed to delete device " << req->uuid() << " for user " << rctx.uctx->userUuid();
                throw server_err{pb::Error::GENERIC_ERROR, format("Failed to delete the device {}", req->uuid())};
            }
        } else {
            throw server_err{pb::Error::NOT_FOUND, "Device not found"};
        }
    });
}


GrpcServer::GrpcServer(Server &server)
    : server_{server}, sessionManager_{server}
{
}

void GrpcServer::start() {

    // Load the certificate and key for the gRPC server.
    boost::asio::co_spawn(server_.ctx(), [&]() -> boost::asio::awaitable<void> {
        co_await loadCert();
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
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, config().keepalive_time_sec * 1000);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, config().keepalive_timeout_sec * 1000);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, config().min_recv_ping_interval_without_data_sec * 1000);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, config().max_ping_strikes);

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

} // ns
