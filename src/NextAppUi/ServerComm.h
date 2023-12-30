#pragma once

#include <qqmlregistration.h>

#include <memory>

#include "nextapp_client.grpc.qpb.h"

#include <QObject>

class ServerComm : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString version READ version  NOTIFY versionChanged)
public:
    explicit ServerComm();

    void start();

    [[nodiscard]] QString version();

signals:
    void versionChanged();

private:
    void errorOccurred();
    void errorRecieved(const QString &value);
    void onServerInfo(nextapp::pb::ServerInfo info);

    std::unique_ptr<nextapp::pb::Nextapp::Client> client_;
    nextapp::pb::ServerInfo server_info_;
    QString server_version_{"Unknown"};
};
