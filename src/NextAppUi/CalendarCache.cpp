
#include <deque>
#include <QCryptographicHash>
#include <QSet>

#include <QProtobufSerializer>
#include "CalendarCache.h"
#include "ActionsOnCurrentCalendar.h"

using namespace std;

namespace {

    static const QString insert_query = R"(INSERT INTO time_block (id, start_time, end_time, kind, data, updated, updated_id)
        VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
        start_time = excluded.start_time,
        end_time = excluded.end_time,
        kind = excluded.kind,
        data = excluded.data,
        updated = excluded.updated,
        updated_id = excluded.updated_id
        WHERE excluded.updated_id >= COALESCE(time_block.updated_id, 0))";

    QList<QVariant> getParams(const nextapp::pb::TimeBlock& tb)
    {
        QList<QVariant> params;
        params << tb.id_proto();
        params << QDateTime::fromSecsSinceEpoch(tb.timeSpan().start());
        params << QDateTime::fromSecsSinceEpoch(tb.timeSpan().end());
        params << static_cast<int>(tb.kind());
        QProtobufSerializer serializer;
        params << tb.serialize(&serializer);
        params << static_cast<qlonglong>(tb.updated());
        params << static_cast<qulonglong>(tb.updatedId());
        return params;
    };

    QCoro::Task<std::optional<QSet<QString>>> loadExistingActionIds(
        DbStore& db,
        const std::optional<DbStore::transaction_token_t>& token = std::nullopt)
    {
        QSet<QString> existing;

        const auto res = token
            ? co_await db.legacyQueryInTransaction(*token, "SELECT id FROM action")
            : co_await db.legacyQuery("SELECT id FROM action");
        if (!res) {
            LOG_ERROR_N << "Failed to load action ids for calendar repair: " << res.error();
            co_return std::nullopt;
        }

        for (const auto& row : *res) {
            if (row.empty()) {
                LOG_ERROR_N << "Encountered empty action-id row while repairing calendar time blocks";
                co_return std::nullopt;
            }
            existing.insert(row.at(0).toString());
        }

        co_return existing;
    }

    QByteArray canonicalSyncIssueKeyBytes(const nextapp::pb::SyncIssueKey& key)
    {
        QByteArray bytes;
        bytes += "object_type=" + QByteArray::number(static_cast<int>(key.objectType())) + '\n';
        bytes += "object_id=" + key.objectId().toUtf8() + '\n';
        bytes += "problem=" + QByteArray::number(static_cast<int>(key.problem())) + '\n';
        bytes += "referenced_type=" + QByteArray::number(static_cast<int>(key.referencedType())) + '\n';
        bytes += "referenced_id=" + key.referencedId().toUtf8() + '\n';
        return bytes;
    }

    nextapp::pb::SyncIssue makeMissingTimeBlockActionIssue(
        const QString& time_block_id,
        const QString& action_id)
    {
        nextapp::pb::SyncIssueKey key;
        key.setObjectType(nextapp::pb::SyncObjectTypeGadget::SyncObjectType::SYNC_OBJECT_TYPE_TIME_BLOCK);
        key.setObjectId(time_block_id);
        key.setProblem(nextapp::pb::SyncFaultKindGadget::SyncFaultKind::SYNC_FAULT_KIND_MISSING_REFERENCE);
        key.setReferencedType(nextapp::pb::SyncObjectTypeGadget::SyncObjectType::SYNC_OBJECT_TYPE_ACTION);
        key.setReferencedId(action_id);

        nextapp::pb::SyncIssue issue;
        issue.setKey(key);
        issue.setIssueId(QCryptographicHash::hash(
            canonicalSyncIssueKeyBytes(issue.key()),
            QCryptographicHash::Sha256));
        return issue;
    }

    void recordMissingTimeBlockActionIssues(
        ServerCommAccess& server_comm,
        const nextapp::pb::TimeBlock& tb,
        const QSet<QString>& existing_actions)
    {
        QSet<QString> seen;
        for (const auto& action_id : tb.actions().list()) {
            if (seen.contains(action_id)) {
                continue;
            }
            seen.insert(action_id);
            if (existing_actions.contains(action_id)) {
                continue;
            }
            server_comm.recordSyncIssue(
                makeMissingTimeBlockActionIssue(tb.id_proto(), action_id));
        }
    }

    std::deque<std::pair<QString, QString>> getActionRefs(
        const QList<nextapp::pb::TimeBlock>& items,
        const QSet<QString>& existing_actions)
    {
        std::deque<std::pair<QString, QString>> refs;
        for (const auto& tb : items) {
            if (tb.kind() == nextapp::pb::TimeBlock::Kind::DELETED) {
                continue;
            }

            QSet<QString> seen;
            for (const auto& aid : tb.actions().list()) {
                if (seen.contains(aid)) {
                    continue;
                }
                seen.insert(aid);
                if (!existing_actions.contains(aid)) {
                    continue;
                }
                refs.emplace_back(tb.id_proto(), aid);
            }
        }

        return refs;
    }

} // anon ns

CalendarCache::CalendarCache()
    : CalendarCache(*NextAppCore::instance())
{
}

CalendarCache::CalendarCache(RuntimeServices& runtime)
    : ServerSynchedCahce<nextapp::pb::TimeBlock, CalendarCache>{runtime}
    , runtime_{runtime}
{
    audio_timer_update_delay_.setSingleShot(true);
    audio_timer_update_delay_.setInterval(500);
    connect(&audio_timer_update_delay_, &QTimer::timeout, this, [this] {
        setAudioTimers();
    });

    if (auto *core = dynamic_cast<NextAppCore*>(&runtime_)) {
        connect(core, &NextAppCore::settingsChanged, this, [this]{
            requestAudioTimerUpdate();
        });

        connect(core, &NextAppCore::currentDateChanged, this, [this]{
            requestAudioTimerUpdate();
        });

        connect(core, &NextAppCore::propertyChanged, this, [this](const QString& name){
            if (name == "primaryForActionList") {
                auto new_date = runtime_.appProperty(name).toDate();
                if (new_date != current_calendar_date_) {
                    LOG_TRACE_N << "CalendarCache: primaryForActionList changed from "
                        << current_calendar_date_.toString() << " to " << new_date.toString();
                    current_calendar_date_ = new_date;
                    updateActionsOnCalendarCache();
                }
            }
        });
    }
}

CalendarCache *CalendarCache::instance() noexcept
{
    static CalendarCache instance;
    return &instance;
}

QCoro::Task<bool> CalendarCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    const auto op = update->op();

    bool need_to_update_alarms = false;
    bool need_to_update_actions_on_calendar = false;
    const auto todays_date = QDate::currentDate();

    if (update->hasCalendarEvents()) {
        const auto existing_actions = co_await loadExistingActionIds(syncDb());
        if (!existing_actions) {
            runtime_.serverComm().requestIncrementalRepair();
            co_return false;
        }
        const auto& ce_list = update->calendarEvents().events();

        for (const auto& ce : ce_list) {
            if (!ce.hasTimeBlock()) {
                LOG_WARN_N << "Received calendar event " << ce.id_proto()
                           << " without time block. Ignoring.";
                continue;
            }
            const auto& tb = ce.timeBlock();
            runtime_.serverComm().clearSyncIssuesForObject(
                nextapp::pb::SyncObjectTypeGadget::SyncObjectType::SYNC_OBJECT_TYPE_TIME_BLOCK,
                tb.id_proto());
            recordMissingTimeBlockActionIssues(runtime_.serverComm(), tb, *existing_actions);

            const bool is_relevant_for_audio =
                todays_date == QDateTime::fromSecsSinceEpoch(ce.timeBlock().timeSpan().start()).date()
                                               || todays_date == QDateTime::fromSecsSinceEpoch(ce.timeBlock().timeSpan().end()).date();
            const bool is_relevant_for_actions =
                current_calendar_date_ == QDateTime::fromSecsSinceEpoch(ce.timeBlock().timeSpan().start()).date()
                                               || current_calendar_date_ == QDateTime::fromSecsSinceEpoch(ce.timeBlock().timeSpan().end()).date();

            const QUuid id{ce.id_proto()};

            if (ce.hasTimeBlock()) {
                if (op == ::nextapp::pb::Update::Operation::DELETED) {
                    if (tb.kind() != nextapp::pb::TimeBlock::Kind::DELETED) {
                        LOG_ERROR_N << "Received deleted calendar event " << ce.id_proto()
                                    << " with non-deleted time block. Requesting incremental repair.";
                        runtime_.serverComm().requestIncrementalRepair();
                        co_return false;
                    }
                    events_.erase(id);
                    if (!co_await save_(ce.timeBlock())) {
                        runtime_.serverComm().requestIncrementalRepair();
                        co_return false;
                    }
                    emit eventRemoved(id);
                    if (is_relevant_for_audio) {
                        need_to_update_alarms = true;
                    }
                    if (is_relevant_for_actions) {
                        need_to_update_actions_on_calendar = true;
                    }
                    continue;
                }
                if (tb.kind() == nextapp::pb::TimeBlock::Kind::DELETED) {
                    LOG_ERROR_N << "Received non-deleted calendar event " << ce.id_proto()
                                << " with deleted time block. Requesting incremental repair.";
                    runtime_.serverComm().requestIncrementalRepair();
                    co_return false;
                }
                if (!co_await save_(ce.timeBlock())) {
                    runtime_.serverComm().requestIncrementalRepair();
                    co_return false;
                }

                if (auto it = events_.find(id); it != events_.end()) {
                    *it->second = ce;
                } else {
                    events_[id] = std::make_shared<nextapp::pb::CalendarEvent>(ce);
                }

                if (op == ::nextapp::pb::Update::Operation::UPDATED) {
                    emit eventUpdated(id);
                } else {
                    emit eventAdded(id);
                }

                if (is_relevant_for_audio) {
                    need_to_update_alarms = true;
                }
                if (is_relevant_for_actions) {
                    need_to_update_actions_on_calendar = true;
                }

            } // if timeblock
        }
    } else if (update->hasAction() && op == ::nextapp::pb::Update::Operation::DELETED) {
        need_to_update_actions_on_calendar = true;
    }

    if (need_to_update_alarms) {
        LOG_DEBUG_N << "Updating audio timers after calendar update.";
        requestAudioTimerUpdate();
    }

    if (need_to_update_actions_on_calendar) {
        co_await updateActionsOnCalendarCache();
    }
    co_return true;
}

QCoro::Task<bool> CalendarCache::save(const QProtobufMessage &item)
{
    return save_(static_cast<const nextapp::pb::TimeBlock&>(item));
}


QCoro::Task<bool> CalendarCache::saveBatch(const QList<nextapp::pb::TimeBlock> &items)
{
    auto& db = syncDb();
    const auto token = syncTransactionToken();
    const auto existing_actions = co_await loadExistingActionIds(db, token);
    if (!existing_actions) {
        co_return false;
    }

    bool success = true;

    if (!isFullSync()) {
        // First we must delete any references in the actions/timeblock table
        const auto deleted_old_refs = token
            ? co_await db.queryBatchInTransaction(*token, "DELETE FROM time_block_actions WHERE time_block = ?", "", items,
            /* getArgs */ [](const nextapp::pb::TimeBlock& tb) {
                return tb.id_proto();
            },
            /* isDeleted */[](const auto&) {return false;},
            /* getId */ [](auto&){assert(false); return "";})
            : co_await db.queryBatch("DELETE FROM time_block_actions WHERE time_block = ?", "", items,
            /* getArgs */ [](const nextapp::pb::TimeBlock& tb) {
                return tb.id_proto();
            },
            /* isDeleted */[](const auto&) {return false;},
            /* getId */ [](auto&){assert(false); return "";}
        );
        if (!deleted_old_refs) {
            success = false;
        }
    }

    // Now, insert the new time blocks
    const auto updated_blocks = token
        ? co_await db.queryBatchInTransaction(*token, insert_query, "DELETE FROM time_block WHERE id = ?", items,
        getParams,
        /* isDeleted */ [](const nextapp::pb::TimeBlock& tb) {return tb.kind() == nextapp::pb::TimeBlock::Kind::DELETED;
        },
        /* getId */ [](const nextapp::pb::TimeBlock& tb){return tb.id_proto();})
        : co_await db.queryBatch(insert_query, "DELETE FROM time_block WHERE id = ?", items,
        getParams,
        /* isDeleted */ [](const nextapp::pb::TimeBlock& tb) {return tb.kind() == nextapp::pb::TimeBlock::Kind::DELETED;
        },
        /* getId */ [](const nextapp::pb::TimeBlock& tb){return tb.id_proto();}
    );
    if (!updated_blocks) {
        success = false;
    }

    for (const auto& tb : items) {
        runtime_.serverComm().clearSyncIssuesForObject(
            nextapp::pb::SyncObjectTypeGadget::SyncObjectType::SYNC_OBJECT_TYPE_TIME_BLOCK,
            tb.id_proto());
        if (tb.kind() != nextapp::pb::TimeBlock::Kind::DELETED) {
            recordMissingTimeBlockActionIssues(runtime_.serverComm(), tb, *existing_actions);
        }
    }

    // Rebuild refs best-effort and let SQLite reject missing dependencies.
    for (const auto& ref : getActionRefs(items, *existing_actions)) {
        QList<QVariant> params;
        params << ref.first << ref.second;
        const auto inserted_ref = token
            ? co_await db.legacyQueryInTransaction(
                *token, "INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)", &params)
            : co_await db.legacyQuery(
                "INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)", &params);
        if (!inserted_ref) {
            LOG_DEBUG_N << "Ignoring failed time block/action reference insert for time_block="
                        << ref.first << " action=" << ref.second << " err=" << inserted_ref.error();
        }
    }

    co_return success;
}

QCoro::Task<bool> CalendarCache::save_(const nextapp::pb::TimeBlock &tblock)
{
    auto& db = syncDb();
    const auto token = syncTransactionToken();
    const auto existing_actions = co_await loadExistingActionIds(db, token);
    if (!existing_actions) {
        co_return false;
    }
    runtime_.serverComm().clearSyncIssuesForObject(
        nextapp::pb::SyncObjectTypeGadget::SyncObjectType::SYNC_OBJECT_TYPE_TIME_BLOCK,
        tblock.id_proto());
    if (tblock.kind() != nextapp::pb::TimeBlock::Kind::DELETED) {
        recordMissingTimeBlockActionIssues(runtime_.serverComm(), tblock, *existing_actions);
    }

    // Remove all old references
    {
        QList<QVariant> params;
        const QString sql = "DELETE FROM time_block_actions WHERE time_block = ?";
        params << tblock.id_proto();
        const auto rval = token
            ? co_await db.legacyQueryInTransaction(*token, sql, &params)
            : co_await db.legacyQuery(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to delete time block refs: " << tblock.id_proto() << " err=" << rval.error();
            co_return false;
        }
    }

    if (tblock.kind() == nextapp::pb::TimeBlock::Kind::DELETED) {
        QList<QVariant> params;
        params << tblock.id_proto();
        QString sql = "DELETE FROM time_block WHERE id = ?";
        const auto rval = token
            ? co_await db.legacyQueryInTransaction(*token, sql, &params)
            : co_await db.legacyQuery(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to delete time block: " << tblock.id_proto() << " err=" << rval.error();
            co_return false;
        }
    } else {
        const auto params = getParams(tblock);
        const auto rval = token
            ? co_await db.legacyQueryInTransaction(*token, insert_query, &params)
            : co_await db.legacyQuery(insert_query, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to update time block: " << tblock.id_proto() << " " << tblock.name()
            << " err=" << rval.error();
            co_return false; // TODO: Add proper error handling. Probably a full resynch.
        }
    }

    // Add current refrerences
    if (tblock.kind() != nextapp::pb::TimeBlock::Kind::DELETED) {
        QList<QVariant> params;
        QSet<QString> seen;
        const auto& al = tblock.actions();
        for (const auto& aid : al.list()) {
            if (seen.contains(aid)) {
                continue;
            }
            seen.insert(aid);
            if (!existing_actions->contains(aid)) {
                continue;
            }
            const QString sql = "INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)";
            params.clear();
            params << tblock.id_proto();
            params << aid;
            const auto rval = token
                ? co_await db.legacyQueryInTransaction(*token, sql, &params)
                : co_await db.legacyQuery(sql, &params);
            if (!rval) {
                LOG_DEBUG_N << "Ignoring failed time block reference insert for " << tblock.id_proto()
                            << " action=" << aid << " err=" << rval.error();
            }
        }
    }

    co_return true;
}

QCoro::Task<bool> CalendarCache::loadFromCache()
{
    co_await updateActionsOnCalendarCache();
    co_return true;
}

void CalendarCache::requestAudioTimerUpdate()
{
    if (audio_timer_update_delay_.isActive()) {
        LOG_TRACE_N << "Audio timer update is already scheduled.";
        return;
    }

    audio_timer_update_delay_.start();
}

QCoro::Task<void> CalendarCache::setAudioTimers()
{
    // For now, find the next audio-event and set a timer for that.
    // In the future, we may want to set a timer for all audio-events.

    // Make sure we only enter once.
    if (audio_timers_in_progress_) {
        audio_timer_update_pending_ = true;
        LOG_TRACE_N << "Audio timers are already being set; queued one follow-up update.";
        co_return;
    }
    audio_timers_in_progress_ = true;

    ScopedExit exit_scope{[this] {
        audio_timers_in_progress_ = false;
        if (audio_timer_update_pending_) {
            audio_timer_update_pending_ = false;
            requestAudioTimerUpdate();
        }
    }};

    LOG_TRACE_N << "Setting audio timers...";

    if (!runtime_.settings().value("alarms/calendarEvent.enabled", true).toBool()) {
        LOG_TRACE_N << "Audio events are disabled.";
        co_return;
    }

    const auto now = time(nullptr);

    // Find the next event to end

    next_event_.reset();
    time_t next_time{};

    const auto pre_ending_mins = runtime_.settings().value("alarms/calendarEvent.SoonToEndMinutes", 5).toInt();
    const auto pre_start_mins = runtime_.settings().value("alarms/calendarEvent.SoonToStartMinutes", 1).toInt();

    // Get the next enevts that either start or ends after now.
    auto& db = syncDb();
    QList<QVariant> params;

    auto ae = make_unique<AudioEvent>();
    const auto all_events = co_await getCalendarEvents(QDate::currentDate(), QDate::currentDate().addDays(2));

    for(const auto &ev_p : all_events) {
        const auto& ev = *ev_p;
        if (ev.hasTimeSpan()) {
            const auto starting = ev.timeSpan().start();

            // Check actual start time AE_START
            if (starting > now && (!next_time || starting < next_time)) {
                next_time = starting;
                ae->event = ev_p;
                ae->kind = AudioEvent::Kind::AE_START;
            }

            // Check if the AE_SOON_START event is the next to start
            if (pre_start_mins > 0) {
                const auto pre_start = starting - (pre_start_mins * 60);
                if (pre_start > now && (!next_time || pre_start < next_time)) {
                    next_time = pre_start;
                    ae->event = ev_p;
                    ae->kind = AudioEvent::Kind::AE_PRE;
                }
            }

            // Break the loop if we are done with the relevant items
            if (next_time && starting > now) {
                break;
            }

            // Check actual end-time AE_END
            const auto ending = ev.timeSpan().end();
            if (ending > now && (!next_time || ending < next_time)) {
                next_time = ending;
                ae->event = ev_p;
                ae->kind = AudioEvent::Kind::AE_END;
            }

            // Check if the AE_SOON_ENDING event is the next to end
            if (pre_ending_mins > 0) {
                const auto pre_ending = ending - (pre_ending_mins * 60);
                if (pre_ending > now && (!next_time || pre_ending < next_time)) {
                    next_time = pre_ending;
                    ae->event = ev_p;
                    ae->kind = AudioEvent::Kind::AE_SOON_ENDING;
                }
            }
        }
    }

    if (next_time) {
        auto delay = max<int>(next_time - time(nullptr), 1);
        ae->timer.setSingleShot(true);
        ae->timer.start(delay * 1000);
        LOG_TRACE_N << "Next audio event is in " << delay << " seconds and of type " << static_cast<int>(ae->kind);
        next_event_ = std::move(ae);
        connect(&next_event_->timer, &QTimer::timeout, this, &CalendarCache::onAudioEvent, Qt::SingleShotConnection);
    } else {
        LOG_TRACE_N << "No future audio events found for today.";
    }
}

void CalendarCache::onAudioEvent()
{
    if (next_event_) {
        LOG_TRACE_N << "Audio event: " << next_event_->event->id_proto()
        << " at " << QDateTime::fromSecsSinceEpoch(next_event_->event->timeSpan().start()).toString();

        QString audio_file;
        switch(next_event_->kind) {
        case AudioEvent::Kind::AE_START:
            audio_file = "qrc:/qt/qml/NextAppUi/sounds/387351__cosmicembers__simple-ding.wav";
            break;
        case AudioEvent::Kind::AE_PRE:
        case AudioEvent::Kind::AE_SOON_ENDING:
            audio_file = "qrc:/qt/qml/NextAppUi/sounds/515643__mashedtatoes2__ding2_edit.wav";
            break;
        case AudioEvent::Kind::AE_END:
            audio_file = "qrc:/qt/qml/NextAppUi/sounds/611112__5ro4__bell-ding-2.wav";
            break;
        }

        const auto volume = runtime_.settings().value("alarms/calendarEvent.volume", 0.4).toDouble();
        LOG_DEBUG_N << "Playing audio: " << audio_file << " of type " << next_event_->kind << " at volume "
                    << volume;

        runtime_.playSound(volume, audio_file);
    } else {
        LOG_DEBUG_N << "Called, but there is no event to play audio for.";
    }

    // Set the timer for the next event
    requestAudioTimerUpdate();
}

QCoro::Task<void> CalendarCache::updateActionsOnCalendarCache()
{
    LOG_TRACE_N << "Updating actions on calendar cache for date " << current_calendar_date_.toString();

    if (is_updating_actions_cache_) {
        update_actions_cache_pending_ = true;
        co_return;
    }
    is_updating_actions_cache_ = true;
    ScopedExit exit_scope{[this] {
        is_updating_actions_cache_ = false;
        if (update_actions_cache_pending_) {
            update_actions_cache_pending_ = false;
            QTimer::singleShot(0, this, [this]{
                updateActionsOnCalendarCache();
            });
        }
    }};

    std::vector<QUuid> actions;

    auto& db = syncDb();
    auto res = co_await db.query(R"(SELECT tba.action FROM time_block_actions AS tba
JOIN time_block AS tb ON tb.id = tba.time_block
JOIN action AS a ON a.id = tba.action
WHERE DATE(tb.start_time) >= DATE(?)
AND DATE(tb.end_time) <= DATE(?))", current_calendar_date_, current_calendar_date_);

    if (!res) {
        LOG_ERROR_N << "Failed to get actions on current calendar date: " << current_calendar_date_.toString()
            << " err=" << res.error();
        co_return;
    }

    const auto& rows = res.value().rows;
    actions.reserve(rows.size());

    for (const auto& row : rows) {
        if (row.size() < 1) {
            LOG_ERROR_N << "Invalid action row while loading actions on current calendar";
            continue;
        }
        const auto& aid = row[0].toString();
        actions.push_back(QUuid{aid});
    }

    ActionsOnCurrentCalendar::instance()->setActions(actions);
}

std::shared_ptr<GrpcIncomingStream> CalendarCache::openServerStream(nextapp::pb::GetNewReq req)
{
    return runtime_.serverComm().synchTimeBlocks(req);
}

void CalendarCache::clear()
{
    events_.clear();
}

QCoro::Task<bool> CalendarCache::remove(const nextapp::pb::TimeBlock &tb)
{
    auto& db = syncDb();
    QList<QVariant> params;

    QString sql = "DELETE FROM time_block WHERE id = ?";
    params << tb.id_proto();
    auto res = co_await db.legacyQuery(sql, &params);
    if (!res) {
        LOG_ERROR_N << "Failed to delete time block: " << tb.id_proto() << " err=" << res.error();
        co_return false;
    }
    co_return true;
}

QCoro::Task<QList<std::shared_ptr<nextapp::pb::CalendarEvent> > > CalendarCache::getCalendarEvents(QDate start, QDate end)
{
    LOG_TRACE_N << "Getting calendar events from " << start.toString() << " to " << end.toString();
    QList<std::shared_ptr<nextapp::pb::CalendarEvent>> events;

    auto& db = syncDb();
    QList<QVariant> params;

    QString sql = "SELECT id, data FROM time_block WHERE start_time >= ? AND end_time < ? ORDER BY start_time";
    params << start.startOfDay();
    params << end.startOfDay();
    auto res = co_await db.legacyQuery(sql, &params);
    if (!res) {
        LOG_ERROR_N << "Failed to get time blocks: " << res.error();
        co_return events;
    }

    for (const auto& row : *res) {
        if (row.size() < 2) {
            LOG_ERROR_N << "Invalid time_block row while loading calendar events";
            continue;
        }
        const auto& id = row[0].toUuid();
        const auto& data = row[1].toByteArray();

        if (auto it = events_.find(id); it != events_.end()) {
            events << it->second;
        } else {
            QProtobufSerializer serializer;
            nextapp::pb::TimeBlock tb;
            if (!tb.deserialize(&serializer, data)) {
                LOG_ERROR_N << "Failed to deserialize time block: " << id.toString();
                continue;
            }
            auto ce = std::make_shared<nextapp::pb::CalendarEvent>();
            ce->setId_proto(tb.id_proto());
            ce->setTimeBlock(tb);
            ce->setTimeSpan(tb.timeSpan());
            events_[id] = ce;
            events << ce;
        }
    }

    LOG_TRACE_N << "Found " << events.size() << " events";
    co_return events;
}
