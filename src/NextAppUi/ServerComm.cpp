#include <iostream>
#include <string_view>
#include "ServerComm.h"
#include "logging.h"

#include <QSettings>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>
#include <QSslKey>
#include <QSslCertificate>

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

#include "NextAppCore.h"

using namespace std;

ostream& operator << (ostream&o, const ServerComm::Status& v) {
    static constexpr auto names = to_array<string_view>({
        "OFFLINE",
        "CONNECTING",
        "INITIAL_SYNC",
        "ONLINE",
        "ERROR"});

    return o << names.at(static_cast<size_t>(v));
}

namespace {

auto createWorkEventReq(const QString& sessionId, nextapp::pb::WorkEvent::Kind kind) {
    nextapp::pb::AddWorkEventReq req;
    nextapp::pb::WorkEvent event;
    event.setKind(kind);
    req.setWorkSessionId(sessionId);
    req.events().append(event);

    return req;
}

} // anon nssu.signUpResponse()


ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client}, signup_client_{new signup::pb::SignUp::Client}
{

    instance_ = this;

    QSettings settings;
    device_uuid_ = QUuid{settings.value("device/uuid", QString()).toString()};
    if (device_uuid_.isNull()) {
        device_uuid_ = QUuid::createUuid();
        settings.setValue("device/uuid", device_uuid_.toString());
        LOG_INFO << "Created new device-uuid for this device: " << device_uuid_.toString();
    }

    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

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
            start();
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
                }
            }, req);
        }
    });

    LOG_DEBUG << "Ping interval is " << ping_timer_interval_sec_ << " seconds";
    ping_timer_.start(ping_timer_interval_sec_ * 1000);
}

ServerComm::~ServerComm()
{
    instance_ = {};
}

void ServerComm::start()
{
    QSettings settings;
    session_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    current_server_address_ = settings.value("server/url", QString{}).toString();

    QGrpcMetadata metadata;
    metadata.emplace("session-id", session_id_.toLatin1());

    QSslConfiguration sslConfig;
    sslConfig.setPeerVerifyMode(QSslSocket::QueryPeer);
    //sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfig.setProtocol(QSsl::TlsV1_3);

    if (auto cert = settings.value("server/clientCert").toString(); !cert.isEmpty()) {
        LOG_DEBUG_N << "Loading our private client cert/key.";
        sslConfig.setLocalCertificate(QSslCertificate{cert.toUtf8()});
        sslConfig.setPrivateKey(QSslKey{settings.value("server/clientKey").toByteArray(), QSsl::Rsa});
        auto caCert = QSslCertificate{settings.value("server/caCert").toByteArray()};
        sslConfig.setCaCertificates({caCert});
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    }

    // For some reason, the standard GRPC server reqire ALPN to be configured when using TLS, even though
    // it only support https/2.
    sslConfig.setAllowedNextProtocols({{"h2"}});

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    auto channelOptions = QGrpcChannelOptions{QUrl(current_server_address_, QUrl::StrictMode)}
                              .withMetadata(metadata)
                              .withSslConfiguration(sslConfig);
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#else
    auto channelOptions = QGrpcChannelOptions{}
                              .setMetadata(metadata)
                              .setSslConfiguration(sslConfig);
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(QUrl(current_server_address_, QUrl::StrictMode), channelOptions));
#endif

    LOG_INFO << "Using server at " << current_server_address_;
    setStatus(Status::CONNECTING);

    callRpc<nextapp::pb::Status>([this]() {
        return client_->GetServerInfo({});
    }, [this](const nextapp::pb::Status& status) {
        if (status.error() != nextapp::pb::ErrorGadget::Error::OK) {
            LOG_ERROR << "Failed to connect to server: " << status.message();
            setStatus(Status::ERROR);
            return;
        }
        if (status.hasServerInfo()) {
            auto se = status.serverInfo();
            if (!se.properties().empty()) {
                setStatus(Status::INITIAL_SYNC);
                // assert(se.properties().front().key() == "version");
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
            initGlobalSettings();
        }
    }, GrpcCallOptions{false});
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
    req.date().setYear(year);
    req.date().setMonth(month);
    req.date().setMday(day);
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

    callRpc<nextapp::pb::Status>([this](nextapp::pb::CreateNodeReq req) {
        return client_->CreateNode(req);
    }, req);
}

void ServerComm::updateNode(const nextapp::pb::Node &node)
{
    callRpc<nextapp::pb::Status>([this](nextapp::pb::Node node) {
        return client_->UpdateNode(node);
    }, node);
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

    callRpc<nextapp::pb::Status>([this](nextapp::pb::MoveNodeReq req) {
        return client_->MoveNode(req);
    }, req);
}

void ServerComm::deleteNode(const QUuid &uuid)
{
    callRpc<nextapp::pb::Status>([this](QUuid uuid) {
        nextapp::pb::DeleteNodeReq req;
        req.setUuid(uuid.toString(QUuid::WithoutBraces));
        return client_->DeleteNode(req);
    }, uuid);
}

void ServerComm::getNodeTree()
{
    nextapp::pb::GetNodesReq req;

    callRpc<nextapp::pb::NodeTree>([this](nextapp::pb::GetNodesReq req) {
        return client_->GetNodes(req);
    }, [this](const nextapp::pb::NodeTree& tree) {
        emit receivedNodeTree(tree);
    }, req);
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

void ServerComm::getActions(nextapp::pb::GetActionsReq &filter)
{
    callRpc<nextapp::pb::Status>([this, filter]() {
        return client_->GetActions(filter);
    } , [this](const nextapp::pb::Status& status) {
        if (status.hasActions()) {
            auto actions = make_shared<nextapp::pb::Actions>(status.actions());
            emit receivedActions(actions,
                status.hasHasMore() && status.hasMore(),
                status.hasFromStart() && status.fromStart());
        }
    });
}

void ServerComm::getAction(nextapp::pb::GetActionReq &req)
{
    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->GetAction(req);
    }, [this](const nextapp::pb::Status& status) {
        emit receivedAction(status);
    });
}

void ServerComm::addAction(const nextapp::pb::Action &action)
{
    callRpc<nextapp::pb::Status>([this, action]() {
        return client_->CreateAction(action);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::updateAction(const nextapp::pb::Action &action)
{
    callRpc<nextapp::pb::Status>([this, action]() {
        return client_->UpdateAction(action);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::deleteAction(const QString &actionUuid)
{
    nextapp::pb::DeleteActionReq req;
    req.setActionId(actionUuid);

    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->DeleteAction(req);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::markActionAsDone(const QString &actionUuid, bool done)
{
    nextapp::pb::ActionDoneReq req;
    req.setUuid(actionUuid);
    req.setDone(done);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->MarkActionAsDone(req);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::markActionAsFavorite(const QString &actionUuid, bool favorite)
{
    nextapp::pb::ActionFavoriteReq req;
    req.setUuid(actionUuid);
    req.setFavorite(favorite);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->MarkActionAsFavorite(req);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
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

void ServerComm::startWork(const QString &actionId)
{
    nextapp::pb::CreateWorkReq req;
    req.setActionId(actionId);

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
    req.events().append(event);
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

    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->MoveAction(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::addTimeBlock(const nextapp::pb::TimeBlock &tb)
{
    callRpc<nextapp::pb::Status>([this, tb]() {
        return client_->CreateTimeblock(tb);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::updateTimeBlock(const nextapp::pb::TimeBlock &tb)
{
    callRpc<nextapp::pb::Status>([this, tb]() {
        return client_->UpdateTimeblock(tb);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::deleteTimeBlock(const QString &timeBlockUuid)
{
    nextapp::pb::DeleteTimeblockReq req;
    req.setId_proto(timeBlockUuid);

    callRpc<nextapp::pb::Status>([this, req]() {
        return client_->DeleteTimeblock(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
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
            done(CbError{nextapp::pb::ErrorGadget::GENERIC_ERROR, "Missing events"});
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
            done(CbError{nextapp::pb::ErrorGadget::GENERIC_ERROR, "Missing categories"});
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
    dev.setAppVersion(NEXTAPP_VERSION);
    dev.setCsr(csr);
    dev.setProductType(QSysInfo::productType());
    dev.setProductVersion(QSysInfo::productVersion());
    dev.setArch(QSysInfo::currentCpuArchitecture());
    dev.setPrettyName(QSysInfo::prettyProductName());

    auto finish = [this, key](const signup::pb::Reply& su) {
        LOG_DEBUG << "Received signup response from server ";

        QSettings settings;

        switch(su.error()) {
        case signup::pb::ErrorGadget::OK:
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
        case signup::pb::ErrorGadget::EMAIL_ALREADY_EXISTS:
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

    if (status.code() == QGrpcStatus::StatusCode::Cancelled) {
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
            emit globalSettingsChanged();
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
    }, opts);
}

void ServerComm::onGrpcReady()
{
    reconnect_after_seconds_ = 0;
    setStatus(Status::ONLINE);
    emit versionChanged();
    emit connectedChanged();
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
            LOG_TRACE << "Got update: " << msg->when().seconds();

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
            if (msg->hasDayColor()) {
                LOG_DEBUG << "Day color is " << msg->dayColor().color();
                QUuid color;
                if (!msg->dayColor().color().isEmpty()) {
                    color = QUuid{msg->dayColor().color()};
                }
                const auto& date = msg->dayColor().date();
                emit dayColorChanged(date.year(), date.month(), date.mday(), color);
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

#ifdef ANDROID
    userGlobalSettings_.setTimeZone("Europe/Sofia");
#else
    userGlobalSettings_.setTimeZone(QString::fromUtf8(chrono::current_zone()->name()));
#endif

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

    if (status.code() < errors.size()) {
        return errors[status.code()] + ": " + status.message();
    }

    return tr("UNKNOWN (code=%1): %2").arg(status.code()).arg(status.message());
}

void ServerComm::connectToSignupServer()
{
    QGrpcMetadata metadata;
    metadata.emplace("session-id", session_id_.toLatin1());
    QSslConfiguration sslConfig{QSslConfiguration::defaultConfiguration()};
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyPeer);
    sslConfig.setProtocol(QSsl::TlsV1_3);

    LOG_TRACE_N << "CA certs: ";
    for(const auto& c : sslConfig.caCertificates()) {
        LOG_TRACE_N << "  " << c.subjectDisplayName();
    }

    // For some reason, the standard GRPC server reqire ALPN to be configured when using TLS, even though
    // it only support https/2.
    sslConfig.setAllowedNextProtocols({{"h2"}});

    const bool use_tls = signup_server_address_.startsWith("https://");

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
    auto channelOptions = QGrpcChannelOptions{QUrl(signup_server_address_, QUrl::StrictMode)}
                              .withMetadata(metadata)
                              .withSslConfiguration(sslConfig);
    signup_client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#else
    auto channelOptions = QGrpcChannelOptions{}
                              .setMetadata(metadata);
    if (use_tls) {
        channelOptions.setSslConfiguration(sslConfig);
    }
    signup_client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(QUrl(signup_server_address_, QUrl::StrictMode), channelOptions));
#endif

    connect(signup_client_.get(), &signup::pb::SignUp::Client::errorOccurred, [this](const QGrpcStatus &status) {
        LOG_ERROR_N << "Connection to signup server failed: " << status.message();
        addMessage(tr("Connection to signup server failed: %1").arg(toString(status)));
        signup_status_ = SignupStatus::SIGNUP_ERROR;
        emit signupStatusChanged();
    });

    LOG_INFO << "Using signup server at " << signup_server_address_;

    callRpc<signup::pb::Reply>([this]() {
        setMessage(tr("Connecting to server ..."));
        return signup_client_->GetInfo({});
    }, [this](const signup::pb::Reply& reply) {
        if (reply.hasGetInfoResponse() && reply.error() == signup::pb::ErrorGadget::OK) {
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
