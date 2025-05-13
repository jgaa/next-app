#include <regex>

#include "WorkSessionsModel.h"
#include "ActionsModel.h"
#include "ServerComm.h"
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
    : WorkModelBase{parent}
{
    assert(instance_ == nullptr);
    instance_ = this;

    connect(WorkCache::instance(), &WorkCache::activeChanged, this, &WorkSessionsModel::fetchIf);
    connect(WorkCache::instance(), &WorkCache::WorkSessionAdded, this, &WorkSessionsModel::fetchIf);
    connect(WorkCache::instance(), &WorkCache::WorkSessionChanged, this, &WorkSessionsModel::fetchIf);
    connect(WorkCache::instance(), &WorkCache::WorkSessionActionMoved, this, &WorkSessionsModel::fetchIf);
    connect(WorkCache::instance(), &WorkCache::WorkSessionDeleted, this, &WorkSessionsModel::fetchIf);
    connect(WorkCache::instance(), &WorkCache::activeDurationChanged, this, &WorkSessionsModel::onDurationChanged);
    connect(this, &WorkSessionsModel::visibleChanged, this, &WorkSessionsModel::fetchIf);
}

void WorkSessionsModel::startWork(const QString &actionId)
{
    ServerComm::instance().startWork(actionId);
}

void WorkSessionsModel::startWorkSetActive(const QString &actionId)
{
    ServerComm::instance().startWork(actionId, true);
}

void WorkSessionsModel::deleteWork(const QString &actionId)
{
    ServerComm::instance().deleteWork(actionId);
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
    ServerComm::instance().pauseWork(sessionId);
}

void WorkSessionsModel::resume(const QString &sessionId)
{
    ServerComm::instance().resumeWork(sessionId);
}

void WorkSessionsModel::done(const QString &sessionId)
{
    ServerComm::instance().doneWork(sessionId);
}

void WorkSessionsModel::touch(const QString &sessionId)
{
    ServerComm::instance().touchWork(sessionId);
}

void WorkSessionsModel::finishAction(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            ServerComm::instance().markActionAsDone(session->action(), true);
        }
    }
}

void WorkSessionsModel::addCalendarEvent(const QString &eventId)
{
    ServerComm::instance().addWorkFromTimeBlock(eventId);
}

void WorkSessionsModel::fetch()
{
    beginResetModel();
    ScopedExit reset{[this] { endResetModel(); }};

    auto active = WorkCache::instance()->getActive();
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

// Assume that the order of the changes is the same as the order of the sessions
void WorkSessionsModel::onDurationChanged(const WorkCache::active_duration_changes_t &changes)
{
    uint row = 0;
    for (const auto& change : changes) {
        if (change.duration) {
            QModelIndex index = createIndex(row, USED);
            emit dataChanged(index, index);
        }
        if (change.paused) {
            QModelIndex index = createIndex(row, PAUSE);
            emit dataChanged(index, index);
        }
        ++row;
    }
}

