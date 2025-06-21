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
#include "nextapp/certs.h"
#include "nextapp/util.h"
#include "nextapp/AsyncServerWriteReactor.h"
#include "nextapp/AsyncServerReadReactor.h"

#include "mysqlpool/mysqlpool.h"

namespace nextapp::grpc {

// Use this so that we don't forget to set the operation
std::shared_ptr<nextapp::pb::Update> newUpdate(nextapp::pb::Update::Operation op);

class RequestCtx {
public:
    RequestCtx(std::shared_ptr<UserContext::Session> session)
        : session_{std::move(session)}, uctx{&session->user()} {}

    std::optional<jgaa::mysqlpool::Mysqlpool::Handle> dbh;
    UserContext *uctx{};

    struct Update {
        Update(std::shared_ptr<nextapp::pb::Update> update, bool send_on_fail = false)
            : update{std::move(update)}, send_on_fail{send_on_fail} {}

        std::shared_ptr<nextapp::pb::Update> update;
        bool send_on_fail = false;
    };

    std::vector<Update> updates;

    void publishLater(std::shared_ptr<nextapp::pb::Update> update, bool sendOnFail = false) {
        updates.emplace_back(update, sendOnFail);
    }

    nextapp::pb::Update& publishLater(pb::Update::Operation op, bool sendOnFail = false) {
        auto& up = updates.emplace_back(newUpdate(op), sendOnFail);
        return *up.update;
    }

    UserContext::Session& session() const noexcept {
        assert(session_);
        return *session_;
    }

    UserContext::Session& session() noexcept {
        assert(session_);
        return *session_;
    }


private:
    std::shared_ptr<UserContext::Session> session_;
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

    /*! RPC implementation
     *
     *  This class overrides our RPC methods from the code
     *  generatoed by rpcgen. This is where we receive the RPC events from gRPC.
     */
    class NextappImpl : public pb::Nextapp::CallbackService {
    public:
        NextappImpl(GrpcServer& owner)
            : owner_{owner} {}

        ::grpc::ServerUnaryReactor *Hello(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *Ping(::grpc::CallbackServerContext *ctx, const pb::PingReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetServerInfo(::grpc::CallbackServerContext *, const pb::Empty *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *GetDayColorDefinitions(::grpc::CallbackServerContext *, const pb::Empty *, pb::DayColorDefinitions *) override;
        ::grpc::ServerUnaryReactor *GetDay(::grpc::CallbackServerContext *ctx, const pb::Date *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetMonth(::grpc::CallbackServerContext *ctx, const pb::MonthReq *req, pb::Month *reply) override;
        ::grpc::ServerUnaryReactor *SetColorOnDay(::grpc::CallbackServerContext *ctx, const pb::SetColorReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *SetDay(::grpc::CallbackServerContext *ctx, const pb::CompleteDay *req, pb::Status *reply) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Update>* SubscribeToUpdates(::grpc::CallbackServerContext* context, const ::nextapp::pb::UpdatesReq* request) override;
        ::grpc::ServerUnaryReactor *CreateTenant(::grpc::CallbackServerContext *ctx, const pb::CreateTenantReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateDevice(::grpc::CallbackServerContext *ctx, const pb::CreateDeviceReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateNode(::grpc::CallbackServerContext *ctx, const pb::CreateNodeReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateNode(::grpc::CallbackServerContext *ctx, const pb::Node*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *MoveNode(::grpc::CallbackServerContext *ctx, const pb::MoveNodeReq*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteNode(::grpc::CallbackServerContext *ctx, const pb::DeleteNodeReq*req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetNodes(::grpc::CallbackServerContext *ctx, const pb::GetNodesReq *req, pb::NodeTree *reply) override;
        ::grpc::ServerUnaryReactor *GetActions(::grpc::CallbackServerContext *ctx, const pb::GetActionsReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetAction(::grpc::CallbackServerContext *ctx, const pb::GetActionReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateAction(::grpc::CallbackServerContext *ctx, const pb::Action *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateActions(::grpc::CallbackServerContext *ctx, const pb::UpdateActionsReq *req, pb::Status *reply) override;
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
        ::grpc::ServerUnaryReactor *MoveAction(::grpc::CallbackServerContext *ctx, const pb::MoveActionReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateTimeblock(::grpc::CallbackServerContext *ctx, const pb::TimeBlock *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateTimeblock(::grpc::CallbackServerContext *ctx, const pb::TimeBlock *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteTimeblock(::grpc::CallbackServerContext *ctx, const pb::DeleteTimeblockReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetCalendarEvents(::grpc::CallbackServerContext *ctx, const pb::TimeSpan *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *CreateActionCategory(::grpc::CallbackServerContext *ctx, const pb::ActionCategory *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *UpdateActionCategory(::grpc::CallbackServerContext *ctx, const pb::ActionCategory *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *DeleteActionCategory(::grpc::CallbackServerContext *ctx, const pb::DeleteActionCategoryReq *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetActionCategories(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) override;
        ::grpc::ServerUnaryReactor *GetOtpForNewDevice(::grpc::CallbackServerContext *ctx, const pb::OtpRequest *req, pb::Status *reply) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewDays(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerUnaryReactor *GetNewDayColorDefinitions(::grpc::CallbackServerContext *, const pb::GetNewReq *, pb::Status *) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewNodes(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewActions(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerUnaryReactor *GetDataVersions(::grpc::CallbackServerContext *, const pb::Empty *, pb::Status *) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewWork(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewTimeBlocks(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerUnaryReactor *GetDevices(::grpc::CallbackServerContext *, const pb::Empty *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *UpdateDevice(::grpc::CallbackServerContext *, const pb::DeviceUpdateReq *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *DeleteDevice(::grpc::CallbackServerContext *, const common::Uuid *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *ResetPlayback(::grpc::CallbackServerContext *, const pb::ResetPlaybackReq *, pb::Status *) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* ListTenants(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::ListTenantsReq *req) override;
        ::grpc::ServerUnaryReactor *ListCurrentSessions(::grpc::CallbackServerContext *, const pb::Empty *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *SendNotification(::grpc::CallbackServerContext *, const pb::Notification *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *DeleteNotification(::grpc::CallbackServerContext *, const pb::DeleteNotificationReq *, pb::Status *) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* GetNewNotifications(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::GetNewReq *req) override;
        ::grpc::ServerUnaryReactor *GetLastReadNotification(::grpc::CallbackServerContext *, const pb::Empty *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *SetLastReadNotification(::grpc::CallbackServerContext *, const pb::SetReadNotificationReq *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *CreateNodesFromTemplate(::grpc::CallbackServerContext *, const pb::NodeTemplate *, pb::Status *) override;
        ::grpc::ServerUnaryReactor *DeleteAccount(::grpc::CallbackServerContext *ctx, const pb::Empty *req, pb::Status *reply) override;
        ::grpc::ServerWriteReactor<::nextapp::pb::Status>* ExportData(::grpc::CallbackServerContext* ctx, const ::nextapp::pb::ExportDataReq *req) override;
        ::grpc::ServerReadReactor< ::nextapp::pb::ImportDataMsg>* ImportData(::grpc::CallbackServerContext* ctx, ::nextapp::pb::Status* reply) override;


    private:
        // Boilerplate code to run async SQL queries or other async coroutines from an unary gRPC callback
        template <typename ReqT, typename ReplyT, typename FnT>
        ::grpc::ServerUnaryReactor*
        unaryHandler(::grpc::CallbackServerContext *ctx, const ReqT * req, ReplyT *reply, FnT fn, std::string_view name = {},
                     bool allowNewSession = false,
                     bool restrictedToAdmin = false) noexcept {
            assert(ctx);
            assert(reply);

            auto* reactor = ctx->DefaultReactor();

            boost::asio::co_spawn(owner_.server().ctx(),
                                  [this, ctx, req, reply, reactor, allowNewSession, fn=std::move(fn), name, restrictedToAdmin]
                                  () mutable -> boost::asio::awaitable<void> {
                    std::optional<RequestCtx> rctx;
                    try {
                        // Start measuring latency for this request
                        const auto latency = owner_.server().metrics().grpc_request_latency().scoped();

                        rctx.emplace(co_await owner_.sessionManager().getSession(ctx, allowNewSession));
                        LOG_TRACE_EX(*rctx) << "Request [" << name << "] " << req->GetDescriptor()->name() << ": " << owner_.toJsonForLog(*req);
                        rctx->session().touch();

                        if (restrictedToAdmin) {
                            if (!rctx->uctx->isAdmin()) {
                                LOG_WARN << "Request [" << name << "] Restricted to admin. User "
                                         << rctx->uctx->userUuid() <<  " is not an admin.";
                                if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                                    reply->Clear();
                                    reply->set_error(nextapp::pb::Error::PERMISSION_DENIED);
                                    reply->set_message("Permission denied.");
                                    reactor->Finish(::grpc::Status::OK);
                                    co_return;
                                } else {
                                    assert(false && "admin requests must use pb::Status reply type!");
                                    reactor->Finish(::grpc::Status::CANCELLED);
                                    co_return;
                                }
                            }
                        }

                        // We only provide reply-protection for standard rpc calls returning a Status object.
                        if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                            if (co_await owner_.isReplay(ctx, *rctx)) {
                                LOG_DEBUG << "Request [" << name << "] is a replay.";
                                reply->Clear();
                                reply->set_error(nextapp::pb::Error::REPLAY_DETECTED);
                                reply->set_message("Replay. Ignoring request.");
                                reactor->Finish(::grpc::Status::OK);
                                co_return;
                            }
                        }

                        if constexpr (UnaryFnWithoutContext<FnT, ReplyT>) {
                            co_await fn(reply);
                        } else if constexpr (UnaryFnWithContext<FnT, ReplyT>) {
                            rctx->dbh.emplace(co_await owner_.server().db().getConnection(rctx->uctx->dbOptions()));
                            co_await fn(reply, *rctx);
                            owner_.publishUpdates(*rctx);
                        } else if constexpr (!UnaryFnWithoutContext<FnT, ReplyT *> && !UnaryFnWithContext<FnT, ReplyT *>) {
                            static_assert(false, "Invalid unary handler function");
                        }
                        LOG_TRACE << "Replying [" << name << "]: " << owner_.toJsonForLog(*reply);
                        reactor->Finish(::grpc::Status::OK);
                    } catch (const server_err& ex) {
                        if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                            LOG_DEBUG << "Request [" << name << "] Caught server_err exception while handling grpc request: " << ex.what();
                            if (rctx) {
                                owner_.publishUpdates(*rctx, true);
                            }
                            reply->Clear();
                            reply->set_error(ex.error());
                            reply->set_message(ex.what());
                            reactor->Finish(::grpc::Status::OK);
                        } else {
                            LOG_WARN << "Request [" << name << "] Caught server_err exception while handling grpc request coro: " << ex.what();
                            if (rctx) {
                                owner_.publishUpdates(*rctx, true);
                            }
                            reactor->Finish(::grpc::Status::CANCELLED);
                        }
                    } catch (const std::exception& ex) {
                        if constexpr (std::is_same_v<pb::Status *, decltype(reply)>) {
                            LOG_DEBUG << "Request [" << name << "] Caught exception while handling grpc request: " << ex.what();
                            if (rctx) {
                                owner_.publishUpdates(*rctx, true);
                            }
                            reply->Clear();
                            reply->set_error(nextapp::pb::Error::GENERIC_ERROR);
                            reply->set_message(ex.what());
                            reactor->Finish(::grpc::Status::OK);
                        } else {
                            LOG_WARN << "Request [" << name << "] Caught exception while handling grpc request coro: " << ex.what();
                            if (rctx) {
                                owner_.publishUpdates(*rctx, true);
                            }
                            reactor->Finish(::grpc::Status::CANCELLED);
                        }
                    }

                    LOG_TRACE << "Request [" << name << "] Exiting unary handler.";

                }, boost::asio::detached);

            return reactor;
        }

        template <typename ReqT, typename FnT, typename ReplyT=::nextapp::pb::Status>
        ::grpc::ServerWriteReactor<ReplyT> *
        writeStreamHandler(::grpc::CallbackServerContext *ctx, const ReqT * req, FnT &&fn, std::string_view name = {}) noexcept {
            assert(ctx);
            assert(req);

            auto stream = make_write_dstream<ReplyT>(ctx, owner_.server().ctx(), owner_);
            stream->start();

            boost::asio::co_spawn(owner_.server().ctx(),
              [this, ctx, req, stream, fn=std::move(fn), name]
              () mutable -> boost::asio::awaitable<void> {
                  nextapp::pb::Status err_reply;
                  ::grpc::Status finish_status;
                  std::optional<RequestCtx> rctx;
                  try {
                      LOG_TRACE << "Request [" << name << "] " << req->GetDescriptor()->name() << ": " << owner_.toJsonForLog(*req);

                      rctx.emplace(co_await owner_.sessionManager().getSession(ctx));
                      rctx->session().touch();
                      rctx->dbh.emplace(co_await owner_.server().db().getConnection(rctx->uctx->dbOptions()));
                      co_await fn(stream, *rctx);
                      owner_.publishUpdates(*rctx);
                      LOG_TRACE << "Finished reply stream [" << name << "]: ";
                      goto done;
                  } catch (const server_err& ex) {
                      LOG_DEBUG << "Request [" << name << "] Caught server_err exception while handling grpc request: " << ex.what();
                      if (rctx) {
                          owner_.publishUpdates(*rctx, true);
                      }
                      err_reply.set_error(ex.error());
                      err_reply.set_message(ex.what());
                      finish_status = {::grpc::Status::CANCELLED};
                  } catch (const std::exception& ex) {
                      LOG_WARN << "Request [" << name << "] Caught exception while handling grpc request coro: " << ex.what();

                      err_reply.set_error(nextapp::pb::Error::GENERIC_ERROR);
                      err_reply.set_message(ex.what());
                      finish_status = {::grpc::Status::CANCELLED};
                  }

                  assert(err_reply.error() != nextapp::pb::Error::OK);
                  co_await stream->sendMessage(std::move(err_reply), boost::asio::use_awaitable);

              done:
                  // Ignored if the stream is already closed.
                  stream->close(finish_status);

                  LOG_TRACE << "Request [" << name << "] Exiting unary handler.";

              }, boost::asio::detached);

            return stream.get();
        }

        template <typename ReqT, typename FnT, typename ReplyT=::nextapp::pb::Status>
        ::grpc::ServerReadReactor<ReqT> *
        readStreamHandler(::grpc::CallbackServerContext *ctx, ReplyT* reply, FnT &&fn, std::string_view name = {}) noexcept {
            assert(ctx);
            assert(reply);

            auto stream = make_client_streamer<ReqT>(ctx, owner_.server().ctx(), owner_);
            stream->start();
            boost::asio::co_spawn(owner_.server().ctx(),
              [this, reply, ctx, stream, fn=std::move(fn), name]
              () mutable -> boost::asio::awaitable<void> {
                  ::grpc::Status finish_status;
                  std::optional<RequestCtx> rctx;
                  try {
                      LOG_TRACE << "Request [" << name << "]";

                      rctx.emplace(co_await owner_.sessionManager().getSession(ctx));
                      rctx->session().touch();
                      rctx->dbh.emplace(co_await owner_.server().db().getConnection(rctx->uctx->dbOptions()));
                      co_await fn(stream, *rctx);
                      co_await stream->finish();
                      owner_.publishUpdates(*rctx);
                      LOG_TRACE << "Finished client stream [" << name << "]: ";
                      goto done;
                  } catch (const server_err& ex) {
                      LOG_DEBUG << "Request [" << name << "] Caught server_err exception while handling grpc request: " << ex.what();
                      if (rctx) {
                          owner_.publishUpdates(*rctx, true);
                      }
                      reply->Clear();
                      reply->set_error(ex.error());
                      reply->set_message(ex.what());
                      finish_status = {::grpc::Status::OK};
                  } catch (const std::exception& ex) {
                      LOG_WARN << "Request [" << name << "] Caught exception while handling grpc request coro: " << ex.what();
                      if (rctx) {
                          owner_.publishUpdates(*rctx, true);
                      }
                      reply->Clear();
                      reply->set_error(nextapp::pb::Error::GENERIC_ERROR);
                      reply->set_message(ex.what());
                      finish_status = {::grpc::Status::OK};
                  }

                  assert(reply->error() != nextapp::pb::Error::OK);
              done:
                  // Ignored if the stream is already closed.
                  LOG_TRACE << "Request [" << name << "] Exiting client-stream handler.";

              }, boost::asio::detached);

            return stream.get();
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

    boost::asio::awaitable<void> validateNode(const std::string& parentUuid, const std::string& userUuid);
    boost::asio::awaitable<void> validateNode(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string& parentUuid, const std::string& userUuid);
    boost::asio::awaitable<void> validateAction(const std::string &actionId, const std::string &userUuid, std::string *name = {});
    boost::asio::awaitable<void> validateAction(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string &actionId, const std::string &userUuid, std::string *name = {});
    boost::asio::awaitable<void> validateTimeBlock(jgaa::mysqlpool::Mysqlpool::Handle& handle, const std::string &timeBlockId, const std::string &userUuid);
    boost::asio::awaitable<nextapp::pb::Node> fetcNode(const std::string& uuid, const std::string& userUuid, RequestCtx& rctx);
    boost::asio::awaitable<pb::WorkSession> fetchWorkSession(const std::string& uuid, RequestCtx& rctx);
    boost::asio::awaitable<void> saveWorkSession(nextapp::pb::WorkSession& work, RequestCtx& rctx, bool touch = true);
    boost::asio::awaitable<boost::mysql::results> insertWork(const pb::WorkSession& work, RequestCtx& rctx, bool addStartEvent = true);
    boost::asio::awaitable<void> getAction(nextapp::pb::Action& action, const std::string& uuid, RequestCtx& rctx);

    boost::asio::awaitable<void> deleteWorkSession(const std::string& uuid, RequestCtx& rctx);
    boost::asio::awaitable<void> deleteNode(const std::string& uuid, RequestCtx& rctx);
    boost::asio::awaitable<void> deleteActionInTimeBlocks(const std::string& uuid, RequestCtx& rctx);
    boost::asio::awaitable<void> deleteAction(const std::string& uuid, RequestCtx& rctx);

    bool active() const noexcept {
        return active_;
    }

    //const std::shared_ptr<UserContext> userContext(::grpc::CallbackServerContext *ctx) const;
    //boost::asio::awaitable<std::shared_ptr<UserContext>> userContext(::grpc::CallbackServerContext *ctx) const;

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
    boost::asio::awaitable<void> resumeWorkSession(pb::WorkSession &work, RequestCtx& rctx, bool makeUpdate = true);
    boost::asio::awaitable<std::optional<pb::WorkSession> > fetchActiveWorkSession(RequestCtx& rctx);
    boost::asio::awaitable<void> endWorkSessionForAction(const std::string_view& actionId, RequestCtx& rctx);
    boost::asio::awaitable<std::optional<pb::CompleteDay>> fetchDay(const nextapp::pb::Date& date, RequestCtx& rctx);
    boost::asio::awaitable<void> updateTimeSpentInAction(const std::string& uuid, RequestCtx& rctx);

    // Fetch actions that start and are due on given day, and is of due_kind 'datetime'.
    // The matching actions are inserted in events.
    boost::asio::awaitable<void> fetchActionsForCalendar(pb::CalendarEvents& events, RequestCtx& rctx, const time_t& day);
    boost::asio::awaitable<void> getGlobalSettings(pb::UserGlobalSettings& settings, RequestCtx& rctx);
    using notificatation_req_t = std::variant<uint32_t, boost::uuids::uuid>;

    // Internal method to get a notification from the db.
    // NB: Does not enforce access control!
    boost::asio::awaitable<nextapp::pb::Notification> getNotification(RequestCtx& rctx, notificatation_req_t req);

    void handleSession(::grpc::CallbackServerContext *ctx);

    SessionManager& sessionManager() {
        return sessionManager_;
    }

    std::shared_ptr<UserContext::Session> session(::grpc::ServerContextBase& ctx);
    static uint getInstanceId(::grpc::CallbackServerContext *ctx);

    uint64_t getLastNotificationUpdated() const noexcept {
        return last_notification_udated_.load(std::memory_order_relaxed);
    }

    void setLastNotificationUpdated(uint64_t lastNotificationUpdated) noexcept;

    using export_flush_fn_t = std::function<boost::asio::awaitable<void>(pb::Status& req)>;
    boost::asio::awaitable<uint64_t> exportActions(
        const uint64_t since,
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const export_flush_fn_t& flush_fn,
        RequestCtx& rctx,
        bool removeDeleted = false);

    boost::asio::awaitable<uint64_t> exportDays(
        const uint64_t since,
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const export_flush_fn_t& flush_fn,
        RequestCtx& rctx);

    boost::asio::awaitable<uint64_t> exportNodes(
        const uint64_t since,
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const export_flush_fn_t& flush_fn,
        RequestCtx& rctx,
        bool removeDeleted = false);

    boost::asio::awaitable<uint64_t> exportWork(
        const uint64_t since,
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const export_flush_fn_t& flush_fn,
        RequestCtx& rctx,
        bool removeDeleted = false);

    boost::asio::awaitable<uint64_t> exportTimeBlocks(
        const uint64_t since,
        jgaa::mysqlpool::Mysqlpool::Handle& dbh,
        const export_flush_fn_t& flush_fn,
        RequestCtx& rctx,
        bool removeDeleted = false);

    boost::asio::awaitable<pb::User> getUser(jgaa::mysqlpool::Mysqlpool::Handle& dbh, std::string_view uuid);
    boost::asio::awaitable<pb::DayColorDefinitions> getDayColorDefinitions(jgaa::mysqlpool::Mysqlpool::Handle& dbh,
                                                            const std::string& tenantUuid /* unused */);
    boost::asio::awaitable<pb::ActionCategories> getActionCategories(jgaa::mysqlpool::Mysqlpool::Handle& dbh, std::string_view userId);

    // returns true of the settings was added, false if they were updated
    boost::asio::awaitable<bool> saveUserGlobalSettings(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::UserGlobalSettings& settings, RequestCtx& rctx);
    boost::asio::awaitable<void> saveActionCategories(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::ActionCategories& categories, RequestCtx& rctx);
    boost::asio::awaitable<void> saveDays(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::ListOfDays& days, RequestCtx& rctx);
    boost::asio::awaitable<void> saveNodes(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::Nodes& nodes, RequestCtx& rctx);
    boost::asio::awaitable<void> saveActions(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::CompleteActions& actions, RequestCtx& rctx);
    boost::asio::awaitable<void> saveWorkSessions(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::WorkSessions& sessions, RequestCtx& rctx);
    boost::asio::awaitable<void> saveTimeBlocks(jgaa::mysqlpool::Mysqlpool::Handle& dbh, const pb::TimeBlocks& timeblocks, RequestCtx& rctx);

private:

    void publishUpdates(RequestCtx& rctx, bool failed = false) {
        try {
            LOG_TRACE_EX(rctx) << "Publishing updates.";
            for (auto& update : rctx.updates) {
                if (failed && !update.send_on_fail) {
                    LOG_TRACE_EX(rctx) << "Skipping update on failure: " << update.update->DebugString();
                    continue;
                }
                rctx.uctx->publish(update.update);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR_EX(rctx) << "Failed to publish updates: " << ex.what();
        }
    }

    // The Server instance where we get objects in the application, like config and database
    Server& server_;

    // Thread-safe method to get a unique client-id for a new RPC.
    static size_t getNewClientId() {
        static std::atomic_size_t id{0};
        return ++id;
    }

    boost::asio::awaitable<void> loadCert();
    boost::asio::awaitable<bool> isReplay(::grpc::CallbackServerContext *ctx, RequestCtx& rctx);
    boost::asio::awaitable<void> initNotifications();
    boost::asio::awaitable<void> addNodes(const std::string& parent_id, const pb::NodeTemplate& t, RequestCtx& rctx);

    SessionManager sessionManager_;

    // An instance of our service, compiled from code generated by protoc
    std::unique_ptr<NextappImpl> service_;

    // A gRPC server object
    std::unique_ptr<::grpc::Server> grpc_server_;

    std::map<boost::uuids::uuid, std::weak_ptr<Publisher>> publishers_;
    mutable std::mutex mutex_;
    std::atomic_bool active_{false};
    std::atomic_uint64_t last_notification_udated_{0};
    CertData cert_;
};

} // ns


std::ostream& operator << (std::ostream& out, const nextapp::grpc::RequestCtx& ctx);

