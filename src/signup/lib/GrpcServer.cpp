
#include <map>
#include <chrono>
#include <iostream>

#include <boost/json.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "signup/GrpcServer.h"
#include "signup/Server.h"
//#include "nextapp/util.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

using stub_t = ::nextapp::pb::Nextapp::Stub;

namespace nextapp::grpc {

using namespace ::signup::pb;

GrpcServer::GrpcServer(Server &server)
: server_(server)
{
}

::grpc::ServerUnaryReactor *GrpcServer::SignupImpl::GetInfo(
    ::grpc::CallbackServerContext *ctx,
    const GetInfoRequest *req,
    GetInfoResponse *reply)
{
    return unaryHandler(ctx, req, reply, [this, req, ctx](GetInfoResponse *reply) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Saying hello to client at " << ctx->peer();
        reply->set_greeting(owner_.server().getWelcomeText(*req));
        reply->set_eula(owner_.server().getEulaText(*req));
        co_return;
    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::SignupImpl::SignUp(
    ::grpc::CallbackServerContext *ctx,
    const signup::pb::SignUpRequest *req,
    signup::pb::SignUpResponse *reply)
{
    return unaryHandler(ctx, req, reply, [this, req, ctx](SignUpResponse *reply) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Signup request from client at " << ctx->peer();



        co_return;
    }, __func__);
}

void GrpcServer::start() {
    startNextapp();
    startSignup();
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down GrpcServer.";
    active_ = false;
    auto deadline = std::chrono::system_clock::now() + 6s;
    grpc_server_->Shutdown(deadline);
    grpc_server_.reset();
    LOG_DEBUG << "GrpcServer is done.";

    LOG_DEBUG << "Shutting down NextApp gRPC channel to Nextapp server.";
    nextapp_stub_.reset();
}

void GrpcServer::startNextapp()
{
    LOG_INFO << "Connecting to NextApp gRPC endpoint at "
             << nextapp_config().address;

    // Create a gRPC channel to the NextApp service.
    // Set up TLS credentials with our own certificate.
    decltype(::grpc::InsecureChannelCredentials()) creds;
    if (!nextapp_config().server_cert.empty()) {
        ::grpc::SslCredentialsOptions ssl_opts;
        if (!nextapp_config().ca_cert.empty()) {
            ssl_opts.pem_root_certs = readFileToBuffer(nextapp_config().ca_cert);
        }
        ssl_opts.pem_private_key = readFileToBuffer(nextapp_config().server_key);
        ssl_opts.pem_cert_chain = readFileToBuffer(nextapp_config().server_cert);
        auto creds = ::grpc::SslCredentials(ssl_opts);
        LOG_DEBUG_N << "Using client-cert from " << nextapp_config().server_cert;
    } else {
        creds = ::grpc::InsecureChannelCredentials();
        LOG_WARN_N << "No TLS cert provided. Proceeding without client cert.";
    }
    assert(creds);
    if (!creds) {
        LOG_ERROR_N << "Failed to create gRPC channel credentials.";
        throw runtime_error{"Failed to create gRPC channel credentials."};
    }

    auto channel = ::grpc::CreateChannel(nextapp_config().address, creds);
    nextapp_stub_ = nextapp::pb::Nextapp::NewStub(channel);

    // Test the connection to the NextApp server.
    // Create asio C++20 coro scope
    auto f = asio::co_spawn(server().ctx(), [this]() -> asio::awaitable<void> {
        ::grpc::ClientContext ctx;

        auto resp = co_await callRpc<nextapp::pb::ServerInfo>(
            nextapp::pb::Empty{},
            &stub_t::async::GetServerInfo,
            asio::use_awaitable);

        LOG_INFO << "Connected to nextapp-server at " << nextapp_config().address;
        for(const auto& kv : resp.properties()) {
            LOG_INFO << kv.key() << ": " << kv.value();
        }

        co_return;

    }, asio::use_future);

    try {
        f.get();
    } catch (const std::exception &ex) {
        LOG_ERROR << "Failed to connect to NextApp server: " << ex.what();
        throw;
    }
}

void GrpcServer::startSignup()
{
    ::grpc::ServerBuilder builder;

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.

    if (signup_config().tls_mode == "none") {
        builder.AddListeningPort(signup_config().address, ::grpc::InsecureServerCredentials());
        LOG_WARN << "Using non-TLS (unencrypted, plain stream) on gRPC signup-server endpoint";
    } else if (signup_config().tls_mode == "cert") {
        LOG_INFO << "Using 'cert' mode for TLS on gRPC signup-server endpoint";
        ::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE};
        if (!server_.config().grpc_signup.ca_cert.empty()) {
            ssl_opts.pem_root_certs = readFileToBuffer(server_.config().grpc_signup.ca_cert);
        }

        auto cert = readFileToBuffer(server_.config().grpc_signup.server_cert);
        auto key = readFileToBuffer(server_.config().grpc_signup.server_key);
        ssl_opts.pem_key_cert_pairs.push_back({key, cert});
        std::ranges::fill(key, 0); // Don't keep the key in memory that we will release
        builder.AddListeningPort(signup_config().address, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        throw runtime_error{"Unknown signup-server TLS mode: " + signup_config().tls_mode};
    }

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<SignupImpl>(*this);
    builder.RegisterService(service_.get());

    // Finally assemble the server.
    grpc_server_ = builder.BuildAndStart();
    LOG_INFO << "Signup-server listening on " << signup_config().address;
    active_ = true;
}

} // ns
