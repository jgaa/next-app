#include "WorkModel.h"
#include "ServerComm.h"
#include "MainTreeModel.h"
#include "logging.h"
#include <algorithm>
#include <iterator>
#include <QQmlEngine>
#include "util.h"

#include "QDateTime"


using namespace std;

namespace {
int compareOnTouched (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session.state() != b.session.state()) {
        return a.session.state() < b.session.state(); // order active first
    }

    return a.session.touched() > b.session.touched(); // order newest first
}

int compareOnStart (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session.start() == b.session.start()) {
        // Avoid inconsistent results
        return a.session.id_proto() < b.session.id_proto();
    }

    return a.session.start() < b.session.start();
}

int compareOnStartDesc (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session.start() == b.session.start()) {
        // Avoid inconsistent results
        return a.session.id_proto() < b.session.id_proto();
    }

    return a.session.start() > b.session.start();
}

constexpr auto sort_fn = to_array({
    compareOnTouched,
    compareOnStart,
    compareOnStartDesc
});


} // anon ns

void WorkModel::fetchAll()
{
    nextapp::pb::GetWorkSessionsReq req;
    ServerComm::instance().getWorkSessions(req, uuid());
}

void WorkModel::fetchSome(FetchWhat what)
{
    doFetchSome(what, true);
}

void WorkModel::doFetchSome(FetchWhat what, bool firstPage)
{
    LOG_DEBUG << "WorkModel::fetchSome() called, what=" << what << ", uuid=" << uuid().toString()
              << ", firstPage=" << firstPage;

    nextapp::pb::GetWorkSessionsReq req;
    currentTreeNode_.clear();
    fetch_what_ = what;

    if (firstPage) {
        pagination_.reset();
    } else {
        req.page().setPrevTime(pagination_.prev);
    }

    req.page().setPageSize(pagination_.pageSize());

    switch(what) {
    case TODAY: {
        auto date = QDate::currentDate();
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(1);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case YESTERDAY: {
        auto date = QDate::currentDate().addDays(-1);
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(1);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case CURRENT_WEEK: {
        auto date = getFirstDayOfWeek();
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(7);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case LAST_WEEK: {
        auto date = getFirstDayOfWeek().addDays(-7);
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(7);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case CURRENT_MONTH: {
        auto date = QDate::currentDate();
        date.setDate(date.year(), date.month(), 1);
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addMonths(1);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case LAST_MONTH: {
        auto date = QDate::currentDate();
        date.setDate(date.year(), date.month(), 1);
        date = date.addMonths(-1);
        req.timeSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addMonths(1);
        req.timeSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case SELECTED_LIST: {
        if (auto nodeid = MainTreeModel::instance()->selected(); !nodeid.isEmpty()) {
            req.setNodeId(nodeid);
            currentTreeNode_ = nodeid;
        }
    } break;
    default:
        LOG_ERROR << "Unknown what: " << what;
        return;
    } // switch what

    ServerComm::instance().getWorkSessions(req, uuid());
}

void WorkModel::setSorting(Sorting sorting)
{
    LOG_DEBUG << "WorkModel::setSorting() called, sorting=" << sorting << ", uuid=" << uuid().toString();
    sorting_ = sorting;
    beginResetModel();

    if (pagination_.hasMore()) {
        // We can't re-sort locally unless we have all the data.
        // So let's start over fetching from the server.
        doFetchSome(fetch_what_, true);
        return;
    }

    ScopedExit scoped{[this] { endResetModel(); }};
    sort();
}

WorkModel::WorkModel(QObject *parent)
    : QAbstractTableModel{parent}
{
}

void WorkModel::start()
{
    LOG_DEBUG << "WorkModel::start() called" << uuid().toString();

    std::call_once(start_once_, [this] {
        LOG_DEBUG << "WorkModel::start() exceuting" << uuid().toString();
        connect(&ServerComm::instance(), &ServerComm::onUpdate, this, &WorkModel::onUpdate);
        connect(&ServerComm::instance(), &ServerComm::receivedWorkSessions, this, &WorkModel::receivedWorkSessions);
        connect(MainTreeModel::instance(), &MainTreeModel::selectedChanged, this, &WorkModel::selectedChanged);
    });
}

// WorkModel *WorkModel::createModel()
// {
//     auto model = make_unique<WorkModel>();
//     QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
//     return model.release();
// }

void WorkModel::doStart() {
    LOG_DEBUG << "WorkModel::doStart() " << uuid().toString();
    start();
}

void WorkModel::setIsVisible(bool isVisible) {
    if (is_visible_ != isVisible) {
        is_visible_ = isVisible;
        emit isVisibleChanged();

        LOG_DEBUG << "WorkModel::setIsVisible() called, isVisible=" << isVisible << ", uuid=" << uuid().toString();

        if (isVisible && skipped_node_fetch_ && fetch_what_ == SELECTED_LIST) {
            doFetchSome(FetchWhat::SELECTED_LIST);
        }
    }
}

void WorkModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
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

            sort();
        } catch (const std::exception& e) {
            LOG_ERROR << "Error updating work sessions: " << e.what();
        }
    }
}

const nextapp::pb::WorkSession *WorkModel::lookup(const QUuid &id) const
{
    const auto &sessions = session_by_id();
    auto it = sessions.find(id);
    if (auto it = sessions.find(id); it != sessions.end()) {
        return &it->session;
    }
    return nullptr;
}

int WorkModel::rowCount(const QModelIndex &parent) const
{
    if (enable_debug_) {
        LOG_DEBUG << "WorkModel::rowCount() called " << uuid().toString() << " rows=" << sessions_.size();
    }

    return sessions_.size();
}

QVariant WorkModel::data(const QModelIndex &index, int role) const
{
    if (enable_debug_) {
        LOG_TRACE << "WorkModel::data() called, role=" << role << ", uuid=" << uuid().toString()
                  << "row=" << index.row() << ", col=" << index.column()
                  << ". valid=" << index.isValid();
    }

    if (!index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= sessions_.size()) {
        return {};
    }

    const auto& session = *std::next(session_by_ordered().begin(), row);
    if (enable_debug_) {
        int i = 0;
    }
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
    }

    return {};
}

QHash<int, QByteArray> WorkModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[UuidRole] = "uuid";
    roles[IconRole] = "icon";
    roles[ActiveRole] = "active";

    return roles;
}

QVariant WorkModel::headerData(int section, Qt::Orientation orientation, int role) const
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

bool WorkModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

void WorkModel::fetchMore(const QModelIndex &parent)
{

    LOG_DEBUG << "WorkModel::fetchMore() called " << uuid().toString()
              << ", more=" << pagination_.more << ", prev=" << pagination_.prev
              << ", page = " << pagination_.page;

    if (pagination_.hasMore()) {
        doFetchSome(fetch_what_, false);
    }
}

bool WorkModel::canFetchMore(const QModelIndex &parent) const
{
    LOG_DEBUG << "WorkModel::canFetchMore() called " << uuid().toString()
              << ", more=" << pagination_.more << ", prev=" << pagination_.prev
              << ", page = " << pagination_.page;
    return pagination_.hasMore();
}

WorkModel::Outcome WorkModel::updateOutcome(nextapp::pb::WorkSession &work)
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

    if (work.hasEnd()) {
        if (work.end() <= work.start()) {
            work.setDuration(0);
        } else {
            auto duration = work.end() - work.start();
            work.setDuration(std::max<long>(0, duration - work.paused()));
        }
    } else {
        const auto now = time({});
        if (now <= work.start()) {
            work.setDuration(0);
        } else {
            work.setDuration(std::min<long>(std::max<long>(0, (now - work.start()) - work.paused()), 3600 * 24 *7));
        }
    }

    outcome.start = orig_start != work.start() / 60;
    outcome.end = orig_end != (work.hasEnd() ? work.end() / 60 : 0);
    outcome.duration = orig_duration != work.duration() / 60;
    outcome.paused= orig_paused != work.paused() / 60;
    outcome.name = orig_name != work.name();

    // LOG_DEBUG << "Updated work session " << work.name() << " from " << full_orig_duration << " to "
    //           << work.duration()
    //           << " outcome.duration= " << outcome.duration;

    return outcome;
}

void WorkModel::selectedChanged()
{
    LOG_DEBUG << "Tree selection changed...";
    if (fetch_what_ == SELECTED_LIST) {
        // TODO: Handle race condition when we select a node wile we are still fetching some other nodes data
        if (!is_visible_) {
            LOG_DEBUG << "Skipped fetching selected node, as the model is not visible";
            skipped_node_fetch_ = true;
        } else {
            doFetchSome(SELECTED_LIST);
        }
    }
}

void WorkModel::replace(const nextapp::pb::WorkSessions &sessions)
{
    auto add_sessions = [this, &sessions]() {
        size_t new_rows = 0;
        try {
            for (auto session : sessions.sessions()) {
                updateOutcome(session);
                const auto [_, inserted] = session_by_ordered().emplace_back(session);
                if (inserted) {
                    ++new_rows;
                }
            }
            sort();
        } catch (const std::exception& e) {
            LOG_ERROR << "Error parsing work sessions: " << e.what();
        }

        return new_rows;
    };

    if (pagination_.isFirstPage()) {
        beginResetModel();
        ScopedExit scoped{[this] { endResetModel(); }};
        sessions_.clear();
        add_sessions();
    } else {
        // Find out how many new rows we have
        // We will almost certainly have one duplicate, a the last row from the previous
        const auto& ses_by_id = session_by_id();
        const auto new_rows = std::ranges::count_if(sessions.sessions(), [&ses_by_id](const auto& s) {
            return !ses_by_id.contains(toQuid((s.id_proto())));
        });

        beginInsertRows(QModelIndex(), sessions_.size(), sessions_.size() + new_rows -1);
        ScopedExit scoped{[this] { endInsertRows(); }};
        const auto addes_rows = add_sessions();
        assert(addes_rows == new_rows);
        sort();
    }

    LOG_TRACE << "WorkModel::replace(): I now have " << sessions_.size() << " work-sessions cached.";
}

void WorkModel::sort()
{
    session_by_ordered().sort(sort_fn.at(sorting_));
}

void WorkModel::receivedCurrentWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
{
    replace(*sessions);
}

void WorkModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions, const ServerComm::MetaData meta)
{
    if (meta.requester == uuid()) {
        replace(*sessions);
        pagination_.increment();

        if (meta.more) {
            pagination_.more = *meta.more;
            if (*meta.more) {
                assert(!sessions->sessions().empty());
                if (sorting_ == Sorting::SORT_TOUCHED) {
                    pagination_.prev = sessions->sessions().back().touched();
                } else {
                    pagination_.prev = sessions->sessions().back().start();
                }
            }
        } else {
            pagination_.more = false;
        }

        LOG_TRACE << "WorkModel::receivedWorkSessions() called, uuid=" << uuid().toString()
                  << ", more=" << pagination_.more << ", prev=" << pagination_.prev
                  << ", page = " << pagination_.page;
    }
}
