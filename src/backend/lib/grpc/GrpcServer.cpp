
#include "shared_grpc_server.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

#include <grpcpp/support/server_interceptor.h>

namespace nextapp::grpc {

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
        }

        ~ServerWriteReactorImpl() {
            LOG_DEBUG_N << "Remote client " << uuid() << " is going...";
        }

        void start() {
            self_ = shared_from_this();

            try {
                auto session = owner_.sessionManager().getExistingSession(context_);
                assert(session);
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
            {
                scoped_lock lock{mutex_};
                state_ = State::DONE;
            }

            if (auto session = session_.lock()) {
                session->user().removePublisher(uuid());
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
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, config().min_recv_ping_interval_without_cata_sec * 1000);
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

} // ns
