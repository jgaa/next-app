
#include <set>
#include <ranges>
#include <algorithm>

#include <QTimer>
#include <QSettings>


#include "CalendarModel.h"
#include "ServerComm.h"
#include "NextAppCore.h"
#include "util.h"
#include "ActionsModel.h"
#include "ActionsOnCurrentCalendar.h"
#include "ActionCategoriesModel.h"

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

QDate get_date(const nextapp::pb::CalendarEvent& event) {
    if (event.hasTimeSpan()) {
        return QDateTime::fromSecsSinceEpoch(event.timeSpan().start()).date();
    }
    return {};
}

} // anon ns

CalendarModel::CalendarModel()
{
    connect(NextAppCore::instance(), &NextAppCore::settingsChanged, this, [this]{
        setAudioTimers();
    });

    connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
        setOnline(ServerComm::instance().connected());
    });

    connect(&ServerComm::instance(), &ServerComm::firstDayOfWeekChanged, [this] {
        LOG_DEBUG_N << "First day of week changed";
        if (mode_ == CM_WEEK) {
            first_ = getFirstDayOfWeek(target_);
            last_ = first_.addDays(6);
            setValid(false);
            updateDayModelsDates();
            updateToday();
            fetchIf();
        }
    });

    connect(CalendarCache::instance(), &CalendarCache::eventRemoved, this, &CalendarModel::onCalendarEventRemoved);
    connect(CalendarCache::instance(), &CalendarCache::eventAdded, this, &CalendarModel::onCalendarEventAddedOrUpdated);
    connect(CalendarCache::instance(), &CalendarCache::eventUpdated, this, &CalendarModel::onCalendarEventAddedOrUpdated);
    connect(CalendarCache::instance(), &CalendarCache::stateChanged, [this] {
        fetchIf();
    });

    connect(&next_audio_event_, &QTimer::timeout, this, &CalendarModel::onAudioEvent);

    setOnline(ServerComm::instance().connected());

    minute_timer_ = std::make_unique<QTimer>(this);
    connect(minute_timer_.get(), &QTimer::timeout, this, &CalendarModel::onMinuteTimer);
    minute_timer_->start(1000);
    fetchIf();
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

void CalendarModel::set(CalendarMode mode, int year, int month, int day, bool primaryForActionList) {
    const auto was_valid = valid_;

    LOG_TRACE_N << "mode_=" << mode_ << " mode=" << mode << " year=" << year << " month=" << month << " day=" << day;
    is_primary_ = primaryForActionList;

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
        if (primaryForActionList) {
            NextAppCore::instance()->setProperty("primaryForActionList", QDate{});
        }
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
        updateToday();
    }

    if (need_fetch) {
        fetchIf();
    }

    updateIfPrimary();
}

void CalendarModel::moveEventToDay(const QString &eventId, time_t start)
{
    auto it = std::ranges::find_if(all_events_, [&eventId](const auto& event) {
        return event->id_proto() == eventId;
    });

    if (it == all_events_.end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return;
    }

    auto& event = *it;
    auto ts = event->timeSpan();
    const auto length = ts.end() - ts.start();

    ts.setStart(start);
    ts.setEnd(start + length);

    auto tb = event->timeBlock();
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

bool CalendarModel::addAction(const QString &eventId, const QString &action)
{
    if (auto* event = lookup(eventId)) {
        if (event->hasTimeBlock()) {
            auto tb = event->timeBlock();

            // Check if the action is already in the block
            const auto& list = tb.actions().list();
            if (std::ranges::find(list, action) == list.end()) {
                // Add it
                tb.setActions(nextapp::append(tb.actions(), action));
                ServerComm::instance().updateTimeBlock(tb);
                return true;
            }
        }
    }

    return false;
}

void CalendarModel::removeAction(const QString &eventId, const QString &action)
{
    if (auto* event = lookup(eventId)) {
        if (event->hasTimeBlock()) {
            auto tb = event->timeBlock();

            // Check if the action is already in the block
            const auto& list = tb.actions().list();
            auto it = std::ranges::find(list, action);
            if (it != list.end()) {
                // Remove it
                auto index = std::distance(list.begin(), it);
                tb.setActions(nextapp::remove(tb.actions(), index));
                //ServerComm::instance().updateTimeBlock(tb);
            }
        }
    }
}

CategoryUseModel *CalendarModel::getCategoryUseModel()
{
    //if (!category_use_model_) {
        auto get_list = [this]() {
            LOG_DEBUG_N << "Getting category usage list";
            return getCategoryUsage();
        };

        auto model = new CategoryUseModel(get_list);
        model->connect(this, &CalendarModel::validChanged, model, &CategoryUseModel::listChanged);
        model->setList(getCategoryUsage());
    //}

        return model;
    //return category_use_model_;
}

nextapp::pb::CalendarEvent *CalendarModel::lookup(const QString &eventId)
{
    auto it = std::ranges::find_if(all_events_, [&eventId](const auto& event) {
        return event->id_proto() == eventId;
    });

    if (it == all_events_.end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return nullptr;
    }

    return it->get();
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

QCoro::Task<void> CalendarModel::onCalendarEventAddedOrUpdated(const QUuid id)
{
    const auto id_str = id.toString(QUuid::WithoutBraces);
    std::set<QDate> modified_dates;
    std::vector<QDate> dates;

    // Make a list of all the dates we handle
    ranges::copy(
        views::transform(day_models_, [](const auto& pair) {
            return pair.second->date();
        }), std::back_inserter(dates));

    auto event_it = std::ranges::find_if(all_events_, [&id_str](const auto& e) {
        return e->id_proto() == id_str;
    });

    nextapp::pb::CalendarEvent *existing = (event_it == all_events_.end()) ? nullptr : event_it->get();

    auto event = CalendarCache::instance()->getFromCache(id);
    if (!event) {
        LOG_WARN_N << "Failed to get event from cache: " << id_str;
        co_return;
    }

    if (existing && version(*existing) > version(*event)) {
        LOG_TRACE_N << "Ignoring event with lower version: " << event->id_proto() << " with version " << version(*event);
        co_return;
    }

    QDate new_date = get_date(*event);
    QDate old_date = existing ? get_date(*existing) : QDate();
    bool removed = false;

    if (existing) {
        // Check id the updated event's date is inside our range of dates
        if (std::ranges::find(dates.begin(), dates.end(), new_date) != dates.end()) {
            LOG_TRACE_N << "Updating event: " << existing->id_proto() << " with start_time " << existing->timeSpan().start()
                        << " from incoming event with start-time " << event->timeSpan().start();
            *event_it = event;
            existing = event_it->get();
            LOG_TRACE_N << "Updated event: " << existing->id_proto() << " with start_time " << existing->timeSpan().start();
        } else {
            // We had it, but now it's outside our range of dates
            all_events_.erase(event_it);
            new_date = {};
            removed = true;
        }
    } else {
        // New event?
        if (std::ranges::find(dates.begin(), dates.end(), new_date) != dates.end()) {
            all_events_.push_back(event);
        } else {
            co_return; // Not in our range of dates, so we ignore it
        }
    }

    if (old_date.isValid()) {
        modified_dates.insert(old_date);
    }

    if (new_date.isValid()) {
        modified_dates.insert(new_date);
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

    if (is_primary_) {
        updateActionsOnCalendarCache();
    }
}

QCoro::Task<void> CalendarModel::onCalendarEventRemoved(const QUuid id)
{
    const auto id_str = id.toString(QUuid::WithoutBraces);
    auto it = std::ranges::find_if(all_events_, [&id_str](const auto& event) {
        return event->id_proto() == id_str;
    });

    if (it != all_events_.end()) {
        assert(*it);
        const auto event_day = get_date(**it);
        all_events_.erase(it);
        sort();
        updateDayModels();
        setValid(true);
        for (auto& [_, day] : day_models_) {
            if (event_day == day->date()) {
                day->setValid(true, true); // Trigger a redraw
            }
        }
    }

    if (is_primary_) {
        updateActionsOnCalendarCache();
    }

    co_return;
}


QCoro::Task<void> CalendarModel::fetchIf()
{
    LOG_TRACE_N << "Called. mode_=" << mode_ << " online_=" << online_ << " valid_=" << valid_;
    setValid(false);

    if (mode_ == CM_UNSET) {
        LOG_TRACE_N << "CM_UNSET, returning.";
        co_return;
    }

    if (CalendarCache::instance()->state() == CalendarCache::State::VALID) {

        auto events = co_await CalendarCache::instance()->getCalendarEvents(first_, last_.addDays(1));

        LOG_TRACE_N << "Received calendar data: " << events.size() << " events.";

        // Reset all the day models
        for (auto& [_, dm] : day_models_) {
            dm->events() = {};
        }

        all_events_ = events;

        updateDayModels();
        setValid(online_);
        if (online_ && is_primary_) {
            updateActionsOnCalendarCache();
        }
    }

    LOG_TRACE_N << "Done.";
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

    auto get_date = [](const nextapp::pb::CalendarEvent& ce) {
        return QDateTime::fromSecsSinceEpoch(ce.timeSpan().start()).date();
    };

    // Clear the data in all the day-models, since our data probably has changed
    // If events are deleted or moved, we may not visit a day in the loop below.
    for (auto& [_, dm] : day_models_) {
        dm->events() = {};
    }

    auto events = span(all_events_);
    auto start_of_day = events.begin();
    if (start_of_day != events.end()) {

        auto date = get_date(**start_of_day);

        // Step trough the days and update the day-models
        for (auto it = events.begin(); it != events.end(); ++it) {
            const auto cdate = get_date(**it);
            if ( date != cdate) {
                // Start of a new day
                handle_day(date, start_of_day, it);
                start_of_day = it;
                date = cdate;
            }
        }

        handle_day(date, start_of_day, events.end());
    }

    setAudioTimers();
    setValid(online_);
}

void CalendarModel::sort()
{
    ranges::sort(all_events_, [](const auto& a, const auto& b) {
        return compare(*a, *b);
    });
}

void CalendarModel::updateDayModelsDates()
{
    for (auto& [_, dm] : day_models_) {
        dm->setDate(first_.addDays(dm->index()));
    }
}

void CalendarModel::alignDates()
{
    set(mode_, target_.year(), target_.month(), target_.day(), is_primary_);
}

void CalendarModel::onMinuteTimer()
{
    updateToday();
    const auto msToNextMinute = (60 - QTime::currentTime().second()) * 1000;
    minute_timer_->start(msToNextMinute);
}

void CalendarModel::updateToday()
{
    const auto today = QDate::currentDate();
    for(auto& [_, dm] : day_models_) {
        if (dm->date() == today) {
            dm->setToday(true);
            dm->emitTimeChanged();
        } else {
            dm->setToday(false);
        }
    }
}

void CalendarModel::setAudioTimers()
{
    // For now, find the next audio-event and set a timer for that.
    // In the future, we may want to set a timer for all audio-events.

    LOG_TRACE_N << "Called.";

    QSettings settings{};

    if (!settings.value("alarms/calendarEvent.enabled", true).toBool()) {
        LOG_TRACE_N << "Audio events are disabled.";
        return;
    }

    const auto now = time(nullptr);

    // Find the next event to end

    audio_event_ = {};
    time_t next_time{};

    const auto pre_ending_mins = settings.value("alarms/calendarEvent.SoonToEndMinutes", 5).toInt();
    const auto pre_start_mins = settings.value("alarms/calendarEvent.SoonToStartMinutes", 1).toInt();

    for(const auto &ev_p : all_events_) {
        const auto& ev = *ev_p;
        if (ev.hasTimeSpan()) {
            const auto starting = ev.timeSpan().start();

            // Check actual start time AE_START
            if (starting > now && (!next_time || starting < next_time)) {
                next_time = starting;
                audio_event_ = &ev;
                audio_event_type_ = AudioEventType::AE_START;
            }

            // Check if the AE_SOON_START event is the next to start
            if (pre_start_mins > 0) {
                const auto pre_start = starting - (pre_start_mins * 60);
                if (pre_start > now && (!next_time || pre_start < next_time)) {
                    next_time = pre_start;
                    audio_event_ = &ev;
                    audio_event_type_ = AudioEventType::AE_PRE;
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
                audio_event_ = &ev;
                audio_event_type_ = AudioEventType::AE_END;
            }

            // Check if the AE_SOON_ENDING event is the next to end
            if (pre_ending_mins > 0) {
                const auto pre_ending = ending - (pre_ending_mins * 60);
                if (pre_ending > now && (!next_time || pre_ending < next_time)) {
                    next_time = pre_ending;
                    audio_event_ = &ev;
                    audio_event_type_ = AudioEventType::AE_SOON_ENDING;
                }
            }
        }
    }

    if (next_time) {
        auto delay = max<int>(next_time - time(nullptr), 1);
        next_audio_event_.setSingleShot(true);
        next_audio_event_.start(delay * 1000);
        LOG_TRACE_N << "Next audio event is in " << delay << " seconds and of type " << audio_event_type_;
    } else {
        next_audio_event_.stop();
        audio_event_ = {};
    }
}

void CalendarModel::onAudioEvent()
{
    QSettings settings{};
    if (audio_event_) {
        LOG_TRACE_N << "Audio event: " << audio_event_->id_proto()
                    << " at " << QDateTime::fromSecsSinceEpoch(audio_event_->timeSpan().start()).toString();

        QString audio_file;
        switch(audio_event_type_) {
            case AudioEventType::AE_START:
                audio_file = "qrc:/qt/qml/NextAppUi/sounds/387351__cosmicembers__simple-ding.wav";
                break;
            case AudioEventType::AE_PRE:
            case AudioEventType::AE_SOON_ENDING:
                audio_file = "qrc:/qt/qml/NextAppUi/sounds/515643__mashedtatoes2__ding2_edit.wav";
                break;
            case AudioEventType::AE_END:
                audio_file = "qrc:/qt/qml/NextAppUi/sounds/611112__5ro4__bell-ding-2.wav";
                break;
        }

        const auto volume = settings.value("alarms/calendarEvent.volume", 0.4).toDouble();
        LOG_DEBUG_N << "Playing audio: " << audio_file << " of type " << audio_event_type_ << " at volume "
                    << volume;

        NextAppCore::instance()->playSound(volume, audio_file);
    } else {
        LOG_DEBUG_N << "Called, but there is no event to play audio for.";
    }

    // Set the timer for the next event
    setAudioTimers();
}

void CalendarModel::updateIfPrimary()
{
#ifdef ANDROID
    if (first_ == last_) {
#else
    if (is_primary_) {
#endif
        assert(first_ == last_);
        NextAppCore::instance()->setProperty("primaryForActionList", first_);
    }
}

void CalendarModel::updateActionsOnCalendarCache()
{
    std::vector<QUuid> actions;
    actions.reserve(96);

    for(const auto& event : all_events_) {
        if (event->hasTimeBlock()) {
            ranges::transform(event->timeBlock().actions().list(), std::back_inserter(actions), [](const auto& action) {
                return QUuid{action};
            });
        }
    }

    ranges::transform(all_events_, std::back_inserter(actions), [](const auto& event) {
        return QUuid(event->id_proto());
    });

    ActionsOnCurrentCalendar::instance()->setActions(actions);
}

CategoryUseModel::list_t CalendarModel::getCategoryUsage()
{
    std::map<QString, CategoryUseModel::Data> use;
    CategoryUseModel::list_t list;
    uint total = 0;
    uint without_cat = 0;
    if (valid_) {
        for(const auto& event : all_events_) {
            if (event->hasTimeBlock()) {
                const auto& tb = event->timeBlock();
                const uint duration_minutes = (tb.timeSpan().end() - tb.timeSpan().start()) / 60;
                total += duration_minutes;

                if (tb.category().isEmpty()) {
                    without_cat += duration_minutes;
                } else if (auto it = use.find(tb.category()); it != use.end()) {
                    it->second.minutes += duration_minutes;
                } else {
                    const auto& cat = ActionCategoriesModel::instance().getFromUuid(tb.category());
                    use[tb.category()] = {cat.name(), cat.color(), duration_minutes};
                };
            }
        }
    }

    vector<CategoryUseModel::Data> values;
    ranges::transform(use, std::back_inserter(values),
                      [](const auto& pair) { return pair.second; });

    if (without_cat) {
        values.push_back({tr("No category"), "transparent", without_cat});
    }

    ranges::sort(values, ranges::greater{}, &CategoryUseModel::Data::minutes);
    values.push_back({tr("Total"), "transparent", total});

    return values;
}

