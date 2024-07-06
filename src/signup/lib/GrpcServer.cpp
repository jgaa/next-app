
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

void GrpcServer::start() {
    ::grpc::ServerBuilder builder;

    // Tell gRPC what TCP address/port to listen to and how to handle TLS.
    // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.

    if (config().tls_mode == "none") {
        builder.AddListeningPort(config().address, ::grpc::InsecureServerCredentials());
    } else if (config().tls_mode == "cert") {
        LOG_INFO << "Using cert mode for TLS on gRPC endpoint";
        ::grpc::SslServerCredentialsOptions ssl_opts{GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE};
        if (!server_.config().grpc.ca_cert.empty()) {
            ssl_opts.pem_root_certs = readFileToBuffer(server_.config().grpc.ca_cert);
        }

        auto cert = readFileToBuffer(server_.config().grpc.server_cert);
        auto key = readFileToBuffer(server_.config().grpc.server_key);
        ssl_opts.pem_key_cert_pairs.push_back({key, cert});
        std::ranges::fill(key, 0); // Don't keep the key in memory that we will release
        builder.AddListeningPort(config().address, ::grpc::SslServerCredentials(ssl_opts));
    } else {
        throw runtime_error{"Unknown TLS mode: " + config().tls_mode};
    }

    // Feed gRPC our implementation of the RPC's
    service_ = std::make_unique<SignupImpl>(*this);
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
    auto deadline = std::chrono::system_clock::now() + 6s;
    grpc_server_->Shutdown(deadline);
    grpc_server_.reset();
    LOG_DEBUG << "GrpcServer is done.";
}

} // ns
