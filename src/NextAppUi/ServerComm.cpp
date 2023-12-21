
#include "ServerComm.h"

#include <QDebug>
#include <QGrpcHttp2Channel>

ServerComm::ServerComm()
    : client_{new nextapp::pb::Nextapp::Client} {

    connect(client_.get(), &nextapp::pb::Nextapp::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

}

void ServerComm::start()
{
    QGrpcChannelOptions channelOptions(QUrl("http://127.0.0.1:10321", QUrl::StrictMode));
    client_->attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));

    auto x = client_->GetServerInfo({});
    x->subscribe(this, [x, this]() {
            nextapp::pb::ServerInfo se = x->read<nextapp::pb::ServerInfo>();
            assert(se.properties().front().key() == "version");
            server_version_ = se.properties().front().value();
            emit versionChanged();
        }, [this](QGrpcStatus status) {
            qWarning() << "Comm error: " << status.message();
        });
}

QString ServerComm::version()
{
    emit errorRecieved({});
    return {"wait..."};
}

void ServerComm::errorOccurred()
{
    qWarning() << "Connection error occurred.";

    emit errorRecieved("No connection\nto\nserver");
}

void ServerComm::errorRecieved(const QString &value)
{

}
