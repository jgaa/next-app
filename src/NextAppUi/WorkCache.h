#pragma once

#include <map>
#include <vector>

#include <QObject>
#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

class WorkCache : public QObject
    , public ServerSynchedCahce<nextapp::pb::WorkSession, WorkCache>
{
    Q_OBJECT
public:
    struct Outcome {
        bool duration = false;
        bool paused = false;
    };

    // For the active work sessions when we have updated their durations.
    struct ActiveDurationChanged {
        QUuid id;
        bool duration = false;
        bool paused = false;
    };

    using active_duration_changes_t = std::vector<ActiveDurationChanged>;
    using active_t = std::vector<std::shared_ptr<nextapp::pb::WorkSession>>;

    explicit WorkCache(QObject *parent = nullptr);
    WorkCache(RuntimeServices& runtime, QObject *parent = nullptr);

    bool haveBatch() const noexcept override { return true; }
    QCoro::Task<bool> saveBatch(const QList<nextapp::pb::WorkSession>& items) override;
    QCoro::Task<bool> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const QProtobufMessage& item) override;
    QCoro::Task<bool> finalizeSyncPersistence() override;
    QCoro::Task<bool> loadFromCache() override;
    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasWorkSessions();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasWork() || update.hasAction();
    }
    QList<nextapp::pb::WorkSession> getItems(const nextapp::pb::Status& status) override{
        return status.workSessions().sessions();
    }
    std::string_view itemName() const noexcept override {
        return "work_session";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;
    void clear() override;

    QCoro::Task<std::vector<std::shared_ptr<nextapp::pb::WorkSession>>>
    getWorkSessions(nextapp::pb::GetWorkSessionsReq req);

    static WorkCache *instance() noexcept;

    const active_t& getActive() const noexcept {
        return active_;
    }

signals:
    void WorkSessionAdded(const QUuid& item);
    void WorkSessionChanged(const QUuid& item);
    void WorkSessionActionMoved(const QUuid& item);
    void WorkSessionDeleted(const QUuid& item);
    void stateChanged();

    // Only sessions whose displayed minutes changed; identified independently of row order.
    void activeDurationChanged(const active_duration_changes_t& changes);

    // The active work sessions has changed. Reset the UI model
    void activeChanged();

private:
    friend class tst_NextAppUiRuntime;
    RuntimeServices& runtime_;
    void purge();
    void onTimer();
    void updateSessionsDurations(qint64 now);
    QCoro::Task<bool> validateStoredWorkSessions();
    QCoro::Task<bool> remove(const QUuid& id);
    Outcome updateOutcome(nextapp::pb::WorkSession &work, qint64 now);

    std::map<QUuid, std::shared_ptr<nextapp::pb::WorkSession>> items_;    
    std::vector<std::shared_ptr<nextapp::pb::WorkSession>> active_;
    QTimer *timer_ = {};
};
