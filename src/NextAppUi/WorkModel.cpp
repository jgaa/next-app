#include "WorkModel.h"
#include "ServerComm.h"
#include "MainTreeModel.h"
#include "ActionsModel.h"
#include "logging.h"
#include <algorithm>
#include <iterator>
#include <QQmlEngine>
#include "util.h"

#include "QDateTime"

#include "WorkCache.h"

using namespace std;

namespace {
int compareOnTouched (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session->state() != b.session->state()) {
        return a.session->state() < b.session->state(); // order active first
    }

    return a.session->touched() > b.session->touched(); // order newest first
}

int compareOnStart (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session->start() == b.session->start()) {
        // Avoid inconsistent results
        return a.session->id_proto() < b.session->id_proto();
    }

    return a.session->start() < b.session->start();
}

int compareOnStartDesc (const WorkModel::Session& a, const WorkModel::Session& b) {
    if (a.session->start() == b.session->start()) {
        // Avoid inconsistent results
        return a.session->id_proto() < b.session->id_proto();
    }

    return a.session->start() > b.session->start();
}

constexpr auto sort_fn = to_array({
    compareOnTouched,
    compareOnStart,
    compareOnStartDesc
});


} // anon ns


void WorkModel::fetchSome(FetchWhat what)
{
    doFetchSome(what, true);
}

QCoro::Task<void> WorkModel::doFetchSome(FetchWhat what, bool firstPage)
{
    LOG_DEBUG << "WorkModel::fetchSome() called, what=" << what << ", uuid=" << uuid().toString()
              << ", firstPage=" << firstPage;

    nextapp::pb::GetWorkSessionsReq req;
    currentTreeNode_.clear();
    fetch_what_ = what;

    beginResetModel();
    ScopedExit scoped{[this] { endResetModel(); }};

    if (firstPage) {
        pagination_.reset();
        sessions_.clear();
    }

    switch(what) {
    case TODAY: {
        auto date = QDate::currentDate();
        nextapp::pb::TimeSpan ts;
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(1);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case YESTERDAY: {
        auto date = QDate::currentDate().addDays(-1);
        nextapp::pb::TimeSpan ts;
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(1);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case CURRENT_WEEK: {
        auto date = getFirstDayOfWeek();
        nextapp::pb::TimeSpan ts;
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(7);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case LAST_WEEK: {
        auto date = getFirstDayOfWeek().addDays(-7);
        nextapp::pb::TimeSpan ts;
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addDays(7);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case CURRENT_MONTH: {
        auto date = QDate::currentDate();
        nextapp::pb::TimeSpan ts;
        date.setDate(date.year(), date.month(), 1);
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addMonths(1);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case LAST_MONTH: {
        auto date = QDate::currentDate();
        nextapp::pb::TimeSpan ts;
        date.setDate(date.year(), date.month(), 1);
        date = date.addMonths(-1);
        ts.setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addMonths(1);
        ts.setEnd(date.startOfDay().toSecsSinceEpoch());
        req.setTimeSpan(ts);
    } break;
    case SELECTED_LIST: {
        if (auto nodeid = MainTreeModel::instance()->selected(); !nodeid.isEmpty()) {
            req.setNodeId(nodeid);
            currentTreeNode_ = nodeid;
        }
    } break;
    default:
        LOG_ERROR << "Unknown what: " << what;
        co_return;
    } // switch what

    // ServerComm::instance().getWorkSessions(req, uuid());

    if (firstPage) {
        pagination_.reset();
    };


    nextapp::pb::Page page;
    page.setPageSize(pagination_.pageSize());
    page.setOffset(sessions_.size());
    req.setPage(page);
    auto res = co_await WorkCache::instance()->getWorkSessions(req);

    if (!res.empty()) {
        res.reserve(res.size());
        for (auto& session : res) {
            session_by_ordered().emplace_back(session);
        }
    }

    if (res.size() < pagination_.pageSize()) {
        pagination_.more = false;
    } else {
        pagination_.more = true;
        pagination_.increment();
    }

    sort();
}

void WorkModel::setSorting(Sorting sorting)
{
    LOG_DEBUG << "WorkModel::setSorting() called, sorting=" << sorting << ", uuid=" << uuid().toString();
    sorting_ = sorting;

    if (pagination_.hasMore()) {
        // We can't re-sort locally unless we have all the data.
        // So let's start over fetching from the server.
        doFetchSome(fetch_what_, true);
        return;
    }

    beginResetModel();
    ScopedExit scoped{[this] { endResetModel(); }};
    sort();
}

WorkModel::WorkModel(QObject *parent)
    : WorkModelBase{parent}
{
    // connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
    //     start();
    //     setActive(ServerComm::instance().connected());
    // });

    // if (ServerComm::instance().connected()) {
    //     start();
    //     setActive(true);
    // }

    connect(WorkCache::instance(), &WorkCache::stateChanged, this, [&] {
        if (WorkCache::instance()->valid()) {
            fetchIf();
        }
    });

    connect(MainTreeModel::instance(), &MainTreeModel::selectedChanged, this, &WorkModel::selectedChanged);

    connect(this, &WorkModelBase::visibleChanged, this, [this] {
        if (isVisible() && skipped_node_fetch_ && fetch_what_ == SELECTED_LIST) {
            doFetchSome(FetchWhat::SELECTED_LIST);
        }
    });
}

// void WorkModel::start()
// {
//     if (!started_) {
//         LOG_DEBUG << "WorkModel::start() called" << uuid().toString();

//         connect(&ServerComm::instance(), &ServerComm::onUpdate, this, &WorkModel::onUpdate);
//         connect(&ServerComm::instance(), &ServerComm::receivedWorkSessions, this, &WorkModel::receivedWorkSessions);
//         connect(MainTreeModel::instance(), &MainTreeModel::selectedChanged, this, &WorkModel::selectedChanged);
//         started_ = true;
//     }
// }

// WorkModel *WorkModel::createModel()
// {
//     auto model = make_unique<WorkModel>();
//     QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
//     return model.release();
// }

// void WorkModel::doStart() {
//     LOG_DEBUG << "WorkModel::doStart() " << uuid().toString();
//     start();
// }


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
    LOG_DEBUG_N << "Called " << uuid().toString()
              << ", more=" << pagination_.more << ", prev=" << pagination_.prev
              << ", page = " << pagination_.page;
    return pagination_.hasMore();
}



// void WorkModel::setActive(bool active)
// {
//     if (active != is_active_) {
//         is_active_ = active;
//         emit activeChanged();
//         QTimer::singleShot(0, this, &WorkModel::fetchIf);
//     }
// }

void WorkModel::selectedChanged()
{
    if (!isVisible()) {
        return;
    }

    LOG_DEBUG << "Tree selection changed...";
    if (fetch_what_ == SELECTED_LIST) {
        // TODO: Handle race condition when we select a node wile we are still fetching some other nodes data
        doFetchSome(SELECTED_LIST);
    }
}

// void WorkModel::replace(const nextapp::pb::WorkSessions &sessions)
// {
//     auto add_sessions = [this, &sessions]() {
//         size_t new_rows = 0;
//         try {
//             for (auto session : sessions.sessions()) {
//                 //updateOutcome(session);
//                 const auto [_, inserted] = session_by_ordered().emplace_back(session);
//                 if (inserted) {
//                     ++new_rows;
//                 }
//             }
//             sort();
//         } catch (const std::exception& e) {
//             LOG_ERROR << "Error parsing work sessions: " << e.what();
//         }

//         return new_rows;
//     };

//     if (pagination_.isFirstPage()) {
//         beginResetModel();
//         ScopedExit scoped{[this] { endResetModel(); }};
//         sessions_.clear();
//         add_sessions();
//     } else {
//         // Find out how many new rows we have
//         // We will almost certainly have one duplicate, a the last row from the previous
//         const auto& ses_by_id = session_by_id();
//         const auto new_rows = std::ranges::count_if(sessions.sessions(), [&ses_by_id](const auto& s) {
//             return !ses_by_id.contains(toQuid((s.id_proto())));
//         });

//         beginInsertRows(QModelIndex(), sessions_.size(), sessions_.size() + new_rows -1);
//         ScopedExit scoped{[this] { endInsertRows(); }};
//         const auto addes_rows = add_sessions();
//         assert(addes_rows == new_rows);
//         sort();
//     }

//     LOG_TRACE << "WorkModel::replace(): I now have " << sessions_.size() << " work-sessions cached.";
// }

void WorkModel::sort()
{
    session_by_ordered().sort(sort_fn.at(sorting_));
}

QCoro::Task<void> WorkModel::fetchIf()
{
    if (is_visible_ && is_active_) {
        co_await doFetchSome(fetch_what_, true);
    }

    co_return;
}

// void WorkModel::receivedCurrentWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
// {
//     replace(*sessions);
// }

// void WorkModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions, const ServerComm::MetaData meta)
// {
//     if (meta.requester == uuid()) {
//         replace(*sessions);
//         pagination_.increment();

//         if (meta.more) {
//             pagination_.more = *meta.more;
//             if (*meta.more) {
//                 assert(!sessions->sessions().empty());
//                 if (sorting_ == Sorting::SORT_TOUCHED) {
//                     pagination_.prev = sessions->sessions().back().touched();
//                 } else {
//                     pagination_.prev = sessions->sessions().back().start();
//                 }
//             }
//         } else {
//             pagination_.more = false;
//         }

//         LOG_TRACE << "WorkModel::receivedWorkSessions() called, uuid=" << uuid().toString()
//                   << ", more=" << pagination_.more << ", prev=" << pagination_.prev
//                   << ", page = " << pagination_.page;
//     }
// }


nextapp::pb::WorkSession WorkModel::getSession(const QString &sessionId)
{
    if (!sessionId.isEmpty()) {
        if (auto session = lookup(toQuid(sessionId))) {
            LOG_DEBUG_N << "Returning session " << sessionId << " from cache";
            return *session;
        }
    }

    return {};
}

nextapp::pb::WorkSession WorkModel::createSession(const QString &actionId, const QString& name)
{
    nextapp::pb::WorkSession session;

    session.setAction(actionId);
    session.setStart((time({}) / (60 * 5)) * (60 * 5));
    session.setEnd(session.start() + (60 * 60));
    session.setState(nextapp::pb::WorkSession::State::DONE);
    session.setName(name);

    return session;
}

bool WorkModel::update(const nextapp::pb::WorkSession &session)
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
