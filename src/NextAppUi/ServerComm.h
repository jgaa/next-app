#pragma once

#include <queue>
#include <qqmlregistration.h>
#include <memory>

#include <boost/type_index.hpp>

#include "nextapp_client.grpc.qpb.h"
#include "signup_client.grpc.qpb.h"

#include <QObject>
#include <QUuid>
#include <QTimer>
#include <QFuture>
#include <QNetworkInformation>
#include <QProtobufSerializer>
#include <QSettings>

#include "qcorotask.h"
#include "qcorofuture.h"


#include "logging.h"
#include "util.h"
#include "GrpcIncomingStream.h"

template <typename T, typename... Y>
concept IsValidFunctor = std::invocable<T&, Y...>;

// template <typename T>
// concept ProtoMessage = std::is_base_of_v<QProtobufMessage, T>;

namespace nextapp {
template <typename T, ProtoMessage M>
auto append(const T& list, M event) {
    auto tmp = list;
    tmp.append(std::move(event));
    return tmp;
}

template <typename T>
auto append(const nextapp::pb::StringList& sl, T&& value) {
    auto list = sl.list();
    list.append(std::forward<T>(value));
    nextapp::pb::StringList new_sl;
    new_sl.setList(std::move(list));
    return new_sl;
}

template <typename T>
auto remove(const nextapp::pb::StringList& sl, T index) {
    auto list = sl.list();
    list.removeAt(index);
    nextapp::pb::StringList new_sl;
    new_sl.setList(std::move(list));
    return new_sl;
}


} // ns

class ServerComm : public QObject
{
public:
    enum Status {
        MANUAL_OFFLINE,
        OFFLINE,
        READY_TO_CONNECT,
        CONNECTING,
        INITIAL_SYNC,
        ONLINE,
        ERROR
    };

    enum SignupStatus {
        SIGNUP_NOT_STARTED,
        SIGNUP_HAVE_INFO,
        SIGNUP_SIGNING_UP,
        SIGNUP_SUCCESS,
        SIGNUP_OK,
        SIGNUP_ERROR
    };

    enum class ReconnectLevel {
        ONLINE,
        SITE,
        LAN,
        NEVER
    };

private:
    Q_OBJECT
    Q_ENUM(Status)
    Q_ENUM(SignupStatus)
    QML_ELEMENT

    Q_PROPERTY(QString version
                   READ version
                   NOTIFY versionChanged)

    Q_PROPERTY(QString defaultServerAddress READ getDefaultServerAddress CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText MEMBER status_text_ NOTIFY statusChanged)
    Q_PROPERTY(QString statusColor MEMBER status_color_ NOTIFY statusChanged)
    Q_PROPERTY(QString version READ version NOTIFY versionChanged)
    Q_PROPERTY(nextapp::pb::UserGlobalSettings globalSettings
                READ getGlobalSettings
                NOTIFY globalSettingsChanged)
    Q_PROPERTY(signup::pb::GetInfoResponse signupInfo READ getSignupInfo NOTIFY signupInfoChanged)
    Q_PROPERTY(SignupStatus signupStatus MEMBER signup_status_ NOTIFY signupStatusChanged)
    Q_PROPERTY(QString messages MEMBER messages_ NOTIFY messagesChanged)

    struct QueuedRequest {
        enum class Type {
            INVALID,
            ADD_ACTION,
            UPDATE_ACTION,
            DELETE_ACTION,
            MARK_ACTION_AS_DONE,
            MARK_ACTION_AS_FAVORITE,
            MOVE_ACTION,
            //CREATE_ACTION_CATEGORY,
            ADD_NODE,
            UPDATE_NODE,
            MOVE_NODE,
            DELETE_NODE,
            ADD_TIME_BLOCK,
            UPDATE_TIME_BLOCK,
            DELETE_TIME_BLOCK,
            UPDATE_ACTIONS,
        };

        quint32 id{0};
        QUuid uuid = QUuid::createUuid();
        Type type{Type::INVALID};
        QDateTime time;
        QByteArray data;
    };

public:
    struct CbError {
        nextapp::pb::ErrorGadget::Error error;
        QString message;
    };

    template <typename T>
    using callback_arg_t = std::variant<CbError,T>;

    template <typename T>
    using callback_t = std::function<void(callback_arg_t<T>)>;

    using colors_in_months_t = std::shared_ptr<QList<QUuid>>;

    struct MetaData {
        std::optional<bool> more;
        QUuid requester;
    };

    explicit ServerComm();
    ~ServerComm();

    void start();
    void stop();
    void close();

    [[nodiscard]] QString version();
    [[nodiscard]] bool connected() const noexcept;

    Q_INVOKABLE void toggleConnect();

    // Called when the servers app settings may have changed
    Q_INVOKABLE void reloadSettings();
    Q_INVOKABLE void saveGlobalSettings(const nextapp::pb::UserGlobalSettings &settings);
    Q_INVOKABLE nextapp::pb::UserGlobalSettings getGlobalSettings() const;
    signup::pb::GetInfoResponse getSignupInfo() const;
    Q_INVOKABLE void setSignupServerAddress(const QString &address);
    Q_INVOKABLE void signup(const QString &name, const QString &email,
                            const QString &company, const QString& deviceName,
                            int region);
    Q_INVOKABLE void addDeviceWithOtp(const QString &otp, const QString &email, const QString &deviceName);
    Q_INVOKABLE void signupDone();
    Q_INVOKABLE QString deviceId() const {
        return device_uuid_.toString(QUuid::WithoutBraces);
    }
    Q_INVOKABLE QStringList getRegionsForSignup() const;

    static ServerComm& instance() noexcept {
        assert(instance_);
        return *instance_;
    }

    static const nextapp::pb::ServerInfo& getServerInfo() noexcept {
        return instance().server_info_;
    }

    const nextapp::pb::UserGlobalSettings& globalSettings() const noexcept {
        return userGlobalSettings_;
    }

    void getColorsInMonth(unsigned year, unsigned month);

    void setDayColor(int year, int month, int day, QUuid colorUuid);
    void setDay(const nextapp::pb::CompleteDay& day);
    void addNode(const nextapp::pb::Node& node);

    // node.parent cannot be changed. Only data-values can be updated.
    void updateNode(const nextapp::pb::Node& node);

    // Move node to another parent
    void moveNode(const QUuid &uuid, const QUuid &toParentUuid);

    void deleteNode(const QUuid& uuid);

    void getDayColorDefinitions();

    void fetchDay(int year, int month, int day);

    //void getActions(nextapp::pb::GetActionsReq &filter);
    //void getAction(nextapp::pb::GetActionReq &req);
    void addAction(const nextapp::pb::Action& action);
    void updateAction(const nextapp::pb::Action& action);
    void updateActions(const nextapp::pb::UpdateActionsReq& action);
    void deleteAction(const QString& actionUuid);
    void markActionAsDone(const QString& actionUuid, bool done);
    void markActionAsFavorite(const QString& actionUuid, bool favorite);
    void getActiveWorkSessions();
    void startWork(const QString& actionId, bool activate = false);
    void addWorkFromTimeBlock(const QString& timeBlockUuid);
    void pauseWork(const QString& sessionId);
    void resumeWork(const QString& sessionId);
    void doneWork(const QString& sessionId);
    void touchWork(const QString& sessionId);
    void sendWorkEvent(const QString& sessionId, const nextapp::pb::WorkEvent& event);
    void sendWorkEvents(const nextapp::pb::AddWorkEventReq& reqt);
    void deleteWork(const QString& sessionId);
    void getWorkSessions(const nextapp::pb::GetWorkSessionsReq& req, const QUuid& requester);
    void getDetailedWorkSummary(const nextapp::pb::DetailedWorkSummaryRequest& req, const QUuid& requester);
    void addWork(const nextapp::pb::WorkSession& ws);
    void moveAction(const QString& actionUuid, const QString& nodeUuid);
    void addTimeBlock(const nextapp::pb::TimeBlock& tb);
    void updateTimeBlock(const nextapp::pb::TimeBlock& tb);
    void deleteTimeBlock(const QString& timeBlockUuid);
    void fetchCalendarEvents(QDate start, QDate end, callback_t<nextapp::pb::CalendarEvents>&& done);
    void fetchActionCategories(callback_t<nextapp::pb::ActionCategories>&& done);
    void createActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done);
    void updateActionCategory(const nextapp::pb::ActionCategory& category, callback_t<nextapp::pb::Status>&& done);
    void deleteActionCategory(const QString& id, callback_t<nextapp::pb::Status>&& done);
    void requestOtp(callback_t<nextapp::pb::Status>&& done);

    std::shared_ptr<GrpcIncomingStream> synchGreenDays(const nextapp::pb::GetNewReq& req);
    QCoro::Task<nextapp::pb::Status> getNewDayColorDefinitions(const nextapp::pb::GetNewReq& req);
    std::shared_ptr<GrpcIncomingStream> synchNodes(const nextapp::pb::GetNewReq& req);
    std::shared_ptr<GrpcIncomingStream> synchActions(const nextapp::pb::GetNewReq& req);
    QCoro::Task<nextapp::pb::Status> getActionCategories(const nextapp::pb::Empty& req);
    std::shared_ptr<GrpcIncomingStream> synchWorkSessions(const nextapp::pb::GetNewReq& req);
    std::shared_ptr<GrpcIncomingStream> synchTimeBlocks(const nextapp::pb::GetNewReq& req);
    std::shared_ptr<GrpcIncomingStream> synchNotifications(const nextapp::pb::GetNewReq& req);

    QCoro::Task<nextapp::pb::Status> fetchDevices();
    QCoro::Task<nextapp::pb::Status> enableDevice(const QString &deviceId, bool enabled);
    QCoro::Task<nextapp::pb::Status> deleteDevice(const QString &deviceId);

    static QString getDefaultServerAddress() {
        return SERVER_ADDRESS;
    }

    Status status() const noexcept {
        return status_;
    }

    void setStatus(Status status);

    const QUuid& deviceUuid() const;

    const auto& getServerDataVersions() const noexcept {
        return server_data_versions_;
    }

    const auto& getLocalDataVersions() const noexcept {
        return server_data_versions_;
    }

    static bool isTemporaryError(nextapp::pb::ErrorGadget::Error error);

signals:
    void versionChanged();
    void connectedChanged();
    void errorRecieved(const QString &value);
    void globalSettingsChanged();
    void firstDayOfWeekChanged();
    void statusChanged();
    void signupInfoChanged();

    // When we get the full node-list
    void receivedNodeTree(const nextapp::pb::NodeTree& tree);

    void receivedMonth(const nextapp::pb::Month& month);
    void receivedDay(const nextapp::pb::CompleteDay& day);
    void receivedDayColorDefinitions(const nextapp::pb::DayColorDefinitions& defs);
    void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions, bool more, bool first);
    void receivedAction(const nextapp::pb::Status& status);
    void receivedCurrentWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);
    void receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions, const MetaData meta);
    void receivedDetailedWorkSummary(const nextapp::pb::DetailedWorkSummary& summary, const MetaData& meta);

    // Triggered on all updates from the server
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void signupStatusChanged();
    void messagesChanged();

private:
    void onReachabilityChanged(QNetworkInformation::Reachability reachability);
    void errorOccurred(const QGrpcStatus &status);
    void onServerInfo(nextapp::pb::ServerInfo info);
    void initGlobalSettings();
    void onGrpcReady();
    void onUpdateMessage();
    void setDefaulValuesInUserSettings();
    void scheduleReconnect();
    void connectToSignupServer();
    QCoro::Task<void> signupOrAdd(QString name,
                     QString email,
                     QString company,
                     QString deviceName,
                     QString otp,
                     int region);

    void clearMessages();
    void addMessage(const QString &msg);
    void setMessage(const QString &msg);
    QString lastSeenUpdateIdKey() const;
    QString lastSeenServerInstance() const;

    struct GrpcCallOptions {

        GrpcCallOptions(const GrpcCallOptions&) = default;
        GrpcCallOptions(GrpcCallOptions&&) = default;
        GrpcCallOptions& operator=(const GrpcCallOptions&) = default;
        GrpcCallOptions& operator=(GrpcCallOptions&&) = default;

        GrpcCallOptions(bool enable_queue = false,
                        bool ignore_errors = false,
                        bool ignore_offline = false,
                        const QGrpcCallOptions& qopts = {})
            : enable_queue(enable_queue), ignore_errors(ignore_errors), ignore_offline(ignore_offline), qopts(qopts) {}

        bool enable_queue;
        bool ignore_errors;
        bool ignore_offline;
        QGrpcCallOptions qopts;
    };

    template <typename respT, typename callT, typename doneT, typename ...Args>
    void callRpc_(callT&& call, doneT && done, const GrpcCallOptions& opts, Args... args) {

        auto exec = [this, call=std::move(call), done=std::move(done), opts, args...]() {
            if (!opts.ignore_offline && (status_ <= Status::OFFLINE || status_ == Status::ERROR)) {
                LOG_ERROR << "ServerComm::callRpc_ Called when status is " << status_;
                return;
            }

            auto rpc_method = call(args...);
            //rpc_method->subscribe(this, [this, rpc_method, done=std::move(done), opts=std::move(opts)] () {
                //respT rval = rpc_method-> template read<respT>();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            auto ptr = rpc_method.get();
            connect(ptr, &QGrpcCallReply::finished, this, [this, rpc_method=std::move(rpc_method), done=std::move(done), opts=std::move(opts)] (const QGrpcStatus& status) {
                if (!status.isOk()) [[unlikely]] {
                    LOG_ERROR <<  "RPC failed: " << toString(status);
                    errorOccurred(status);
                    if constexpr (IsValidFunctor<doneT, CbError>) {
                        done(CbError{nextapp::pb::ErrorGadget::Error::GENERIC_ERROR, status.message()});
                        // setStatus(Status::ERROR);
                        // stop();
                        // scheduleReconnect();
                        return;
                    }
                }
                if (auto orval = rpc_method-> template read<respT>()) {
                    auto& rval = *orval;
#else

            rpc_method->subscribe(this, [this, rpc_method, done=std::move(done), opts=std::move(opts)] () {
                    respT rval = rpc_method-> template read<respT>();
#endif
                    if constexpr (std::is_same_v<nextapp::pb::Status, respT>) {
                        if (!opts.ignore_errors && rval.error() != nextapp::pb::ErrorGadget::Error::OK) {
                            LOG_ERROR << "RPC request failed with error #" <<
                                rval.error() << " : " << rval.message();

                            if constexpr (IsValidFunctor<doneT, CbError>) {
                                done(CbError{rval.error(), rval.message()});
                            }
                            return;
                        }
                    }
                    if constexpr (IsValidFunctor<doneT, respT>) {
                        done(rval);
                    } else {
                        // The done functor must either be valid callable functor, or 'false'
                        static_assert(std::is_same_v<doneT, bool>);
                        assert(!done);
                    }
                }
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
            ,[this](QGrpcStatus status) {
                LOG_ERROR << "callRpc_ Comm error code=" << static_cast<unsigned>(status.code()) << ": " << status.message();
            }
#else
        }
#endif
            );
        };

        exec();
    }

    template <typename respT, typename callT, typename doneT, typename ...Args>
        requires IsValidFunctor<doneT, respT>
    void callRpc(callT&& call, doneT && done, const GrpcCallOptions& opts, Args... args) {
        callRpc_<respT>(std::move(call), std::move(done), opts, args...);
    }

    template <typename respT, typename callT, typename doneT, typename ...Args>
        requires IsValidFunctor<doneT, respT>
    void callRpc(callT&& call, doneT && done, Args... args) {
        const GrpcCallOptions opts;
        callRpc_<respT>(std::move(call), std::move(done), opts, args...);
    }

    template <typename respT, typename callT,typename ...Args>
    void callRpc(callT&& call, const GrpcCallOptions& opts, Args... arg) {
        callRpc_<respT>(std::move(call), false, opts, arg...);
    }

    template <typename respT, typename callT,typename ...Args>
    void callRpc(callT&& call, Args... arg) {
        const GrpcCallOptions opts;
        callRpc_<respT>(std::move(call), false, opts, arg...);
    }

    template <ProtoMessage reqT, ProtoMessage replyT = nextapp::pb::Status>
    QCoro::Task<replyT> rpc(
        reqT request,
        std::unique_ptr<QGrpcCallReply>(::nextapp::pb::Nextapp::Client::*call)(const reqT& req, const QGrpcCallOptions &options),
        const GrpcCallOptions &options = {}) {

        co_return co_await rpc_<reqT, replyT>(client_, std::forward<reqT>(request), std::forward<decltype(call)>(call), options);
    }

    template <ProtoMessage reqT, ProtoMessage replyT = signup::pb::Reply>
    QCoro::Task<replyT> rpcSignup(
        reqT request,
        std::unique_ptr<QGrpcCallReply>(::signup::pb::SignUp::Client::*call)(const reqT& req, const QGrpcCallOptions &options),
        const GrpcCallOptions &options = {}) {

        co_return co_await rpc_<reqT, replyT, signup::pb::ErrorGadget::Error>(signup_client_, std::forward<reqT>(request), std::forward<decltype(call)>(call), options);
    }

    // template <ProtoMessage reqT, ProtoMessage replyT = nextapp::pb::Status, typename rpcT = ::nextapp::pb::Nextapp::Client>
    // QCoro::Task<replyT> rpc(
    //     reqT request,
    //     std::unique_ptr<QGrpcCallReply>(rpcT::*call)(const reqT& req, const QGrpcCallOptions &options),
    //     const GrpcCallOptions &options = {}) {

    //     if constexpr (std::is_same_v<::nextapp::pb::Nextapp::Client, rpcT>) {
    //         co_return co_await rpc_<reqT, replyT, rpcT>(client_, request, call, options);
    //     } else if constexpr (std::is_same_v<::signup::pb::SignUp::Client, rpcT>) {
    //         co_return co_await rpc_<reqT, replyT, rpcT>(signup_client_, request, call, options);
    //     }

    //     throw std::runtime_error("Invalid rpcT");
    // }

    template <ProtoMessage reqT, ProtoMessage replyT = nextapp::pb::Status, typename ErrorT = nextapp::pb::ErrorGadget::Error, typename rpcT = ::nextapp::pb::Nextapp::Client, typename rpcProxyT>
    QCoro::Task<replyT> rpc_(rpcProxyT& client,
        reqT request,
        std::unique_ptr<QGrpcCallReply>(rpcT::*call)(const reqT& req, const QGrpcCallOptions &options),
        const GrpcCallOptions &options = {}) {

        QPromise<replyT> promise;
        auto future = promise.future();

        auto qopts = options.qopts;

        if (!session_id_.empty()) {
// TODO: Enable when we remove the "sid" from the static metadata in the channel
//       Keep existing options
// #if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
//             qopts.setMetadata({std::make_pair("sid", session_id_.c_str())});
// #else
//             qopts.withMetadata({{"sid", session_id_.c_str()}});
// #endif
        }

        auto handle = (client.get()->*call)(request, options.qopts);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        auto ptr = handle.get();
        if (!ptr) {
            LOG_ERROR_N << "handle returned null. Unable to initiate RPC call.";
            if constexpr (std::is_same_v<nextapp::pb::Status, replyT>) {
                nextapp::pb::Status err;
                err.setError(ErrorT::GENERIC_ERROR);
                err.setMessage("Unable to initiate RPC call");
                co_return err;
            } else {
                abort();
            }
        }
        connect(ptr, &QGrpcCallReply::finished, this, [this, handle=std::move(handle), options, promise=std::move(promise)] (const QGrpcStatus& status) mutable {
            if (!status.isOk()) [[unlikely]] {
                // gRPC level error
                LOG_ERROR <<  "RPC failed: " << toString(status);
                errorOccurred(status);
                replyT rval;
                rval.setError(ErrorT::CLIENT_GRPC_ERROR);
                rval.setMessage(status.message());
                promise.start();
                promise.addResult(std::move(rval));
                promise.finish();
                return;
            }

            if (auto orval = handle-> template read<replyT>()) [[likely]] {
                auto& rval = *orval;
#else
    handle->subscribe(this, [this, options, handle, promise=std::move(promise)] () mutable {
        {
            auto rval = handle-> template read<replyT>();
#endif
                if constexpr (std::is_same_v<nextapp::pb::Status, replyT>) {
                    if (!options.ignore_errors && rval.error() != nextapp::pb::ErrorGadget::Error::OK) {
                        LOG_ERROR << "RPC request failed with error #" <<
                            rval.error() << " : " << rval.message();
                    }
                }

                promise.start();
                promise.addResult(std::move(rval));
                promise.finish();
                return;
            }

            replyT errval;
            errval.setError(ErrorT::CLIENT_GRPC_ERROR);
            errval.setMessage("Failed to read RPC response in client");
            promise.start();
            promise.addResult(std::move(errval));
            promise.finish();

        });

        // TODO: Can we move to using the subscription directly when we drop support for QT 6.7?
        co_return co_await qCoro(future).result();
    }

    template <ProtoMessage reqT, ProtoMessage replyT = nextapp::pb::Status>
    std::shared_ptr<GrpcIncomingStream> rpcOpenReadStream(
        const reqT& request,
        std::unique_ptr<QGrpcServerStream>(::nextapp::pb::Nextapp::Client::*call)(const reqT& req, const QGrpcCallOptions &options),
        const GrpcCallOptions &options = {}) {

        auto svr_stream = (client_.get()->*call)(request, options.qopts);
        return std::make_shared<GrpcIncomingStream>(std::move(svr_stream));
    }

    template <QueuedRequest::Type rq, ProtoMessage reqT>
    QCoro::Task<bool> rpcQueueAndExecute(const reqT& request) {
        QProtobufSerializer serializer;
        QueuedRequest qr;
        qr.type = rq;
        qr.time = QDateTime::currentDateTime();
        qr.data = request.serialize(&serializer);

        bool delete_req = false;

        if (QSettings{}.value("server/resend_requests", true).toBool()) {
            LOG_DEBUG << "Queuing request: " << qr.uuid.toString()
                     << " type: " << static_cast<int>(rq)
                     << " name: " << boost::typeindex::type_id<reqT>().pretty_name();

            if (!co_await save(qr)) {
                LOG_WARN_N << "Failed to save the request";
                co_return false;
            }

            // If there are other queued requests, we must return
            // Requests must be exceuted sequentially, in order
            if (queued_requests_count_ > 1 || !connected()) {
                co_return true;
            }

            co_await retryRequests();
            co_return true;
        } else {
            co_return co_await execute(qr, false);
        }
    }

    QCoro::Task<bool> save(QueuedRequest& qr);
    QCoro::Task<bool> execute(const QueuedRequest& qr, bool deleteRequest);
    QCoro::Task<void> retryRequests();
    QCoro::Task<bool> deleteRequestFromDb(uint id);
    QCoro::Task<void> housekeeping();


    bool shouldReconnect() const noexcept;
    bool canConnect() const noexcept;

    QCoro::Task<void> startNextappSession();
    QCoro::Task<bool> getDataVersions();
    QCoro::Task<bool> getGlobalSetings();
    std::pair<QString, QString> createCsr();
    QCoro::Task<std::optional<std::pair<QString, QString>>> createCsrAsync();
    QString toString(const QGrpcStatus& ex);
    void updateVisualStatus();

    std::unique_ptr<nextapp::pb::Nextapp::Client> client_;
    std::unique_ptr<signup::pb::SignUp::Client> signup_client_;
    nextapp::pb::ServerInfo server_info_;
    QString server_version_{"Unknown"};
    Status status_{Status::OFFLINE};
    QString status_text_;
    QString status_color_;
    static ServerComm *instance_;
    std::shared_ptr<QGrpcServerStream> updates_;
    QString current_server_address_;
    nextapp::pb::UserGlobalSettings userGlobalSettings_;
    std::string session_id_;
    int reconnect_after_seconds_{0};
    QString signup_server_address_;
    signup::pb::GetInfoResponse signup_info_;
    SignupStatus signup_status_{SIGNUP_NOT_STARTED};
    QUuid device_uuid_;
    QString messages_;
    QTimer ping_timer_;
    unsigned ping_timer_interval_sec_{60}; // 60 seconds
    nextapp::pb::DataVersionsInfo server_data_versions_;
    nextapp::pb::DataVersionsInfo local_data_versions_;
    int executing_request_count_{0};
    int queued_requests_count_{0};
    bool retrying_requests_{false};
    uint last_seen_update_id_{0};
    uint64_t last_seen_server_instance_{};
    bool closed_{false};
};
