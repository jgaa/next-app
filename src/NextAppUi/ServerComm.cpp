
#include "ServerComm.h"
#include "logging.h"

#include <QDebug>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

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
    QGrpcChannelOptions channelOptions(QUrl("http://127.0.0.1:10321", QUrl::StrictMode));
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
#ifdef ASYNC_GRPC
    auto info_call = client_->GetServerInfo({});
    info_call->subscribe(this, [info_call, this]() {
            nextapp::pb::ServerInfo se = info_call->read<nextapp::pb::ServerInfo>();
            assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            LOG_INFO << "Connected to server version " << server_version_;
            emit versionChanged();
        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });

    auto day_color_defs_call = client_->GetDayColorDefinitions({});
    day_color_defs_call->subscribe(this, [day_color_defs_call, this]() {
            day_color_definitions_ = day_color_defs_call->read<nextapp::pb::DayColorDefinitions>();
            LOG_DEBUG_N << "Received " << day_color_definitions_.dayColors().size()
                        << " day-color definitions.";
            emit dayColorDefinitionsChanged();
        }, [this](QGrpcStatus status) {
            LOG_ERROR_N << "Comm error: " << status.message();
        });

#else
    {
        nextapp::pb::ServerInfo se;
        auto res = client_->GetServerInfo({}, &se);
        if (res == QGrpcStatus::Ok) {
            assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            LOG_INFO << "Connected to server version " << server_version_;
            emit versionChanged();
        }
    }

    {
        nextapp::pb::DayColorDefinitions dcd;
        auto res = client_->GetDayColorDefinitions({}, &dcd);
        if (res == QGrpcStatus::Ok) {
            day_color_definitions_ = dcd;
            LOG_DEBUG_N << "Received " << day_color_definitions_.dayColors().size()
                        << " day color definitions.";
            emit dayColorDefinitionsChanged();
        }
    }

#endif
}

QString ServerComm::version()
{
    emit errorRecieved({});
    return server_version_;
}

nextapp::pb::DayColorRepeated ServerComm::getDayColorsDefinitions()
{
    return day_color_definitions_.dayColors();
}

void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR_N << "Call to gRPC server failed: " << status.message();

    emit errorRecieved(tr("Call to gRPC server failed: %1").arg(status.message()));
}
