
#include <set>
#include <ranges>
#include <algorithm>

#include <QTimer>
#include "CalendarModel.h"
#include "ServerComm.h"
#include "util.h"

using namespace std;

namespace {
bool compare(const nextapp::pb::CalendarEvent& a, const nextapp::pb::CalendarEvent& b) {

    if (const auto diff = a.timeSpan().start() - b.timeSpan().start(); diff != 0) {
        return diff < 0;
    }

    if (const auto diff = a.timeSpan().end() - b.timeSpan().end(); diff != 0) {
        return diff < 0;
    }

    return a.id_proto() < b.id_proto();
}

int version(const nextapp::pb::CalendarEvent& event) {
    if (event.hasTimeBlock()) {
        return event.timeBlock().version();
    }
    return -1;
};


} // anon ns

CalendarModel::CalendarModel()
{
    connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
        setOnline(ServerComm::instance().connected());
    });

    connect(&ServerComm::instance(), &ServerComm::onUpdate, this, &CalendarModel::onUpdate);
    connect(&ServerComm::instance(), &ServerComm::firstDayOfWeekChanged, [this] {
        LOG_DEBUG_N << "First day of week changed";
        if (mode_ == CM_WEEK) {
            first_ = getFirstDayOfWeek(target_);
            last_ = first_.addDays(6);
            setValid(false);
            updateDayModelsDates();
            fetchIf();
        }
    });

    setOnline(ServerComm::instance().connected());
}

CalendarDayModel *CalendarModel::getDayModel(QObject *obj, int index) {
    assert(obj != nullptr);
    auto& dm = day_models_[obj];

    QDate when = first_.isValid() ? first_.addDays(index) : QDate{};

    if (!dm) {
        dm = std::make_unique<CalendarDayModel>(when, *obj, *this, index);
        QQmlEngine::setObjectOwnership(dm.get(), QQmlEngine::CppOwnership);
    };

    LOG_TRACE_N << "Returning day model for " << when.toString() << " with index " << index;

    return dm.get();
}

void CalendarModel::set(CalendarMode mode, int year, int month, int day) {
    const auto was_valid = valid_;

    LOG_TRACE_N << "mode_=" << mode_ << " mode=" << mode << " year=" << year << " month=" << month << " day=" << day;

    bool need_fetch = false;

    if (mode_ != mode) {
        mode_ = mode;
        setValid(false);
        emit modeChanged();
        need_fetch = true;
    }

    QDate when(year, month, day), first, last;
    target_ = when;

    switch(mode) {
    case CM_UNSET:
        setValid(false);
        need_fetch = false;
        return;
    case CM_DAY:
        first = when;
        last = when;
        break;
    case CM_WEEK:
        first = getFirstDayOfWeek(when);
        last = first.addDays(6);
        break;
    case CM_MONTH:
        first = when.addDays(-when.day());
        last = when.addDays(when.daysInMonth() - when.day());
        break;
    }

    if (first_ != first || last_ != last) {
        first_ = first;
        last_ = last;
        setValid(false);
        need_fetch = true;
        updateDayModelsDates();
    }

    if (need_fetch) {
        fetchIf();
    }
}

void CalendarModel::moveEventToDay(const QString &eventId, time_t start)
{
    auto it = std::ranges::find_if(all_events_.events(), [&eventId](const auto& event) {
        return event.id_proto() == eventId;
    });

    if (it == all_events_.events().end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return;
    }

    auto& event = *it;
    auto ts = event.timeSpan();
    const auto length = ts.end() - ts.start();

    ts.setStart(start);
    ts.setEnd(start + length);

    auto tb = event.timeBlock();
    tb.setTimeSpan(ts);

    ServerComm::instance().updateTimeBlock(tb);
}

void CalendarModel::deleteTimeBlock(const QString &eventId)
{
    ServerComm::instance().deleteTimeBlock(eventId);
}

time_t CalendarModel::getDate(int index)
{
    return first_.addDays(index).startOfDay().toSecsSinceEpoch();
}

QString CalendarModel::getDateStr(int index)
{
    return first_.addDays(index).toString("ddd, MMM d");
}

void CalendarModel::goPrev()
{
    switch(mode_) {
        case CM_DAY:
        target_ = target_.addDays(-1);
        break;
        case CM_WEEK:
        target_ = target_.addDays(-7);
        break;
        case CM_MONTH:
        target_ = target_.addMonths(-1);
        break;
        case CM_UNSET:
        break;
    }

    alignDates();
}

void CalendarModel::goNext()
{
    switch (mode_) {
    case CM_DAY:
        target_ = target_.addDays(1);
        break;
    case CM_WEEK:
        target_ = target_.addDays(7);
        break;
    case CM_MONTH:
        target_ = target_.addMonths(1);
        break;
    case CM_UNSET:
        break;
    }

    alignDates();
}

void CalendarModel::goToday()
{
    target_ = QDate::currentDate();
    alignDates();
}

void CalendarModel::setValid(bool value) {
    LOG_TRACE_N << "valid_=" << valid_ << " value=" << value;
    if (valid_ != value) {
        valid_ = value;
        emit validChanged();
        for (auto& [_, dm] : day_models_) {
            dm->setValid(valid_);
        }
    }
}

void CalendarModel::setTarget(QDate target)
{
    if (target_ == target) {
        return;
    }
    target_ = target;
    emit targetChanged();
}

QDate get_date(const nextapp::pb::CalendarEvent& event) {
    if (event.hasTimeSpan()) {
        return QDateTime::fromSecsSinceEpoch(event.timeSpan().start()).date();
    }
    return {};
}

void CalendarModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    // TODO: Handle Action updates (done, blocked, deleted)
    // TODO: Handle other events that have cascading delete effects on the calendar
    if (update->hasCalendarEvents()) {
        onCalendarEventUpdated(update->calendarEvents(), update->op());
    }
}

void CalendarModel::onCalendarEventUpdated(const nextapp::pb::CalendarEvents &events, nextapp::pb::Update::Operation op)
{
    std::set<QDate> modified_dates;
    std::vector<QDate> dates;

    // Make a list of all the dates we handle
    ranges::copy(
        views::transform(day_models_, [](const auto& pair) {
            return pair.second->date();
    }), std::back_inserter(dates));

    for(const auto& event : events.events()) {
        auto event_it = std::ranges::find_if(all_events_.events(), [&event](const auto& e) {
            return e.id_proto() == event.id_proto();
        });

        nextapp::pb::CalendarEvent *existing = (event_it == all_events_.events().end()) ? nullptr : &*event_it;

        if (existing && version(*existing) > version(event)) {
            LOG_TRACE_N << "Ignoring event with lower version: " << event.id_proto() << " with version " << version(event);
            continue;
        }

        QDate new_date = get_date(event);
        QDate old_date = existing ? get_date(*existing) : QDate();

        switch(op) {
            case nextapp::pb::Update_QtProtobufNested::Operation::DELETED:
                if (existing) {
                    all_events_.events().erase(event_it);
                } else {
                    continue; // irrelevant
                }
                break;
            case nextapp::pb::Update_QtProtobufNested::Operation::ADDED:
add:
                if (std::ranges::find(dates.begin(), dates.end(), new_date) != dates.end()) {
                    all_events_.events().push_back(event);
                } else {
                    continue ; // irrelevant
                }
                break;
            case nextapp::pb::Update_QtProtobufNested::Operation::UPDATED:
                if (existing) {
                    assert(old_date.isValid());

                    // Check id the updated event's date is inside our range of dates
                    if (std::ranges::find(dates.begin(), dates.end(), new_date) != dates.end()) {
                        LOG_TRACE_N << "Updating event: " << existing->id_proto() << " with start_time " << existing->timeSpan().start() << " from incoming event with start-time " << event.timeSpan().start();
                        *event_it = event;
                        existing = &*event_it;
                        LOG_TRACE_N << "Updated event: " << existing->id_proto() << " with start_time " << existing->timeSpan().start();
                    } else {
                        // We had it, but now it's outside our range of dates
                        all_events_.events().erase(event_it);
                        new_date = {};
                    }
                } else /* an event may have moved into our range */ {
                    goto add;
                }
                break;
            case nextapp::pb::Update_QtProtobufNested::Operation::MOVED:
                assert(false && "Move not used/implemented");
                break;
        }

        if (old_date.isValid()) {
            modified_dates.insert(old_date);
        }

        if (new_date.isValid()) {
            modified_dates.insert(new_date);
        }
    }

    if (!modified_dates.empty()) {
        sort();
        updateDayModels();

        for (auto& [_, day] : day_models_) {
            if (modified_dates.contains(day->date())) {
                day->setValid(true, true); // Trigger a redraw
            }
        }
    }
}

void CalendarModel::fetchIf()
{
    setValid(false);

    if (mode_ == CM_UNSET) {
        return;
    }

    if (online_ && !day_models_.empty()) {
        ServerComm::instance().fetchCalendarEvents(first_, last_, [this](auto val) ->void {
            if (std::holds_alternative<ServerComm::CbError>(val)) {
                LOG_WARN_N << "Failed to get calendar events: " << std::get<ServerComm::CbError>(val).message;
                setValid(false);
                // TODO: What do we do now? Retry after a while?
                return;
            }

            auto& events = std::get<nextapp::pb::CalendarEvents>(val);
            onReceivedCalendarData(events);
        });
    }
}

void CalendarModel::setOnline(bool online)
{
    LOG_TRACE_N << "online_=" << online_ << " online=" << online;
    if (online != online_) {
        online_ = online;
        if (online_) {
            QTimer::singleShot(0, this, &CalendarModel::fetchIf);
        } else {
            setValid(false);
        }
    }
}

void CalendarModel::onReceivedCalendarData(nextapp::pb::CalendarEvents &data)
{
    // We now have the data for all the days we handle.
    LOG_TRACE_N << "Received calendar data: " << data.events().size() << " events.";

    // Reset all the day models
    for (auto& [_, dm] : day_models_) {
        dm->events() = {};
    }

    all_events_.events().swap(data.events());

    updateDayModels();
    setValid(online_);
}

void CalendarModel::updateDayModels()
{
    // Assign the events for one day to any day model that has that date
    auto handle_day = [this](const QDate& day, auto begin, auto end) {
        for(auto& [_, dm] : day_models_) {
            auto range = span{begin, end};
            LOG_TRACE_N << "Filling " << range.size() << "events on day: " << dm->date().toString() << " for events on " << day.toString();
            if (dm->date() == day) {
                dm->events() = range;
            }
        }
    };

    auto get_date = [](const auto& it) {
        return QDateTime::fromSecsSinceEpoch(it->timeSpan().start()).date();
    };

    // Clear the data in all the day-models, since our data probably has changed
    // If events are deleted or moved, we may not visit a day in the loop below.
    for (auto& [_, dm] : day_models_) {
        dm->events() = {};
    }

    auto events = span(all_events_.events());
    auto start_of_day = events.begin();
    if (start_of_day != events.end()) {

        auto date = get_date(start_of_day);

        // Step trough the days and update the day-models
        for (auto it = events.begin(); it != events.end(); ++it) {
            const auto cdate = get_date(it);
            if ( date != cdate) {
                // Start of a new day
                handle_day(date, start_of_day, it);
                start_of_day = it;
                date = cdate;
            }
        }

        handle_day(date, start_of_day, events.end());
    }

    setValid(online_);
}

void CalendarModel::sort()
{
    ranges::sort(all_events_.events(), compare);
}

void CalendarModel::updateDayModelsDates()
{
    for (auto& [_, dm] : day_models_) {
        dm->setDate(first_.addDays(dm->index()));
    }
}

void CalendarModel::alignDates()
{
    set(mode_, target_.year(), target_.month(), target_.day());
}

