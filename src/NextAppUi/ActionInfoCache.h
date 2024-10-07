#pragma once

#include <optional>
#include <map>
#include <set>

#include <QObject>
#include <QQmlEngine>
#include <QUuid>

#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

class ActionInfoCache;

class ActionInfoPrx : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(nextapp::pb::ActionInfo *action READ getAction NOTIFY actionChanged)

public:
    ActionInfoPrx(QUuid actionUuid, ActionInfoCache *model);

    const nextapp::pb::ActionInfo* getActionInfo(const QUuid &uuid);

    void setAction(const std::shared_ptr<nextapp::pb::ActionInfo>& action) {
        action_ = action;
        emit actionChanged();
    }

    void actionDeleted(QUuid uuid) {
        if (uuid_ == uuid) {
            //valid_ = false;
            action_.reset();
            emit actionChanged();
        }
    }

    nextapp::pb::ActionInfo *getAction() {
        return action_.get();
    }

    void onActionChanged(const QUuid &uuid);

signals:
    void actionChanged();

private:
    //bool valid_{true};
    QUuid uuid_;
    std::shared_ptr<nextapp::pb::ActionInfo> action_;
};


class ActionInfoCache : public QObject
    , public ServerSynchedCahce<nextapp::pb::Action, ActionInfoCache>
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid READ valid NOTIFY stateChanged)

public:
    ActionInfoCache(QObject *parent = nullptr);

    static ActionInfoCache *instance() {
        assert(instance_);
        return instance_;
    }

    Q_INVOKABLE ActionInfoPrx *getAction(QString uuid);

    // Get an entry if it exitst in the cache.
    std::shared_ptr<nextapp::pb::ActionInfo> get(const QString &action_uuid, bool fetch = false);

    bool online() const noexcept {
        return state() == State::VALID;
    }

    void actionWasAdded(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);

signals:
    void actionDeleted(const QUuid &uuid);
    void actionReceived(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);
    void actionChanged(const QUuid &uuid);
    void stateChanged();

public:
    // ServerSynchedCahce overrides
    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const QProtobufMessage& item) override;
    QCoro::Task<bool> loadFromCache() override;
    QCoro::Task<bool> loadSomeFromCache(std::optional<QString> id);
    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasCompleteActions();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasAction();
    }
    QList<nextapp::pb::Action> getItems(const nextapp::pb::Status& status) override{
        return status.completeActions().actions();
    }
    std::string_view itemName() const noexcept override {
        return "action";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;
    void clear() override;

    template <typename T>
    QCoro::Task<bool> fill(T& items) {
        for (auto& item : items) {
            const QUuid& id = item.uuid;
            if (auto a = get_(id)) {
                item.action = a;
                continue;
            }
            co_await fetchFromDb(id);
            if (auto a = get_(id)) {
                item.action = a;
            } else {
                LOG_WARN << "Cannot find action with id: " << id.toString()
                         << " in the db.";
            }
        }
        co_return true;
    }


private:
    //void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    //void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions, bool more, bool first);
    //void receivedAction(const nextapp::pb::Status& status);

    std::shared_ptr<nextapp::pb::ActionInfo>& get_(const QString &action_uuid);
    std::shared_ptr<nextapp::pb::ActionInfo>& get_(const QUuid &action_uuid);

    // Fetches from db and adds to cache. Returns the item from the cache.
    QCoro::Task<void> fetchFromDb(QUuid action_uuid);

    static ActionInfoCache *instance_;

    /* The idea is to have two caches, one hot with all the items currently used,
     * and one cold with the most recent items organized as a LRU cache.
     * In time we should probably add a third cache in a local sqlite database and
     * keep it syncronized with the server.
     */
    std::map<QUuid, std::shared_ptr<nextapp::pb::ActionInfo>> hot_cache_;
    //std::set<QUuid> pending_fetch_;
};
