
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

namespace nextapp {

using namespace ::signup::pb;

GrpcServer::GrpcServer(Server &server)
: server_(server)
{
    LOG_DEBUG_N << "GrpcServer created.";
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
            *response = co_await owner_.server().getInfo(*req);
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

        if (!req->region().empty()) {
            throw runtime_error{"Region is unset"};
        }

        // Get an instance to use
        const auto assigned_instance = co_await owner_.server().assignInstance(toUuid(req->region()));

        // Get the RPC connection to that instance
        auto conn = owner_.getInstance(assigned_instance.instance);

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

            auto resp = co_await conn->callRpc<nextapp::pb::Status>(
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

            auto *auth = device_req.mutable_userid();
            if (!auth) {
                throw runtime_error{"Failed to create userid object"};
            }
            *auth = newTenant.users(0).uuid();

            auto resp = co_await conn->callRpc<nextapp::pb::Status>(
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
                response->set_serverurl(owner_.server().config().cluster.nextapp_public_url);
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

::grpc::ServerUnaryReactor *GrpcServer::SignupImpl::CreateNewDevice(::grpc::CallbackServerContext *ctx,
                                                                    const signup::pb::CreateNewDeviceRequest *req,
                                                                    signup::pb::Reply *reply)
{
    return unaryHandler(ctx, req, reply, [this, req, ctx](signup::pb::Reply *reply) -> boost::asio::awaitable<void> {
        LOG_DEBUG << "CreateNewDevice request from client at " << ctx->peer();

        // Validate the information in the request

        {
            nextapp::pb::CreateDeviceReq device_req;
            if (auto *dev = device_req.mutable_device()) {
                dev->CopyFrom(req->device());
            } else {
                throw runtime_error{"Failed to create device object"};
            }

            if (req->has_otpauth()) {
                auto *auth = device_req.mutable_otpauth();
                if (!auth) {
                    throw runtime_error{"Failed to create otpauth object"};
                }
                auth->CopyFrom(req->otpauth());
            } else {
                throw server_err{nextapp::pb::Error::MISSING_AUTH, "Missing OTP"};
            }

            // TODO: Deduce the tenant from the email and see if we have a connection to the tenants nextapp instance.
            auto instance = co_await owner_.server().getInstanceFromUserEmail(req->otpauth().email());
            if (!instance) {
                throw server_err{nextapp::pb::Error::GENERIC_ERROR, "Failed to lookup instance from email"};
            }

            auto conn = owner_.getInstance(*instance);
            if (!conn) {
                throw server_err{nextapp::pb::Error::TEMPORATY_FAILURE, "No connection for your instance."};
            }

            auto resp = co_await conn->callRpc<nextapp::pb::Status>(
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
                response->set_serverurl(owner_.server().config().cluster.nextapp_public_url);
                response->set_cacert(dresp.cacert());
                assert(!response->cacert().empty());
            } else {
                throw runtime_error{"Failed to create signupresponse object"};
            }
        }
    }, __func__);
}

void GrpcServer::start() {
    while(!server_.is_done()) {
        // Wait for us to be connected to the nextapp instances
        try {

            break; // OK
        } catch(const std::exception &ex) {
            LOG_ERROR << "Caught exception while connecting to nextappd: " << ex.what();

            if (const auto delay = server_.config().options.retry_connect_to_nextappd_secs) {
                LOG_ERROR << "Retrying in " << delay << " seconds.";
                std::this_thread::sleep_for(chrono::seconds(delay));
            } else {
                throw;
            }
        }
    }
    startSignup();
}

void GrpcServer::stop() {
    LOG_INFO << "Shutting down GrpcServer.";
    active_ = false;
    auto deadline = std::chrono::system_clock::now() + 6s;
    grpc_server_->Shutdown(deadline);
    grpc_server_.reset();
    LOG_DEBUG << "GrpcServer is done.";

    LOG_DEBUG << "Shutting down NextApp gRPC channels to Nextapp servers.";
    for(auto& [id, conn] : instances_) {
        try {
            conn->shutdown();
        } catch(const std::exception &ex) {
            LOG_ERROR << "Caught exception while shutting down NextApp connection "
                << id << ": " << ex.what();
        }
    };
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

    // Set up keepalive options
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, server_.config().grpc_signup.keepalive_time_sec * 1000);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, server_.config().grpc_signup.keepalive_timeout_sec * 1000);
    builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
    builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 1);

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

boost::asio::awaitable<bool> GrpcServer::InstanceCommn::connect(const InstanceInfo &info)
{
    url_ = info.url;

    LOG_DEBUG << "Connecting to NextApp gRPC endpoint at " << url_;
    const bool use_tls = url_.starts_with("https://");

    auto server_address = url_;
    if (auto pos = server_address.find("://"); pos != string::npos) {
        server_address = server_address.substr(pos + 3);
    }

    // Create a gRPC channel to the NextApp service.
    // Set up TLS credentials with our own certificate.
    std::shared_ptr<::grpc::ChannelCredentials> creds;
    if (use_tls) {
        ::grpc::SslCredentialsOptions ssl_opts;
        if (!info.x509_ca_cert.empty()) {
            ssl_opts.pem_root_certs = info.x509_ca_cert;
        }
        if (!info.x509_cert.empty() && !info.x509_key.empty()) {
            // Load our own certificate and private key (for client auth)
            ssl_opts.pem_private_key = info.x509_key;
            ssl_opts.pem_cert_chain = info.x509_cert;
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

    ::grpc::ChannelArguments args;
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, owner_.nextapp_config().keepalive_time_sec * 1000);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, owner_.nextapp_config().keepalive_timeout_sec * 1000);

    channel_ = ::grpc::CreateCustomChannel(server_address, creds, args);
    nextapp_stub_ = nextapp::pb::Nextapp::NewStub(channel_);

    // Connect
    try {
        ::grpc::ClientContext ctx;
        auto resp = co_await callRpc<nextapp::pb::Status>(
            nextapp::pb::Empty{},
            &stub_t::async::Hello,
            asio::use_awaitable);

        if (resp.error() != nextapp::pb::Error::OK) {
            throw runtime_error{"Failed to connect and authorize with nextappd: " + resp.message()};
        } else {
            if (resp.has_hello()) {
                const auto& hello = resp.hello();
                session_id_ = hello.sessionid();
                LOG_DEBUG << "Got session id: " << session_id_;
            } else {
                LOG_WARN << "Failed to get hello from nextapp-server's reply";
            }
        }

        resp = co_await callRpc<nextapp::pb::Status>(
            nextapp::pb::Empty{},
            &stub_t::async::GetServerInfo,
            asio::use_awaitable);

        LOG_INFO << "Connected to nextapp-server at " << owner_.nextapp_config().address;
        if (resp.has_serverinfo()) {
            for(const auto& kv : resp.serverinfo().properties()) {
                LOG_INFO << kv.key() << ": " << kv.value();
            }
            server_info_ = resp.serverinfo();
        }
    } catch (const std::exception &ex) {
        LOG_ERROR << "Failed to connect to NextApp server at "
                  << info.url
                  <<": "<< ex.what();
        co_return false;
    }

    startNextTimer(owner_.server_.config().options.timer_interval_sec
                   // Add some randomness to avoid all the timers to fire at the same time
                   + getRandomNumber32() % 15);
    co_return true;
}

void GrpcServer::InstanceCommn::startNextTimer(size_t seconds)
{
    try {
        timer_.expires_after(std::chrono::seconds(owner_.server_.config().options.timer_interval_sec));
    } catch(const std::exception &ex) {
        if (owner_.server_.is_done()) {
            LOG_DEBUG_N << "Failed to set timer, but it appears as we are shutting down.";
            return;
        }
        LOG_ERROR << "Failed to set timer: " << ex.what();
        // TODO: Should we shut down? How do we recover from this?
        assert(false);
    }

    timer_.async_wait([this](const boost::system::error_code &ec) {
        if (ec) {
            LOG_DEBUG_N << "Timer cancelled: " << ec.message();
            return;
        }
        LOG_DEBUG << "Timer fired. Refreshing server info for " << url_;
        try {
            onTimer();
        } catch(const std::exception &ex) {
            LOG_ERROR << "Caught exception from onTimer(): " << ex.what();
        }
        startNextTimer(owner_.server_.config().options.timer_interval_sec
                       + getRandomNumber32() % 15);
    });
}

void GrpcServer::InstanceCommn::onTimer() {
    if (owner_.server_.is_done()) {
        LOG_DEBUG_N << "Timer fired, but server is done. Ignoring.";
        return;
    }

    // Test the connection to the NextApp server.
    // Create asio C++20 coro scope
    auto f = asio::co_spawn(owner_.server().ctx(), [this]() -> asio::awaitable<void> {
        ::grpc::ClientContext ctx;

        try {
            auto resp = co_await callRpc<nextapp::pb::Status>(
                nextapp::pb::PingReq{},
                &stub_t::async::Ping,
                asio::use_awaitable);

            LOG_DEBUG_N << "Still connected to nextapp-server at " << url_;
        } catch(const std::exception &ex) {
            LOG_ERROR << "Failed to ping nextapp-server: " << ex.what();
        }
    }, boost::asio::use_awaitable);
}

boost::asio::awaitable<std::optional<::nextapp::pb::ServerInfo>>
GrpcServer::connectToInstance(const boost::uuids::uuid&uuid,
                              const InstanceCommn::InstanceInfo &info)
{
    auto instance = std::make_shared<InstanceCommn>(*this);
    auto res = co_await instance->connect(info);
    if (res) {
        addInstance(uuid, instance);
        co_return instance->serverInfo();
    }

    LOG_ERROR << "Failed to connect to instance at " << info.url;
    co_return std::nullopt;
}

} // ns

