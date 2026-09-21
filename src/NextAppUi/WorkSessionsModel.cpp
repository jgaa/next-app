#include <regex>

#include "WorkSessionsModel.h"
#include "ActionsModel.h"
#include "ServerCommAccess.h"
#include "NextAppCore.h"
#include "WorkCache.h"
#include "logging.h"
#include <algorithm>
#include <iterator>
#include "util.h"

#include "QDateTime"

using namespace std;


WorkSessionsModel* WorkSessionsModel::instance_;

WorkSessionsModel::WorkSessionsModel(QObject *parent)
    : WorkSessionsModel(*NextAppCore::instance(), parent)
{
}

WorkSessionsModel::WorkSessionsModel(RuntimeServices& runtime, QObject *parent)
    : WorkSessionsModel(runtime, *WorkCache::instance(), parent)
{
}

WorkSessionsModel::WorkSessionsModel(RuntimeServices& runtime, WorkCache& cache, QObject *parent)
    : WorkModelBase{runtime, parent}
    , cache_{cache}
{
    assert(instance_ == nullptr);
    instance_ = this;

    connect(&cache_, &WorkCache::activeChanged, this, &WorkSessionsModel::fetchIf);
    connect(&cache_, &WorkCache::WorkSessionAdded, this, &WorkSessionsModel::fetchIf);
    connect(&cache_, &WorkCache::WorkSessionChanged, this, &WorkSessionsModel::fetchIf);
    connect(&cache_, &WorkCache::WorkSessionActionMoved, this, &WorkSessionsModel::fetchIf);
    connect(&cache_, &WorkCache::WorkSessionDeleted, this, &WorkSessionsModel::fetchIf);
    connect(&cache_, &WorkCache::activeDurationChanged, this, &WorkSessionsModel::onDurationChanged);
    connect(this, &WorkSessionsModel::visibleChanged, this, &WorkSessionsModel::fetchIf);
}

WorkSessionsModel::~WorkSessionsModel()
{
    instance_ = nullptr;
}

void WorkSessionsModel::startWork(const QString &actionId)
{
    runtime_.serverComm().startWork(actionId);
}

void WorkSessionsModel::startWorkSetActive(const QString &actionId)
{
    runtime_.serverComm().startWork(actionId, true);
}

void WorkSessionsModel::deleteWork(const QString &actionId)
{
    runtime_.serverComm().deleteWork(actionId);
}

bool WorkSessionsModel::isActive(const QString &actionId) const
{
    if (auto session = lookup(toQuid(actionId))) {
        return session->state() == nextapp::pb::WorkSession::State::ACTIVE;
    }
    return false;
}

bool WorkSessionsModel::isStarted(const QString &sessionId) const
{
    if (auto session = lookup(toQuid(sessionId))) {
        return session->start() > 0;
    }
    return false;
}

void WorkSessionsModel::pause(const QString &sessionId)
{
    runtime_.serverComm().pauseWork(sessionId);
}

void WorkSessionsModel::resume(const QString &sessionId)
{
    runtime_.serverComm().resumeWork(sessionId);
}

void WorkSessionsModel::done(const QString &sessionId)
{
    runtime_.serverComm().doneWork(sessionId);
}

void WorkSessionsModel::touch(const QString &sessionId)
{
    runtime_.serverComm().touchWork(sessionId);
}

void WorkSessionsModel::finishAction(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            runtime_.serverComm().markActionAsDone(session->action(), true);
        }
    }
}

void WorkSessionsModel::addCalendarEvent(const QString &eventId)
{
    runtime_.serverComm().addWorkFromTimeBlock(eventId);
}

void WorkSessionsModel::fetch()
{
    beginResetModel();
    ScopedExit reset{[this] { endResetModel(); }};

    const auto& active = cache_.getActive();
    sessions_.clear();
    for (const auto& session : active) {
        if (session->state() >= nextapp::pb::WorkSession::State::DONE) {
            continue;
        }
        sessions_.push_back(session);
    }
    sortAndValidate();
}

void WorkSessionsModel::sortAndValidate()
{
    // Remove anyhthing that is done or deleted.
    auto& sessions = session_by_ordered();

    sessions.sort([](const auto& lhs, const auto& rhs) {
        // Sort on state, and then touched time DESC
        if (lhs.session->state() != rhs.session->state()) {
            return lhs.session->state() < rhs.session->state();
        }
        if (lhs.session->touched() != rhs.session->touched()) {
            return lhs.session->touched() > rhs.session->touched();
        }
        return false;
    });

    sessions.remove_if([](const auto& v) {
        return v.session->state() >= nextapp::pb::WorkSession::State::DONE;
    });
}

bool WorkSessionsModel::actionIsInSessionList(const QUuid &actionId) const
{
    const auto& actions = session_by_action();
    return actions.find(actionId) != actions.end();
}

void WorkSessionsModel::fetchIf()
{
    if (isVisible()) {
        fetch();        
    }
}

void WorkSessionsModel::onDurationChanged(const WorkCache::active_duration_changes_t &changes)
{
    // Hidden views fetch a fresh snapshot when shown again.
    if (!isVisible()) {
        return;
    }

    for (const auto& change : changes) {
        const auto& by_id = session_by_id();
        const auto it = by_id.find(change.id);
        if (it == by_id.end()) {
            continue;
        }
        const auto ordered = sessions_.project<ordered_tag>(it);
        const auto row = static_cast<int>(std::distance(session_by_ordered().begin(), ordered));
        if (change.duration) {
            const auto cell = index(row, USED);
            emit dataChanged(cell, cell, {Qt::DisplayRole, DurationRole});
        }
        if (change.paused) {
            const auto cell = index(row, PAUSE);
            emit dataChanged(cell, cell, {Qt::DisplayRole, PauseRole});
        }
    }
}
