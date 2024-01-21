#pragma once

#include <queue>
#include <map>
#include <boost/uuid/uuid.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "nextapp/Server.h"
#include "nextapp/nextapp.h"
#include "nextapp/config.h"
#include "nextapp.pb.h"
#include "nextapp.grpc.pb.h"
#include "nextapp/logging.h"
#include "nextapp/errors.h"

namespace nextapp::grpc {

boost::uuids::uuid newUuid();

class GrpcServer {
public:
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

    class Publisher {
    public:
        virtual ~Publisher() = default;

        virtual void publish(const std::shared_ptr<pb::Update>& message) = 0;

        auto& uuid() const noexcept {
            return uuid_;
        }

    private:
        const boost::uuids::uuid uuid_ = newUuid();
    };

    /*! RPC implementation
     *
     *  This class overrides our RPC methods from the code
     *  generatoed by rpcgen. This is where we receive the RPC events from gRPC.
     */
    class NextappImpl : public pb::Nextapp::CallbackService {
    public:
        NextappImpl(GrpcServer& owner)
            : owner_{owner} {}

        ::grpc::ServerUnaryReactor *GetServerInfo(::grpc::CallbackServerContext *, const pb::Empty *, pb::ServerInfo *) override;
        ::grpc::ServerUnaryReactor *GetDayColorDefinitions(::grpc::CallbackServerContext *, const pb::Empty *, pb::DayColorDefinitions *) override;
        ::grpc::ServerUnaryReactor *GetDay(::grpc::CallbackServerContext *ctx, const pb::Date *req, pb::CompleteDay *reply) override;
        ::grpc::ServerUnaryReactor *GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply) override;
        ::grpc::ServerUnaryReactor *SetColorOnDay(::grpc::CallbackServerContext *ctx, const pb::SetColorReq *req, pb::Status *reply) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Update>* SubscribeToUpdates(::grpc::CallbackServerContext* context, const ::nextapp::pb::UpdatesReq* request) override;
        ::grpc::ServerUnaryReactor *CreateTenant(::grpc::CallbackServerContext *ctx, const pb::CreateTenantReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateNode(::grpc::CallbackServerContext *ctx, const pb::CreateNodeReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetNodes(::grpc::CallbackServerContext *ctx, const pb::GetNodesReq *req, pb::NodeTree *reply) override;

    private:
        // Boilerplate code to run async SQL queries or other async coroutines from an unary gRPC callback
        auto unaryHandler(::grpc::CallbackServerContext *ctx, const auto * req, auto *reply, auto fn) noexcept {
            assert(ctx);
            assert(reply);

            auto* reactor = ctx->DefaultReactor();

            boost::asio::co_spawn(owner_.server().ctx(), [this, ctx, req, reply, reactor, fn]() -> boost::asio::awaitable<void> {

                    try {
                        co_await fn(reply);
                        reactor->Finish(::grpc::Status::OK);
                    } catch (const db_err& ex) {
                        if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                            reply->Clear();
                            reply->set_error(ex.error());
                            reply->set_message(ex.what());
                            reactor->Finish(::grpc::Status::OK);
                        } else {
                            LOG_WARN_N << "Caught db_err exception while handling grpc request coro: " << ex.what();
                            reactor->Finish(::grpc::Status::CANCELLED);
                        }
                    } catch (const std::exception& ex) {
                        LOG_WARN_N << "Caught exception while handling grpc request coro: " << ex.what();
                        reactor->Finish(::grpc::Status::CANCELLED);
                    }

                    LOG_TRACE_N << "Exiting unary handler.";

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

    const GrpcConfig& config() const noexcept {
        return server_.config().grpc;
    }

    void addPublisher(const std::shared_ptr<Publisher>& publisher);
    void removePublisher(const boost::uuids::uuid& uuid);
    void publish(const std::shared_ptr<pb::Update>& update);

private:

    // TODO: Implement auth
    std::string currentUser(::grpc::CallbackServerContext */*ctx*/) const {
        return "dd2068f6-9cbb-11ee-bfc9-f78040cadf6b";
    }

    // TODO: Implement auth
    std::string currentTenant(::grpc::CallbackServerContext */*ctx*/) const {
        return "a5e7bafc-9cba-11ee-a971-978657e51f0c";
    }

    // The Server instance where we get objects in the application, like config and database
    Server& server_;

    // Thread-safe method to get a unique client-id for a new RPC.
    static size_t getNewClientId() {
        static std::atomic_size_t id{0};
        return ++id;
    }

    // An instance of our service, compiled from code generated by protoc
    std::unique_ptr<NextappImpl> service_;

    // A gRPC server object
    std::unique_ptr<::grpc::Server> grpc_server_;

    std::map<boost::uuids::uuid, std::weak_ptr<Publisher>> publishers_;
    std::mutex mutex_;
};

} // ns
