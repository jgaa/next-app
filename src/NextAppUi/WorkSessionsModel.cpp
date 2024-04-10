#include <regex>

#include "WorkSessionsModel.h"
#include "ActionsModel.h"
#include "ServerComm.h"
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

bool WorkSessionsModel::sessionExists(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            return true;
        }
    }

    return false;
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

void WorkSessionsModel::start()
{
    WorkModel::start();

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedCurrentWorkSessions,
            this,
            &WorkSessionsModel::receivedCurrentWorkSessions);

    fetch();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &WorkSessionsModel::onTimer);
    timer_->start(5000);
}

void WorkSessionsModel::fetch()
{
    ServerComm::instance().getActiveWorkSessions();
}


bool WorkSessionsModel::actionIsInSessionList(const QUuid &actionId) const
{
    const auto& actions = session_by_action();
    return actions.find(actionId) != actions.end();
}

void WorkSessionsModel::onTimer()
{
    updateSessionsDurations();
}

void WorkSessionsModel::updateSessionsDurations()
{
    auto& sessions = session_by_ordered();
    int row = 0;
    for(auto it = sessions.begin(); it != sessions.end(); ++it, ++row ){
        sessions.modify(it, [this, row](auto& v ) {
            const auto outcome = updateOutcome(v.session);
            if (outcome.changed()) {
                if (outcome.start) {
                    const auto ix = index(row, FROM);
                    dataChanged(ix, ix);
                }
                if (outcome.end) {
                    const auto ix = index(row, TO);
                    dataChanged(ix, ix);
                }
                if (outcome.duration) {
                    const auto ix = index(row, USED);
                    dataChanged(ix, ix, {});
                }
                if (outcome.paused) {
                    const auto ix = index(row, PAUSE);
                    dataChanged(ix, ix);
                }
                if (outcome.name) {
                    const auto ix = index(row, NAME);
                    dataChanged(ix, ix);
                }
            }
        });
    }
}


