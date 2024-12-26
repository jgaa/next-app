#include "ActionsWorkedOnTodayCache.h"
#include "NextAppCore.h"
#include "WorkSessionsModel.h"
#include "ServerComm.h"
#include "WorkModelBase.h"
#include "WorkCache.h"
#include "util.h"

ActionsWorkedOnTodayCache::ActionsWorkedOnTodayCache() {
    connect(&ServerComm::instance(), &ServerComm::statusChanged, [this]() {
        if (ServerComm::instance().status() == ServerComm::Status::ONLINE) {
            init();
        };
    });

    connect(WorkCache::instance(), &WorkCache::WorkSessionAdded, [this](const QUuid& /*item*/) {
        init();
    });

    connect(WorkCache::instance(), &WorkCache::WorkSessionDeleted, [this](const QUuid& /*item*/) {
        init();
    });

    connect(WorkCache::instance(), &WorkCache::WorkSessionActionMoved, [this](const QUuid& /*item*/) {
        init();
    });

    connect(WorkCache::instance(), &WorkCache::activeChanged, [this]() {
        init();
    });

    connect(NextAppCore::instance(), &NextAppCore::currentDateChanged, this, [this]() {
        init();
    });

    if (ServerComm::instance().status() == ServerComm::Status::ONLINE) {
        init();
    };
}

ActionsWorkedOnTodayCache *ActionsWorkedOnTodayCache::instance()
{
    static ActionsWorkedOnTodayCache instance;
    return &instance;
}

QCoro::Task<void> ActionsWorkedOnTodayCache::init()
{
    if (is_initializing_) {
        pending_initialize_ = true;
        co_return;
    }

again:

    is_initializing_ = true;
    ScopedExit guard([this]() {
        is_initializing_ = false;

    });

    // Query the db for all sessions worked on today
    auto& db = NextAppCore::instance()->db();

    QString query = R"(SELECT DISTINCT action
        FROM work_session
        WHERE DATE(start_time) = DATE('now', 'localtime');
    )";

    auto rval = co_await db.query(query);

    if (pending_initialize_) {
        pending_initialize_ = false;
        LOG_TRACE_N << "Reinitializing";
        goto again;
    }

    actions_.clear();
    if (rval && !rval->empty()) {
        for (const auto& row : *rval) {
            actions_[row[0].toUuid()] = Info{};
        }
    }

    auto sessions = WorkSessionsModel::instance().getAllActionIds(true);
    for (const auto& action_uuid: sessions) {
        Info info;
        info.current = true;
        actions_[action_uuid] = info;
    }

    emit modelReset();
}
