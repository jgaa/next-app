
#include "shared_grpc_server.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

//#include <grpcpp/security/auth_metadata_processor.h>
#include <grpcpp/support/server_interceptor.h>

namespace nextapp::grpc {

namespace {

    // class CustomAuthProcessor : public ::grpc::AuthMetadataProcessor {
    // public:
    //     ::grpc::Status Process(
    //         const ::grpc::AuthMetadataProcessor::InputMetadata& auth_metadata,
    //         ::grpc::AuthContext* auth_context,
    //         ::grpc::AuthMetadataProcessor::OutputMetadata* consumed_auth_metadata,
    //         ::grpc::AuthMetadataProcessor::OutputMetadata* response_metadata
    //         ) override {
    //         auto peer_name = auth_context->FindPropertyValues(GRPC_X509_CN_PROPERTY_NAME);
    //         LOG_DEBUG_N << "Peer name: " << string_view(reinterpret_cast<const char *>(peer_name.data()), peer_name.size());
    //         if (!peer_name.empty()) {
    //             // std:: user_identity = peer_cert.front();
    //             // if (IsUserAllowed(user_identity)) {
    //             //     return ::grpc::Status::OK;
    //             // }
    //         }
    //         return ::grpc::Status(::grpc::StatusCode::PERMISSION_DENIED, "Unauthorized");
    //     }

    // private:
    //     bool IsUserAllowed(const std::string& user_identity) {
    //         // Implement your logic to determine if the user is allowed
    //         // For example, check against a whitelist of allowed user identities
    //         return true; // Replace with actual logic
    //     }
    // };

    // using ::grpc::experimental::InterceptionHookPoints;
    // using ::grpc::experimental::Interceptor;
    // using ::grpc::experimental::InterceptorBatchMethods;
    // using ::grpc::experimental::ServerInterceptorFactoryInterface;
    // using ::grpc::experimental::ServerRpcInfo;

    // class SessionInterceptor : public Interceptor {
    // public:
    //     explicit SessionInterceptor(GrpcServer& server)
    //         : server_{server}
    //     {
    //     }

    //     void Intercept(InterceptorBatchMethods* methods) override {
    //         if (methods->QueryInterceptionHookPoint(InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
    //             // auto* user = server_.userContext(ctx);
    //             // if (user) {
    //             //     ctx->AddInitialMetadata("user-uuid", user->userUuid());
    //             //     ctx->AddInitialMetadata("tenant-uuid", user->tenantUuid());
    //             //     ctx->AddInitialMetadata("user-kind", pb::User::Kind_Name(user->kind()));
    //             //     ctx->AddInitialMetadata("session-id", user->sessionId());
    //             // }

    //             //auto* context = methods->`

    //             auto *meta = methods->GetRecvInitialMetadata();
    //             for(const auto& [key, value]: *meta) {
    //                 LOG_TRACE << "Metadata: " << key << " = " << value;
    //             }

    //             auto channel = methods->GetInterceptedChannel();

    //         }
    //         methods->Proceed();
    //     }

    // private:
    //     GrpcServer& server_;
    // };

    // class SessionFactory : public ServerInterceptorFactoryInterface {
    // public:
    //     explicit SessionFactory(GrpcServer& server)
    //         : server_{server}
    //     {
    //     }

    //     Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override {
    //         return new SessionInterceptor(server_);
    //     }

    // private:
    //     GrpcServer& server_;
    // };

} // anon ns

::grpc::ServerUnaryReactor *
GrpcServer::NextappImpl::GetServerInfo(::grpc::CallbackServerContext *ctx,
                                       const pb::Empty *,
                                       pb::ServerInfo *reply)
{
    assert(ctx);
    assert(reply);

    auto add = [&reply](string key, string value) {
        auto prop = reply->mutable_properties()->Add();
        prop->set_key(key);
        prop->set_value(value);
    };

    add("version", NEXTAPP_VERSION);

    auto* reactor = ctx->DefaultReactor();
    reactor->Finish(::grpc::Status::OK);
    return reactor;
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
            // Tell owner about us
            LOG_DEBUG << "Remote client " << context_->peer() << " is subscribing to updates as subscriber " << uuid();
            self_ = shared_from_this();

            try {
                auto session = owner_.sessionManager().getExistingSession(context_);
                assert(session);
                session->user().addPublisher(self_);
                session_ = session;
            } catch (const server_err& err) {
                LOG_WARN << "Caught server error when fetching session: " << err.what();
                close();
            }

            //owner_.addPublisher(self_);
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
            {
                scoped_lock lock{mutex_};
                updates_.emplace(message);
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

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.

    if (config().tls_mode == "none") {
        builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());
    } else if (config().tls_mode == "ca") {
        LOG_INFO << "Using CA mode for TLS on gRPC endpoint";
        ::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY};
        //::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY};
        //::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE};
        ssl_opts.pem_root_certs = server_.ca().rootCert();
        ssl_opts.pem_key_cert_pairs.push_back({cert_.key, cert_.cert});
        builder.AddListeningPort(config().address, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        throw runtime_error{"Unknown TLS mode: " + config().tls_mode};
    }

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<NextappImpl>(*this);
    builder.RegisterService(service_.get());

    // Set the custom auth processor
    // auto auth_processor = std::make_unique<CustomAuthProcessor>();
    // builder.SetAuthMetadataProcessor(std::move(auth_processor));

    // // Set up interceptors
    // std::vector<std::unique_ptr<ServerInterceptorFactoryInterface>> creators;
    // creators.push_back(std::unique_ptr<ServerInterceptorFactoryInterface>(new SessionFactory(*this)));
    // builder.experimental().SetInterceptorCreators(std::move(creators));

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
