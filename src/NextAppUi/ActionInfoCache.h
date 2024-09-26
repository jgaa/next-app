#pragma once

#include <optional>
#include <map>
#include <set>

#include <QObject>
#include <QQmlEngine>
#include <QUuid>

#include "nextapp.qpb.h"

class ActionInfoCache;

class ActionInfoPrx : public QObject {
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
{
    Q_OBJECT
    QML_ELEMENT
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
        return online_;
    }

    void setOnline(bool online);

    void actionWasAdded(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);

signals:
    void onlineChanged();
    void actionDeleted(const QUuid &uuid);
    void actionReceived(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);
    void actionChanged(const QUuid &uuid);

private:
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions, bool more, bool first);
    void receivedAction(const nextapp::pb::Status& status);

    std::shared_ptr<nextapp::pb::ActionInfo>& get_(const QString &action_uuid);
    std::shared_ptr<nextapp::pb::ActionInfo>& get_(const QUuid &action_uuid);
    void fetch(const QUuid &action_uuid);

    bool online_;
    static ActionInfoCache *instance_;

    /* The idea is to have two caches, one hot with all the items currently used,
     * and one cold with the most recent items organized as a LRU cache.
     * In time we should probably add a third cache in a local sqlite database and
     * keep it syncronized with the server.
     */
    std::map<QUuid, std::shared_ptr<nextapp::pb::ActionInfo>> hot_cache_;
    std::set<QUuid> pending_fetch_;
};
