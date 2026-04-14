#include <iostream>
#include <string_view>
#include <fstream>
#include "ServerComm.h"
#include "logging.h"

#include <QSettings>
#include <QFile>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>
#include <QSslKey>
#include <QSslCertificate>
#include <QNetworkInformation>
#include <QProtobufSerializer>
#include <qcorocore.h>
#include <qcorothread.h>
#include <qcorosignal.h>
#include <qcorofuture.h>


#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#   include <QSslConfiguration>
#   include <QGrpcChannelOptions>
#endif

#include <QByteArray>

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QMultiHash>
using grpc_metadata_t = QMultiHash<QByteArray, QByteArray>;
#else
// Qt 6.9 and earlier
#include <QHash>
// If you have Qgrpc_metadata_t typedef available, you can use that instead:
//   using grpc_metadata_t = Qgrpc_metadata_t;
using grpc_metadata_t = QHash<QByteArray, QByteArray>;
#endif

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

#include "nextapp.h"
#include "NextAppCore.h"
#include "GreenDaysModel.h"
#include "ActionInfoCache.h"
#include "ActionCategoriesModel.h"
#include "ActionsOnCurrentCalendar.h"
#include "MainTreeModel.h"
#include "WorkCache.h"
#include "CalendarCache.h"
#include "AppInstanceMgr.h"
#include "NotificationsModel.h"
#include "LogModel.h"

#if defined(ANDROID_BUILD) && defined(WITH_FCM)
#include "AndroidFcmBridge.h"
#endif

using namespace std;

ostream& operator << (ostream&o, const ServerComm::Status& v) {
    static constexpr auto names = to_array<string_view>({
        "MANUAL_OFFLINE",
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

filesystem::path getDataDir(RuntimeServices& runtime) {
    return runtime.db().dataDir();
}

filesystem::path getServerReqIdPath(RuntimeServices& runtime) {
    return getDataDir(runtime) / "svrreqid.dat";
}

void saveValuesToFile(RuntimeServices& runtime, uint32_t lastSeenUpdateId, uint64_t lastSeenServerInstance,
                      uint64_t lastSeenUserPublishEpoch, string_view why) {
    const auto path = getServerReqIdPath(runtime);
    LOG_TRACE_N << "Saving lastSeenUpdateId=" << lastSeenUpdateId
                << ", lastSeenServerInstance=" << lastSeenServerInstance
                << ", lastSeenUserPublishEpoch=" << lastSeenUserPublishEpoch
                << " because " << why << " to " << path;

    const auto tmp = path.string() + ".tmp";
    std::ofstream file(tmp, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&lastSeenUpdateId), sizeof(lastSeenUpdateId));
        file.write(reinterpret_cast<const char*>(&lastSeenServerInstance), sizeof(lastSeenServerInstance));
        file.write(reinterpret_cast<const char*>(&lastSeenUserPublishEpoch), sizeof(lastSeenUserPublishEpoch));
        file.close();

        // Rename the temporary file to the final file
        try {
            std::filesystem::rename(tmp, path);
        } catch (const std::exception& e) {
            LOG_ERROR << "Failed to rename " << tmp << " to " << path << ": " << e.what();
        }
    } else {
        LOG_ERROR << "Failed to open " << tmp << " for writing.";
    }
}

bool loadValuesFromFile(RuntimeServices& runtime, uint32_t& lastSeenUpdateId, uint64_t& lastSeenServerInstance,
                        uint64_t& lastSeenUserPublishEpoch) {
    const auto path = getServerReqIdPath(runtime);
    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&lastSeenUpdateId), sizeof(lastSeenUpdateId));
        file.read(reinterpret_cast<char*>(&lastSeenServerInstance), sizeof(lastSeenServerInstance));
        if (!file.read(reinterpret_cast<char*>(&lastSeenUserPublishEpoch), sizeof(lastSeenUserPublishEpoch))) {
            lastSeenUserPublishEpoch = 0;
        }
        file.close();
        LOG_TRACE_N << "Loaded lastSeenUpdateId=" << lastSeenUpdateId
                    << ", lastSeenServerInstance=" << lastSeenServerInstance
                    << ", lastSeenUserPublishEpoch=" << lastSeenUserPublishEpoch
                    << " from " << path;
        return true;
    } else {
        // Handle error: unable to open file
        LOG_DEBUG_N << "Failed to open '" << path << "' for reading.";
    }
    return false;
}

} // anon ns


ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : ServerComm(*NextAppCore::instance())
{
}

ServerComm::ServerComm(RuntimeServices& runtime)
    : runtime_{runtime}
    , client_{new nextapp::pb::Nextapp::Client}
    , signup_client_{new signup::pb::SignUp::Client}
{

    nextapp::pb::Nextapp::Client xx;
    instance_ = this;

    device_uuid_ = QUuid{settings().value("device/uuid", QString()).toString()};
    if (device_uuid_.isNull()) {
        device_uuid_ = QUuid::createUuid();
        settings().setValue("device/uuid", device_uuid_.toString());
        LOG_INFO << "Created new device-uuid for this device: " << device_uuid_.toString();
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred
            ,
            this, &ServerComm::errorOccurred);
#endif

    setDefaulValuesInUserSettings();
    loadLocalDataVersions();

    if (const auto name = settings().value("deviceName", QString{}).toString();
        name.isEmpty()) {
        const auto host_name = QSysInfo::machineHostName();
        if (host_name != "localhost") {
            settings().setValue("deviceName", host_name);
        }
    }

    if (settings().value("onboarding", false).toBool()
        && !settings().value("server/url", QString{}).toString().isEmpty()) {
        if (settings().value("server/auto_login", true).toBool()) {
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
                    if (status_ != Status::MANUAL_OFFLINE) {
                        setStatus(Status::ERROR);
                    }
                    setStatus(Status::ERROR);
                } else {
                    housekeeping();
                }
            }, req);
        }
    });

    connect(&runtime_.appEventSource(), SIGNAL(wokeFromSleep()),
            this, SLOT(onAppWokeFromSleep()));

#if defined(ANDROID_BUILD) && defined(WITH_FCM)
    connect(&runtime_.appEventSource(), SIGNAL(settingsChanged()),
            this, SLOT(onAppSettingsChanged()));

    connect(&AndroidFcmBridge::instance(), &AndroidFcmBridge::messageReceived, [this]
            (const QString &messageId, const QString &notification, const QString &data) {
        LOG_DEBUG_N << "Got push message from Android FCM bridge: "
                    << "id=" << messageId
                    << ", notification=" << notification
                    << ", data=" << data;

        // Try to deserialize a base-64 encoded Update message
        if (data.isEmpty()) {
            LOG_DEBUG_N << "No data in the push message.";
            return;
        }


        const auto str = data.toUtf8();

        string_view v{ str }, kind, message;
        const string_view kind_token = "kind=", message_token = "message=";

        if (v.starts_with('{')) {
            v = v.substr(1);
        }
        if (v.ends_with('}')) {
            v = v.substr(0, v.size() - 1);
        }

        static const auto get_value = [](const string_view v, const string_view token) -> string_view {
            if (auto pos = v.find(token); pos != string_view::npos) {
                auto value = v.substr(pos + token.size());
                if (auto end_pos = value.find(','); end_pos != string_view::npos) {
                    value = value.substr(0, end_pos);
                }
                return value;
            }
            return {};
        };

        kind = get_value(v, kind_token);
        message = get_value(v, message_token);

        if (message.empty()) {
            LOG_DEBUG_N  << "Push message does not contain a 'message' field: " << data;
            return;
        }

        LOG_DEBUG_N << "kind=" << kind << ", message=" << message;

        // Decode the base-64 encoded data
        // Unfortunately, QT does not provide a way to decode base64 directly from a memory view.
        // So we have to convert it to a QByteArray and then decode it.
        const QByteArray qmessage{message.data(), static_cast<qsizetype>(message.size())};
        auto decoded_data = QByteArray::fromBase64(qmessage);
        auto update = make_shared<nextapp::pb::Update>();
        QProtobufSerializer serializer;
        if (!update->deserialize(&serializer, decoded_data)) {
            LOG_ERROR_N << "Failed to deserialize Update message from push data: "
                        << serializer.lastErrorString();
            return;
        }

        // Check if the current message-id from grpc updates is greater than the one we received
        if (update->messageId() <= last_seen_update_id_) {
            LOG_DEBUG_N << "Received Update from push message with id #"
                        << update->messageId() << " <= last_seen_update_id_=" << last_seen_update_id_
                        << ". Ignoring it.";
            return;
        }

        // Only handle the update if it it has events we expect from push messages
        if (update->hasCalendarEvents()) {
            LOG_DEBUG_N << "Received Update from push message. Processing...";
            try {
                emit onUpdate(update);
            } catch (const std::exception &e) {
                LOG_ERROR_N << "Failed to process Update from push message: " << e.what();
            }
        }

    });
#endif

    connect(&runtime_.appEventSource(), SIGNAL(suspending()),
            this, SLOT(onAppSuspending()));

    if (QNetworkInformation::loadDefaultBackend()) {
        LOG_DEBUG_N << "Network information backend loaded.";
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
    updateVisualStatus();
}

ServerComm::~ServerComm()
{
    LOG_TRACE_N << "ServerComm destructor. last_seen_update_id_=" << last_seen_update_id_;
    close();
    instance_ = {};
}

SettingsAccess& ServerComm::settings() const noexcept
{
    return runtime_.settings();
}

DbStore& ServerComm::db() const noexcept
{
    return runtime_.db();
}

void ServerComm::start()
{
    LOG_DEBUG_N << "starting server-comm.";

    if (!last_seen_update_id_) {
        loadValuesFromFile(runtime_, last_seen_update_id_, last_seen_server_instance_, last_seen_user_publish_epoch_);
    }

    if (!canConnect()) {
        LOG_WARN << "We can't connect to nextappd at this time.";
        return;
    }

    ScopedExit log{[] {
        LOG_DEBUG << "exiting ServerComm::start()";
    }};

    // TODO: Clear the uuid and let the server decide the session-id.
    //       We can't change the channel medatada with QT gRPC, so we have to wait until
    //       `callRpc` is totally removed before we can do this
    //       Remember to remove the `setMetadata` call below.
    session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    current_server_address_ = settings().value("server/url", QString{}).toString();

    const QUrl url(current_server_address_, QUrl::StrictMode);

    clearMessages();
    addMessage(tr("Connecting to server %1").arg(current_server_address_));

    optional<QSslConfiguration> sslConfig;

    if (url.scheme() == "https") {
        sslConfig.emplace();
        sslConfig->setPeerVerifyMode(QSslSocket::QueryPeer);
        //sslConfig->setPeerVerifyMode(QSslSocket::VerifyNone);
        sslConfig->setProtocol(QSsl::TlsV1_3);

        if (auto cert = settings().value("server/clientCert").toString(); !cert.isEmpty()) {
            LOG_DEBUG_N << "Loading our private client cert/key.";
            sslConfig->setLocalCertificate(QSslCertificate{cert.toUtf8()});
            sslConfig->setPrivateKey(QSslKey{settings().value("server/clientKey").toByteArray(), QSsl::Rsa});
            auto caCert = QSslCertificate{settings().value("server/caCert").toByteArray()};
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

void ServerComm::onAppWokeFromSleep()
{
    if (status_ == Status::MANUAL_OFFLINE) {
        return;
    }

    LOG_DEBUG << "ServerComm: Woke up from sleep.";
    if (status_ == Status::ONLINE) {
        LOG_DEBUG << "ServerComm: Already online. Need to stop before going online again.";
        QTimer::singleShot(0, this, &ServerComm::stop);
    }

    const bool reconnect = shouldReconnect();
    auto delay = reconnect ? 100ms : 3s;
    QTimer::singleShot(delay, this, [this] {
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

void ServerComm::onAppSettingsChanged()
{
#if defined(ANDROID_BUILD) && defined(WITH_FCM)
    const auto fcm_enabled = settings().value("push/updates", false).toBool();
    if (fcm_enabled != fcm_requested_ && status() == Status::ONLINE) {
        changePushConfigOnServer();
    }
#endif
}

void ServerComm::onAppSuspending()
{
    LOG_INFO_N << "ServerComm: Suspending...";
    stop();
}

void ServerComm::stop()
{
    if (closed_) {
        if (updates_) {
            disconnect(updates_.get(), nullptr, this, nullptr);
            updates_->cancel();
            updates_.reset();
        }
        return;
    }

    if (status_ != Status::ERROR) {
        setStatus(Status::OFFLINE);
    }

    emit connectedChanged();
    if (updates_) {
        disconnect(updates_.get(), nullptr, this, nullptr);
        updates_->cancel();
        updates_.reset();
    }
}

void ServerComm::close()
{
    if (!closed_) {
        closed_ = true;
        LOG_DEBUG_N << "Closing down ServerComm...";
        ping_timer_.stop();
        stop();
        client_.reset();
        signup_client_.reset();
        LOG_DEBUG_N << "Done closing down ServerComm.";
        saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                         last_seen_user_publish_epoch_, "closing up shop");
    }
}

QString ServerComm::version()
{
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
        setStatus(Status::MANUAL_OFFLINE);
    } else {
        if (status() == Status::MANUAL_OFFLINE) {
            setStatus(Status::OFFLINE);
        }
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
        if (month.year() != y || month.month() != m) {
            LOG_WARN_N << "Ignoring mismatched month payload. requested="
                       << y << '-' << m << " received=" << month.year() << '-' << month.month();
            return;
        }

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
        LOG_TRACE_N << "Received " << defs.dayColors().size()
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
        if (date.year() != year || date.month() != month || date.mday() != day) {
            LOG_WARN_N << "Ignoring mismatched day payload. requested="
                       << year << '-' << month << '-' << day
                       << " received=" << date.year() << '-' << date.month() << '-' << date.mday();
            return;
        }
        emit receivedDay(cday);
    }, req);
}

void ServerComm::addAction(const nextapp::pb::Action& action)
{
    rpcQueueAndExecute<QueuedRequest::Type::ADD_ACTION>(action,
                                                        action.name().toStdString());
}

void ServerComm::updateAction(const nextapp::pb::Action &action)
{
    rpcQueueAndExecute<QueuedRequest::Type::UPDATE_ACTION>(action,
                                                           action.name().toStdString());
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

QCoro::Task<bool> ServerComm::createActionCategory(const nextapp::pb::ActionCategory &category)
{
    co_return co_await rpcQueueAndExecute<QueuedRequest::Type::CREATE_ACTION_CATEGORY>(category);
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

std::shared_ptr<GrpcIncomingStream> ServerComm::synchNotifications(const nextapp::pb::GetNewReq &req)
{
    return rpcOpenReadStream(req, &nextapp::pb::Nextapp::Client::GetNewNotifications);
}

QCoro::Task<nextapp::pb::Status> ServerComm::fetchDevices()
{
    co_return co_await rpc({}, &nextapp::pb::Nextapp::Client::GetDevices);
}

QCoro::Task<std::optional<nextapp::pb::Subscription>> ServerComm::fetchSubscription(bool forceRefresh)
{
    nextapp::pb::GetSubscriptionReq req;
    req.setForceRefresh(forceRefresh);

    auto res = co_await rpc(req, &nextapp::pb::Nextapp::Client::GetSubscription);
    if (res.error() != nextapp::pb::ErrorGadget::Error::OK) {
        LOG_WARN_N << "Failed to get subscription data: " << res.message();
        co_return std::nullopt;
    }

    if (!res.hasSubscription()) {
        LOG_INFO_N << "Subscription data is not available from server.";
        co_return std::nullopt;
    }

    co_return res.subscription();
}

QCoro::Task<nextapp::pb::Status> ServerComm::fetchPaymentsPage()
{
    nextapp::pb::PaymentsPageReq req;
    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::GetPaymentsPage);
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
    common::Uuid req;
    req.setUuid(deviceId);

    co_return co_await rpc(req, &nextapp::pb::Nextapp::Client::DeleteDevice);
}

QCoro::Task<void> ServerComm::setLastReadNotification(uint32_t id)
{
    nextapp::pb::SetReadNotificationReq req;
    req.setNotificationId(id);

    co_await rpc(req, &nextapp::pb::Nextapp::Client::SetLastReadNotification);
}

QCoro::Task<void> ServerComm::updateLastReadNotification()
{
    nextapp::pb::Empty req;
    auto reply = co_await rpc(req, &nextapp::pb::Nextapp::Client::GetLastReadNotification);
    if (reply.error() == nextapp::pb::ErrorGadget::Error::OK && reply.hasLastReadNotificationId()) {
        auto id = reply.lastReadNotificationId();
        NotificationsModel::instance()->SetLastReadValue(id);
    } else {
        LOG_WARN_N << "Failed to get last-read notification: " << reply.message();
    }
}

QCoro::Task<void> ServerComm::createNodesFromTemplate(nextapp::pb::NodeTemplate root)
{
    co_await rpc(root, &nextapp::pb::Nextapp::Client::CreateNodesFromTemplate);
}

QCoro::Task<void> ServerComm::resetNodes(nextapp::pb::ResetNodesReq req)
{
    co_await rpc(req, &nextapp::pb::Nextapp::Client::ResetNodes);
}

QCoro::Task<nextapp::pb::Status> ServerComm::deleteAccount()
{
    co_return co_await rpc({}, &nextapp::pb::Nextapp::Client::DeleteAccount);
}

QCoro::Task<void> ServerComm::exportData(std::shared_ptr<QFile> file, const write_export_fn_t& write)
{
    nextapp::pb::ExportDataReq req;
    auto stream = client_->ExportData(req);
    if (!stream) {
        throw std::runtime_error("Failed to export data: Unable to create stream from server");
    }

    bool done = false;
    bool ok = true;

    QObject::connect(
        stream.get(), &QGrpcServerStream::finished, stream.get(),
        [&](const QGrpcStatus &status) {
            if (status.isOk()) {
                LOG_DEBUG_N << "Export stream finished successfully.";
                done = true;
            } else {
                LOG_ERROR_N << "Export stream finished with error: " << status.message();
                done = true;
                ok = false;
            }
        },
        Qt::SingleShotConnection);

    // We can't use co-routines on a Qt grpc stream as the stream cursor moves forward
    // when the messageReceived signal is emitted, not when we read the message.
    QObject::connect(stream.get(), &QGrpcServerStream::messageReceived, [&]() {
        if (ok) {
            if (auto status = stream->read<nextapp::pb::Status>()) {
                if (status->error() == nextapp::pb::ErrorGadget::Error::OK) {
                    // Happy path
                    try {
                        write(*status, *file);
                    } catch (const std::exception& e) {
                        LOG_ERROR_N << "Failed to write data to file: " << e.what();
                        ok = false;
                        stream->cancel();
                        return;
                    }
                } else {
                    LOG_ERROR_N << "Error from server in data-export stream: " << status->message();
                    ok = false;
                    stream->cancel();
                }
            } else {
                if (!done) {
                    LOG_WARN_N << "Failed to read the current message from the export stream.";
                    ok = false;
                    stream->cancel();
                }
            }
        } else {
            LOG_TRACE_N << "Ignoring message from export stream, since we had an error.";
            stream->read<nextapp::pb::Status>();
        }
    });

    // Wait for the stream to end
    const auto status = co_await qCoro(stream.get(), &QGrpcServerStream::finished);
    file->close();
    if (status.isOk()) {
        LOG_DEBUG_N << "Export stream finished successfully.";
        done = true;
        co_return;
    }

    LOG_ERROR_N << "Export stream finished with error: " << status.message();
#ifndef ANDROID_BUILD
    QFile::remove(file->fileName());
#endif
    throw std::runtime_error("Export stream was not successful");
}

QCoro::Task<void> ServerComm::importData(const read_export_fn_t &read)
{
    import_data_in_progress_ = true;
    import_data_resync_received_ = false;
    ScopedExit importing{[this] {
        import_data_in_progress_ = false;
    }};

    nextapp::pb::ImportDataMsg req;
    req.setRequest({});
    auto stream = client_->ImportData(req);
    if (!stream) {
        throw std::runtime_error("Failed to import data: Unable to create stream from server");
    }

    bool finished = false;
    QPromise<bool> ok;
    auto future = ok.future();

    QObject::connect(stream.get(), &QGrpcClientStream::finished, [&](const QGrpcStatus &status) {
        if (status.isOk()) {
            LOG_DEBUG_N << "Import stream finished successfully.";
            ok.addResult(true);
        } else {
            LOG_ERROR_N << "Import stream finished with error: " << status.message();
            ok.addResult(false);
        }
        ok.finish();
        finished = true;
    });

    while(!finished) {
        nextapp::pb::ImportDataMsg req;
        nextapp::pb::Status status;

        try {
            if (read(status)) {
                if (status.hasUser()) {
                    req.setUser(status.user());
                } else if (status.hasUserGlobalSettings()) {
                    req.setUserGlobalSettings(status.userGlobalSettings());
                } else if (status.hasDayColorDefinitions()) {
                    req.setDayColorDefinitions(status.dayColorDefinitions());
                } else if (status.hasDays()) {
                    req.setDays(status.days());
                } else if (status.hasNodes()) {
                    req.setNodes(status.nodes());
                } else if (status.hasActionCategories()) {
                    req.setActionCategories(status.actionCategories());
                } else if (status.hasCompleteActions()) {
                    req.setActions(status.completeActions());
                } else if (status.hasWorkSessions()) {
                    req.setWorkSessions(status.workSessions());
                } else if (status.hasTimeBlocks()) {
                    req.setTimeBlocks(status.timeBlocks());
                } else if (static_cast<int>(status.whatField()) == 0) {
                    // Probably a message with only a HasMore value set to false.
                    // we will ignore it and keep reading until read() returns false.
                    continue;
                } else {
                    LOG_WARN_N << "Received unexpected field# in import stream: "
                               << static_cast<int>(status.whatField());
                    continue;
                }
                stream->writeMessage(req);
            } else {
                req.setCompleted(true);
                stream->writeMessage(req);
                stream->writesDone();
                break;
            }
        } catch (const std::exception &e) {
            LOG_ERROR_N << "Failed to read data from file: " << e.what();
            if (!stream->isFinished()) {
                req.setCompleted(false);
                stream->writeMessage(req);
                stream->writesDone();
            }

            finished = true;
            break;
        }
    }

    auto result = co_await future;
    if (result) {
        if (import_data_resync_received_) {
            LOG_INFO_N << "Import completed after receiving server resync request.";
            QTimer::singleShot(0, this, [this] {
                resync();
            });
        } else {
            LOG_WARN_N << "Import completed without receiving a server resync request.";
        }
        co_return;
    }
    throw std::runtime_error("Import was not successful");
}

void ServerComm::setStatus(Status status) {
    if (status_ != status) {
        LOG_INFO << "Status changed from " << status_ << " to " << status;
        status_ = status;
        updateVisualStatus();
        emit statusChanged();

        if (status == Status::ONLINE
            || status == Status::OFFLINE
            || status == Status::ERROR) {
            emit connectedChanged();
        }
    }
}

const QUuid &ServerComm::deviceUuid() const
{
    if (device_uuid_.isNull()) {
        LOG_ERROR_N << "deviceUuid() requested before initialization";
    }
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
                        const QString &company, const QString &deviceName, int region)
{
    signupOrAdd(name, email, company, deviceName, {}, region);
}

QCoro::Task<void> ServerComm::signupOrAdd(QString name,
                             QString email,
                             QString company,
                             QString deviceName,
                             QString otp,
                             int region)
{
    const bool is_new_user = otp.isEmpty();
    // Create a new device ID
    {
        device_uuid_ = QUuid::createUuid();
        LOG_INFO_N << "Generated new device UUID: " << device_uuid_.toString(QUuid::WithoutBraces);
        settings().setValue("deviceUuid", device_uuid_.toString(QUuid::WithoutBraces));
    }

    // Create CSR
    signup_status_ = SignupStatus::SIGNUP_SIGNING_UP;
    emit signupStatusChanged();

    setMessage(tr("Creating client TLS cert..."));

    auto csr_result = co_await createCsrAsync();
    if (!csr_result) {
        LOG_ERROR << "Failed to create CSR";
        signup_status_ = SignupStatus::SIGNUP_ERROR;
        emit signupStatusChanged();
        co_return;
    }

    // Send signup/add-device request

    const QString& csr = csr_result->first;
    const QString& key = csr_result->second;

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

    setMessage(tr("Sending request to the signup service..."));

    auto finish = [this, key, is_new_user](const signup::pb::Reply& su) {
        LOG_DEBUG << "Received signup response from server ";

        switch(su.error()) {
        case signup::pb::ErrorGadget::Error::OK:
            signup_status_ = SignupStatus::SIGNUP_OK;
            if (!su.hasSignUpResponse()) {
                signup_status_ = SignupStatus::SIGNUP_ERROR;
                addMessage(tr("Signup succeeded without a response payload from the server."));
                LOG_ERROR_N << "Signup service returned OK without SignUpResponse payload.";
                break;
            }
            settings().setValue("onboarding", true);
            settings().setValue("server/url", su.signUpResponse().serverUrl());
            settings().setValue("server/auto_login", true);
            settings().setValue("server/clientCert", su.signUpResponse().cert());
            settings().setValue("server/clientKey", key);
            settings().setValue("server/caCert", su.signUpResponse().caCert());

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

    if (!otp.isEmpty()) {
        LOG_TRACE_N << "Adding new device with OTP for email: " << email;
        signup::pb::CreateNewDeviceRequest add_device_req;
        common::OtpAuth otp_auth;
        otp_auth.setOtp(otp);
        otp_auth.setEmail(email);
        add_device_req.setOtpAuth(otp_auth);
        add_device_req.setDevice(dev);

        auto res = co_await rpcSignup(add_device_req, &signup::pb::SignUp::Client::CreateNewDevice);
        finish(res);
    } else {
        LOG_TRACE_N << "Signing up new account with email: " << email;
        signup::pb::SignUpRequest signup_req;
        signup_req.setUserName(name);
        signup_req.setEmail(email);
        common::Uuid region_uuid;
        if (region >= 0 && region < signup_info_.regions().size()) {
            region_uuid.setUuid(signup_info_.regions().at(region).uuid().uuid());
            signup_req.setRegion(region_uuid);
        }

        if (company.isEmpty()) {
            auto uuid = QUuid::createUuid();
            signup_req.setTenantName("NonCompany " + uuid.toString(QUuid::WithoutBraces));
        } else {
            signup_req.setTenantName(company);
        }

        signup_req.setDevice(dev);

        auto res = co_await rpcSignup(signup_req, &signup::pb::SignUp::Client::SignUp);

        if (res.error() == signup::pb::ErrorGadget::Error::OK) {
            LOG_INFO_N << "Signup successful for email: " << email;
        } else {
            LOG_ERROR_N << "Signup failed for email: " << email << ". Error: " << res.message();
        }

        finish(res);
    }
}

void ServerComm::addDeviceWithOtp(const QString &otp, const QString &email, const QString &deviceName)
{
    signupOrAdd({}, email, {}, deviceName, otp, -1);
}

void ServerComm::signupDone()
{
    LOG_TRACE_N << "Signup process is done. Current signup status: " << signup_status_;
    if (signup_status_ == SignupStatus::SIGNUP_SUCCESS) {
        signup_status_ = SignupStatus::SIGNUP_OK;
        emit signupStatusChanged();
    }
}

QStringList ServerComm::getRegionsForSignup() const
{
    QStringList regions;
    for (const auto& r : signup_info_.regions()) {
        regions.append(r.name());
    }
    return regions;
}

void ServerComm::sendFeedback(const nextapp::pb::Feedback &feedback)
{
    doSendFeedback(feedback);
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
    if (status_ != Status::MANUAL_OFFLINE) {
        setStatus(Status::ERROR);
    }
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
        emit globalSettingsChanged();
    }, opts);
}

void ServerComm::onGrpcReady()
{
    reconnect_after_seconds_ = 0;
    setStatus(Status::ONLINE);
    emit versionChanged();
    runtime_.setOnline(true);
}

bool ServerComm::shouldDeferUpdateUntilInitialSyncComplete() const noexcept
{
    return status_ == Status::INITIAL_SYNC;
}

uint ServerComm::effectiveLastSeenUpdateId() const noexcept
{
    return deferred_update_id_ > last_seen_update_id_ ? deferred_update_id_ : last_seen_update_id_;
}

void ServerComm::applyUpdateMessage(const std::shared_ptr<nextapp::pb::Update>& msg)
{
    assert(msg);

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
                settings().setValue("server/deleted", true);
                return;
            }
        };
    }

    if (msg->hasReload()) {
        const auto what = msg->reload();
        switch(what) {
        case nextapp::pb::Update::Reload::NODES:
            LOG_DEBUG << "Received reload-nodes update";
            MainTreeModel::instance()->doSynch(true);
        }
    }

    if (msg->hasAccountDeleted() && msg->accountDeleted()) {
        LOG_WARN << "Account deleted! Logging out.";
        QTimer::singleShot(0, [this]() {
            runtime_.onAccountDeleted();
        });
    }

    if (msg->hasResync() && msg->resync()) {
        LOG_INFO_N << "Received resync request from server";
        if (import_data_in_progress_) {
            import_data_resync_received_ = true;
            return;
        }
        QTimer::singleShot(500ms, [this]() {
            resync();
        });
    }

    emit onUpdate(msg);
}

QCoro::Task<void> ServerComm::replayDeferredUpdates()
{
    if (deferred_updates_.empty()) {
        LOG_TRACE_N << "No deferred updates to replay after initial sync.";
        co_return;
    }

    LOG_INFO_N << "Replaying " << deferred_updates_.size()
               << " deferred updates after initial sync. last_seen_update_id_="
               << last_seen_update_id_ << ", deferred_update_id_=" << deferred_update_id_;

    while (!deferred_updates_.empty()) {
        auto msg = deferred_updates_.front();
        deferred_updates_.pop_front();
        applyUpdateMessage(msg);

        last_seen_update_id_ = msg->messageId();
        saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                         last_seen_user_publish_epoch_, "replayed deferred update");
    }

    deferred_update_id_ = 0;
    LOG_TRACE_N << "Deferred update replay completed. last_seen_update_id_=" << last_seen_update_id_;
    co_return;
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
            const auto effective_last_seen = effectiveLastSeenUpdateId();
            if (msgid <= effective_last_seen) {
                LOG_WARN_N << "Ignoring duplicate or stale update #" << msgid
                           << " because effective last seen update id is " << effective_last_seen
                           << " (durable=" << last_seen_update_id_
                           << ", deferred=" << deferred_update_id_ << ')';
                return;
            }

            if (msgid != effective_last_seen + 1) {
                LOG_WARN_N << "Received out of order update #" << msgid
                           << ". I expected messageid #" << (effective_last_seen + 1)
                           << " (durable=" << last_seen_update_id_
                           << ", deferred=" << deferred_update_id_ << ')';
                requestResyncAfterStreamGap(msgid);
                return;
            };

            if (shouldDeferUpdateUntilInitialSyncComplete()) {
                deferred_update_id_ = msgid;
                deferred_updates_.push_back(msg);
                LOG_INFO_N << "Deferring update #" << msgid
                           << " while initial sync is running. deferred_updates="
                           << deferred_updates_.size();
                return;
            }

            last_seen_update_id_ = msgid;
            saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                             last_seen_user_publish_epoch_, "normal update");
            applyUpdateMessage(msg);
            saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                             last_seen_user_publish_epoch_, "applied update");
        }
    } catch (const exception& ex) {
        LOG_WARN << "Failed to read proto message: " << ex.what();
    }
}

void ServerComm::requestResyncAfterStreamGap(uint64_t received_message_id)
{
    LOG_INFO_N << "Requesting full sync after stream gap at update #" << received_message_id << '.';
    setStatus(Status::ERROR);

    // Persist the intent before reconnecting so startup cannot skip the
    // mandatory full sync if the stream broke during onboarding or replay.
    settings().setValue("sync/resync", true);
    settings().sync();

    QTimer::singleShot(0, this, [this] {
        resync();
    });
}

void ServerComm::setDefaulValuesInUserSettings()
{
    nextapp::pb::WorkHours wh;
    wh.setStart(parseDuration("08:00"));
    wh.setEnd(parseDuration("16:00"));

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

    LOG_INFO_N << "Will consider reconnecting in " << reconnect_after_seconds_ << " seconds";
    QTimer::singleShot(reconnect_after_seconds_ * 1000, [this] () {
        const auto shold_reconnect = shouldReconnect();
        if (status_ == Status::ERROR && shold_reconnect) {
            LOG_INFO_N << "Reconnecting!";
            start();
            return;
        }

        LOG_DEBUG_N << "Not reconnecting. status=" << status_ << ", shouldReconnect=" << shold_reconnect;
    });
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

    LOG_INFO << "Using signup server at " << signup_server_address_;

    struct SignupInfoReplyHandler final {
        ServerComm *self;

        void operator()(const signup::pb::Reply& reply) const
        {
            if (reply.hasGetInfoResponse() && reply.error() == signup::pb::ErrorGadget::Error::OK) {
                LOG_DEBUG << "Received signup info from server ";
                self->signup_info_ = reply.getInfoResponse();
                self->signup_status_ = ServerComm::SignupStatus::SIGNUP_HAVE_INFO;
                emit self->signupInfoChanged();
                emit self->signupStatusChanged();
                self->addMessage(QObject::tr("Connected to signup server!"));
                return;
            }

            const auto details = reply.message().isEmpty()
                ? QObject::tr("Failed to get server information")
                : QObject::tr("Failed to get server information: %1").arg(reply.message());
            LOG_ERROR_N << "Failed to get signup server info from " << self->signup_server_address_
                        << ": error=" << static_cast<int>(reply.error())
                        << " message=" << reply.message();
            self->setMessage(details);
            self->signup_status_ = ServerComm::SignupStatus::SIGNUP_ERROR;
            emit self->signupStatusChanged();
        }

        void operator()(const CbError& error) const
        {
            LOG_ERROR_N << "Connection to signup server failed: " << error.message;
            self->addMessage(QObject::tr("Connection to signup server failed: %1").arg(error.message));
            self->signup_status_ = ServerComm::SignupStatus::SIGNUP_ERROR;
            emit self->signupStatusChanged();
        }
    };

    callRpc_<signup::pb::Reply>([this]() {
        setMessage(tr("Connecting to server ..."));
        return signup_client_->GetInfo({});
    }, SignupInfoReplyHandler{this}, GrpcCallOptions{false, false, true});
}

QCoro::Task<void> ServerComm::updateDataEpoc()
{
    LOG_DEBUG_N << "Updating data-epoc";
    nextapp::pb::Empty req;
    auto& db = runtime_.db();
    (void) co_await db.query("UPDATE nextapp SET data_epoc = ?", nextapp::app_data_epoc);
    (void) co_await db.query(
        "INSERT INTO sync_state(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        QStringLiteral("user_data_epoch"),
        QVariant::fromValue<qulonglong>(session_user_data_epoch_));
}

void ServerComm::clearMessages()
{
    LOG_DEBUG_N << "Cleared messages";
    messages_ = "";
    emit messagesChanged();
}

void ServerComm::addMessage(const QString &msg)
{
    if (!messages_.isEmpty()) {
        messages_ += "\n";
    }
    messages_ += msg;
    LOG_DEBUG_N << "Messages: " << messages_;
    emit messagesChanged();
}

void ServerComm::setMessage(const QString &msg)
{
    messages_ = msg;
    LOG_DEBUG_N << "Messages: " << messages_;
    emit messagesChanged();
}

QString ServerComm::lastSeenUpdateIdKey() const {
    return AppInstanceMgr::instance()->name() + "/last_seen_update_id";
}

QString ServerComm::localActionCategoryVersionKey() const
{
    return AppInstanceMgr::instance()->name() + "/sync/action_category_version";
}

void ServerComm::loadLocalDataVersions()
{
    local_data_versions_.setActionCategoryVersion(
        settings().value(localActionCategoryVersionKey(), 0).toULongLong());
}

void ServerComm::persistLocalDataVersions() const
{
    settings().setValue(localActionCategoryVersionKey(),
                      QVariant::fromValue(local_data_versions_.actionCategoryVersion()));
}

void ServerComm::setLocalActionCategoryVersion(uint64_t version)
{
    if (local_data_versions_.actionCategoryVersion() == version) {
        return;
    }

    local_data_versions_.setActionCategoryVersion(version);
    persistLocalDataVersions();
}

QString ServerComm::lastSeenServerInstance() const
{
    return AppInstanceMgr::instance()->name() + "/last_seen_server_instance";
}

QCoro::Task<void> ServerComm::doSendFeedback(nextapp::pb::Feedback feedback)
{
    if (feedback.hasLog()) {
        // Attach the log
        if (auto *logger = LogModel::instance()) {
            const auto plain = logger->getAll();
            const auto compressed = gzipCompress(plain.toUtf8());
            feedback.setLog(compressed);
        }
    };

    LOG_DEBUG_N << "Sending feedback to server";
    co_await rpc(feedback, &nextapp::pb::Nextapp::Client::SendFeedback);
}

QCoro::Task<void> ServerComm::startNextappSession()
{
    LOG_INFO << "Starting a new session with nextapp server at: " << current_server_address_;
    const auto start_time = chrono::steady_clock::now();
    ScopedExit finish_session_init{[this, &start_time] {
        clearMessages();
        if (status() == Status::ONLINE) {
            LOG_INFO << "Initialized nextapp session in "
                     << std::fixed << std::setprecision(2)
                     << std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count()
                     << " seconds.";
        }
    }};


    session_id_.clear();
    deferred_updates_.clear();
    deferred_update_id_ = 0;
    updated_id_sync_initialized_ = settings().value("sync/updatedIdInitialized", false).toBool();
    server_supports_updated_id_ = false;

    {
        auto prev_version = settings().value("client/version", "").toString();
        if (prev_version != NEXTAPP_UI_VERSION) {
            LOG_INFO << "Client version changed from " << prev_version << " to " << NEXTAPP_UI_VERSION;
            settings().setValue("client/version", NEXTAPP_UI_VERSION);
            settings().sync();
        }
    }

    bool full_sync = co_await needFullResync();
    if (full_sync) {
        // Clear stale in-memory calendar references before the cache is rebuilt.
        ActionsOnCurrentCalendar::instance()->clear();
    }

    setStatus(Status::CONNECTING);

    GrpcCallOptions options;
    // The default instance_id is 1. If we have a higher id, we need to send "instance_id".
    if (auto iid = AppInstanceMgr::instance()->instanceId(); iid > 1) {
        const auto str = to_string(iid);
        grpc_metadata_t metadata;
        metadata.insert("instance_id", str.c_str());
        options.qopts.setMetadata(std::move(metadata));
    };

    bool needs_sync = false;
    bool server_reset_detected = false;
    uint64_t notification_sync_target{0};
    uint64_t hello_last_notification{0};
    auto res = co_await rpc(nextapp::pb::Empty{},
                            &nextapp::pb::Nextapp::Client::Hello,
                            options);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasHello()) {
            session_id_ = res.hello().sessionId().toLatin1();

            LOG_DEBUG_N << "Last seen update id: " << last_seen_update_id_
                        << ", server last publish id: " << res.hello().lastPublishId()
                        << ", last seen server instance: " << last_seen_server_instance_
                        << ", last seen user publish epoch: " << last_seen_user_publish_epoch_
                        << ", server instance tag: " << res.hello().serverInstanceTag()
                        << ", server user publish epoch: " << res.hello().userPublishEpoch()
                        << ", server user data epoch: " << res.hello().userDataEpoch()
                        << ", server-id: " << res.hello().serverId()
                        << ", session-id: " << session_id_
                        << ", plans enabled: " << res.hello().plansEnabled()
                        << ", supports updated_id: " << res.hello().supportsUpdatedId();

            session_start_publish_id_ = res.hello().lastPublishId();
            session_user_publish_epoch_ = res.hello().userPublishEpoch();
            session_user_data_epoch_ = res.hello().userDataEpoch();
            hello_last_notification = res.hello().lastNotification();
            server_supports_updated_id_ = res.hello().supportsUpdatedId();
            if (auto local_epoch = co_await runtime_.db().queryOne<qulonglong>(
                    "SELECT value FROM sync_state WHERE key = ?",
                    QStringLiteral("user_data_epoch"))) {
                last_seen_user_data_epoch_ = local_epoch.value();
            } else {
                last_seen_user_data_epoch_ = 0;
            }
            if (res.hello().serverInstanceTag() != last_seen_server_instance_) {
                needs_sync = true;
                server_reset_detected = true;
                last_seen_server_instance_ = res.hello().serverInstanceTag();
                last_seen_user_publish_epoch_ = session_user_publish_epoch_;
                last_seen_update_id_ = 0;
                LOG_INFO_N << "Needs sync. Server instance tag changed to " << last_seen_server_instance_;
            } else if (res.hello().userPublishEpoch() != last_seen_user_publish_epoch_) {
                needs_sync = true;
                last_seen_user_publish_epoch_ = res.hello().userPublishEpoch();
                last_seen_update_id_ = 0;
                LOG_INFO_N << "Needs sync. User publish epoch changed to " << last_seen_user_publish_epoch_;
            } else if (!res.hello().supportsUpdatedId()
                       && res.hello().lastPublishId() < last_seen_update_id_) {
                needs_sync = true;
                LOG_INFO_N << "Needs sync. Server last publish id "
                            << res.hello().lastPublishId()
                            << " is behind our last applied update id "
                            << last_seen_update_id_;
            } else if (res.hello().supportsUpdatedId()
                       && res.hello().lastPublishId() < last_seen_update_id_) {
                LOG_INFO_N << "Ignoring lower server last publish id "
                           << res.hello().lastPublishId()
                           << " because updated_id sync is supported and the durable baseline is tracked locally.";
            } else if (res.hello().lastPublishId() > last_seen_update_id_) {
                LOG_INFO_N << "Server has " << (res.hello().lastPublishId() - last_seen_update_id_)
                           << " unapplied updates. Will request replay from message id "
                           << last_seen_update_id_;
            }

            if (session_user_data_epoch_ != last_seen_user_data_epoch_) {
                full_sync = true;
                needs_sync = true;
                LOG_INFO_N << "Full sync required. User data epoch changed from "
                           << last_seen_user_data_epoch_ << " to " << session_user_data_epoch_;
            }

            saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                             last_seen_user_publish_epoch_, "server hello");

            if (hello_last_notification > NotificationsModel::instance()->lastUpdateSeen()) {
                notification_sync_target = hello_last_notification;
                LOG_DEBUG_N << "Server notification watermark advanced to " << notification_sync_target
                            << " from local " << NotificationsModel::instance()->lastUpdateSeen();
            }

            runtime_.setPlansEnabled(res.hello().plansEnabled());

            if (server_supports_updated_id_ && !updated_id_sync_initialized_) {
                LOG_INFO_N << "Server supports updated_id sync, but local baseline is not initialized. Forcing one full sync.";
                full_sync = true;
                needs_sync = true;
            } else if (!server_supports_updated_id_ && updated_id_sync_initialized_) {
                LOG_INFO_N << "Server does not support updated_id sync. Falling back to legacy since-based sync.";
            }
        } else {
            LOG_ERROR <<  "Server did not send a Hello message!";
            goto failed;
        }
        LOG_INFO << "Session started. Session-id: " << session_id_;
        addMessage(tr("Connected and authenticated with the server."));
    } else {
failed:
        bool retry = true;
        if (status_ != Status::MANUAL_OFFLINE) {
            setStatus(Status::ERROR);
        }

        LOG_ERROR_N << "Failed to start new session: " << res.message();
        if (res.error() == nextapp::pb::ErrorGadget::Error::NOT_FOUND) {
            addMessage(tr("*** The server does not recognize this device. You should re-run the signup process, select 'Add Device' and use a One Time Password (OTP) fom another devive to autorize it."));
            runtime_.showUnrecognizedDeviceError();
            retry = false;
        } else if (res.error() == nextapp::pb::ErrorGadget::Error::DEVICE_DISABLED) {
            addMessage(tr("*** This device has been disabled ***\nYou must enable it again from another device before it can be used."));
            retry = false;
        }

        stop();
        if (retry) {
            scheduleReconnect();
        }
        co_return;
    }

    setStatus(Status::INITIAL_SYNC);

    res = co_await rpc(nextapp::pb::Empty{}, &nextapp::pb::Nextapp::Client::GetServerInfo);

    if (res.error() != nextapp::pb::ErrorGadget::Error::OK) {
        goto failed;
    }

    if (res.hasServerInfo()) {
        const auto& se = res.serverInfo();
        server_version_ = se.properties().kv()["version"];
        server_id_ = se.properties().kv()["server-id"];
        LOG_INFO << "Connected to server version " << server_version_ << " at " << current_server_address_;
    } else {
        LOG_WARN << "We are connected to a server, but it did not send ServerInfo.";
    }

    nextapp::pb::UpdatesReq ureq;
    if (!full_sync && !needs_sync && session_start_publish_id_ >= last_seen_update_id_) {
        ureq.setFromMessageId(last_seen_update_id_);
    }
#if defined(ANDROID_BUILD) && defined(WITH_FCM)
    auto fcn_config = getPushConfig();
    fcm_requested_ = fcn_config.kind() != nextapp::pb::PushNotificationConfig::Kind::DISABLE;
    ureq.setWithPush(std::move(fcn_config));

    if (fcm_requested_) {
        LOG_DEBUG_N << "Sending Android FCM token: " << ureq.withPush().token().left(8) << "...";
        LOG_TRACE_N << "Full token: " << ureq.withPush().token();
    }
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    updates_ = client_->streamSubscribeToUpdates(ureq);
#else
    updates_ = client_->SubscribeToUpdates(ureq);
#endif

    auto *active_updates = updates_.get();
    connect(active_updates, &QGrpcServerStream::messageReceived, this, [this, active_updates] {
        if (updates_.get() != active_updates) {
            LOG_DEBUG_N << "Ignoring messageReceived signal from stale update stream.";
            return;
        }
        onUpdateMessage();
    });
    connect(active_updates, &QGrpcServerStream::finished, this, [this, active_updates] (const QGrpcStatus &status) {
        if (updates_.get() != active_updates) {
            LOG_DEBUG_N << "Ignoring finished signal from stale update stream: " << status.message();
            return;
        }
        if (status_ == Status::ONLINE) {
            LOG_WARN << "Server stream finished: " << status.message();
            setStatus(Status::ERROR);
            addMessage(tr("Server stream finished: %1").arg(toString(status)));
            scheduleReconnect();
        } else {
            LOG_DEBUG_N << "Server stream finished while status is "<< status_ << ": " << status.message();
        }
    });

    std::unique_ptr<DbStore> staged_db;
    const auto configure_sync_targets = [&](DbStore* db_override, bool load_after_sync) {
        GreenDaysModel::instance()->setSyncDbOverride(db_override);
        GreenDaysModel::instance()->setLoadAfterSync(load_after_sync);
        MainTreeModel::instance()->setSyncDbOverride(db_override);
        MainTreeModel::instance()->setLoadAfterSync(load_after_sync);
        ActionCategoriesModel::instance().setSyncDbOverride(db_override);
        ActionCategoriesModel::instance().setLoadAfterSync(load_after_sync);
        ActionInfoCache::instance()->setSyncDbOverride(db_override);
        ActionInfoCache::instance()->setLoadAfterSync(load_after_sync);
        WorkCache::instance()->setSyncDbOverride(db_override);
        WorkCache::instance()->setLoadAfterSync(load_after_sync);
        CalendarCache::instance()->setSyncDbOverride(db_override);
        CalendarCache::instance()->setLoadAfterSync(load_after_sync);
        NotificationsModel::instance()->setSyncDbOverride(db_override);
        NotificationsModel::instance()->setLoadAfterSync(load_after_sync);
    };
    ScopedExit reset_sync_targets{[&] {
        configure_sync_targets(nullptr, true);
    }};
    const auto abort_staged_full_sync = [&]() -> QCoro::Task<void> {
        if (!staged_db) {
            co_return;
        }

        (void) co_await staged_db->legacyQuery("ROLLBACK");
        co_await staged_db->closeAndDeleteDb();
        staged_db.reset();
        configure_sync_targets(nullptr, true);
    };

    if (needs_sync || full_sync) {
        if (full_sync) {
            LOG_WARN << "Resyncing from the server using a staged local database.";
            addMessage(tr("Doing a full synch with the server. This may take a few moments..."));
            staged_db = std::make_unique<DbStore>(QStringLiteral("db.staging.sqlite"));
            co_await staged_db->init();
            if (!co_await staged_db->legacyQuery("SELECT 1")) {
                LOG_WARN_N << "Failed to initialize staged database connection.";
                co_await abort_staged_full_sync();
                goto failed;
            }
            if (!co_await staged_db->clear()) {
                LOG_WARN_N << "Failed to initialize staged database.";
                co_await abort_staged_full_sync();
                goto failed;
            }
            configure_sync_targets(staged_db.get(), false);
            if (!co_await staged_db->legacyQuery("BEGIN IMMEDIATE")) {
                LOG_WARN_N << "Failed to start transaction for staged full sync.";
                co_await abort_staged_full_sync();
                goto failed;
            }
        }

        // Do the initial synch.
        LOG_DEBUG_N << "Fetching data versions...";
        if (!co_await getDataVersions()) {
            LOG_WARN_N << "Failed to get data versions.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        LOG_DEBUG_N << "Fetching global settings...";
        if (!co_await fetchGlobalSettings(full_sync)) {
            LOG_WARN_N << "Failed to get global settings.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        addMessage(tr("Fetching green days"));
        LOG_DEBUG_N << "Fetching green days...";
        if (!co_await GreenDaysModel::instance()->synchFromServer()) {
            LOG_WARN_N << "Failed to get green days.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        addMessage(tr("Fetching lists"));
        LOG_DEBUG_N << "Fetching nodes...";
        if (!co_await MainTreeModel::instance()->doSynch(full_sync)) {
            LOG_WARN_N << "Failed to get nodes.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        LOG_DEBUG_N << "Fetching  action categories...";
        if (!co_await ActionCategoriesModel::instance().synch(full_sync)) {
            LOG_WARN_N << "Failed to get action categories.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        addMessage(tr("Fetching actions"));
        LOG_DEBUG_N << "Fetching  actions...";
        if (!co_await ActionInfoCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get action info.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        addMessage(tr("Fetching work sessions"));
        LOG_DEBUG_N << "Fetching work sessions...";
        if (!co_await WorkCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get work sessions.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        addMessage(tr("Fetching time blocks for the calendar"));
        LOG_DEBUG_N << "Fetching time blocks...";
        if (!co_await CalendarCache::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get time-blocks for the calendar.";
            co_await abort_staged_full_sync();
            goto failed;
        }

        if (full_sync) {
            nextapp::pb::ResetPlaybackReq req;
            req.setInstanceId(AppInstanceMgr::instance()->instanceId());
            co_await rpc(req, &nextapp::pb::Nextapp::Client::ResetPlayback);
        }

        addMessage(tr("Fetching notifications"));
        LOG_DEBUG_N << "Fetching notifications...";
        if (!co_await NotificationsModel::instance()->synch(full_sync)) {
            LOG_WARN_N << "Failed to get notifications.";
            co_await abort_staged_full_sync();
            goto failed;
        }
        NotificationsModel::instance()->setLastUpdateSeen(
            std::max(NotificationsModel::instance()->lastUpdateSeen(), hello_last_notification));

        if (full_sync) {
            if (!co_await staged_db->legacyQuery("COMMIT")) {
                LOG_WARN_N << "Failed to commit staged full sync transaction.";
                co_await abort_staged_full_sync();
                goto failed;
            }

            staged_db->close();
            if (!co_await runtime_.db().replaceWithDatabaseFile(staged_db->dbPath())) {
                LOG_WARN_N << "Failed to replace live database with staged full sync database.";
                QFile::remove(staged_db->dbPath());
                staged_db.reset();
                configure_sync_targets(nullptr, true);
                goto failed;
            }

            staged_db.reset();
            configure_sync_targets(nullptr, true);

            if (!co_await GreenDaysModel::instance()->loadFromCache()) {
                LOG_WARN_N << "Failed to load green days from swapped full-sync database.";
                goto failed;
            }

            if (!co_await MainTreeModel::instance()->doLoadLocally()) {
                LOG_WARN_N << "Failed to load nodes from swapped full-sync database.";
                goto failed;
            }

            if (!co_await ActionCategoriesModel::instance().loadFromDb()) {
                LOG_WARN_N << "Failed to load action categories from swapped full-sync database.";
                goto failed;
            }

            if (!co_await ActionInfoCache::instance()->loadLocally()) {
                LOG_WARN_N << "Failed to load actions from swapped full-sync database.";
                goto failed;
            }

            if (!co_await WorkCache::instance()->loadLocally()) {
                LOG_WARN_N << "Failed to load work sessions from swapped full-sync database.";
                goto failed;
            }

            if (!co_await CalendarCache::instance()->loadLocally(true)) {
                LOG_WARN_N << "Failed to load calendar data from swapped full-sync database.";
                goto failed;
            }

            if (!co_await NotificationsModel::instance()->loadLocally()) {
                LOG_WARN_N << "Failed to load notifications from swapped full-sync database.";
                goto failed;
            }
        }

    } else {
        LOG_INFO << "Omitting server-sync as the local cache is up to date.";
        // // Omit this if we are just reconnecting?
        // emit globalSettingsChanged();

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

        if (notification_sync_target) {
            LOG_DEBUG_N << "Fetching notifications...";
            if (!co_await NotificationsModel::instance()->synch(false)) {
                LOG_WARN_N << "Failed to get notifications.";
                goto failed;
            }
            NotificationsModel::instance()->setLastUpdateSeen(
                std::max(NotificationsModel::instance()->lastUpdateSeen(), notification_sync_target));
            co_await updateLastReadNotification();
        } else {
            if (!co_await NotificationsModel::instance()->loadLocally()) {
                LOG_WARN_N << "Failed to load notifications locally";
                goto failed;
            }
        }
    }

    if ((needs_sync || full_sync)
        && (server_reset_detected || last_seen_update_id_ < session_start_publish_id_)) {
        last_seen_update_id_ = session_start_publish_id_;
        last_seen_user_publish_epoch_ = session_user_publish_epoch_;
        saveValuesToFile(runtime_, last_seen_update_id_, last_seen_server_instance_,
                         last_seen_user_publish_epoch_, "completed sync baseline");
    }

    co_await replayDeferredUpdates();

    if (userGlobalSettings_.version() == 0) {
        initGlobalSettings();
    }

    onGrpcReady();

    addMessage(tr("Retrying any pending server-reqests..."));
    LOG_DEBUG_N << "Retrying server requests...";
    co_await retryRequests();

    // If we re-run this later, we don't need to do a full sync again.
    runtime_.db().clearDbInitializedFlag();
    if (full_sync) {
        settings().setValue("sync/resync", "false");
        if (server_supports_updated_id_) {
            settings().setValue("sync/updatedIdInitialized", true);
            updated_id_sync_initialized_ = true;
        }
        co_await updateDataEpoc();
    }

    // execute later
    QTimer::singleShot(0, [this] {
        LOG_TRACE_N << "Emitting dataUpdated";
        emit dataUpdated();
    });

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

QCoro::Task<bool> ServerComm::fetchGlobalSettings(bool overwrite)
{
    auto res = co_await rpc(nextapp::pb::Empty{}, &nextapp::pb::Nextapp::Client::GetUserGlobalSettings);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasUserGlobalSettings()) {
            if (overwrite || (res.userGlobalSettings().version() >= userGlobalSettings_.version())) {
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
    if (!canConnect()) {
        LOG_TRACE_N << "Cannot connect now.";
        return false;
    }

    const auto level = settings().value("server/reconnect_level", 0).toInt();
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
    auto& db = runtime_.db();
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
        co_return false;
    }

    ++queued_requests_count_;
    co_return true;
}

// Execute the request (send it to the server)
// If it is successful, remove the request from the database.
QCoro::Task<bool> ServerComm::execute(const QueuedRequest &qr, bool deleteRequest)
{
    ++executing_request_count_;
    //assert(executing_request_count_ == 1);

    ScopedExit check_for_more{[this] {
        if (queued_requests_count_ > 0) {
            LOG_DEBUG_N << "There are more queued requests. Scheduling execution.";
            QTimer::singleShot(0, this, &ServerComm::retryRequests);
        }
    }};

    ScopedExit decr{[this] {
        --executing_request_count_;
        if (executing_request_count_ < 0) {
            LOG_ERROR_N << "Queued request execution counter underflowed.";
            executing_request_count_ = 0;
        }
    }};

    GrpcCallOptions options;
    options.enable_queue = true; // Informative

    grpc_metadata_t metadata;

    // Don't enable replay protection for direct requests that are not stored in the database.
    if (deleteRequest) {
        if (qr.id <= 0) {
            LOG_ERROR_N << "Cannot execute queued request without a persisted request id. uuid="
                        << qr.uuid.toString();
            co_return false;
        }
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
            LOG_ERROR_N << "Invalid request type.";
            co_return false;
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
        case  QueuedRequest::Type::CREATE_ACTION_CATEGORY: {
            const auto req = deserialize<nextapp::pb::ActionCategory>(qr.data);
            res = co_await rpc(req, &nextapp::pb::Nextapp::Client::CreateActionCategory, options);
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
    // Only enter once at a time.
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

    auto& db = runtime_.db();
    uint current = 0;

    while (connected()) {
        const auto rval = co_await db.legacyQuery("SELECT id, uuid, time, tries, rpcid, data FROM requests ORDER BY id LIMIT 1");
        if (!rval) {
            LOG_WARN_N << "Failed to get the oldest request from the database. " << rval.error();
            co_return;
        }
        if (rval.value().isEmpty()) {
            if (current) {
                LOG_DEBUG_N << "No more requests to retry.";
            } else {
                LOG_TRACE_N << "No requests to retry.";
            }
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
        const auto max_ttl_hours = settings().value("server/reqests_ttl", 24*7).toInt();
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
        if (tries > settings().value("server/reqests_retries", 9).toInt()) {
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
    auto& db = runtime_.db();
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

void ServerComm::resetSignupStatus() {
    signup_status_ = SIGNUP_NOT_STARTED;
    stop();
    emit signupStatusChanged();
}

QCoro::Task<bool> ServerComm::needFullResync()
{
    if (settings().value("sync/resync", false).toBool()) {
        LOG_INFO_N << "Full resync is required as the user requested it.";
        co_return true;
    }

    if (runtime_.db().dbWasInitialized()) {
        LOG_INFO_N << "Full resync is required because the db was initialized.";
        co_return true;
    }

    auto& db = runtime_.db();
    if (auto val = co_await db.queryOne<quint32>("SELECT data_epoc FROM nextapp WHERE id=1")) {
        LOG_INFO_N << "Data epoc in the database is: " << val.value() <<
            ", app epoc is: " << nextapp::app_data_epoc;
        bool need_full_sync = val.value() != nextapp::app_data_epoc;
        if (need_full_sync) {
            LOG_INFO_N << "Data epoc mismatch. Full resync is required.";
        }
        co_return need_full_sync;
    }

    LOG_INFO_N << "No data epoc found in the database. Full resync is required.";
    co_return true;
}

void ServerComm::resync()
{
    LOG_INFO_N << tr("Resynching with the server.");
    settings().setValue("sync/resync", "true");

    if (resync_scheduled_) {
        LOG_DEBUG_N << "Full resync is already scheduled. Ignoring duplicate request.";
        return;
    }

    resync_scheduled_ = true;
    clearMessages();
    addMessage(tr("Initiating full resynch with the server"));
    emit resynching();
    stop();
    setStatus(Status::OFFLINE);

    QTimer::singleShot(1000, this, [this]() {
        resync_scheduled_ = false;
        if (closed_) {
            return;
        }

        if (status_ != Status::OFFLINE && status_ != Status::ERROR) {
            LOG_DEBUG_N << "Skipping scheduled resync start because status is " << status_;
            return;
        }

        LOG_DEBUG_N << "Starting resynch with the server.";
        start();
    });
}

bool ServerComm::canConnect() const noexcept
{
    // If the user manually took us offline, we must stay offline until they change their mind.
    if (status() == MANUAL_OFFLINE) {
        LOG_DEBUG_N << "Cannot connect because we are manually offline.";
        return false;
    }

    if (signup_status_ != SignupStatus::SIGNUP_OK && signup_status_ != SignupStatus::SIGNUP_SUCCESS) {
        LOG_DEBUG_N << "Cannot connect because we are not (fully) signed up.";
        return false;
    }

    return true;
}


QCoro::Task<std::optional<std::pair<QString, QString>>> ServerComm::createCsrAsync()
{
    std::optional<std::pair<QString, QString>> result;
    std::unique_ptr<QThread> thread(QThread::create([&]() {
        QString csr, key;
        try {
            std::tie(csr, key) = createCsr();
            result = std::make_pair(csr, key);
        } catch (const std::exception& e) {
            LOG_ERROR_N << "Failed to create CSR: " << e.what();
        }
    }));
    thread->start();

    co_await qCoro(thread.get()).waitForFinished();
    co_return result;
}

void ServerComm::updateVisualStatus()
{
    static const array<QString, 7> names = {
                                            tr("Disconnected"),
                                            tr("Offline"),
                                            tr("Readying"),
                                            tr("Connecting"),
                                            tr("Sync"),
                                            tr("Online"),
                                            tr("Error")};

    static const array<QString, 7> colors = {"gray", "orange", "gold", "yellow", "blue", "green", "red"};

    status_text_ = names.at(static_cast<int>(status_));
    status_color_ = colors.at(static_cast<int>(status_));
}

nextapp::pb::PushNotificationConfig ServerComm::getPushConfig()
{
    nextapp::pb::PushNotificationConfig config;
#if defined(ANDROID_BUILD) && defined(WITH_FCM)
    if (settings().value("push/updates", true).toBool()) {
        if (auto token = AndroidFcmBridge::instance().getToken(); !token.isEmpty()) {
            config.setToken(token);
            config.setKind(nextapp::pb::PushNotificationConfig::Kind::GOOGLE);
            return config;
        } else {
            LOG_WARN_N << "Push is enabled, but I have no FCM token, so can't enable it with the server.";
        }
    }
#endif

    config.setKind(nextapp::pb::PushNotificationConfig::Kind::DISABLE);
    return config;
}

QCoro::Task<void> ServerComm::changePushConfigOnServer()
{
#if defined(ANDROID_BUILD) && defined(WITH_FCM)
    auto config = getPushConfig();
    LOG_DEBUG_N << "Setting push notification config on the server to kind  : "
                << static_cast<int>(config.kind());
    co_await rpc(config, &nextapp::pb::Nextapp::Client::SetPushNotificationConfig);
    fcm_requested_ = config.kind() != nextapp::pb::PushNotificationConfig::Kind::DISABLE;
#endif
    co_return;
}
