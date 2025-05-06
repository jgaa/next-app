#pragma once

#include <optional>
#include <map>
#include <set>

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QUuid>

#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

template <typename T>
T& getRef(T& v) {
    return v;
}

template <typename T>
T& getRef(T* v) {
    return *v;
}

template <typename T>
concept hasMemberFnUuid = requires(T t) {
    t.uuid();
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

    // Get an entry if it exitst in the cache.
    QCoro::Task<std::shared_ptr<nextapp::pb::ActionInfo>> get(const QString &action_uuid, bool fetch = false);
    QCoro::Task<std::shared_ptr<nextapp::pb::ActionInfo>> get(const QUuid &uuid, bool fetch = false);
    QCoro::Task<std::shared_ptr<nextapp::pb::Action>> getAction(const QUuid &action_uuid);
    std::shared_ptr<nextapp::pb::ActionInfo> getIfCached(const QUuid &action_uuid) {
        return get_(action_uuid);
    }

    bool online() const noexcept {
        return state() == State::VALID;
    }

    static float getScore(const nextapp::pb::ActionInfo& action);
    static float getScore(const nextapp::pb::Action& action);
    static QColor getScoreColor(double score);
    QCoro::Task<void> updateAllScores();

signals:
    void actionDeleted(const QUuid &uuid);
    // Added to cache
    void actionAdded(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);
    void actionChanged(const QUuid &uuid);
    void stateChanged();
    void cacheReloaded();

public:
    // ServerSynchedCahce overrides
    bool haveBatch() const noexcept override { return true;}
    QCoro::Task<bool> saveBatch(const QList<nextapp::pb::Action>& items) override;
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
    QCoro::Task<bool> fill(T& items, bool keepExisting = false) {
        for (auto& item : items) {
            // remove pointer type from item so we can access members in a uniform way
            auto &v = getRef(item); //std::is_pointer_v<decltype(item)> ? *item : item;
            if (keepExisting && v.action) {
                continue;
            }

            QUuid uuid;
            if constexpr (hasMemberFnUuid<decltype(v)>) {
                uuid = v.uuid();
            } else {
                uuid = v.uuid;
            }

            if (v.action = co_await get(uuid, true); !v.action) {
                LOG_WARN << "Cannot find action with id: " << uuid.toString();
            }
        }
        co_return true;
    }

private:
    std::shared_ptr<nextapp::pb::ActionInfo> get_(const QString &action_uuid);
    std::shared_ptr<nextapp::pb::ActionInfo> get_(const QUuid &action_uuid);

    // Fetches from db and adds to cache. Returns the item from the cache.
    QCoro::Task<bool> fetchFromDb(QUuid action_uuid);
    QCoro::Task<bool> updateTags(const nextapp::pb::Action& action);
    bool updateTagsDirect(const nextapp::pb::Action& action);

    static ActionInfoCache *instance_;

    /* The idea is to have two caches, one hot with all the items currently used,
     * and one cold with the most recent items organized as a LRU cache.
     */
    std::map<QUuid, std::shared_ptr<nextapp::pb::ActionInfo>> hot_cache_;
    std::atomic_bool updating_scores_{false};
};
