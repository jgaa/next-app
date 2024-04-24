
#include "ServerComm.h"
#include "logging.h"

#include <QSettings>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

#include "NextAppCore.h"

using namespace std;

namespace {

auto createWorkEventReq(const QString& sessionId, nextapp::pb::WorkEvent::Kind kind) {
    nextapp::pb::AddWorkEventReq req;
    nextapp::pb::WorkEvent event;
    event.setKind(kind);
    req.setWorkSessionId(sessionId);
    req.events().append(event);

    return req;
}

} // anon ns


ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client} {

    instance_ = this;

    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

    // TODO: Make it configurable if the comm starts immediately.
    //       For example, the user may need to select a server first.

    setDefaulValuesInUserSettings();

    start();
}

ServerComm::~ServerComm()
{
    instance_ = {};
}

void ServerComm::start()
{   
    current_server_address_ = QSettings{}.value("serverAddress", getDefaultServerAddress()).toString();
    QGrpcChannelOptions channelOptions(QUrl(current_server_address_ , QUrl::StrictMode));
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
    LOG_INFO << "Using server at " << current_server_address_;

    callRpc<nextapp::pb::ServerInfo>([this]() {
        return client_->GetServerInfo({});
    }, [this](const nextapp::pb::ServerInfo& se) {
        if (!se.properties().empty()) {
            // assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            LOG_INFO << "Connected to server version " << server_version_ << " at " << current_server_address_;
            updates_ = client_->streamSubscribeToUpdates({});
            connect(updates_.get(), &QGrpcServerStream::messageReceived, this, &ServerComm::onUpdateMessage);
            initGlobalSettings();
        }
    }, GrpcCallOptions{false});
}

void ServerComm::stop()
{

}

QString ServerComm::version()
{
    //emit errorRecieved({});
    return server_version_;
}

bool ServerComm::connected()
{
    return grpc_is_ready_;
}

void ServerComm::reloadSettings()
{
    QTimer::singleShot(200, [this] {
        auto server_address = QSettings{}.value("serverAddress", getDefaultServerAddress()).toString();
        if (server_address != current_server_address_) {
            LOG_INFO_N << "Server address change from " << current_server_address_
                       << " to " << server_address
                       << ". I will re-connect.";
            start();
        }
    });
}

void ServerComm::getColorsInMonth(unsigned int year, unsigned int month)
{
    nextapp::pb::MonthReq req;
    req.setYear(year);
    req.setMonth(month);

    callRpc<nextapp::pb::Month>([this](nextapp::pb::MonthReq req) {
        return client_->GetMonth(req);
    } , [this, y=year, m=month](const nextapp::pb::Month& month) {
        LOG_TRACE << "Received colors for " << month.days().size()
                  << " days for month: " << y << "-" << (m + 1);

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
    callRpc<nextapp::pb::Status>([this, &filter]() {
        return client_->GetActions(filter);
    } , [this](const nextapp::pb::Status& status) {
        if (status.hasActions()) {
            auto actions = make_shared<nextapp::pb::Actions>(status.actions());
            emit receivedActions(actions);
        }
    });
}

void ServerComm::getAction(nextapp::pb::GetActionReq &req)
{
    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->GetAction(req);
    }, [this](const nextapp::pb::Status& status) {
        emit receivedAction(status);
    });
}

void ServerComm::addAction(const nextapp::pb::Action &action)
{
    callRpc<nextapp::pb::Status>([this, &action]() {
        return client_->CreateAction(action);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::updateAction(const nextapp::pb::Action &action)
{
    callRpc<nextapp::pb::Status>([this, &action]() {
        return client_->UpdateAction(action);
    } , [this](const nextapp::pb::Status& status) {
        ;
    });
}

void ServerComm::deleteAction(const QString &actionUuid)
{
    nextapp::pb::DeleteActionReq req;
    req.setActionId(actionUuid);

    callRpc<nextapp::pb::Status>([this, &req]() {
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

nextapp::pb::UserGlobalSettings ServerComm::getGlobalSettings() const
{
    return userGlobalSettings_;
}

void ServerComm::saveGlobalSettings(const nextapp::pb::UserGlobalSettings &settings)
{
    LOG_DEBUG_N << "Saving global user-settings";
    callRpc<nextapp::pb::Status>([this, settings]() {
        return client_->SetUserGlobalSettings(settings);
    }, [this](const nextapp::pb::Status& status) {
        // if (status.hasUserGlobalSettings()) {
        //     userGlobalSettings_ = status.userGlobalSettings();
        //     emit globalSettingsChanged();
        // }
    });
}

void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR_N << "Call to gRPC server failed: " << status.message();

    emit errorRecieved(tr("Call to gRPC server failed: %1").arg(status.message()));
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
            stop();
            return;
        }
        onGrpcReady();
    }, opts);
}

void ServerComm::onGrpcReady()
{
    grpc_is_ready_ = true;
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
        auto msg = make_shared<nextapp::pb::Update>(updates_->read<nextapp::pb::Update>());
        LOG_TRACE << "Got update: " << msg->when().seconds();

        if (msg->hasUserGlobalSettings()) {
            LOG_DEBUG << "Received global user-settings";
            userGlobalSettings_ = msg->userGlobalSettings();
            emit globalSettingsChanged();
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

    userGlobalSettings_.setTimeZone(QString::fromUtf8(chrono::current_zone()->name()));

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
