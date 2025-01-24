#include <iostream>
#include <string_view>
#include "ServerComm.h"
#include "logging.h"

#include <QSettings>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>
#include <QSslKey>
#include <QSslCertificate>
#include <QNetworkInformation>
#include <QProtobufSerializer>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#   include <QSslConfiguration>
#   include <QGrpcChannelOptions>
#endif

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

#include "NextAppCore.h"
#include "GreenDaysModel.h"
#include "ActionInfoCache.h"
#include "ActionCategoriesModel.h"
#include "MainTreeModel.h"
#include "WorkCache.h"
#include "CalendarCache.h"
#include "AppInstanceMgr.h"

using namespace std;

ostream& operator << (ostream&o, const ServerComm::Status& v) {
    static constexpr auto names = to_array<string_view>({
        "OFFLINE",
        "READY_TO_CONNECT",
        "CONNECTING",
        "INITIAL_SYNC",
        "ONLINE",
        "ERROR"});

    return o << names.at(static_cast<size_t>(v));
}

namespace {

template <typename T>
T deserialize(const QByteArray& data) {
    T t;
    QProtobufSerializer serializer;
    if (!t.deserialize(&serializer, data)) {
        throw runtime_error{"Failed to deserialize data"};
    }
    return t;
}


auto createWorkEventReq(const QString& sessionId, nextapp::pb::WorkEvent::Kind kind) {
    nextapp::pb::AddWorkEventReq req;
    nextapp::pb::WorkEvent event;
    event.setKind(kind);
    req.setWorkSessionId(sessionId);
    //req.events().append(event);
    req.setEvents(nextapp::append(req.events(), std::move(event)));
    return req;
}

} // anon nssu.signUpResponse()


ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client}, signup_client_{new signup::pb::SignUp::Client}
{

    nextapp::pb::Nextapp::Client xx;
    instance_ = this;
    last_seen_update_id_ = QSettings{}.value(lastSeenUpdateIdKey(), 0u).toUInt();
    last_seen_server_instance_ = QSettings{}.value(lastSeenServerInstance(), 0u).toULongLong();

    QSettings settings;
    device_uuid_ = QUuid{settings.value("device/uuid", QString()).toString()};
    if (device_uuid_.isNull()) {
        device_uuid_ = QUuid::createUuid();
        settings.setValue("device/uuid", device_uuid_.toString());
        LOG_INFO << "Created new device-uuid for this device: " << device_uuid_.toString();
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred
            ,
            this, &ServerComm::errorOccurred);
#endif

    setDefaulValuesInUserSettings();

    if (!settings.contains("deviceName")) {
        settings.setValue("deviceName", QSysInfo::machineHostName());
    }

    if (settings.value("onboarding", false).toBool()
        && !settings.value("server/url", QString{}).toString().isEmpty()) {
        if (settings.value("server/auto_login", true).toBool()) {
            LOG_DEBUG << "Auto-login is enabled. Starting the server comm...";
            signup_status_ = SignupStatus::SIGNUP_OK;
            emit signupStatusChanged();
            setStatus(Status::READY_TO_CONNECT);
        }
    } else {
        LOG_WARN << "Server address is unset.";
    }

    connect(&ping_timer_, &QTimer::timeout, [this](const auto&) {
        if (status_ == Status::ONLINE) {
            nextapp::pb::PingReq req;
            LOG_TRACE << "Sending ping to server...";
            callRpc<nextapp::pb::Status>([this](nextapp::pb::PingReq req) {
                return client_->Ping(req);
            }, [this](const nextapp::pb::Status& status) {
                if (status.error() != nextapp::pb::ErrorGadget::Error::OK) {
                    LOG_ERROR << "Ping failed: " << status.message();
                    setStatus(Status::ERROR);
                } else {
                    housekeeping();
                }
            }, req);
        }
    });

    connect(NextAppCore::instance(), &NextAppCore::wokeFromSleep, [this] {
        if (status_ == Status::ONLINE) {
            LOG_DEBUG << "ServerComm: Woke up from sleep.";
            QTimer::singleShot(0, this, &ServerComm::stop);

            const bool reconnect = shouldReconnect();
            auto delay = reconnect ? 100ms : 3s;
            QTimer::singleShot(delay, this, [this] () {
                if (status_ == Status::OFFLINE || status_ == Status::ERROR) {
                    LOG_DEBUG << "ServerComm: Not online. Considering to start...";
                    if (!shouldReconnect()) {
                        LOG_DEBUG << "ServerComm: Network not in a desired state.";
                        return;
                    }
                }
                LOG_DEBUG << "ServerComm: Starting after sleep.";
                start();
            });
        }
    });

    if (QNetworkInformation::loadDefaultBackend()) {
        LOG_INFO << "Network information backend loaded.";
        if (auto *networkInfo = QNetworkInformation::instance()) {

        connect(networkInfo, &QNetworkInformation::reachabilityChanged,
                this, &ServerComm::onReachabilityChanged);
        } else {
            LOG_ERROR << "Failed to get network information instance.";
        }
    } else {
        LOG_ERROR << "Failed to load network information backend.";
    }


    LOG_DEBUG << "Ping interval is " << ping_timer_interval_sec_ << " seconds";
    ping_timer_.start(ping_timer_interval_sec_ * 1000);
}

ServerComm::~ServerComm()
{
    LOG_TRACE_N << "ServerComm destructor. last_seen_update_id_=" << last_seen_update_id_;
    QSettings{}.setValue(lastSeenUpdateIdKey(), last_seen_update_id_);
    QSettings{}.setValue(lastSeenServerInstance(), static_cast<qulonglong>(last_seen_server_instance_));
    instance_ = {};
}

void ServerComm::start()
{
    LOG_DEBUG_N << "starting server-comm.";

    ScopedExit log{[] {
        LOG_DEBUG << "exiting ServerComm::start()";
    }};

    QSettings settings;
    // TODO: Clear the uuid and let the server decide the session-id.
    //       We can't change the channel medatada with QT gRPC, so we have to wait until
    //       `callRpc` is totally removed before we can do this
    //       Remember to remove the `setMetadata` call below.
    session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    current_server_address_ = settings.value("server/url", QString{}).toString();

    const QUrl url(current_server_address_, QUrl::StrictMode);

    clearMessages();
    addMessage(tr("Connecting to server %1").arg(current_server_address_));

    optional<QSslConfiguration> sslConfig;

    if (url.scheme() == "https") {
        sslConfig.emplace();
        sslConfig->setPeerVerifyMode(QSslSocket::QueryPeer);
        //sslConfig->setPeerVerifyMode(QSslSocket::VerifyNone);
        sslConfig->setProtocol(QSsl::TlsV1_3);

        if (auto cert = settings.value("server/clientCert").toString(); !cert.isEmpty()) {
            LOG_DEBUG_N << "Loading our private client cert/key.";
            sslConfig->setLocalCertificate(QSslCertificate{cert.toUtf8()});
            sslConfig->setPrivateKey(QSslKey{settings.value("server/clientKey").toByteArray(), QSsl::Rsa});
            auto caCert = QSslCertificate{settings.value("server/caCert").toByteArray()};
            sslConfig->setCaCertificates({caCert});
            sslConfig->setPeerVerifyMode(QSslSocket::VerifyPeer);
        }

        // For some reason, the standard GRPC server reqire ALPN to be configured when using TLS, even though
        // it only support https/2.
        sslConfig->setAllowedNextProtocols({{"h2"}});
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    auto channelOptions = QGrpcChannelOptions{url}
                              .withSslConfiguration(sslConfig);
                              .withMetadata({std::make_pair("sid", session_id_.c_str())});
    if (sslConfig) {
        channelOptions.withSslConfiguration(*sslConfig);
    }
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#else
    const auto device_id = device_uuid_.toString(QUuid::WithoutBraces).toStdString();
    auto channelOptions = QGrpcChannelOptions{}
                              //.setSslConfiguration(sslConfig)
                              .setMetadata({
                                            std::make_pair("sid", session_id_.c_str()),
                                            {"did", device_id.c_str()}});
    if (sslConfig) {
        LOG_DEBUG_N << "Using SSL configuration.";
        channelOptions.setSslConfiguration(*sslConfig);
    }
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(url, channelOptions));
#endif

    startNextappSession();
}

void ServerComm::stop()
{
    if (status_ != Status::ERROR) {
        setStatus(Status::OFFLINE);
    }

    LOG_DEBUG_N << "Purging " << grpc_queue_.size() << " pending gRPC calls";
    while(!grpc_queue_.empty()) {
        grpc_queue_.pop();
    }
    emit connectedChanged();
    if (updates_) {
        updates_->cancel();
        updates_.reset();
    }
}

QString ServerComm::version()
{
    //emit errorRecieved({});
    return server_version_;
}

bool ServerComm::connected() const noexcept
{
    return status_ == Status::ONLINE;
}

void ServerComm::toggleConnect()
{
    if (connected()) {
        stop();
    } else {
        start();
    }
}

void ServerComm::reloadSettings()
{
    // QTimer::singleShot(200, [this] {
    //     auto server_address = QSettings{}.value("serverAddress", getDefaultServerAddress()).toString();
    //     if (server_address != current_server_address_) {
    //         LOG_INFO_N << "Server address change from " << current_server_address_
    //                    << " to " << server_address
    //                    << ". I will re-connect.";
    //         start();
    //     }
    // });
}

void ServerComm::getColorsInMonth(unsigned int year, unsigned int month)
{
    nextapp::pb::MonthReq req;
    req.setYear(year);
    req.setMonth(month);

    callRpc<nextapp::pb::Month>([this](nextapp::pb::MonthReq req) {
        return client_->GetMonth(req);
    } , [this, y=year, m=month](const nextapp::pb::Month& month) {
        // LOG_TRACE << "Received colors for " << month.days().size()
        //           << " days for month: " << y << "-" << (m + 1);

        assert(month.year() == y);
        assert(month.month() == m);

        emit receivedMonth(month);
    }, req);
}

void ServerComm::setDayColor(int year, int month, int day, QUuid colorUuid)
{
    nextapp::pb::SetColorReq req;
    auto d = req.date();
    d.setYear(year);
    d.setMonth(month);
    d.setMday(day);
    req.setDate(d);

    // req.date().setYear(year);
    // req.date().setMonth(month);
    // req.date().setMday(day);
    req.setColor(colorUuid.toString(QUuid::WithoutBraces));

    callRpc<nextapp::pb::Status>([this](nextapp::pb::SetColorReq req) {
        return client_->SetColorOnDay(req);
    }, req);
}

void ServerComm::setDay(const nextapp::pb::CompleteDay &day)
{
    const auto date = day.day().date();
    callRpc<nextapp::pb::Status>([this](nextapp::pb::CompleteDay day) {
        return client_->SetDay(day);
    }, day);
}

void ServerComm::addNode(const nextapp::pb::Node &node)
{
    nextapp::pb::CreateNodeReq req;
    req.setNode(node);

    rpcQueueAndExecute<QueuedRequest::Type::ADD_NODE>(req);
}

void ServerComm::updateNode(const nextapp::pb::Node &node)
{
    rpcQueueAndExecute<QueuedRequest::Type::UPDATE_NODE>(node);
}

void ServerComm::moveNode(const QUuid &uuid, const QUuid &toParentUuid)
{
    nextapp::pb::MoveNodeReq req;
    req.setUuid(uuid.toString(QUuid::WithoutBraces));

    if (!toParentUuid.isNull()) {
        req.setParentUuid(toParentUuid.toString(QUuid::WithoutBraces));
    }

    if (uuid == toParentUuid) {
        LOG_ERROR << "A node cannnot be its own parent!";
        assert(false);
        return;
    }

    rpcQueueAndExecute<QueuedRequest::Type::MOVE_NODE>(req);

    callRpc<nextapp::pb::Status>([this](nextapp::pb::MoveNodeReq req) {
        return client_->MoveNode(req);
    }, req);
}

void ServerComm::deleteNode(const QUuid &uuid)
{
    nextapp::pb::DeleteNodeReq req;
    req.setUuid(uuid.toString(QUuid::WithoutBraces));

    rpcQueueAndExecute<QueuedRequest::Type::DELETE_NODE>(req);
}

void ServerComm::getDayColorDefinitions()
{    
    callRpc<nextapp::pb::DayColorDefinitions>([this]() {
        return client_->GetDayColorDefinitions({});
    } , [this](const nextapp::pb::DayColorDefinitions& defs) {
        LOG_DEBUG_N << "Received " << defs.dayColors().size()
                    << " day-color definitions.";

        emit receivedDayColorDefinitions(defs);
    });
}

void ServerComm::fetchDay(int year, int month, int day)
{
    nextapp::pb::Date req;
    req.setYear(year);
    req.setMonth(month);
    req.setMday(day);

    callRpc<nextapp::pb::CompleteDay>([this](nextapp::pb::Date req) {
        return client_->GetDay(req);
    } , [this, year, month, day](const nextapp::pb::CompleteDay& cday) {
        const auto& date = cday.day().date();
        assert(date.year() == year);
        assert(date.month() == month);
        assert(date.mday() == day);
        emit receivedDay(cday);
    }, req);
}

void ServerComm::addAction(const nextapp::pb::Action& action)
{
    rpcQueueAndExecute<QueuedRequest::Type::ADD_ACTION>(action);
}

void ServerComm::updateAction(const nextapp::pb::Action &action)
{
    rpcQueueAndExecute<QueuedRequest::Type::UPDATE_ACTION>(action);
}

void ServerComm::updateActions(const nextapp::pb::UpdateActionsReq &action)
{
    rpcQueueAndExecute<QueuedRequest::Type::UPDATE_ACTIONS>(action);
}

void ServerComm::deleteAction(const QString &actionUuid)
{
    nextapp::pb::DeleteActionReq req;
    req.setActionId(actionUuid);
    rpcQueueAndExecute<QueuedRequest::Type::DELETE_ACTION>(req);
}

void ServerComm::markActionAsDone(const QString &actionUuid, bool done)
{
    nextapp::pb::ActionDoneReq req;
    req.setUuid(actionUuid);
    req.setDone(done);

    rpcQueueAndExecute<QueuedRequest::Type::MARK_ACTION_AS_DONE>(req);
}

void ServerComm::markActionAsFavorite(const QString &actionUuid, bool favorite)
{
    nextapp::pb::ActionFavoriteReq req;
    req.setUuid(actionUuid);
    req.setFavorite(favorite);

    rpcQueueAndExecute<QueuedRequest::Type::MARK_ACTION_AS_FAVORITE>(req);
}

void ServerComm::getActiveWorkSessions()
{
    callRpc<nextapp::pb::Status>([this]() {
        nextapp::pb::Empty req;
        return client_->ListCurrentWorkSessions(req);
    } , [this](const nextapp::pb::Status& status) {
        if (status.hasWorkSessions()) {
            auto ws = make_shared<nextapp::pb::WorkSessions>(status.workSessions());
            emit receivedCurrentWorkSessions(ws);
        }
    });
}

void ServerComm::startWork(const QString &actionId, bool activate)
{
    nextapp::pb::CreateWorkReq req;
    req.setActionId(actionId);
    req.setActivate(activate);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->CreateWorkSession(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::addWorkFromTimeBlock(const QString &timeBlockUuid)
{
    nextapp::pb::CreateWorkReq req;
    req.setTimeBlockId(timeBlockUuid);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->CreateWorkSession(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::pauseWork(const QString& sessionId) {
    auto req = createWorkEventReq(sessionId, nextapp::pb::WorkEvent::Kind::PAUSE);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::resumeWork(const QString& sessionId) {
    auto req = createWorkEventReq(sessionId, nextapp::pb::WorkEvent::Kind::RESUME);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}


void ServerComm::doneWork(const QString& sessionId) {
    auto req = createWorkEventReq(sessionId, nextapp::pb::WorkEvent::Kind::STOP);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::touchWork(const QString& sessionId) {
    auto req = createWorkEventReq(sessionId, nextapp::pb::WorkEvent::Kind::TOUCH);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::sendWorkEvent(const QString &sessionId, const nextapp::pb::WorkEvent &event)
{
    nextapp::pb::AddWorkEventReq req;
    req.setEvents(nextapp::append(req.events(), event));
    req.setWorkSessionId(sessionId);

    sendWorkEvents(req);
}

void ServerComm::sendWorkEvents(const nextapp::pb::AddWorkEventReq &req)
{
    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::deleteWork(const QString &sessionId)
{
    nextapp::pb::DeleteWorkReq req;
    req.setWorkSessionId(sessionId);

    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->DeleteWorkSession(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::getWorkSessions(const nextapp::pb::GetWorkSessionsReq &req, const QUuid &requester)
{
    callRpc<nextapp::pb::Status>([this, req, requester]() {
        return client_->GetWorkSessions(req);
    }, [this, requester](const nextapp::pb::Status& status) {
        if (status.hasWorkSessions()) {
            auto ws = make_shared<nextapp::pb::WorkSessions>(status.workSessions());
            MetaData m;
            m.requester = requester;
            if (status.hasHasMore()) {
                m.more = status.hasMore();
            }
            emit receivedWorkSessions(ws, m);
        }
    });
}

void ServerComm::getDetailedWorkSummary(const nextapp::pb::DetailedWorkSummaryRequest &req, const QUuid &requester)
{
    callRpc<nextapp::pb::Status>([this, req, requester]() {
        return client_->GetDetailedWorkSummary(req);
    }, [this, requester](const nextapp::pb::Status& status) {
        if (status.hasDetailedWorkSummary()) {
            auto summary = make_shared<nextapp::pb::DetailedWorkSummary>(status.detailedWorkSummary());
            MetaData m;
            m.requester = requester;
            emit receivedDetailedWorkSummary(*summary, m);
        }
    });
}

void ServerComm::addWork(const nextapp::pb::WorkSession &ws)
{
    nextapp::pb::AddWorkReq req;
    req.setWork(ws);
    callRpc<nextapp::pb::Status>([this, req=std::move(req)]() {
        return client_->AddWork(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::moveAction(const QString &actionUuid, const QString &nodeUuid)
{
    nextapp::pb::MoveActionReq req;
    req.setActionId(actionUuid);
    req.setNodeId(nodeUuid);

    rpcQueueAndExecute<QueuedRequest::Type::MOVE_ACTION>(req);
}

void ServerComm::addTimeBlock(const nextapp::pb::TimeBlock &tb)
{
    rpcQueueAndExecute<QueuedRequest::Type::ADD_TIME_BLOCK>(tb);
}

void ServerComm::updateTimeBlock(const nextapp::pb::TimeBlock &tb)
{
    rpcQueueAndExecute<QueuedRequest::Type::UPDATE_TIME_BLOCK>(tb);
}

void ServerComm::deleteTimeBlock(const QString &timeBlockUuid)
{
    nextapp::pb::DeleteTimeblockReq req;
    req.setId_proto(timeBlockUuid);

    rpcQueueAndExecute<QueuedRequest::Type::DELETE_TIME_BLOCK>(req);
}

void ServerComm::fetchCalendarEvents(QDate start, QDate end, callback_t<nextapp::pb::CalendarEvents> && done)
{
    LOG_TRACE_N << "Fetching calendar events for " << start.toString() << " to " << end.toString();
    nextapp::pb::TimeSpan req;
    req.setStart(QDateTime{start, QTime{}}.toSecsSinceEpoch());
    req.setEnd(QDateTime{end.addDays(1), QTime{}}.toSecsSinceEpoch());

    const GrpcCallOptions opts{false, true};

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->GetCalendarEvents(req);
    }, [this, done=std::move(done)](const nextapp::pb::Status& status) {
        if (auto err = status.error(); err != nextapp::pb::ErrorGadget::Error::OK) {
            done(CbError{err, status.message()});
            return;
        }

        if (status.hasCalendarEvents()) {
            done(std::move(status.calendarEvents()));
        } else {
            done(CbError{nextapp::pb::ErrorGadget::Error::GENERIC_ERROR, "Missing events"});
        }
    }, opts);
}

void ServerComm::fetchActionCategories(callback_t<nextapp::pb::ActionCategories> &&done)
{
    callRpc<nextapp::pb::Status>([this]() {
        return client_->GetActionCategories({});
    }, [this, done=std::move(done)](const nextapp::pb::Status& status) {
        if (status.hasActionCategories()) {
            done(status.actionCategories());
        } else {
            done(CbError{nextapp::pb::ErrorGadget::Error::GENERIC_ERROR, "Missing categories"});
        }
    });
}

void ServerComm::createActionCategory(const nextapp::pb::ActionCategory &category, callback_t<nextapp::pb::Status> &&done)
{
    callRpc<nextapp::pb::Status>([this, category]() {
        return client_->CreateActionCategory(category);
    }, std::move(done));
}

void ServerComm::updateActionCategory(const nextapp::pb::ActionCategory &category, callback_t<nextapp::pb::Status> &&done)
{
    callRpc<nextapp::pb::Status>([this, category]() {
        return client_->UpdateActionCategory(category);
    }, std::move(done));
}

void ServerComm::deleteActionCategory(const QString &id, callback_t<nextapp::pb::Status> &&done)
{
    nextapp::pb::DeleteActionCategoryReq req;
    req.setId_proto(id);

    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->DeleteActionCategory(req);
    }, std::move(done));
}

void ServerComm::requestOtp(callback_t<nextapp::pb::Status> &&done)
{
    callRpc<nextapp::pb::Status>([this]() {
        nextapp::pb::OtpRequest req;
        return client_->GetOtpForNewDevice(req);
    }, std::move(done));
}

std::shared_ptr<GrpcIncomingStream> ServerComm::synchGreenDays(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewDays);
}

QCoro::Task<nextapp::pb::Status> ServerComm::getNewDayColorDefinitions(const nextapp::pb::GetNewReq &req)
{
    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::GetNewDayColorDefinitions);
}

std::shared_ptr<GrpcIncomingStream> ServerComm::synchNodes(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewNodes);
}

std::shared_ptr<GrpcIncomingStream> ServerComm::synchActions(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewActions);
}

QCoro::Task<nextapp::pb::Status> ServerComm::getActionCategories(const nextapp::pb::Empty &req)
{
    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::GetActionCategories);
}

std::shared_ptr<GrpcIncomingStream> ServerComm::synchWorkSessions(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewWork);
}

std::shared_ptr<GrpcIncomingStream> ServerComm::synchTimeBlocks(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewTimeBlocks);
}

QCoro::Task<nextapp::pb::Status> ServerComm::fetchDevices()
{
    co_return co_await rpc({}, &nextapp::pb::Nextapp::Client::GetDevices);
}

QCoro::Task<nextapp::pb::Status> ServerComm::enableDevice(const QString &deviceId, bool enabled)
{
    nextapp::pb::DeviceUpdateReq req;
    req.setId_proto(deviceId);
    req.setEnabled(enabled);

    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::UpdateDevice);
}

QCoro::Task<nextapp::pb::Status> ServerComm::deleteDevice(const QString &deviceId)
{
    nextapp::pb::Uuid req;
    req.setUuid(deviceId);

    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::DeleteDevice);
}

void ServerComm::setStatus(Status status) {
    if (status_ != status) {
        LOG_INFO << "Status changed from " << status_ << " to " << status;
        status_ = status;
        emit statusChanged();

        if (status == Status::ONLINE || Status::OFFLINE || Status::ERROR) {
            emit connectedChanged();
        }
    }
}

const QUuid &ServerComm::deviceUuid() const
{
    assert(!device_uuid_.isNull());
    return device_uuid_;
}

nextapp::pb::UserGlobalSettings ServerComm::getGlobalSettings() const
{
    return userGlobalSettings_;
}

signup::pb::GetInfoResponse ServerComm::getSignupInfo() const {
    return signup_info_;
}

void ServerComm::setSignupServerAddress(const QString &address)
{
    if (address != signup_server_address_) {
        signup_server_address_ = address;
        signup_info_ = {};
        emit signupInfoChanged();

        signup_status_ = SignupStatus::SIGNUP_NOT_STARTED;
        emit signupStatusChanged();
    }

    clearMessages();
    connectToSignupServer();
}

void ServerComm::signup(const QString &name, const QString &email,
                        const QString &company, const QString &deviceName)
{
    signupOrAdd(name, email, company, deviceName, {});
}

void ServerComm::signupOrAdd(const QString &name,
                     const QString &email,
                     const QString &company,
                     const QString& deviceName,
                     const QString &otp)
{
    // Create CSR
    // Send signup request
    signup_status_ = SignupStatus::SIGNUP_SIGNING_UP;
    emit signupStatusChanged();

    setMessage(tr("Creating client TLS cert..."));

    QString key, csr;

    try {
        tie(csr, key) = createCsr();
        LOG_DEBUG << "Have created CSR: " << csr.size() << " bytes";
    } catch (const exception& ex) {
        LOG_ERROR << "Failed to create CSR: " << ex.what();
        signup_status_ = SignupStatus::SIGNUP_ERROR;
        emit signupStatusChanged();
        return;
    }

    common::Device dev;
    dev.setUuid(deviceUuid().toString(QUuid::WithoutBraces));
    dev.setHostName(QSysInfo::machineHostName());
    dev.setName(deviceName.isEmpty() ? dev.hostName() : deviceName);
    dev.setOs(QSysInfo::kernelType());
    dev.setOsVersion(QSysInfo::kernelVersion());
    dev.setAppVersion(NEXTAPP_UI_VERSION);
    dev.setCsr(csr);
    dev.setProductType(QSysInfo::productType());
    dev.setProductVersion(QSysInfo::productVersion());
    dev.setArch(QSysInfo::currentCpuArchitecture());
    dev.setPrettyName(QSysInfo::prettyProductName());

    auto finish = [this, key](const signup::pb::Reply& su) {
        LOG_DEBUG << "Received signup response from server ";

        QSettings settings;

        switch(su.error()) {
        case signup::pb::ErrorGadget::Error::OK:
            signup_status_ = SignupStatus::SIGNUP_OK;
            assert(su.hasSignUpResponse());
            settings.setValue("onboarding", true);
            settings.setValue("server/url", su.signUpResponse().serverUrl());
            settings.setValue("server/auto_login", true);
            settings.setValue("server/clientCert", su.signUpResponse().cert());
            settings.setValue("server/clientKey", key);
            settings.setValue("server/caCert", su.signUpResponse().caCert());
            addMessage(tr("Your account was successfully created!"));
            signup_status_ = SignupStatus::SIGNUP_SUCCESS;
            QMetaObject::invokeMethod(qApp, [this]() {
                start();
            }, Qt::QueuedConnection);
            break;
        case signup::pb::ErrorGadget::Error::EMAIL_ALREADY_EXISTS:
            signup_status_ = SignupStatus::SIGNUP_ERROR;
            addMessage(tr("Your email is already in use for an account."));
            break;
        default:
            signup_status_ = SignupStatus::SIGNUP_ERROR;
            addMessage(tr("Failed to create your account. Error: %1").arg(su.message()));
            break;
        }

        emit signupStatusChanged();
    };

    addMessage(tr("Contacting the sign-up server..."));

    if (!otp.isEmpty()) {
        signup::pb::CreateNewDeviceRequest add_device_req;
        common::OtpAuth otp_auth;
        otp_auth.setOtp(otp);
        otp_auth.setEmail(email);
        add_device_req.setOtpAuth(otp_auth);
        add_device_req.setDevice(dev);

        callRpc<signup::pb::Reply>([this, add_device_req, key]() {
            return signup_client_->CreateNewDevice(add_device_req);
        }, std::move(finish), GrpcCallOptions{false, false, true});
    } else {
         signup::pb::SignUpRequest signup_req;
        signup_req.setUserName(name);
        signup_req.setEmail(email);

        if (company.isEmpty()) {
            auto uuid = QUuid::createUuid();
            signup_req.setTenantName("NonCompany " + uuid.toString(QUuid::WithoutBraces));
        } else {
            signup_req.setTenantName(company);
        }

        signup_req.setDevice(dev);

        callRpc<signup::pb::Reply>([this, signup_req, key]() {
            return signup_client_->SignUp(signup_req);
        }, std::move(finish), GrpcCallOptions{false, false, true});
    }
}

void ServerComm::addDeviceWithOtp(const QString &otp, const QString &email, const QString &deviceName)
{
    signupOrAdd({}, email, {}, deviceName, otp);
}

void ServerComm::signupDone()
{
    if (signup_status_ == SignupStatus::SIGNUP_SUCCESS) {
        signup_status_ = SignupStatus::SIGNUP_OK;
        emit signupStatusChanged();
    }
}

void ServerComm::saveGlobalSettings(const nextapp::pb::UserGlobalSettings &settings)
{
    LOG_DEBUG_N << "Saving global user-settings";
    callRpc<nextapp::pb::Status>([this, settings]() {
        return client_->SetUserGlobalSettings(settings);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR_N << "Connection to gRPC server failed: " << status.message();

    emit errorRecieved(tr("Connection to gRPC server failed: %1").arg(status.message()));

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    if (status.code() == QGrpcStatus::StatusCode::Cancelled) {
#else
    if (status.code() == QtGrpc::StatusCode::Cancelled) {
#endif
        setStatus(Status::OFFLINE);
        return;
    }
    setStatus(Status::ERROR);
    scheduleReconnect();
}

void ServerComm::initGlobalSettings()
{
    // This is done before onGrpcReady is called and the RPC interface is opened for all components.
    GrpcCallOptions opts;
    opts.enable_queue = false;
    opts.ignore_errors = true;
    callRpc<nextapp::pb::Status>([this]() {
        return client_->GetUserGlobalSettings({});
    }, [this](const nextapp::pb::Status& status) {
        if (status.hasUserGlobalSettings()) {
            userGlobalSettings_ = status.userGlobalSettings();
            LOG_DEBUG_N << "Received global user-settings";
        } else if (status.error() == nextapp::pb::ErrorGadget::Error::NOT_FOUND) {
            LOG_INFO_N << "initializing global settings...";
            saveGlobalSettings(userGlobalSettings_);
        } else {
            LOG_ERROR << "Failed to get global user-settings: " << status.message();
            setStatus(Status::ERROR);
            stop();
            return;
        }
        onGrpcReady();
        emit globalSettingsChanged();
    }, opts);
}

void ServerComm::onGrpcReady()
{
    reconnect_after_seconds_ = 0;
    setStatus(Status::ONLINE);
    emit versionChanged();
    for(; !grpc_queue_.empty(); grpc_queue_.pop()) {
        grpc_queue_.front()();
    }

    NextAppCore::instance()->setOnline(true);
}

void ServerComm::onUpdateMessage()
{
    LOG_TRACE_N << "Received an update...";

    try {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (auto value = updates_->read<nextapp::pb::Update>()) {
            auto msg = make_shared<nextapp::pb::Update>(std::move(*value));
#else
        {
            auto msg = make_shared<nextapp::pb::Update>(std::move(updates_->read<nextapp::pb::Update>()));
#endif
            LOG_TRACE << "Got update: #" << msg->messageId() << " " << msg->when().seconds();
            const auto msgid = msg->messageId();
            if (msgid != last_seen_update_id_ + 1) {
                LOG_WARN_N << "Received out of order update #" << msgid
                           << ". I expected messageid #" << (last_seen_update_id_ + 1);

                // TODO: Reconnect?
            };
            last_seen_update_id_ = msgid;

            if (msg->hasUserGlobalSettings()) {
                const auto& new_settings = msg->userGlobalSettings();
                if (new_settings.version() > userGlobalSettings_.version()) {
                    LOG_DEBUG << "Received updated global user-settings";

                    const bool first_day_of_week_changed
                        = userGlobalSettings_.firstDayOfWeekIsMonday() != new_settings.firstDayOfWeekIsMonday();
                    userGlobalSettings_ = new_settings;
                    emit globalSettingsChanged();

                    if (first_day_of_week_changed) {
                        LOG_DEBUG << "First day of week changed";
                        emit firstDayOfWeekChanged();
                    }
                }
            }
            // if (msg->hasDayColor()) {
            //     LOG_DEBUG << "Day color is " << msg->dayColor().color();
            //     QUuid color;
            //     if (!msg->dayColor().color().isEmpty()) {
            //         color = QUuid{msg->dayColor().color()};
            //     }
            //     const auto& date = msg->dayColor().date();
            //     emit dayColorChanged(date.year(), date.month(), date.mday(), color);
            // }

            if (msg->hasDevice()) {
                const auto& dev = msg->device();
                const auto& uuid = QUuid{dev.id_proto()};
                if (uuid == deviceUuid()) {
                    if (msg->op() == nextapp::pb::Update::Operation::UPDATED) {
                        LOG_DEBUG << "Received updated device info";
                        if (!dev.enabled()) {
                            LOG_WARN << "This device has been disabled! Logging out.";
                            stop();
                            return;
                        }
                    }
                    if (msg->op() == nextapp::pb::Update::Operation::DELETED) {
                        LOG_WARN << "This device has been deleted! Logging out.";
                        stop();
                        QSettings{}.setValue("server/deleted", true);
                        return;
                    }
                };
            }

            emit onUpdate(std::move(msg));
        }
    } catch (const exception& ex) {
        LOG_WARN << "Failed to read proto message: " << ex.what();
    }
}

void ServerComm::setDefaulValuesInUserSettings()
{
    nextapp::pb::WorkHours wh;
    wh.setStart(NextAppCore::parseHourMin("08:00"));
    wh.setEnd(NextAppCore::parseHourMin("16:00"));

    userGlobalSettings_.setDefaultWorkHours(wh);
    userGlobalSettings_.setTimeZone(getSystemTimeZone());

    const auto territory = QTimeZone::systemTimeZone().territory();
    if (territory != QLocale::AnyCountry) {
        QString territory_name;
        territory_name = QLocale::territoryToString(territory);
        userGlobalSettings_.setRegion(territory_name);
    }

    userGlobalSettings_.setLanguage("en");

    const bool monday_is_first_dow = QLocale::system().firstDayOfWeek() == Qt::Monday;
    userGlobalSettings_.setFirstDayOfWeekIsMonday(monday_is_first_dow);
}

void ServerComm::scheduleReconnect()
{
    if (reconnect_after_seconds_ == 0) {
        reconnect_after_seconds_ = 1;
    } else {
        reconnect_after_seconds_ = std::min(reconnect_after_seconds_ * 2, 60 * 5);
    }

    LOG_INFO_N << "Reconnecting in " << reconnect_after_seconds_ << " seconds";
    QTimer::singleShot(reconnect_after_seconds_ * 1000, this, &ServerComm::start);
}

QString ServerComm::toString(const QGrpcStatus& status) {
    static const auto errors = to_array<QString>({
        tr("OK"),
        tr("CANCELLED"),
        tr("UNKNOWN"),
        tr("INVALID_ARGUMENT"),
        tr("DEADLINE_EXCEEDED"),
        tr("NOT_FOUND"),
        tr("ALREADY_EXISTS"),
        tr("PERMISSION_DENIED"),
        tr("RESOURCE_EXHAUSTED"),
        tr("FAILED_PRECONDITION"),
        tr("ABORTED"),
        tr("OUT_OF_RANGE"),
        tr("UNIMPLEMENTED"),
        tr("INTERNAL"),
        tr("UNAVAILABLE"),
        tr("DATA_LOSS"),
        tr("UNAUTHENTICATED"),
    });

    const auto ix = static_cast<size_t>(status.code());
    if (ix < errors.size()) {
        return errors[ix] + ": " + status.message();
    }

    return tr("UNKNOWN (code=%1): %2").arg(ix).arg(status.message());
}

void ServerComm::connectToSignupServer()
{
    QSslConfiguration sslConfig{QSslConfiguration::defaultConfiguration()};
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    sslConfig.setProtocol(QSsl::TlsV1_3);

    // For some reason, the standard GRPC server reqire ALPN to be configured when using TLS, even though
    // it only support https/2.
    sslConfig.setAllowedNextProtocols({{"h2"}});

    const bool use_tls = signup_server_address_.startsWith("https://");

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    auto channelOptions = QGrpcChannelOptions{QUrl(signup_server_address_, QUrl::StrictMode)}
                              .withSslConfiguration(sslConfig);
    signup_client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#else
    auto channelOptions = QGrpcChannelOptions{};
    if (use_tls) {
        channelOptions.setSslConfiguration(sslConfig);
    }
    signup_client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(QUrl(signup_server_address_, QUrl::StrictMode), channelOptions));
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    connect(signup_client_.get(), &signup::pb::SignUp::Client::errorOccurred, [this](const QGrpcStatus &status) {
        LOG_ERROR_N << "Connection to signup server failed: " << status.message();
        addMessage(tr("Connection to signup server failed: %1").arg(toString(status)));
        signup_status_ = SignupStatus::SIGNUP_ERROR;
        emit signupStatusChanged();
    });
#endif

    LOG_INFO << "Using signup server at " << signup_server_address_;

    callRpc<signup::pb::Reply>([this]() {
        setMessage(tr("Connecting to server ..."));
        return signup_client_->GetInfo({});
    }, [this](const signup::pb::Reply& reply) {
        if (reply.hasGetInfoResponse() && reply.error() == signup::pb::ErrorGadget::Error::OK) {
            LOG_DEBUG << "Received signup info from server ";
            signup_info_ = reply.getInfoResponse();
            signup_status_ = SignupStatus::SIGNUP_HAVE_INFO;
            emit signupInfoChanged();
            emit signupStatusChanged();
            addMessage(tr("Connected to signup server!"));
        } else {
            setMessage(tr("Failed to get server information"));
            signup_status_ = SignupStatus::SIGNUP_ERROR;
            emit signupStatusChanged();
        }
     }, GrpcCallOptions{false, false, true});
}

void ServerComm::clearMessages()
{
    messages_ = "";
    emit messagesChanged();
}

void ServerComm::addMessage(const QString &msg)
{
    if (!messages_.isEmpty()) {
        messages_ += "\n";
    }
    messages_ += msg;
    emit messagesChanged();
}

void ServerComm::setMessage(const QString &msg)
{
    messages_ = msg;
    emit messagesChanged();
}

QString ServerComm::lastSeenUpdateIdKey() const {
    return AppInstanceMgr::instance()->name() + "/last_seen_update_id";
}

QString ServerComm::lastSeenServerInstance() const
{
    return AppInstanceMgr::instance()->name() + "/last_seen_server_instance";
}

QCoro::Task<void> ServerComm::startNextappSession()
{
    LOG_INFO << "Starting a new session with nextapp server at: " << current_server_address_;
    NextAppCore::instance()->showSyncPopup(true);
    bool close_popup = true;
    ScopedExit hide_popup_on_exit{[this, &close_popup] {
        if (close_popup) {
            NextAppCore::instance()->showSyncPopup(false);
            clearMessages();
        }
    }};


    QSettings settings;
    session_id_.clear();

    auto prev_version = settings.value("client/version", "").toString();
    if (prev_version != NEXTAPP_UI_VERSION) {
        LOG_INFO << "Client version changed from " << prev_version << " to " << NEXTAPP_UI_VERSION;
        settings.setValue("client/version", NEXTAPP_UI_VERSION);
        settings.setValue("sync/resync", "true");
        settings.sync();
    }

    const bool full_sync = settings.value("sync/resync", false).toBool()
                           || NextAppCore::instance()->db().dbWasInitialized();

    if (full_sync) {
        LOG_WARN << "Resyncing from the server. Will delete the local cache.";
        addMessage(tr("Doing a full synch with the server. This may take a few moments..."));

        // Clear the local cache.
        co_await NextAppCore::instance()->db().clear();

        settings.setValue("sync/resync", "false");
    }

    setStatus(Status::CONNECTING);

    GrpcCallOptions options;
    // The default instance_id is 1. If we have a higher id, we need to send "instance_id".
    if (auto iid = AppInstanceMgr::instance()->instanceId(); iid > 1) {
        const auto str = to_string(iid);
        QHash<QByteArray, QByteArray> metadata;
        metadata.insert("instance_id", str.c_str());
        options.qopts.setMetadata(std::move(metadata));
    };

    bool needs_sync = false;
    auto res = co_await rpc(nextapp::pb::Empty{},
                            &nextapp::pb::Nextapp::Client::Hello,
                            options);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasHello()) {
            session_id_ = res.hello().sessionId().toLatin1();

            if (res.hello().serverInstanceTag() != last_seen_server_instance_) {
                needs_sync = true;
                last_seen_server_instance_ = res.hello().serverInstanceTag();
                LOG_TRACE_N << "Server instance tag changed to " << last_seen_server_instance_;
            }

            if (res.hello().lastPublishId() != last_seen_update_id_) {
                needs_sync = true;
                last_seen_update_id_ = res.hello().lastPublishId();
                LOG_TRACE_N << "Servers update id is " << res.hello().lastPublishId()
                            << " (was " << last_seen_update_id_ << ")";
            }
        } else {
            LOG_ERROR <<  "Server did not send a Hello message!";
            goto failed;
        }
        LOG_INFO << "Session started. Session-id: " << session_id_;
        addMessage(tr("Connected and authenticated with the server."));
    } else {
failed:
        setStatus(Status::ERROR);

        LOG_ERROR_N << "Failed to start new session: " << res.message();
        if (res.error() == nextapp::pb::ErrorGadget::Error::DEVICE_DISABLED) {
            addMessage(tr("*** This device has been disabled ***\nYou must enable it again from another device before it can be used."));
        } else {
            addMessage(tr("Failed to establish a new session with the server:\n") + res.message());
        }

        stop();
        close_popup = false;
        co_return;
    }

    setStatus(Status::INITIAL_SYNC);

    res = co_await rpc(nextapp::pb::Empty{}, &nextapp::pb::Nextapp::Client::GetServerInfo);

    if (res.error() != nextapp::pb::ErrorGadget::Error::OK) {
        goto failed;
    }

    assert(res.hasServerInfo());
    if (res.hasServerInfo()) {
        const auto& se = res.serverInfo();
        server_version_ = se.properties().front().value();
        LOG_INFO << "Connected to server version " << server_version_ << " at " << current_server_address_;
    } else {
        LOG_WARN << "We are connected to a server, but it did not send ServerInfo.";
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    updates_ = client_->streamSubscribeToUpdates({});
#else
    updates_ = client_->SubscribeToUpdates({});
#endif

    connect(updates_.get(), &QGrpcServerStream::messageReceived, this, &ServerComm::onUpdateMessage);

    if (needs_sync || full_sync) {

        // Do the initial synch.
        LOG_DEBUG_N << "Fetching data versions...";
        if (!co_await getDataVersions()) {
            LOG_WARN_N << "Failed to get data versions.";
            goto failed;
        }

        LOG_DEBUG_N << "Fetching global settings...";
        if (!co_await getGlobalSetings()) {
            LOG_WARN_N << "Failed to get global settings.";
            goto failed;
        }

        addMessage(tr("Fetching green days"));
        LOG_DEBUG_N << "Fetching green days...";
        if (!co_await GreenDaysModel::instance()->synchFromServer()) {
            LOG_WARN_N << "Failed to get green days.";
            goto failed;
        }

        addMessage(tr("Fetching lists"));
        LOG_DEBUG_N << "Fetching nodes...";
        if (!co_await MainTreeModel::instance()->doSynch(full_sync)) {
            LOG_WARN_N << "Failed to get nodes.";
            goto failed;
        }

        LOG_DEBUG_N << "Fetching  action categories...";
        if (!co_await ActionCategoriesModel::instance().synch(full_sync)) {
            LOG_WARN_N << "Failed to get action categories.";
            goto failed;
        }

        addMessage(tr("Fetching actions"));
        LOG_DEBUG_N << "Fetching  actions...";
        if (!co_await ActionInfoCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get action info.";
            goto failed;
        }

        addMessage(tr("Fetching work sessions"));
        LOG_DEBUG_N << "Fetching work sessions...";
        if (!co_await WorkCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get work sessions.";
            goto failed;
        }

        addMessage(tr("Fetching time blocks for the calendar"));
        LOG_DEBUG_N << "Fetching time blocks...";
        if (!co_await CalendarCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get time-blocks for the calendar.";
            goto failed;
        }

        if (full_sync) {
            nextapp::pb::ResetPlaybackReq req;
            req.setInstanceId(AppInstanceMgr::instance()->instanceId());
            co_await rpc(req, &nextapp::pb::Nextapp::Client::ResetPlayback);
        }
    } else {
        LOG_INFO << "Omitting server-sync as the local cache is up to date.";
        // Omit this if we are just reconnecting?
        emit globalSettingsChanged();

        if (!co_await GreenDaysModel::instance()->loadFromCache()) {
            LOG_WARN_N << "Failed to get green days.";
            goto failed;
        }

        if (!co_await MainTreeModel::instance()->doLoadLocally()) {
            LOG_WARN_N << "Failed to load nodes.";
            goto failed;
        }

        if (!co_await ActionCategoriesModel::instance().loadFromDb()) {
            LOG_WARN_N << "Failed to get action categories.";
            goto failed;
        }

        if (!co_await ActionInfoCache::instance()->loadLocally()) {
            LOG_WARN_N << "Failed to get action info.";
            goto failed;
        }

        if (!co_await WorkCache::instance()->loadLocally()) {
            LOG_WARN_N << "Failed to get work sessions.";
            goto failed;
        }

        if (!co_await CalendarCache::instance()->loadLocally(full_sync)) {
            LOG_WARN_N << "Failed to get time-blocks for the calendar.";
            goto failed;
        }
    }

    onGrpcReady();

    addMessage(tr("Retrying any pending server-reqests..."));
    LOG_DEBUG_N << "Retrying server requests...";
    co_await retryRequests();

    // If we re-run this later, we don't need to do a full sync again.
    NextAppCore::instance()->db().clearDbInitializedFlag();

    co_return;
}

namespace {
struct X509ReqDeleter {
    void operator()(X509_REQ* x) const {
        X509_REQ_free(x);
    }
};

using X509Req_Ptr = std::unique_ptr<X509_REQ, X509ReqDeleter>;

struct EVP_PKEY_Deleter {
    void operator()(EVP_PKEY* p) const {
        EVP_PKEY_free(p);
    }
};

using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;

template <typename T>
void setSubjects(X509_REQ *cert, const T& subjects) {
    if (auto name = X509_NAME_new()) {

        ScopedExit cleaner{[name] {
            X509_NAME_free(name);
        }};

        int loc = 0;
        for(const auto& [section, subj] : subjects) {
            LOG_DEBUG << "Adding subject: " << subj << " in section: " << section;
            const auto res = X509_NAME_add_entry_by_txt(
                name, section.c_str(), MBSTRING_ASC,
                reinterpret_cast<const unsigned char *>(subj.data()), subj.size(), loc++, 0);
            if (!res) {
                throw runtime_error{"X509_NAME_add_entry_by_txt"};
            }
        }

        const auto res = X509_REQ_set_subject_name(cert, name);
        if (!res) {
            throw runtime_error{"X509_REQ_set_subject_name"};
        }
        return;
    }
    throw runtime_error{"X509_NAME_new"};
}

template <typename T, typename U>
void toBuffer(const T& cert, U& dest) {
    auto * bio = BIO_new(BIO_s_mem());
    if (!bio) {
        throw runtime_error{"BIO_new"};
    }

    ScopedExit scoped{ [bio] {
            BIO_free(bio); }
    };

    if constexpr (is_same_v<T, X509_REQ>) {
        auto rval = PEM_write_bio_X509_REQ(bio, &cert);
        if (rval == 0) {
            throw runtime_error{"PEM_write_bio_X509_REQ"};
        }
    } else if constexpr (is_same_v<T, EVP_PKEY>){
        PEM_write_bio_PrivateKey(bio, &cert, NULL, NULL, 0, 0, NULL);
    } else {
        assert(false && "Unsupported type.");
        throw runtime_error{"toBuffer: Unsupported type."};
    }

    char * data{};
    if (const auto len = BIO_get_mem_data(bio, &data); len > 0) {
        dest = {data, data + len};
    } else {
        throw runtime_error{"BIO_get_mem_data"};
    }
}
} // anon ns

std::pair<QString /* csr */, QString /* key */> ServerComm::createCsr()
{
    const auto subjects = to_array<pair<string, string>>(
        {{"CN", deviceUuid().toString(QUuid::WithoutBraces).toStdString()},
         {"O", "Nextapp"},
         {"OU", "Nextapp"}});

    if (auto req = X509_REQ_new()) {
        X509Req_Ptr req_ptr{req};

        X509_REQ_set_version(req, X509_VERSION_3);
        setSubjects(req, subjects);

        EVP_PKEY_Ptr rkey;
        if (auto rsa_key = EVP_PKEY_new()) {
            rkey.reset(rsa_key);

            if (auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) {
                ScopedExit ctx_cleanup{[&ctx] {
                    EVP_PKEY_CTX_free(ctx);
                }};

                EVP_PKEY_keygen_init(ctx);
                EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 4096);
                if (EVP_PKEY_keygen(ctx, &rsa_key) <= 0) {
                    throw runtime_error{"Error generating EVP_PKEY_RSA key for the certificate request."};
                }
            } else {
                throw runtime_error{"EVP_PKEY_CTX_new_id"};
            }

            X509_REQ_set_pubkey(req, rsa_key);
            if (X509_REQ_sign(req, rsa_key, EVP_sha256()) <= 0) {
                throw runtime_error{"Error signing the certificate request."};
            }

            QString csr, key;
            toBuffer(*req, csr);
            toBuffer(*rsa_key, key);
            return {csr, key};
        } else {
            throw runtime_error{"EVP_PKEY_new()"};
        }
    }

    throw runtime_error{"X509_REQ"};
}

QCoro::Task<bool> ServerComm::getDataVersions()
{
    auto res = co_await rpc(nextapp::pb::Empty{}, &nextapp::pb::Nextapp::Client::GetDataVersions);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasDataVersions()) {
            server_data_versions_ = res.dataVersions();
            co_return true;
        }
    }

    co_return false;
}

QCoro::Task<bool> ServerComm::getGlobalSetings()
{
    auto res = co_await rpc(nextapp::pb::Empty{}, &nextapp::pb::Nextapp::Client::GetUserGlobalSettings);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasUserGlobalSettings()) {
            if (res.userGlobalSettings().version() >= userGlobalSettings_.version()) {
                userGlobalSettings_ = res.userGlobalSettings();
                LOG_DEBUG_N << "Received global user-settings";
                emit globalSettingsChanged();
            }
        }
        co_return true;
    } else if (res.error() == nextapp::pb::ErrorGadget::Error::NOT_FOUND) {
        LOG_INFO_N << "initializing global settings...";
        res = co_await rpc(userGlobalSettings_, &nextapp::pb::Nextapp::Client::SetUserGlobalSettings);
        if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
            co_return true;
        }
    } else {
        LOG_WARN_N << "Failed to get global user-settings: " << res.message();
    }

    co_return false;
}

void ServerComm::onReachabilityChanged(QNetworkInformation::Reachability reachability) {

    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
        LOG_WARN_N << "Network reachability is unknown";
        break;
    case QNetworkInformation::Reachability::Site:
        LOG_INFO_N << "Network is available at site.";
        break;
    case QNetworkInformation::Reachability::Online:
        LOG_INFO_N << "Network is available.";
        break;
    case QNetworkInformation::Reachability::Local:
        LOG_INFO_N << "Local network is available.";
        // Handle local network available case
        break;
    case QNetworkInformation::Reachability::Disconnected:
        LOG_INFO_N << "Network is unavailable.";
        QTimer::singleShot(0, this, &ServerComm::stop);
        break;
    }

    if (shouldReconnect()) {
        LOG_INFO_N << "Network is available. Will consider to reconnect in 100ms.";
        QTimer::singleShot(100ms, [this]() {
            if (status_ == Status::OFFLINE || status_ == Status::ERROR) {
                if (!shouldReconnect()) {
                    LOG_WARN_N << "ServerComm: Network is not reliable available. Will not reconnect now.";
                    return;
                }

                LOG_INFO << "ServerComm: Network became available. Connecting to server.";
                start();
            }
        });
    };
}

bool ServerComm::shouldReconnect() const noexcept
{
    const auto level = QSettings{}.value("server/reconnect_level", 0).toInt();
    const auto reqire = static_cast<ReconnectLevel>(level);

    if (reqire == ReconnectLevel::NEVER) {
        LOG_DEBUG_N << "Reconnect level is: NEVER";
        return false;
    };

    if (auto *instance = QNetworkInformation::instance()) {
        const auto current = instance->reachability();
        LOG_DEBUG_N << "Reconnect level is: " << level << " and current reachability is: "
                    << static_cast<int>(current) << ".";
        switch(current)   {
            case QNetworkInformation::Reachability::Online:
                return reqire == ReconnectLevel::SITE
                       || reqire == ReconnectLevel::LAN
                       || reqire == ReconnectLevel::ONLINE;
            case QNetworkInformation::Reachability::Site:
                return reqire == ReconnectLevel::SITE
                       || reqire == ReconnectLevel::LAN;
            case QNetworkInformation::Reachability::Local:
                return reqire == ReconnectLevel::LAN;
            case QNetworkInformation::Reachability::Disconnected:
            case QNetworkInformation::Reachability::Unknown:
                break;
        }
    }
    return false;
}


QCoro::Task<bool> ServerComm::save(QueuedRequest &qr)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;


    LOG_DEBUG_N << "Saving request " << qr.uuid.toString()
                << " of type " << static_cast<int>(qr.type)
                << " to the database.";

    params << qr.uuid.toString(QUuid::StringFormat::WithoutBraces);
    params << qr.time;
    params << static_cast<uint>(qr.type);
    params << qr.data;

    const auto rval = co_await db.query("INSERT INTO requests (uuid, time, rpcid, data) VALUES (?,?,?,?)",
                                        qr.uuid.toString(QUuid::StringFormat::WithoutBraces),
                                        qr.time,
                                        static_cast<uint>(qr.type),
                                        qr.data);
    if (!rval) {
        LOG_WARN_N << "Failed to save request to the database. " << rval.error();
        co_return false;
    }

    if (auto id = rval.value().insert_id) {
        LOG_TRACE_N << "Saved request #" << *id << " to the database.";
        qr.id = *id;
    } else {
        LOG_WARN_N << "Query apparently suceeded, but the insert id was lost!";
        assert(false);
    }

    ++queued_requests_count_;
    co_return true;
}

// Execute the request (send it to the server)
// If it is successful, remove the request from the database.
QCoro::Task<bool> ServerComm::execute(const QueuedRequest &qr, bool deleteRequest)
{
    ++executing_request_count_;
    assert(executing_request_count_ == 1);

    ScopedExit decr{[this] {
        --executing_request_count_;
        assert(executing_request_count_ == 0);
    }};

    GrpcCallOptions options;
    options.enable_queue = true; // Informative

    QHash<QByteArray, QByteArray> metadata;

    // Don't enable replay protection for direct requests that are not stored in the database.
    if (deleteRequest) {
        assert(qr.id > 0);
        const string id_str = to_string(qr.id); // qr.uuid.toString(QUuid::StringFormat::WithoutBraces).toStdString();
        metadata.insert("req_id", id_str.c_str());

        // The default instance_id is 1. If we have a higher id, we need to send "instance_id".
        if (auto iid = AppInstanceMgr::instance()->instanceId(); iid > 1) {
            const auto str = to_string(iid);
            metadata.insert("instance_id", str.c_str());
        };
    }

    options.qopts.setMetadata(std::move(metadata)); // Prevent replay if the server has seen this id

    nextapp::pb::Status res;
    switch(qr.type) {
        case QueuedRequest::Type::INVALID:
            assert(false);
            LOG_ERROR_N << "Invalid request type.";
            throw runtime_error{"Invalid request type."};
        case QueuedRequest::Type::ADD_ACTION: {
            QProtobufSerializer serializer;
            const auto req = deserialize<nextapp::pb::Action>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::CreateAction, options);
        } break;
        case QueuedRequest::Type::UPDATE_ACTION: {
            QProtobufSerializer serializer;
            const auto req = deserialize<nextapp::pb::Action>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::UpdateAction, options);
        } break;
        case QueuedRequest::Type::UPDATE_ACTIONS: {
            QProtobufSerializer serializer;
            const auto req = deserialize<nextapp::pb::UpdateActionsReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::UpdateActions, options);
        } break;
        case QueuedRequest::Type::DELETE_ACTION: {
            const auto req = deserialize<nextapp::pb::DeleteActionReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::DeleteAction, options);
        } break;
        case QueuedRequest::Type::MARK_ACTION_AS_DONE: {
            const auto req = deserialize<nextapp::pb::ActionDoneReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::MarkActionAsDone, options);
        } break;
        case QueuedRequest::Type::MARK_ACTION_AS_FAVORITE: {
            const auto req = deserialize<nextapp::pb::ActionFavoriteReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::MarkActionAsFavorite, options);
        } break;
        case QueuedRequest::Type::MOVE_ACTION: {
            const auto req = deserialize<nextapp::pb::MoveActionReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::MoveAction, options);
        } break;
        case QueuedRequest::Type::ADD_NODE: {
            const auto req = deserialize<nextapp::pb::CreateNodeReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::CreateNode, options);
        } break;
        case QueuedRequest::Type::UPDATE_NODE: {
            const auto req = deserialize<nextapp::pb::Node>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::UpdateNode, options);
        } break;
        case QueuedRequest::Type::MOVE_NODE: {
            const auto req = deserialize<nextapp::pb::MoveNodeReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::MoveNode, options);
        } break;
        case QueuedRequest::Type::DELETE_NODE: {
            const auto req = deserialize<nextapp::pb::DeleteNodeReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::DeleteNode, options);
        } break;
        case QueuedRequest::Type::ADD_TIME_BLOCK: {
            const auto req = deserialize<nextapp::pb::TimeBlock>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::CreateTimeblock, options);
        } break;
        case QueuedRequest::Type::UPDATE_TIME_BLOCK: {
            const auto req = deserialize<nextapp::pb::TimeBlock>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::UpdateTimeblock, options);
        } break;
        case QueuedRequest::Type::DELETE_TIME_BLOCK: {
            const auto req = deserialize<nextapp::pb::DeleteTimeblockReq>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::DeleteTimeblock, options);
        } break;
    }

    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        LOG_TRACE_N << "Request " << qr.uuid.toString() << " executed";

        if (deleteRequest) {
            co_await deleteRequestFromDb(qr.id);
        }
        co_return true;
    }

    // Handle server-reject replay.
    if (res.error() == nextapp::pb::ErrorGadget::Error::REPLAY_DETECTED) {
        LOG_INFO_N << "Request " << qr.uuid.toString() << " was rejected as replay.";

        if (deleteRequest) {
            co_await deleteRequestFromDb(qr.id);
        }
        co_return true;
    }

    if (res.error() == nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR) {
        LOG_WARN_N << "Failed to send and receive response for request " << qr.uuid.toString() << ": " << res.message();
        // TODO: We need to retry. Should we set a timer?
    } else {
        if (!isTemporaryError(res.error())) {
            LOG_ERROR_N << "Request " << qr.uuid.toString() << " failed with a permanent error: " << res.error();

            if (deleteRequest) {
                co_await deleteRequestFromDb(qr.id);
            }

            co_return true; // Lets processing the next request continue.
        }
    }

    co_return false;
}

/* Re-try the requests we have stored in sequential order, oldest first.
 * If a request fails, and the execute() logic can't handle the error,
 * we break out so we can retry later.
 *
 * TODO: We may need to add a retry-count limit as well as a timeout to prevent one
 *       persistent error for a request from blocking the queue. However, we should
 *       adapt to the error conditions that could block a request from ever passing.
 */
QCoro::Task<void> ServerComm::retryRequests()
{
    // Only enter once as a time.
    if (retrying_requests_) {
        co_return;
    }

    retrying_requests_ = true;
    ScopedExit reset{[this] {
        retrying_requests_ = false;
    }};

    enum Cols {
        ID = 0,
        UUID,
        TIME,
        TRIES,
        RPCID,
        DATA
    };

    auto& db = NextAppCore::instance()->db();
    uint current = 0;

    while (connected()) {
        const auto rval = co_await db.legacyQuery("SELECT id, uuid, time, tries, rpcid, data FROM requests ORDER BY id LIMIT 1");
        if (!rval) {
            LOG_WARN_N << "Failed to get the oldest request from the database. " << rval.error();
            co_return;
        }
        if (rval.value().isEmpty()) {
            LOG_DEBUG_N << "No [more] requests to retry.";
            co_return;
        }

        // Check if it has expired. If it has, delete it.
        const auto& row = rval.value().at(0);
        QueuedRequest qr;
        qr.id = row.at(ID).toUInt();
        qr.uuid = QUuid{row.at(UUID).toString()};
        qr.time = row.at(TIME).toDateTime();
        qr.type = static_cast<QueuedRequest::Type>(row.at(RPCID).toUInt());
        qr.data = row.at(DATA).toByteArray();

        // Sanity check. If a request was somehow not deleted, we don't want to go into a infinite loop re-trying it.
        if (current >= qr.id) {
            LOG_WARN_N << "Request " << qr.uuid.toString() << " has already been processed. Deleting it.";
            deleteRequestFromDb(qr.id);
            continue;
        }

        const QUuid uuid{row.at(UUID).toString()};
        const auto now = QDateTime::currentDateTime();
        const auto max_ttl_hours = QSettings{}.value("server/reqests_ttl", 24*7).toInt();
        if (qr.time.secsTo(now) > max_ttl_hours * 3600) {
            LOG_WARN_N << "Request " << uuid.toString()
                       << " of type " << static_cast<int>(qr.type)
                       << " from " << qr.time.toString()
                       << " has expired. Deleting it.";
            deleteRequestFromDb(qr.id);
            current = qr.id;
            continue;
        }

        const auto tries = row.at(TRIES).toUInt();
        if (tries >  QSettings{}.value("server/reqests_retries", 9).toInt()) {
            LOG_WARN_N << "Request " << uuid.toString()
            << " of type " << static_cast<int>(qr.type)
            << " from " << qr.time.toString()
            << " has reached the maximum number of retries. Deleting it.";
            deleteRequestFromDb(qr.id);
            current = qr.id;
            continue;
        }

        // If we failed, we will retry later.
        if (!co_await execute(qr, true)) {

            db.query("UPDATE requests SET tries = tries + 1 WHERE id = ?", qr.id);

            LOG_WARN_N << "Request " << uuid.toString()
            << " of type " << static_cast<int>(qr.type)
            << " from " << qr.time.toString()
            << " failed retry. Will retry later.";
            co_return;
        }

        current = qr.id;
    }

    co_return;
}

QCoro::Task<bool> ServerComm::deleteRequestFromDb(const uint id)
{
    auto& db = NextAppCore::instance()->db();
    {
        const auto rval = co_await db.query("DELETE FROM requests WHERE id = ?", id);
        if (!rval) {
            LOG_WARN_N << "Failed to delete request from the database. " << rval.error();
            // TODO: Implement a general recovery mechanism. For example, if the disk is full,
            //       notify the user and exit. Do a full sync on startup.
            co_return false;
        }
    }

    {
        // Update the count to the actual rows in the db
        const auto rval2 = co_await db.legacyQuery("SELECT COUNT(*) FROM requests");
        if (!rval2) {
            LOG_WARN_N << "Failed to count request in the database. " << rval2.error();
            co_return false;
        }

        queued_requests_count_ = rval2.value().at(0).at(0).toInt();
    }
    LOG_TRACE_N << "There are " << queued_requests_count_ << " requests remaining in the database.";
    co_return true;
}

QCoro::Task<void> ServerComm::housekeeping()
{
    if (connected()) {
        co_await retryRequests();
    }
}

bool ServerComm::isTemporaryError(nextapp::pb::ErrorGadget::Error error) {
    switch(error) {
        case nextapp::pb::ErrorGadget::Error::OK:
        case nextapp::pb::ErrorGadget::Error::CLIENT_GRPC_ERROR:
        case nextapp::pb::ErrorGadget::Error::AUTH_MISSING_SESSION_ID:
        case nextapp::pb::ErrorGadget::Error::DATABASE_UPDATE_FAILED:
        case nextapp::pb::ErrorGadget::Error::DATABASE_REQUEST_FAILED:
            return true;
        default:
            ;
    }

    return false;
}
