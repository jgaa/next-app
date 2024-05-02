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
#include "nextapp/UserContext.h"
#include "util.h"

#include "mysqlpool/mysqlpool.h"

namespace nextapp::grpc {

struct RequestCtx {
    std::optional<jgaa::mysqlpool::Mysqlpool::Handle> dbh;
    std::shared_ptr<UserContext> uctx;
};

template <typename T, typename Arg>
concept UnaryFnWithoutContext = requires (T fn, Arg *reply) {
    { fn(reply) } ;
};

template <typename T, typename Arg>
concept UnaryFnWithContext = requires (T fn, Arg *reply, RequestCtx& rctx) {
    { fn(reply, rctx) } ;
};

class GrpcServer {
public:

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
        virtual void close() = 0;

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
        ::grpc::ServerUnaryReactor *SetDay(::grpc::CallbackServerContext *ctx, const pb::CompleteDay *req, pb::Status *reply) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Update>* SubscribeToUpdates(::grpc::CallbackServerContext* context, const ::nextapp::pb::UpdatesReq* request) override;
        ::grpc::ServerUnaryReactor *CreateTenant(::grpc::CallbackServerContext *ctx, const pb::CreateTenantReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateNode(::grpc::CallbackServerContext *ctx, const pb::CreateNodeReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateNode(::grpc::CallbackServerContext *ctx, const pb::Node*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *MoveNode(::grpc::CallbackServerContext *ctx, const pb::MoveNodeReq*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteNode(::grpc::CallbackServerContext *ctx, const pb::DeleteNodeReq*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetNodes(::grpc::CallbackServerContext *ctx, const pb::GetNodesReq *req, pb::NodeTree *reply) override;
        ::grpc::ServerUnaryReactor *GetActions(::grpc::CallbackServerContext *ctx, const pb::GetActionsReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetAction(::grpc::CallbackServerContext *ctx, const pb::GetActionReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteAction(::grpc::CallbackServerContext *ctx, const pb::DeleteActionReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *MarkActionAsDone(::grpc::CallbackServerContext *ctx, const pb::ActionDoneReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *MarkActionAsFavorite(::grpc::CallbackServerContext *ctx, const pb::ActionFavoriteReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetFavoriteActions(::grpc::CallbackServerContext *ctx, const pb::Empty *, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateWorkSession(::grpc::CallbackServerContext *ctx, const pb::CreateWorkReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *AddWorkEvent(::grpc::CallbackServerContext *ctx, const pb::AddWorkEventReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *ListCurrentWorkSessions(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetWorkSummary(::grpc::CallbackServerContext *ctx, const pb::WorkSummaryRequest *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteWorkSession(::grpc::CallbackServerContext *ctx, const pb::DeleteWorkReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetWorkSessions(::grpc::CallbackServerContext *ctx, const pb::GetWorkSessionsReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *AddWork(::grpc::CallbackServerContext *ctx, const pb::AddWorkReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetDetailedWorkSummary(::grpc::CallbackServerContext *ctx, const pb::DetailedWorkSummaryRequest *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetUserGlobalSettings(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *SetUserGlobalSettings(::grpc::CallbackServerContext *ctx, const pb::UserGlobalSettings *req, pb::Status *reply) override;

    private:
        // Boilerplate code to run async SQL queries or other async coroutines from an unary gRPC callback
        template <typename ReqT, typename ReplyT, typename FnT>
        ::grpc::ServerUnaryReactor*
        unaryHandler(::grpc::CallbackServerContext *ctx, const ReqT * req, ReplyT *reply, FnT fn, const std::string_view name = {}) noexcept {
            assert(ctx);
            assert(reply);

            auto* reactor = ctx->DefaultReactor();

            boost::asio::co_spawn(owner_.server().ctx(), [this, ctx, req, reply, reactor, fn, name]() -> boost::asio::awaitable<void> {

                    try {
                        LOG_TRACE << "Request [" << name << "] " << req->GetDescriptor()->name() << ": " << owner_.toJsonForLog(*req);

                        if constexpr (UnaryFnWithoutContext<FnT, ReplyT>) {
                            co_await fn(reply);
                        } else if constexpr (UnaryFnWithContext<FnT, ReplyT>) {
                            RequestCtx rctx;
                            rctx.uctx = owner_.userContext(ctx);
                            rctx.dbh.emplace(co_await owner_.server().db().getConnection(rctx.uctx->dbOptions()));
                            co_await fn(reply, rctx);
                        } else if constexpr (!UnaryFnWithoutContext<FnT, ReplyT *> && !UnaryFnWithContext<FnT, ReplyT *>) {
                            static_assert(false, "Invalid unary handler function");
                        }
                        LOG_TRACE << "Replying [" << name << "]: " << owner_.toJsonForLog(*reply);
                        reactor->Finish(::grpc::Status::OK);
                    } catch (const db_err& ex) {
                        if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                            LOG_DEBUG << "Request [" << name << "] Caught db_err exception while handling grpc request: " << ex.what();
                            reply->Clear();
                            reply->set_error(ex.error());
                            reply->set_message(ex.what());
                            reactor->Finish(::grpc::Status::OK);
                        } else {
                            LOG_WARN << "Request [" << name << "] Caught db_err exception while handling grpc request coro: " << ex.what();
                            reactor->Finish(::grpc::Status::CANCELLED);
                        }
                    } catch (const std::exception& ex) {
                        LOG_WARN << "Request [" << name << "] Caught exception while handling grpc request coro: " << ex.what();
                        reactor->Finish(::grpc::Status::CANCELLED);
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

    const GrpcConfig& config() const noexcept {
        return server_.config().grpc;
    }

    void addPublisher(const std::shared_ptr<Publisher>& publisher);
    void removePublisher(const boost::uuids::uuid& uuid);
    void publish(const std::shared_ptr<pb::Update>& update);
    boost::asio::awaitable<void> validateNode(const std::string& parentUuid, const std::string& userUuid);
    boost::asio::awaitable<void> validateNode(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string& parentUuid, const std::string& userUuid);
    boost::asio::awaitable<void> validateAction(const std::string &actionId, const std::string &userUuid, std::string *name = {});
    boost::asio::awaitable<void> validateAction(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string &actionId, const std::string &userUuid, std::string *name = {});
    boost::asio::awaitable<nextapp::pb::Node> fetcNode(const std::string& uuid, const std::string& userUuid);
    boost::asio::awaitable<pb::WorkSession> fetchWorkSession(const std::string& uuid, RequestCtx& rctx);
    boost::asio::awaitable<void> saveWorkSession(nextapp::pb::WorkSession& work, RequestCtx& rctx, bool touch = true);
    boost::asio::awaitable<boost::mysql::results> insertWork(const pb::WorkSession& work, RequestCtx& rctx, bool addStartEvent = true);

    bool active() const noexcept {
        return active_;
    }

    const std::shared_ptr<UserContext> userContext(::grpc::CallbackServerContext *ctx) const;

    // Called when an action change status to done
    boost::asio::awaitable<void> handleActionDone(const pb::Action& orig,
                                                  RequestCtx& rctx,
                                                  ::grpc::CallbackServerContext *ctx);

    // Called when an action change status to active
    boost::asio::awaitable<void> handleActionActive(const pb::Action& orig,
                                                    RequestCtx& rctx,
                                                    ::grpc::CallbackServerContext *ctx);

    static nextapp::pb::Due
    processDueAtDate(time_t from_timepoint, const pb::Action_RepeatUnit &units,
                     pb::ActionDueKind kind, int repeatAfter,
                     const UserContext& uctx);

    static nextapp::pb::Due
    processDueAtDayspec(time_t from_timepoint, const pb::Action_RepeatUnit &units,
                        pb::ActionDueKind kind, int repeatAfter,
                        const UserContext& uctx);

    /*! Set the due.duie time based ion the due.start time and repeat config */
    static nextapp::pb::Due adjustDueTime(const nextapp::pb::Due& due, const UserContext& uctx);

    boost::asio::awaitable<void> stopWorkSession(nextapp::pb::WorkSession& work, RequestCtx& rctx,
                                                 const nextapp::pb::WorkEvent *event = {});
    static void updateOutcome(nextapp::pb::WorkSession& work, const UserContext& rctx);
    boost::asio::awaitable<void> activateNextWorkSession(RequestCtx& rctx);
    boost::asio::awaitable<void> pauseWork(RequestCtx& rctx);
    boost::asio::awaitable<void> syncActiveWork(RequestCtx& rctx);
    boost::asio::awaitable<void> pauseWorkSession(pb::WorkSession &work, RequestCtx& rctx);
    boost::asio::awaitable<void> touchWorkSession(pb::WorkSession &work, RequestCtx& rctx);
    boost::asio::awaitable<void> resumeWorkSession(pb::WorkSession &work, RequestCtx& rctx);
    boost::asio::awaitable<std::optional<pb::WorkSession> > fetchActiveWorkSession(RequestCtx& rctx);
    boost::asio::awaitable<void> endWorkSessionForAction(const std::string_view& actionId, RequestCtx& rctx);

    void updateSessionSettingsForUser(const pb::UserGlobalSettings& settings, ::grpc::CallbackServerContext *ctx);

    boost::uuids::uuid getSessionId(::grpc::CallbackServerContext *ctx) const;

private:
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

    mutable std::map<boost::uuids::uuid, std::shared_ptr<UserContext>> sessions_;
    std::map<boost::uuids::uuid, std::weak_ptr<Publisher>> publishers_;
    mutable std::mutex mutex_;
    std::atomic_bool active_{false};
};

} // ns
