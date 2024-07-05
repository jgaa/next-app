
#include "shared_grpc_server.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

namespace nextapp::grpc {

namespace {
    static constexpr auto system_tenant = "a5e7bafc-9cba-11ee-a971-978657e51f0c";
    static constexpr auto system_user = "dd2068f6-9cbb-11ee-bfc9-f78040cadf6b";

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
            owner_.addPublisher(self_);
            reply();
        }

        /*! Callback event when the RPC is completed */
        void OnDone() override {
            {
                scoped_lock lock{mutex_};
                state_ = State::DONE;
            }

            owner_.removePublisher(uuid());
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
    : server_{server}
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
        ::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY};
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

void GrpcServer::addPublisher(const std::shared_ptr<Publisher> &publisher)
{
    LOG_TRACE_N << "Adding publisher " << publisher->uuid();
    scoped_lock lock{mutex_};
    publishers_[publisher->uuid()] = publisher;
}

void GrpcServer::removePublisher(const boost::uuids::uuid &uuid)
{
    LOG_TRACE_N << "Removing publisher " << uuid;
    scoped_lock lock{mutex_};
    publishers_.erase(uuid);
}

void GrpcServer::publish(const std::shared_ptr<pb::Update>& update)
{
    scoped_lock lock{mutex_};

    LOG_TRACE_N << "Publishing "
                << pb::Update::Operation_Name(update->op())
                << " update to " << publishers_.size() << " subscribers, Json: "
                << toJsonForLog(*update);

    for(auto& [uuid, weak_pub]: publishers_) {
        if (auto pub = weak_pub.lock()) {
            pub->publish(update);
        } else {
            LOG_WARN_N << "Failed to get a pointer to publisher " << uuid;
        }
    }
}


const std::shared_ptr<UserContext> GrpcServer::userContext(::grpc::CallbackServerContext *ctx) const
{
    const auto sid = getSessionId(ctx);

    // TODO: Implement real sessions and authentication
    lock_guard lock{mutex_};
    if (auto it = sessions_.find(sid); it != sessions_.end()) {
        return it->second;
    }

    pb::UserGlobalSettings settings;
    settings.set_timezone(string{chrono::current_zone()->name()});

    // TODO: Remove this when we have proper authentication.
    boost::asio::co_spawn(server_.ctx(), [&]() -> boost::asio::awaitable<void> {
            auto res = co_await server_.db().exec(
                "SELECT settings FROM user_settings WHERE user = ?",
                system_user);

            enum Cols { SETTINGS };
            if (!res.rows().empty()) {
                const auto& row = res.rows().front();
                auto blob = row.at(SETTINGS).as_blob();
                pb::UserGlobalSettings tmp_settings;
                if (tmp_settings.ParseFromArray(blob.data(), blob.size())) {
                    LOG_DEBUG_N << "Loaded UserGlobalSettings for user " << system_user;
                    settings = tmp_settings;
                } else {
                    LOG_WARN_N << "Failed to parse UserGlobalSettings for user " << system_user;
                    throw runtime_error{"Failed to parse UserGlobalSettings"};
                }
            }
            co_return;
    }, boost::asio::use_future).get();

    auto ux = make_shared<UserContext>(system_tenant, system_user, settings, sid);
    sessions_[sid] = ux;
    return ux;
}

void GrpcServer::updateSessionSettingsForUser(const pb::UserGlobalSettings &settings, ::grpc::CallbackServerContext *ctx)
{
    assert(ctx);
    const auto sid = getSessionId(ctx);

    // TODO: Update the session data for all sessions from that user.
    // TODO: Notify other servers about the change.

    lock_guard lock{mutex_};

    auto obsolete = std::move(sessions_);
    sessions_.clear();

    for(const auto& [id, session] : obsolete) {
        if (session->userUuid() == system_user) {
            sessions_[id] = make_shared<UserContext>(system_tenant, system_user, settings, id);
        }
    }
}

boost::uuids::uuid GrpcServer::getSessionId(::grpc::CallbackServerContext *ctx) const
{
    auto session_id = ctx->client_metadata().find("session-id");
    if (session_id == ctx->client_metadata().end()) {
        LOG_WARN_N << "No session-id in metadata from peer: " << ctx->peer();
        throw db_err{pb::Error::AUTH_MISSING_SESSION_ID, "Missing session-id in gRPC request"};
    }

    return toUuid({session_id->second.data(), session_id->second.size()});
}

boost::asio::awaitable<void> GrpcServer::loadCert()
{
    cert_ = co_await server_.getCert("grpc-server", Server::WithMissingCert::CREATE_SERVER   );
    assert(!cert_.cert.empty());
    assert(!cert_.key.empty());
    co_return;
}

} // ns
