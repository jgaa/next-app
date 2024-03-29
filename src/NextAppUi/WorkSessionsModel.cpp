#include "WorkSessionsModel.h"
#include "ServerComm.h"
//#include "MainTreeModel.h"
#include "logging.h"
#include <algorithm>
#include <iterator>

#include "QDateTime"

using namespace std;

namespace {
int compare (const WorkSessionsModel::Session& a, const WorkSessionsModel::Session& b) {
    if (a.session.state() != b.session.state()) {
        return a.session.state() < b.session.state(); // order active first
    }

    return a.session.start() > b.session.start(); // order newest first
}

} // anon ns

WorkSessionsModel::WorkSessionsModel(QObject *parent)
{

}

void WorkSessionsModel::start()
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::onUpdate,
            this,
            &WorkSessionsModel::onUpdate);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedWorkSessions,
            this,
            &WorkSessionsModel::receivedWorkSessions);

    fetch();
}

void WorkSessionsModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (update->hasWork()) {
        // TODO: Set fine-grained update signals
        beginResetModel();
        const auto& work = update->work();
        if (update->op() == nextapp::pb::Update::Operation::ADDED) {
            session_by_ordered().emplace_back(work);
        } else if (update->op() == nextapp::pb::Update::Operation::UPDATED) {
            auto it = session_by_id().find(toQuid(work.id_proto()));
            if (it != session_by_id().end()) {
                if(work.state() == nextapp::pb::WorkSession::State::DONE) {
                    session_by_id().erase(it);
                } else {
                    session_by_id().modify(it, [&work](auto& v) {
                        v.session = work;
                    });
                }
            } else {
                LOG_WARN << "Got update for work session " << work.id_proto() << " which I know nothing about...";
                if (work.state() != nextapp::pb::WorkSession::State::DONE) {
                    session_by_ordered().emplace_back(work);
                }
            }
        }

        session_by_ordered().sort(compare);
        endResetModel();
    }
}

void WorkSessionsModel::fetch()
{
    ServerComm::instance().getActiveWorkSessions();
}

void WorkSessionsModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
{
    beginResetModel();
    sessions_.clear();
    for (const auto& session : sessions->sessions()) {
        session_by_ordered().emplace_back(session);
    }
    endResetModel();
}

int WorkSessionsModel::rowCount(const QModelIndex &parent) const
{
    return sessions_.size();
}

QVariant WorkSessionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= sessions_.size()) {
        return {};
    }

    enum Cols {
        FROM,
        TO,
        PAUSE,
        USED,
        NAME
    };

    const auto& session = *std::next(session_by_ordered().begin(), row);
    switch (role) {
    case Qt::DisplayRole:
        switch(index.column()) {
        case FROM: {
            const auto start = QDateTime::fromMSecsSinceEpoch(session.session.start()).toLocalTime();
            const auto now = QDateTime::currentDateTime().toLocalTime();
            if (start.date() == now.date()) {
                return start.toString("hh:mm");
            }
            }
            return QDateTime::fromMSecsSinceEpoch(session.session.start()).toLocalTime().toString("yyyy-MM-dd hh:mm");
        case TO: {
            const auto start = QDateTime::fromMSecsSinceEpoch(session.session.start()).toLocalTime();
            const auto end = QDateTime::fromMSecsSinceEpoch(session.session.end()).toLocalTime();
            if (start.date() == end.date()) {
                return end.toString("hh:mm");
            }
            return end.toString("yyyy-MM-dd hh:mm");
            }
        case PAUSE:
                return static_cast<int>(session.session.paused());
        case USED:
            return static_cast<int>(session.session.duration());
        case NAME:
            return session.session.action();
        } // switch col

        // case UuidRole:
        //     return session.session.id_proto();
        // case StartRole:
        //     return static_cast<quint64>(session.session.start());
        // case EndRole:
        //     return static_cast<quint64>(session.session.end());
        // case DurationRole:
        //     return static_cast<quint32>(session.session.duration());
        // case PausedRole:
        //     return static_cast<quint32>(session.session.paused());
        // case ActionRole:
        //     return session.session.action();
        // case StateRole:
        //     return session.session.state();
        // case VersionRole:
        //     return static_cast<qint32>(session.session.version());
        // case TouchedRole:
        //     return static_cast<quint64>(session.session.touched());
    }

    return {};
}

QHash<int, QByteArray> WorkSessionsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UuidRole] = "uuid";
    roles[StartRole] = "start";
    roles[EndRole] = "end";
    roles[DurationRole] = "duration";
    roles[PausedRole] = "paused";
    roles[ActionRole] = "action";
    roles[StateRole] = "state";
    roles[VersionRole] = "version";
    roles[TouchedRole] = "touched";

    return roles;
}
