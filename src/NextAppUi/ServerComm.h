#pragma once

#include <queue>
#include <qqmlregistration.h>

#include <memory>


#include "nextapp_client.grpc.qpb.h"
#include "signup_client.grpc.qpb.h"

#include <QObject>
#include <QUuid>
#include "logging.h"

template <typename T, typename... Y>
concept IsValidFunctor = std::invocable<T&, Y...>;


class ServerComm : public QObject
{
public:
    enum Status {
        OFFLINE,
        CONNECTING,
        INITIAL_SYNC,
        ONLINE,
        ERROR
    };

private:
    Q_OBJECT
    Q_ENUM(Status)
    QML_ELEMENT

    Q_PROPERTY(QString version
                   READ version
                   NOTIFY versionChanged)

    Q_PROPERTY(QString defaultServerAddress READ getDefaultServerAddress CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString version READ version NOTIFY versionChanged)
    Q_PROPERTY(nextapp::pb::UserGlobalSettings globalSettings
                READ getGlobalSettings
                NOTIFY globalSettingsChanged)
    Q_PROPERTY(signup::pb::GetInfoResponse signupInfo READ getSignupInfo NOTIFY signupInfoChanged)

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

    [[nodiscard]] QString version();
    [[nodiscard]] bool connected() const noexcept;

    Q_INVOKABLE void toggleConnect();

    // Called when the servers app settings may have changed
    Q_INVOKABLE void reloadSettings();
    Q_INVOKABLE void saveGlobalSettings(const nextapp::pb::UserGlobalSettings &settings);
    Q_INVOKABLE nextapp::pb::UserGlobalSettings getGlobalSettings() const;
    signup::pb::GetInfoResponse getSignupInfo() const;
    Q_INVOKABLE void setSignupServerAddress(const QString &address);

    static ServerComm& instance() noexcept {
        assert(instance_);
        return *instance_;
    }

    static const nextapp::pb::ServerInfo& getServerInfo() noexcept {
        return instance().server_info_;
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

    void getNodeTree();

    void getDayColorDefinitions();

    void fetchDay(int year, int month, int day);

    void getActions(nextapp::pb::GetActionsReq &filter);
    void getAction(nextapp::pb::GetActionReq &req);
    void addAction(const nextapp::pb::Action& action);
    void updateAction(const nextapp::pb::Action& action);
    void deleteAction(const QString& actionUuid);
    void markActionAsDone(const QString& actionUuid, bool done);
    void markActionAsFavorite(const QString& actionUuid, bool favorite);
    void getActiveWorkSessions();
    void startWork(const QString& actionId);
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

    static QString getDefaultServerAddress() {
        return SERVER_ADDRESS;
    }

    Status status() const noexcept {
        return status_;
    }

    void setStatus(Status status);

signals:
    void versionChanged();
    void connectedChanged();
    void dayColorDefinitionsChanged();
    void errorRecieved(const QString &value);
    void globalSettingsChanged();
    void firstDayOfWeekChanged();
    void statusChanged();
    void signupInfoChanged();

    // When the server has replied to our request fo the colors for this month
    void monthColorsChanged(unsigned year, unsigned month, colors_in_months_t colors);

    // When an update-message is received from the server regarding a change for a color on a day
    void dayColorChanged(unsigned year, unsigned month, unsigned day, QUuid uuid);

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

private:
    void errorOccurred(const QGrpcStatus &status);
    void onServerInfo(nextapp::pb::ServerInfo info);
    void initGlobalSettings();
    void onGrpcReady();
    void onUpdateMessage();
    void setDefaulValuesInUserSettings();
    void scheduleReconnect();
    void connectToSignupServer();

    struct GrpcCallOptions {
        bool enable_queue = true;
        bool ignore_errors = false;
        bool ignore_offline = false;
    };

    template <typename respT, typename callT, typename doneT, typename ...Args>
    void callRpc_(callT&& call, doneT && done, const GrpcCallOptions& opts, Args... args) {

        auto exec = [this, call=std::move(call), done=std::move(done), opts, args...]() {
            if (!opts.ignore_offline && (status_ == Status::OFFLINE || status_ == Status::ERROR)) {
                LOG_ERROR << "ServerComm::callRpc_ Called when status is " << status_;
                return;
            }
            auto rpc_method = call(args...);
            rpc_method->subscribe(this, [this, rpc_method, done=std::move(done), opts=std::move(opts)] () {
                //respT rval = rpc_method-> template read<respT>();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
                if (auto orval = rpc_method-> template read<respT>()) {
                    auto& rval = *orval;
#else
                {
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
            },
            [this](QGrpcStatus status) {
                LOG_ERROR << "callRpc_ Comm error: " << status.message();
            });
        };

        if (opts.enable_queue && !connected()) {
            grpc_queue_.emplace(std::move(exec));
            return;
        }
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

    std::unique_ptr<nextapp::pb::Nextapp::Client> client_;
    std::unique_ptr<signup::pb::SignUp::Client> signup_client_;
    nextapp::pb::ServerInfo server_info_;
    QString server_version_{"Unknown"};
    std::queue<std::function<void()>> grpc_queue_;
    //bool grpc_is_ready_ = false;
    Status status_{Status::OFFLINE};
    static ServerComm *instance_;
    std::shared_ptr<QGrpcServerStream> updates_;
    QString current_server_address_;
    nextapp::pb::UserGlobalSettings userGlobalSettings_;
    QString session_id_;
    int reconnect_after_seconds_{0};
    QString signup_server_address_;
    signup::pb::GetInfoResponse signup_info_;
};
