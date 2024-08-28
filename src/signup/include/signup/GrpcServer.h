#pragma once

#include <queue>
#include <map>
#include <boost/uuid/uuid.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "signup/Server.h"
#include "signup/config.h"
// #include "nextapp.pb.h"
// #include "nextapp.grpc_signup.pb.h"
// #include "nextapp/logging.h"
// #include "nextapp/errors.h"
// #include "nextapp/UserContext.h"
// #include "nextapp/certs.h"

#include "signup.pb.h"
#include "signup.grpc.pb.h"
#include "nextapp.grpc.pb.h"

#include "nextapp/util.h"
#include "nextapp/error_mapping.h"

namespace nextapp {

signup::pb::Error translateError(const nextapp::pb::Error& e);

class GrpcServer {
public:

    class Error : public std::runtime_error {
    public:
        Error(const nextapp::pb::Error& err, std::string_view what) noexcept
            : std::runtime_error(what.data()), error_{err} {}

        auto error() const noexcept {
            return error_;
        }

    private:
        nextapp::pb::Error error_{nextapp::pb::Error::GENERIC_ERROR};
    };

    template <ProtoMessage T>
    std::string toJsonForLog(const T& obj) {
        return toJson(obj, server().config().options.log_protobuf_messages);
    }

    template <typename T>
    class ReqBase {
    public:
        ReqBase() {
            LOG_TRACE << "Creating instance for request# " << client_id_;
        }

        void done() {
            // Ugly, ugly, ugly
            LOG_TRACE << "If the program crash now, it was a bad idea to delete this ;)  #"
                      << client_id_ << " at address " << this;
            delete static_cast<T *>(this);
        }

        std::string me() const {
            return boost::typeindex::type_id_runtime(static_cast<const T&>(*this)).pretty_name()
                   + " #"
                   + std::to_string(client_id_);
        }

    protected:
        const size_t client_id_ = getNewClientId();
    };

    template <ProtoMessage replyT, ProtoMessage reqT, typename T>
    struct CallData {
        CallData(reqT && req, T& self)
            : req{std::forward<reqT>(req)}, self_{std::move(self)} {}
        reqT req;
        ::grpc::ClientContext ctx;
        replyT reply;
        std::remove_cvref_t<T> self_;
    };

    template <ProtoMessage replyT, ProtoMessage reqT, typename CompletionToken>
    auto callRpc(reqT request,
        void (::nextapp::pb::Nextapp::Stub::async::*call)(::grpc::ClientContext* context, const reqT* request, replyT* response, std::function<void(::grpc::Status)>),
                 CompletionToken&& token) {

        return boost::asio::async_compose<CompletionToken, void(boost::system::error_code, replyT)>(
            [this, request=std::move(request), call](auto& self) mutable {
                auto cd = std::make_shared<CallData<replyT, reqT, decltype(self)>>(std::move(request), self);

                // TODO: Find a better way! We are using a shared pointer here, which is not good.
                auto fn = [this, cd](const ::grpc::Status& status) mutable {
                    boost::system::error_code ec;
                    if (!status.ok()) {
                        ec = make_error_code(status.error_code());
                    }

                    LOG_TRACE << "RPC call completed. Status: " << status.error_message();
                    LOG_TRACE << "Reply: " << toJsonForLog(cd->reply);
                    cd->self_.complete(ec, cd->reply);
                };

                if (!session_id_.empty()) {
                    cd->ctx.AddMetadata("sid", session_id_);
                }

                (nextapp_stub_->async()->*call)(&cd->ctx, &cd->req, &cd->reply,
                                                [fn=std::move(fn)](const ::grpc::Status& status) mutable {
                    fn(status);
                });

        }, token);
    }

    template <typename T, typename... Args>
    static auto createNew(GrpcServer& parent, Args... args) {

        try {
            return new T(parent, args...);
            // If we got here, the instance should be fine, so let it handle itself.
        } catch(const std::exception& ex) {
            LOG_ERROR << "Got exception while creating a new instance. "
                      << "This ends the jurney for this instance of me. "
                      << " Error: " << ex.what();
        }

        abort();
    }

    /*! RPC implementation
     *
     *  This class overrides our RPC methods from the code
     *  generatoed by rpcgen. This is where we receive the RPC events from gRPC.
     */
    class SignupImpl : public ::signup::pb::SignUp::CallbackService {
    public:
        SignupImpl(GrpcServer& owner)
            : owner_{owner} {}

        ::grpc::ServerUnaryReactor * GetInfo(::grpc::CallbackServerContext *ctx,
                                            const signup::pb::GetInfoRequest *req,
                                            signup::pb::Reply *reply) override;


        ::grpc::ServerUnaryReactor * SignUp(::grpc::CallbackServerContext *ctx,
                                            const signup::pb::SignUpRequest *req,
                                            signup::pb::Reply *reply) override;


        ::grpc::ServerUnaryReactor * CreateNewDevice(::grpc::CallbackServerContext *ctx,
                                           const signup::pb::CreateNewDeviceRequest *req,
                                           signup::pb::Reply *reply) override;


    private:
        // Boilerplate code to run async SQL queries or other async coroutines from an unary gRPC callback
        template <typename ReqT, typename ReplyT, typename FnT>
        ::grpc::ServerUnaryReactor*
        unaryHandler(::grpc::CallbackServerContext *ctx, const ReqT * req, ReplyT *reply, FnT fn, std::string_view name = {}) noexcept {
            assert(ctx);
            assert(reply);

            auto* reactor = ctx->DefaultReactor();

            boost::asio::co_spawn(owner_.server().ctx(), [this, ctx, req, reply, reactor, fn=std::move(fn), name]() -> boost::asio::awaitable<void> {
                try {
                    LOG_TRACE << "Request [" << name << "] " << req->GetDescriptor()->name() << ": " << owner_.toJsonForLog(*req);
                    co_await fn(reply);
                    LOG_TRACE << "Replying [" << name << "]: " << owner_.toJsonForLog(*reply);
                    reactor->Finish(::grpc::Status::OK);
                } catch (const Error& ex) {
                    LOG_WARN << "Request [" << name << "] Caught Error exception while handling grpc request coro: "
                             << ex.error() << ' ' << ex.what();
                    reply->set_error(translateError(ex.error()));
                    reply->set_message(ex.what());
                    reactor->Finish(::grpc::Status::OK);
                } catch (const std::exception& ex) {
                    LOG_WARN << "Request [" << name << "] Caught exception while handling grpc request coro: " << ex.what();
                    reply->set_error(signup::pb::Error::GENERIC_ERROR);
                    reply->set_message("Something went wrong. May be try again later.");
                    reactor->Finish(::grpc::Status::OK);
                }

                LOG_TRACE << "Request [" << name << "] Exiting unary handler.";

            }, boost::asio::detached);

            return reactor;
        }

        GrpcServer& owner_;
    };

    GrpcServer(Server& server);

    Server& server() {
        return server_;
    }

    void start();

    void stop();

    const GrpcConfig& signup_config() const noexcept {
        return server_.config().grpc_signup;
    }

    const GrpcConfig& nextapp_config() const noexcept {
        return server_.config().grpc_nextapp;
    }

private:
    // The Server instance where we get objects in the application, like config and database
    Server& server_;

    void startNextapp();
    void startSignup();
    void startNextTimer(size_t seconds);
    void onTimer();

    // Thread-safe method to get a unique client-id for a new RPC.
    static size_t getNewClientId() {
        static std::atomic_size_t id{0};
        return ++id;
    }

    // An instance of our service, compiled from code generated by protoc
    std::unique_ptr<SignupImpl> service_;

    // A gRPC server object
    std::unique_ptr<::grpc::Server> grpc_server_;

    mutable std::mutex mutex_;
    std::atomic_bool active_{false};
    std::unique_ptr<nextapp::pb::Nextapp::Stub> nextapp_stub_;
    std::optional<boost::asio::steady_timer> timer_;
    std::string session_id_;
};

using server_err = GrpcServer::Error;

} // ns
