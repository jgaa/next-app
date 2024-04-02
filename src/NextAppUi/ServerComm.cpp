
#include "ServerComm.h"
#include "logging.h"

#include <QSettings>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

using namespace std;

namespace {

auto createWorkEventReq(const QString& sessionId, nextapp::pb::WorkEvent::Kind kind) {
    nextapp::pb::AddWorkEventReq req;
    nextapp::pb::WorkEvent event;
    event.setKind(kind);
    req.setWorkSessionId(sessionId);
    req.setEvent(event);

    return req;
}

} // anon ns


ServerComm *ServerComm::instance_;

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client} {

    instance_ = this;

    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

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
            emit versionChanged();
            updates_ = client_->streamSubscribeToUpdates({});
            connect(updates_.get(), &QGrpcServerStream::messageReceived, this, &ServerComm::onUpdateMessage);
            onGrpcReady();
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
            emit receivedWorkSessions(ws);
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
    req.setEvent(event);
    req.setWorkSessionId(sessionId);

    callRpc<nextapp::pb::Status>([this, &req]() {
        return client_->AddWorkEvent(req);
    }, [this](const nextapp::pb::Status& status) {
        ;
    });
}


void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR_N << "Call to gRPC server failed: " << status.message();

    emit errorRecieved(tr("Call to gRPC server failed: %1").arg(status.message()));
}

void ServerComm::onGrpcReady()
{
    grpc_is_ready_ = true;
    for(; !grpc_queue_.empty(); grpc_queue_.pop()) {
        grpc_queue_.front()();
    }
}

void ServerComm::onUpdateMessage()
{
    LOG_TRACE_N << "Received an update...";
    try {
        auto msg = make_shared<nextapp::pb::Update>(updates_->read<nextapp::pb::Update>());
        LOG_TRACE << "Got update: " << msg->when().seconds();
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
