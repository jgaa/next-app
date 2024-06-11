#include "ActionInfoCache.h"
#include "ServerComm.h"
#include "util.h"

#include "logging.h"

namespace {

template <typename T, typename C>
bool add(C& container, const T& action, ActionInfoCache *cache = {}) {
    const auto uuid = toQuid(action.id_proto());
    bool changed = false;

    bool added = false;
    auto it = container.find(uuid);
    if (it == container.end()) {
        it = container.emplace(uuid, std::make_shared<nextapp::pb::ActionInfo>()).first;
        added = true;
    }

    auto &existing = *it->second;

    if (existing.version() >= action.version()) {
        return false;
    }

    if (existing.node() != action.node()) {
        existing.setNode(action.node());
        changed = true;
    }

    if (existing.name() != action.name()) {
        existing.setName(action.name());
        changed = true;
    }

    if (existing.priority() != action.priority()) {
        existing.setPriority(action.priority());
        changed = true;
    }

    if (existing.status() != action.status()) {
        existing.setStatus(action.status());
        changed = true;
    }

    if (existing.favorite() != action.favorite()) {
        existing.setFavorite(action.favorite());
        changed = true;
    }

    if (existing.due() != action.due()) {
        existing.setDue(action.due());
        changed = true;
    }

    if (existing.completedTime() != action.completedTime()) {
        existing.setCompletedTime(action.completedTime());
        changed = true;
    }

    if (existing.kind() != action.kind()) {
        existing.setKind(action.kind());
        changed = true;
    }

    if (existing.version() != action.version()) {
        existing.setVersion(action.version());
        changed = true;
    }

    if (existing.category() != action.category()) {
        existing.setCategory(action.category());
        changed = true;
    }

    if (added && cache) {
        cache->actionWasAdded(it->second);
    }

    return changed;
}

} // anon ns

ActionInfoCache *ActionInfoCache::instance_;

ActionInfoCache::ActionInfoCache(QObject *parent)
: QObject(parent)
{
    assert(!instance_);
    instance_ = this;

    connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
        setOnline(ServerComm::instance().connected());
    });

    connect(&ServerComm::instance(), &ServerComm::receivedActions, this, &ActionInfoCache::receivedActions);
    connect(&ServerComm::instance(), &ServerComm::receivedAction, this, &ActionInfoCache::receivedAction);
    connect(&ServerComm::instance(), &ServerComm::onUpdate, this, &ActionInfoCache::onUpdate);
}

ActionInfoPrx *ActionInfoCache::getAction(QString uuidStr)
{
    auto uuid = toQuid(uuidStr);
    auto ai = std::make_unique<ActionInfoPrx>(uuid, this);

    if (auto& existing = get_(uuid)) {
        ai->setAction(existing);
    } else {
        fetch(uuid);
    }

    QQmlEngine::setObjectOwnership(ai.get(), QQmlEngine::JavaScriptOwnership);
    return ai.release();
}

std::shared_ptr<nextapp::pb::ActionInfo> ActionInfoCache::get(const QString &action_uuid, bool doFetch)
{
    const auto uuid = toQuid(action_uuid);
    if (auto action = get_(uuid)) {
        return action;
    }

    if (doFetch) {
        fetch(uuid);
    }

    return {};
}

void ActionInfoCache::setOnline(bool online) {
    if (online_ == online) {
        return;
    }
    online_ = online;
    emit onlineChanged();
}

void ActionInfoCache::actionWasAdded(const std::shared_ptr<nextapp::pb::ActionInfo> &ai)
{
    emit actionReceived(ai);
}


void ActionInfoCache::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    const bool deleted = update->op() == nextapp::pb::Update::Operation::DELETED;

    if (update->hasAction()) {
        auto &action = update->action();
        const auto uuid = toQuid(action.id_proto());
        if (deleted) [[unlikely ]] {
            emit actionDeleted(uuid);
            hot_cache_.erase(uuid);
        } else {
            if (add(hot_cache_, action)) {
                emit actionChanged(uuid);
            }
        }
    }
}

void ActionInfoCache::receivedActions(const std::shared_ptr<nextapp::pb::Actions> &actions, bool more, bool first)
{
    for (const auto &action : actions->actions()) {
        if (add(hot_cache_, action)) {
            emit actionChanged(toQuid(action.id_proto()));
        }
    }
}

void ActionInfoCache::receivedAction(const nextapp::pb::Status &status)
{
    if (status.hasAction()) {
        if (add(hot_cache_, status.action(), this)) {
            emit actionChanged(toQuid(status.action().id_proto()));
        }
    }
}

std::shared_ptr<nextapp::pb::ActionInfo> &ActionInfoCache::get_(const QUuid &action_uuid)
{
    static std::shared_ptr<nextapp::pb::ActionInfo> empty_action;
    assert(!empty_action);

    auto it = hot_cache_.find(action_uuid);
    if (it != hot_cache_.end()) {
        return it->second;
    }

    return empty_action;
}

void ActionInfoCache::fetch(const QUuid &action_uuid)
{
    if (pending_fetch_.count(action_uuid)) {
        return;
    }

#ifdef _DEBUG
    assert(!get_(action_uuid));
#endif

    pending_fetch_.insert(action_uuid);

    nextapp::pb::GetActionReq req;
    req.setUuid(action_uuid.toString(QUuid::WithoutBraces));
    ServerComm::instance().getAction(req);
}

std::shared_ptr<nextapp::pb::ActionInfo> &ActionInfoCache::get_(const QString &action_uuid)
{
    return get_(toQuid(action_uuid));
}


ActionInfoPrx::ActionInfoPrx(QUuid actionUuid, ActionInfoCache *model)
: uuid_{actionUuid}
{
    connect(model, &ActionInfoCache::actionReceived, this, &ActionInfoPrx::setAction);
    connect(model, &ActionInfoCache::actionDeleted, this, &ActionInfoPrx::actionDeleted);
    connect(model, &ActionInfoCache::actionChanged, this, &ActionInfoPrx::onActionChanged);
}

const nextapp::pb::ActionInfo *ActionInfoPrx::getActionInfo(const QUuid &uuid)
{
    return action_.get();
}

void ActionInfoPrx::onActionChanged(const QUuid &uuid) {
    if (uuid_ == uuid) {
        if (!action_) {
            auto action = ActionInfoCache::instance()->get(uuid_.toString(QUuid::WithoutBraces));
            if (action) {
                setAction(std::move(action));
            }
        }
        emit actionChanged();
    }
}
