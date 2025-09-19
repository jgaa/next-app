#pragma once

#include <QObject>
#include <QList>

#include "nextapp.qpb.h"

#include "ServerSynchedCahce.h"

/*@ The cache for calendar events
 *
 * This cache is used to store the calendar events for the user.
 *
 * The cache is synchronized with the server and the events can be
 * added, removed and updated.
 *
 * Currently it supports TimeBlock's.
 */

class CalendarCache : public QObject
    , public ServerSynchedCahce<nextapp::pb::TimeBlock, CalendarCache>
{
    Q_OBJECT
public:
    struct AudioEvent {
        enum Kind {
            AE_PRE,
            AE_START,
            AE_SOON_ENDING,
            AE_END,
        };

        std::shared_ptr<nextapp::pb::CalendarEvent> event;
        Kind kind{Kind::AE_START};
        QTimer timer;
    };


    CalendarCache();

    static CalendarCache *instance() noexcept;
    [[nodiscard]] QCoro::Task<QList<std::shared_ptr<nextapp::pb::CalendarEvent>>> getCalendarEvents(QDate start, QDate end);

    std::shared_ptr<nextapp::pb::CalendarEvent> getFromCache(const QUuid& id) const noexcept {
        if (auto it = events_.find(id); it != events_.end()) {
            return it->second;
        }
        return {};
    }

signals:
    void eventAdded(const QUuid& id);
    void eventRemoved(const QUuid& id);
    void eventUpdated(const QUuid& id);
    void stateChanged();
    void audioEvent(const QUuid& id, AudioEvent::Kind kind);

private:
    bool haveBatch() const noexcept override { return true; }
    QCoro::Task<bool> saveBatch(const QList<nextapp::pb::TimeBlock>& items) override;
    QCoro::Task<void> pocessUpdate(const std::shared_ptr<nextapp::pb::Update> update) override;
    QCoro::Task<bool> save(const QProtobufMessage& item) override;
    QCoro::Task<bool> save_(const nextapp::pb::TimeBlock& block);
    QCoro::Task<bool> loadFromCache() override;
    QCoro::Task<void> setAudioTimers();
    void onAudioEvent();
    QCoro::Task<void> updateActionsOnCalendarCache();

    bool hasItems(const nextapp::pb::Status& status) const noexcept override {
        return status.hasTimeBlocks();
    }
    bool isRelevant(const nextapp::pb::Update& update) const noexcept override {
        return update.hasCalendarEvents() || update.hasAction();
    }
    QList<nextapp::pb::TimeBlock> getItems(const nextapp::pb::Status& status) override{
        return status.timeBlocks().blocks();
    }
    std::string_view itemName() const noexcept override {
        return "time_block";
    }
    std::shared_ptr<GrpcIncomingStream> openServerStream(nextapp::pb::GetNewReq req) override;
    void clear() override;
    QCoro::Task<bool> remove(const nextapp::pb::TimeBlock& tb);

    std::map<QUuid, std::shared_ptr<nextapp::pb::CalendarEvent>> events_;
    std::unique_ptr<AudioEvent> next_event_;
    QDate current_calendar_date_{QDate::currentDate()};
    bool is_updating_actions_cache_{false};
    bool update_actions_cache_pending_{false};
};
