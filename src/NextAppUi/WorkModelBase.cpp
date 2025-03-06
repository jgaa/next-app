#include "WorkModelBase.h"

WorkModelBase::WorkModelBase(QObject *parent) : QAbstractTableModel(parent) {}

bool WorkModelBase::sessionExists(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            return true;
        }
    }

    return false;
}

const nextapp::pb::WorkSession *WorkModelBase::lookup(const QUuid &id) const
{
    const auto &sessions = session_by_id();
    auto it = sessions.find(id);
    if (auto it = sessions.find(id); it != sessions.end()) {
        return it->session.get();
    }
    return nullptr;
}


// This model is used by both ListVew and TableView and
// needs to handle both roles and coumns.
// We use goto to avoid duplicating code. Goto is not evil if used with care.
QVariant WorkModelBase::data(const QModelIndex &index, int role) const
{
    // if (enable_debug_) {
    //     LOG_TRACE << "WorkModel::data() called, role=" << role << ", uuid=" << uuid().toString()
    //     << "row=" << index.row() << ", col=" << index.column()
    //     << ". valid=" << index.isValid();
    // }

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
        from:
            const auto start_time = session.session->start();
            if (!start_time) {
                return QString{};
            }
            const auto start = QDateTime::fromSecsSinceEpoch(start_time);
            const auto now = QDateTime::currentDateTime();
            if (start.date() == now.date()) {
                return start.toString("hh:mm");
            }
            return start.toString("yyyy-MM-dd hh:mm");
        }
        case TO: {
        to:
            if (!session.session->hasEnd()) {
                return QString{};
            }
            const auto start = QDateTime::fromSecsSinceEpoch(session.session->start());
            const auto end = QDateTime::fromSecsSinceEpoch(session.session->end());
            if (start.date() == end.date()) {
                return end.toString("hh:mm");
            }
            return end.toString("yyyy-MM-dd hh:mm");
        }
        case PAUSE:
        pause:
            return toHourMin(static_cast<int>(session.session->paused()));
        case USED:
        used:
            return toHourMin(static_cast<int>(session.session->duration()));
        case NAME:
        name:
            return session.session->name();
        } // switch col

    case UuidRole:
        return session.session->id_proto();
    case IconRole:
        //return static_cast<int>(session.session->state());
        switch(session.session->state()) {
        case nextapp::pb::WorkSession::State::ACTIVE:
            return QString{QChar{0xf04b}};
        case nextapp::pb::WorkSession::State::PAUSED:
            return QString{QChar{0xf04c}};
        case nextapp::pb::WorkSession::State::DONE:
            return QString{QChar{0xf04d}};
        case nextapp::pb::WorkSession::State::DELETED:
            assert(false);
            break;
        }
        break;
    case ActiveRole:
        return session.session->state() == nextapp::pb::WorkSession::State::ACTIVE;
    case HasNotesRole:
        return !session.session->notes().isEmpty();
    case FromRole:
        goto from;
    case ToRole:
        goto to;
    case PauseRole:
        goto pause;
    case DurationRole:
        goto used;
    case NameRole:
        goto name;
    case StartedRole:
        return session.session->start() > 0;
    }
    return {};
}

void WorkModelBase::setIsVisible(bool isVisible) {
    if (is_visible_ != isVisible) {
        is_visible_ = isVisible;
        emit visibleChanged();
        LOG_DEBUG_N << "isVisible=" << isVisible << ", uuid=" << uuid().toString();
    }
}

void WorkModelBase::setActive(bool active)
{
    if (is_active_ != active) {
        is_active_ = active;
        emit activeChanged();
    }
}

std::vector<QUuid> WorkModelBase::getAllActionIds(bool withDuration) const
{
    std::vector<QUuid> all;
    all.reserve(sessions_.size());
    for (const auto& session : sessions_) {
        if (!withDuration || session.session->duration() > 0) {
            QUuid uuid{session.action};
            if (!uuid.isNull()) {
                all.push_back(session.action);
            }
        }
    }
    return all;
}

QHash<int, QByteArray> WorkModelBase::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[UuidRole] = "uuid";
    roles[IconRole] = "icon";
    roles[ActiveRole] = "active";
    roles[HasNotesRole] = "hasNotes";
    roles[FromRole] = "from";
    roles[ToRole] = "to";
    roles[PauseRole] = "pause";
    roles[DurationRole] = "duration";
    roles[NameRole] = "name";
    roles[StartedRole] = "started";

    return roles;
}

QVariant WorkModelBase::headerData(int section, Qt::Orientation orientation, int role) const
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

bool WorkModelBase::setData(const QModelIndex &index, const QVariant &value, int role)
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
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::CORRECTION);
                    event.setStart(seconds);
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ;
                }
                break;
            case TO:
                try {
                    auto seconds = parseDateOrTime(value.toString());
                    if (seconds < work->start()) {
                        throw std::runtime_error{"End time must be after start time"};
                    }

                    nextapp::pb::WorkEvent event;
                    if (work->state() != nextapp::pb::WorkSession::State::DONE) {
                        // We have to stop the work.
                        event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::STOP);
                    } else {
                        event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::CORRECTION);
                    }
                    event.setEnd(seconds);
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ;
                }
                break;
            case PAUSE:
                try {
                    auto seconds = parseDuration(value.toString());
                    nextapp::pb::WorkEvent event;
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::CORRECTION);
                    event.setPaused(seconds);
                    ServerComm::instance().sendWorkEvent(work->id_proto(), event);
                } catch (const std::runtime_error&) {
                    ; // TODO: Tell the user
                }
                break;

            case NAME:
                try {
                    nextapp::pb::WorkEvent event;
                    event.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::CORRECTION);
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

nextapp::pb::WorkSession WorkModelBase::getSession(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            LOG_DEBUG_N << "Returning session " << sessionId << " from cache";
            return *session;
        }
    }

    return {};
}

nextapp::pb::WorkSession WorkModelBase::createSession(const QString &actionId, const QString& name)
{
    nextapp::pb::WorkSession session;

    session.setAction(actionId);
    session.setStart((time({}) / (60 * 5)) * (60 * 5));
    session.setEnd(session.start() + (60 * 60));
    session.setState(nextapp::pb::WorkSession::State::DONE);
    session.setName(name);

    return session;
}

bool WorkModelBase::update(const nextapp::pb::WorkSession &session)
{
    if (session.action().isEmpty()) {
        LOG_ERROR << "Cannot update a work-session without an action";
        return false;
    }

    if (session.id_proto().isEmpty()) {
        // This is a new session
        ServerComm::instance().addWork(session);
        return true;
    }

    nextapp::pb::AddWorkEventReq req;
    req.setWorkSessionId(session.id_proto());

    // Deduce changes
    auto curr = lookup(toQuid(session.id_proto()));
    if (!curr) {
        LOG_ERROR << "Cannot update a work-session that I don't know about";
        return false;
    }

    nextapp::pb::WorkEvent ev;
    ev.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::CORRECTION);

    if (curr->start() != session.start()) {
        ev.setStart(session.start());
    }

    if (session.paused() != curr->paused()) {
        ev.setPaused(session.paused());
    }

    if (session.name() != curr->name()) {
        ev.setName(session.name());
    }

    if (session.notes() != curr->notes()) {
        ev.setNotes(session.notes());
    }

    if (session.hasEnd() && curr->hasEnd()) {
        // Just correct it. Ending an active session is handled below
        assert(curr->state() == nextapp::pb::WorkSession::State::DONE);
        ev.setEnd(session.end());
    }

    {
        auto events = req.events();
        events.push_back(ev);
        req.setEvents(events);
    }
    //req.events().push_back(ev);
    ev = {};

    if (session.state() != curr->state() && curr->state() != nextapp::pb::WorkSession::State::DONE) {

        switch(session.state()) {
        case nextapp::pb::WorkSession::State::ACTIVE:
            assert(!curr->hasEnd());
            assert(!session.hasEnd());
            ServerComm::instance().resumeWork(session.id_proto());
            break;
        case nextapp::pb::WorkSession::State::PAUSED:
            assert(!curr->hasEnd());
            assert(!session.hasEnd());
            ServerComm::instance().pauseWork(session.id_proto());
            break;
        case nextapp::pb::WorkSession::State::DONE:
            // Stop the session if the user changes the state to DONE but did not set an end time.
            if (!session.hasEnd()) {
                ev.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::STOP);
                auto events = req.events();
                events.push_back(ev);
                req.setEvents(events);
                //req.events().push_back(ev);
                ev = {};
            }
            break;
        default:
            ; // ignore. Setting end-time will set the session state to DONE
        }

        // Stop the session if the user set a stop date but left the state as ACTIVE or PAUSED
        if (session.hasEnd() && session.state() != nextapp::pb::WorkSession::State::DONE) {
            assert(!curr->hasEnd());
            ev.setKind(nextapp::pb::WorkEvent_QtProtobufNested::Kind::STOP);
            ev.setEnd(session.end());
            auto events = req.events();
            events.push_back(ev);
            req.setEvents(events);
            //req.events().push_back(ev);
            ev = {};
        }
    }

    ServerComm::instance().sendWorkEvents(req);
    return true;
}
