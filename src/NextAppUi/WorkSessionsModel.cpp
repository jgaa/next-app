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
    : WorkModel{parent}
{
    assert(instance_ == nullptr);
    instance_ = this;
    exclude_done_ = true;

    connect(WorkCache::instance(), &WorkCache::activeChanged, this, &WorkSessionsModel::fetch);
    connect(WorkCache::instance(), &WorkCache::activeDurationChanged, this, &WorkSessionsModel::onDurationChanged);
    connect(this, &WorkSessionsModel::visibleChanged, [this]() {
        if (isVisible()) {
            fetch();
        }
    });
}

void WorkSessionsModel::startWork(const QString &actionId)
{
    ServerComm::instance().startWork(actionId);
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
    done(sessionId);

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
        sessions_.push_back(session);
    }
}

bool WorkSessionsModel::actionIsInSessionList(const QUuid &actionId) const
{
    const auto& actions = session_by_action();
    return actions.find(actionId) != actions.end();
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

