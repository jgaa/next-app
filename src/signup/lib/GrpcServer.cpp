
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
    Reply *reply)
{
    return unaryHandler(ctx, req, reply, [this, req, ctx](Reply *reply) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Saying hello to client at " << ctx->peer();

        if (auto * response = reply->mutable_getinforesponse()) {
            assert(response);
            response->set_greeting(owner_.server().getWelcomeText(*req));
            response->set_eula(owner_.server().getEulaText(*req));
        } else {
            throw runtime_error{"Failed to create getinforesponse object"};
        }
        co_return;
    }, __func__);
}

::grpc::ServerUnaryReactor *GrpcServer::SignupImpl::SignUp(
    ::grpc::CallbackServerContext *ctx,
    const signup::pb::SignUpRequest *req,
    signup::pb::Reply *reply)
{
    return unaryHandler(ctx, req, reply, [this, req, ctx](signup::pb::Reply *reply) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "Signup request from client at " << ctx->peer();

        // Validate the information in the request

        // TODO: Add support for a cluster of nextapp servers. For now we only have one.

        // Ask the nextapp server to create the tenant and user.
        nextapp::pb::Tenant newTenant;
        {
            nextapp::pb::CreateTenantReq tenant_req;
            auto *tenant = tenant_req.mutable_tenant();
            if (!tenant) {
                throw runtime_error{"Failed to create tenant object"};
            }
            auto * user = tenant_req.add_users();
            if (!user) {
                throw runtime_error{"Failed to create user object"};
            }

            tenant->set_name(req->tenantname());
            tenant->set_kind(nextapp::pb::Tenant::Kind::Tenant_Kind_REGULAR);
            tenant->set_state(nextapp::pb::Tenant::State::Tenant_State_PENDING_ACTIVATION);

            user->set_name(req->username());
            user->set_email(req->email());
            user->set_kind(nextapp::pb::User::Kind::User_Kind_REGULAR);
            user->set_active(true);

            auto resp = co_await owner_.callRpc<nextapp::pb::Status>(
                tenant_req,
                &stub_t::async::CreateTenant,
                asio::use_awaitable);

            if (resp.error() != nextapp::pb::Error::OK) {
                LOG_WARN << "Failed to create tenant and user at nextapp-server: " << resp.message();
                throw Error{resp.error(), format("Failed to create tenant and/or user: {}", resp.message())};
            }

            if (resp.has_tenant()) {
                newTenant = resp.tenant();
            } else {
                throw runtime_error{"Failed to get tenant from nextapp-server's reply"};
            }
        }

        // We now have a tenant and user in a pending state.
        // Create the device and get the signed certificate

        {
            nextapp::pb::CreateDeviceReq device_req;
            if (auto *dev = device_req.mutable_device()) {
                dev->CopyFrom(req->device());
            } else {
                throw runtime_error{"Failed to create device object"};
            }

            if (newTenant.users_size() != 1) {
                throw runtime_error{"I expected one user to be created with the tenant. I got"
                                    + to_string(newTenant.users().size())};
            }

            device_req.set_userid(newTenant.users(0).uuid());

            auto resp = co_await owner_.callRpc<nextapp::pb::Status>(
                device_req,
                &stub_t::async::CreateDevice,
                asio::use_awaitable);

            if (resp.error() != nextapp::pb::Error::OK) {
                LOG_WARN << "Failed to create device at nextapp-server: " << resp.message();
                throw Error{resp.error(), format("Failed to create device: {}", resp.message())};
            }

            if (!resp.has_createdeviceresp()) {
                throw runtime_error{"Failed to get device from nextapp-server's reply"};
            }

            const auto& dresp = resp.createdeviceresp();

            if (auto* response = reply->mutable_signupresponse()) {
                assert(response);

                response->set_uuid(dresp.deviceid());
                response->set_cert(dresp.cert());
                response->set_serverurl(owner_.server().config().grpc_nextapp.address);
                response->set_cacert(dresp.cacert());
                assert(!response->cacert().empty());
            } else {
                throw runtime_error{"Failed to create signupresponse object"};
            }
        }

        // At this time the user is created, but not activated.
        // It will be activated the first time the devcice connects to the server.
        // If the user tries to create a new account now, the non-active account will be
        // replaced wit the new one. The only stable identifiers at this time is the device uuid
        // and the email.

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
    const auto server_url = nextapp_config().address;

    LOG_INFO << "Connecting to NextApp gRPC endpoint at "
             << server_url;
    const bool use_tls = server_url.starts_with("https://");

    auto server_address = server_url;
    if (auto pos = server_address.find("://"); pos != string::npos) {
        server_address = server_address.substr(pos + 3);
    }

    // Create a gRPC channel to the NextApp service.
    // Set up TLS credentials with our own certificate.
    std::shared_ptr<::grpc::ChannelCredentials> creds;
    if (use_tls) {
        ::grpc::SslCredentialsOptions ssl_opts;
        if (!nextapp_config().ca_cert.empty()) {
            ssl_opts.pem_root_certs = readFileToBuffer(nextapp_config().ca_cert);
            LOG_DEBUG_N << "Using ca-cert from " << nextapp_config().ca_cert;
        }
        if (!nextapp_config().server_cert.empty() && !nextapp_config().server_key.empty()) {
            // Load our own certificate and private key (for client auth)
            ssl_opts.pem_private_key = readFileToBuffer(nextapp_config().server_key);
            ssl_opts.pem_cert_chain = readFileToBuffer(nextapp_config().server_cert);
            LOG_DEBUG_N << "Using client-cert from " << nextapp_config().server_cert;
            creds = ::grpc::SslCredentials(ssl_opts);
        } else {
            LOG_WARN_N << "No TLS cert provided. Proceeding without client cert.";
            ::grpc::experimental::TlsChannelCredentialsOptions tls_opts;
            tls_opts.set_verify_server_certs(false);
            creds = ::grpc::experimental::TlsCredentials(tls_opts);
        }

    } else {
        LOG_WARN_N << "Not using TLS on nextapp grpc connection. Don't do this over a public network!!";
        creds = ::grpc::InsecureChannelCredentials();
    }
    assert(creds);
    if (!creds) {
        LOG_ERROR_N << "Failed to create gRPC channel credentials.";
        throw runtime_error{"Failed to create gRPC channel credentials."};
    }

    auto channel = ::grpc::CreateChannel(server_address, creds);
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

signup::pb::Error translateError(const pb::Error &e) {
    switch (e) {
    case nextapp::pb::Error::MISSING_CSR:
        return signup::pb::Error::MISSING_CSR;
    case nextapp::pb::Error::INVALID_CSR:
        return signup::pb::Error::INVALID_CSR;
    case nextapp::pb::Error::ALREADY_EXIST:
        return signup::pb::Error::EMAIL_ALREADY_EXISTS;
    case nextapp::pb::Error::MISSING_USER_EMAIL:
        return signup::pb::Error::MISSING_EMAIL;
    default:
        return signup::pb::Error::GENERIC_ERROR;
    }
}

} // ns
