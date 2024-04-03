#include <regex>

#include "WorkSessionsModel.h"
#include "ServerComm.h"
//#include "MainTreeModel.h"
#include "logging.h"
#include <algorithm>
#include <iterator>
#include "util.h"

#include "QDateTime"

using namespace std;

namespace {
int compare (const WorkSessionsModel::Session& a, const WorkSessionsModel::Session& b) {
    if (a.session.state() != b.session.state()) {
        return a.session.state() < b.session.state(); // order active first
    }

    return a.session.touched() > b.session.touched(); // order newest first
}

// Flags for what was changed in updateOutcome
// We check for whole minutes, since that's whats on the screen
struct Outcome {
    bool duration = false;
    bool paused = false;
    bool end = false;
    bool start = false;
    bool name = false;

    bool changed() const noexcept {
        return duration || paused || end || start || name;
    }
};

enum Cols {
    FROM,
    TO,
    PAUSE,
    USED,
    NAME
};

Outcome updateOutcome(nextapp::pb::WorkSession &work)
{
    Outcome outcome;
    using namespace nextapp;

    // First event *must* be a start event
    if (work.events().empty()) {
        return outcome; // Nothing to do
    }

    const auto orig_start = work.start() / 60;
    const auto orig_end = work.hasEnd() ? work.end() / 60 : 0;
    const auto orig_duration = work.duration() / 60;
    const auto orig_paused = work.paused() / 60;
    const auto orig_state = work.state() / 60;
    const auto orig_name = work.name();
    const auto full_orig_duration = work.duration();

    work.setPaused(0);
    work.setDuration(0);
    work.clearEnd();

    time_t pause_from = 0;

    const auto end_pause = [&](const pb::WorkEvent& event) {
        if (pause_from > 0) {
            auto pduration = event.time() - pause_from;
            work.setPaused(work.paused() + pduration);
            pause_from = 0;
        }
    };

    unsigned row = 0;
    for(const auto &event : work.events()) {
        ++row;
        switch(event.kind()) {
        case pb::WorkEvent_QtProtobufNested::START:
            assert(row == 1);
            work.setStart(event.time());
            work.setState(pb::WorkSession::State::ACTIVE);
            break;
        case pb::WorkEvent_QtProtobufNested::STOP:
            end_pause(event);
            work.setEnd(event.time());
            work.setState(pb::WorkSession::State::DONE);
            break;
        case pb::WorkEvent_QtProtobufNested::PAUSE:
            if (!pause_from) {
                pause_from = event.time();
            }
            work.setState(pb::WorkSession::State::PAUSED);
            break;
        case pb::WorkEvent_QtProtobufNested::RESUME:
            end_pause(event);
            work.setState(pb::WorkSession::State::ACTIVE);
            break;
        case pb::WorkEvent_QtProtobufNested::TOUCH:
            break;
        case pb::WorkEvent_QtProtobufNested::CORRECTION:
            if (event.hasStart()) {
                work.setStart(event.start());
            }
            if (event.hasEnd()) {
                if (work.state() != pb::WorkSession::State::DONE) {
                    throw runtime_error{"Cannot correct end time of an active session"};
                }
                work.setEnd(event.end());
            }
            if (event.hasDuration()) {
                work.setDuration(event.duration());
            }
            if (event.hasPaused()) {
                work.setPaused(event.paused());
                if (pause_from) {
                    // Start the pause timer at the events time
                    pause_from = event.time();
                }
            }
            if (event.hasName()) {
                work.setName(event.name());
            }
            if (event.hasNotes()) {
                work.setNotes(event.notes());
            }
            break;
        default:
            assert(false);
            throw runtime_error{"Invalid work event kind"s + to_string(event.kind())};
        }
    }

    if (pause_from) {
        // If we are paused, we need to account for the time between the last pause and now
        pb::WorkEvent event;
        event.setTime(time({}));
        end_pause(event);
    }

    // Now set the duration. That is, the duration from start to end - paused
    if (work.hasEnd()) {
        work.setDuration(std::max<time_t>(work.end() - work.start() - work.paused(), 0));
    } else {
        assert(work.state() != pb::WorkSession::State::DONE);
        work.setDuration(std::max<time_t>(time({}) - work.start() - work.paused(), 0));
    }

    outcome.start = orig_start != work.start() / 60;
    outcome.end = orig_end != work.hasEnd() ? work.end() / 60 : 0;
    outcome.duration = orig_duration != work.duration() / 60;
    outcome.paused= orig_paused != work.paused() / 60;
    outcome.name = orig_name != work.name();

    LOG_DEBUG << "Updated work session " << work.name() << " from " << full_orig_duration << " to "
              << work.duration()
              << " outcome.duration= " << outcome.duration;

    return outcome;
}

} // anon ns

WorkSessionsModel* WorkSessionsModel::instance_;

WorkSessionsModel::WorkSessionsModel(QObject *parent)
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
    if (auto session = lookup(toQuid(sessionId))) {
        return true;
    }

    return false;
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

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &WorkSessionsModel::onTimer);
    timer_->start(5000);
}

void WorkSessionsModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (update->hasWork()) {
        // TODO: Set fine-grained update signals
        beginResetModel();
        ScopedExit scoped{[this] { endResetModel(); }};

        try {
            const auto op = update->op();
            const auto& work = update->work();
            switch(op) {
                case nextapp::pb::Update::Operation::ADDED:
                    session_by_ordered().emplace_back(work);
                    break;
                case nextapp::pb::Update::Operation::UPDATED: {
                    auto &session = session_by_id();
                    auto it = session.find(toQuid(work.id_proto()));
                    if (it != session.end()) {
                        if(work.state() == nextapp::pb::WorkSession::State::DONE) {
                            session.erase(it);
                        } else {
                            session.modify(it, [&work](auto& v) {
                                v.session = work;
                            });
                        }
                    } else {
                        LOG_WARN << "Got update for work session " << work.id_proto() << " which I know nothing about...";
                        if (work.state() != nextapp::pb::WorkSession::State::DONE) {
                            session_by_ordered().emplace_back(work);
                        }
                    }
                } break;
                case nextapp::pb::Update::Operation::DELETED:
                    auto &session = session_by_id();
                    session.erase(toQuid(work.id_proto()));
                    break;
            }

            session_by_ordered().sort(compare);
        } catch (const std::exception& e) {
            LOG_ERROR << "Error updating work sessions: " << e.what();
        }
    }
}

void WorkSessionsModel::fetch()
{
    ServerComm::instance().getActiveWorkSessions();
}

void WorkSessionsModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
{
    beginResetModel();
    ScopedExit scoped{[this] { endResetModel(); }};

    try {
        sessions_.clear();
        for (auto session : sessions->sessions()) {
            updateOutcome(session);
            session_by_ordered().emplace_back(session);
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "Error parsing work sessions: " << e.what();
    }
}

bool WorkSessionsModel::actionIsInSessionList(const QUuid &actionId) const
{
    const auto& actions = session_by_action();
    return actions.find(actionId) != actions.end();
}

const nextapp::pb::WorkSession *WorkSessionsModel::lookup(const QUuid &id) const
{
    const auto &sessions = session_by_id();
    auto it = sessions.find(id);
    if (auto it = sessions.find(id); it != sessions.end()) {
        return &it->session;
    }
    return nullptr;
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

    const auto& session = *std::next(session_by_ordered().begin(), row);
    switch (role) {
    case Qt::DisplayRole:
        switch(index.column()) {
        case FROM: {
            const auto start = QDateTime::fromSecsSinceEpoch(session.session.start());
            const auto now = QDateTime::currentDateTime();
            if (start.date() == now.date()) {
                return start.toString("hh:mm");
            }
            return start.toString("yyyy-MM-dd hh:mm");
            }
        case TO: {
            if (!session.session.hasEnd()) {
                return QString{};
            }
            const auto start = QDateTime::fromSecsSinceEpoch(session.session.start());
            const auto end = QDateTime::fromSecsSinceEpoch(session.session.end());
            if (start.date() == end.date()) {
                return end.toString("hh:mm");
            }
            return end.toString("yyyy-MM-dd hh:mm");
            }
        case PAUSE:
                return toHourMin(static_cast<int>(session.session.paused()));
        case USED:
            return toHourMin(static_cast<int>(session.session.duration()));
        case NAME:
            return session.session.name();
        } // switch col

        case UuidRole:
            return session.session.id_proto();
        case IconRole:
            //return static_cast<int>(session.session.state());
            switch(session.session.state()) {
                case nextapp::pb::WorkSession::State::ACTIVE:
                    return QString{QChar{0xf04b}};
                case nextapp::pb::WorkSession::State::PAUSED:
                    return QString{QChar{0xf04c}};
                case nextapp::pb::WorkSession::State::DONE:
                    return QString{QChar{0xf04d}};
            }
            break;
        case ActiveRole:
            return session.session.state() == nextapp::pb::WorkSession::State::ACTIVE;
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
    roles[Qt::DisplayRole] = "display";
    roles[UuidRole] = "uuid";
    roles[IconRole] = "icon";
    roles[ActiveRole] = "active";
    // roles[StartRole] = "start";
    // roles[EndRole] = "end";
    // roles[DurationRole] = "duration";
    // roles[PausedRole] = "paused";
    // roles[ActionRole] = "action";
    // roles[StateRole] = "state";
    // roles[VersionRole] = "version";
    // roles[TouchedRole] = "touched";

    return roles;
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

QVariant WorkSessionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch(section) {
        case FROM:
            return tr("From");
        case TO:
            return tr("To");
        case PAUSE:
            return tr("Pause");
        case USED:
            return tr("Used");
        case NAME:
            return tr("Name");
        }
    }

    return {};
}

time_t parseDateOrTime(const QString& str)
{
    if (str.isEmpty()) {
        return 0;
    }

    regex pattern(R"((\d{4}-\d{2}-\d{2} )?(\d{2}:\d{2}))");

    std::smatch match;
    const auto nstr = str.toStdString();
    if (std::regex_match(nstr, match, pattern)) {
        if (match[1].matched) {
            // The input is an ANSI date + time
            auto when = QDateTime::fromString(str, "yyyy-MM-dd hh:mm").toSecsSinceEpoch();
            return when;
        } else {
            // The input is just a time
            auto time = static_cast<time_t>(parseDuration(str));
            auto timedate = QDateTime{QDate::currentDate(), QTime::fromMSecsSinceStartOfDay(time * 1000)};
            auto when = timedate.toSecsSinceEpoch();
            return when;
        }
    } else {
        // The input does not match either format
        LOG_WARN << "Could not parse time/date: " << str;
    }

    throw std::runtime_error{"Invalid date/time format"};
}

bool WorkSessionsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) {
        return false;
    }

    if (auto work = lookup(toQuid(data(index, UuidRole).toString()))) {

        if (role == Qt::DisplayRole) {
            switch(index.column()) {
            case FROM:
                try {
                    auto seconds = parseDateOrTime(value.toString());
                    nextapp::pb::WorkEvent event;
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::CORRECTION);
                    event.setStart(seconds);
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ;
                }
                break;
            case PAUSE:
                try {
                    auto seconds = parseDuration(value.toString());
                    nextapp::pb::WorkEvent event;
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::CORRECTION);
                    event.setPaused(seconds);
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ; // TODO: Tell the user
                }
                break;

            case NAME:
                try {
                    nextapp::pb::WorkEvent event;
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::CORRECTION);
                    event.setName(value.toString());
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ; // TODO: Tell the user
                }
                break;
            }
        }
    }

    return false;
}
