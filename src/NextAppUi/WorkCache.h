#pragma once

#include <map>

#include <QObject>
#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

class WorkCache : public QObject
    , public ServerSynchedCahce<nextapp::pb::WorkSession, WorkCache>
{
    Q_OBJECT
public:
    explicit WorkCache(QObject *parent = nullptr);

    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const QProtobufMessage& item) override;
    QCoro::Task<bool> loadFromCache() override;
    QCoro::Task<bool> loadSomeFromCache(std::optional<QString> id);
    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasWorkSessions();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasWork();
    }
    QList<nextapp::pb::WorkSession> getItems(const nextapp::pb::Status& status) override{
        return status.workSessions().sessions();
    }
    std::string_view itemName() const noexcept override {
        return "work_session";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;
    void clear() override;

signals:
    void WorkSessionAdded(const QUuid& item);
    void WorkSessionChanged(const QUuid& item);
    void WorkSessionDeleted(const QUuid& item);
    void stateChanged();

private:
    void purge();
    QCoro::Task<void> remove(const QUuid& id);

    std::map<QUuid, std::shared_ptr<nextapp::pb::WorkSession>> items_;
};
