

#include <QProtobufSerializer>
#include "CalendarCache.h"

CalendarCache::CalendarCache() {
    connect(&ServerComm::instance(), &ServerComm::onUpdate,
        [this](const std::shared_ptr<nextapp::pb::Update>& update) {
            onUpdate(update);
        });
}

CalendarCache *CalendarCache::instance() noexcept
{
    static CalendarCache instance;
    return &instance;
}

QCoro::Task<void> CalendarCache::pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update)
{
    const auto op = update->op();

    if (update->hasCalendarEvents()) {
        const auto& ce_list = update->calendarEvents().events();

        for (const auto& ce : ce_list) {
            const QUuid id{ce.id_proto()};

            if (ce.hasTimeBlock()) {
                const auto tb = ce.timeBlock();
                if (op == ::nextapp::pb::Update::Operation::DELETED) {
                    assert(tb.kind() == nextapp::pb::TimeBlock::Kind::DELETED);
                    events_.erase(id);
                    co_await save_(tb); // deletes the time block
                    emit eventRemoved(id);
                    continue;
                }
                assert(tb.kind() != nextapp::pb::TimeBlock::Kind::DELETED);
                co_await save_(tb);

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

            } // if timeblock
        }
    }
}

QCoro::Task<bool> CalendarCache::save(const QProtobufMessage &item)
{
    return save_(static_cast<const nextapp::pb::TimeBlock&>(item));
}

QCoro::Task<bool> CalendarCache::save_(const nextapp::pb::TimeBlock &tblock)
{
    auto& db = NextAppCore::instance()->db();

    // Remove all old references
    {
        QList<QVariant> params;
        const QString sql = "DELETE FROM time_block_actions WHERE time_block = ?";
        params << tblock.id_proto();
        const auto rval = co_await db.query(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to delete time block: " << tblock.id_proto() << " err=" << rval.error();
            co_return false;
        }
    }

    if (tblock.kind() == nextapp::pb::TimeBlock::Kind::DELETED) {
        QList<QVariant> params;
        co_await remove(tblock);
        params << tblock.id_proto();
        QString sql = "DELETE FROM time_block WHERE id = ?";
        const auto rval = co_await db.query(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to delete time block: " << tblock.id_proto() << " err=" << rval.error();
            co_return false;
        }
    } else {
        static const QString sql = R"(INSERT INTO time_block (id, start_time, end_time, kind, data, updated)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
            start_time = excluded.start_time,
            end_time = excluded.end_time,
            kind = excluded.kind,
            data = excluded.data,
            updated = excluded.updated)";

        QList<QVariant> params;
        params << tblock.id_proto();
        params << QDateTime::fromSecsSinceEpoch(tblock.timeSpan().start());
        params << QDateTime::fromSecsSinceEpoch(tblock.timeSpan().end());
        params << static_cast<int>(tblock.kind());
        QProtobufSerializer serializer;
        params << tblock.serialize(&serializer);
        params << static_cast<qlonglong>(tblock.updated());

        const auto rval = co_await db.query(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to update action: " << tblock.id_proto() << " " << tblock.name()
            << " err=" << rval.error();
            co_return false; // TODO: Add proper error handling. Probably a full resynch.
        }
    }

    // Add current refrerences
    if (tblock.kind() != nextapp::pb::TimeBlock::Kind::DELETED) {
        QList<QVariant> params;
        const auto& al = tblock.actions();
        for(const auto aid : al.list()) {
            const QString sql = "INSERT INTO time_block_actions (time_block, action) VALUES (?, ?)";
            params.clear();
            params << tblock.id_proto();
            params << aid;
            const auto rval = co_await db.query(sql, &params);
            if (!rval) {
                LOG_WARN_N << "Failed to insert time block reference: " << tblock.id_proto() << " err=" << rval.error();
                co_return false;
            }
        }
    }

    co_return true;
}

QCoro::Task<bool> CalendarCache::loadFromCache()
{
    // Nothing to read by default.
    co_return true;
}

std::shared_ptr<GrpcIncomingStream> CalendarCache::openServerStream(nextapp::pb::GetNewReq req)
{
    return ServerComm::instance().synchTimeBlocks(req);
}

void CalendarCache::clear()
{
    events_.clear();
}

QCoro::Task<bool> CalendarCache::remove(const nextapp::pb::TimeBlock &tb)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    QString sql = "DELETE FROM time_block WHERE id = ?";
    params << tb.id_proto();
    auto res = co_await db.query(sql, &params);
    if (!res) {
        LOG_ERROR_N << "Failed to delete time block: " << tb.id_proto() << " err=" << res.error();
        co_return false;
    }
    co_return true;
}

QCoro::Task<QList<std::shared_ptr<nextapp::pb::CalendarEvent> > > CalendarCache::getCalendarEvents(QDate start, QDate end)
{
    QList<std::shared_ptr<nextapp::pb::CalendarEvent>> events;

    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    QString sql = "SELECT id, data FROM time_block WHERE start_time >= ? AND end_time < ? ORDER BY start_time";
    params << start.startOfDay();
    params << end.startOfDay();
    auto res = co_await db.query(sql, &params);
    if (!res) {
        LOG_ERROR_N << "Failed to get time blocks: " << res.error();
        co_return events;
    }

    for (const auto& row : *res) {
        assert(row.size() >= 2);
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

    co_return events;
}
