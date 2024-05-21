
#include <ranges>
#include <algorithm>

#include <QTimer>
#include "CalendarModel.h"
#include "ServerComm.h"

using namespace std;

CalendarModel::CalendarModel()
{
    connect(&ServerComm::instance(), &ServerComm::connectedChanged, [this] {
        setOnline(ServerComm::instance().connected());
    });

    setOnline(ServerComm::instance().connected());
}

CalendarDayModel *CalendarModel::getDayModel(QObject *obj, int year, int month, int day) {
    assert(obj != nullptr);
    auto& dm = day_models_[obj];
    if (!dm) {
        dm = std::make_unique<CalendarDayModel>(QDate(year, month, day), *obj);
        QQmlEngine::setObjectOwnership(dm.get(), QQmlEngine::CppOwnership);
    };
    return dm.get();
}

void CalendarModel::set(CalendarMode mode, int year, int month, int day) {
    const auto was_valid = valid_;

    LOG_TRACE_N << "mode_=" << mode_ << " mode=" << mode << " year=" << year << " month=" << month << " day=" << day;

    if (mode_ != mode) {
        mode_ = mode;
        setValid(false);
        emit modeChanged();
    }

    QDate when(year, month, day), first, last;

    switch(mode) {
    case CM_UNSET:
        setValid(false);
        return;
    case CM_DAY:
        first = when;
        last = when;
        break;
    case CM_WEEK:
        first = when.addDays(-when.dayOfWeek());
        last = when.addDays(6 - when.dayOfWeek());
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
    }

    fetchIf();
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

void CalendarModel::fetchIf()
{
    setValid(false);
    if (mode_ == CM_UNSET) {
        return;
    }

    if (online_) {
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
    if (online != online_) {
        online_ = online;
        QTimer::singleShot(0, this, &CalendarModel::fetchIf);
        if (!online_) {
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
            if (dm->date() == day) {
                dm->events() = span(begin, end);
            }
        }
    };

    auto get_date = [](const auto& it) {
        return QDateTime::fromSecsSinceEpoch(it->timeSpan().start()).date();
    };

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

