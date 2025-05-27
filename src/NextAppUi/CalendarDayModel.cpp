
#include <algorithm>
#include <ranges>

#include <QQmlComponent>
#include <QQmlProperty>
#include <QQuickItem>

#include "NextAppCore.h"
#include "CalendarDayModel.h"
#include "CalendarModel.h"
#include "ServerComm.h"
#include "TimeBoxActionsModel.h"
#include "ActionCategoriesModel.h"

using namespace std;

namespace {

struct Item {
    Item(const nextapp::pb::CalendarEvent& event, time_t start = 0, time_t end = 0)
        : event{event}, start{start}, end{end} {}

    const nextapp::pb::CalendarEvent& event;
    time_t start{};
    time_t end{};
    int col{};
    int cols{1};
    int overlap{};
    int span_cols{1};
};

using items_t = vector<Item>;

items_t calculatePlacement(CalendarDayModel::events_t events) {
    if (events.empty()) {
        return {};
    }

    items_t items;
    items.reserve(events.size());
    vector<Item *> current;

    for(auto& event : events) {
        if (!event->hasTimeBlock()) {
            LOG_WARN_N << "Event " << event->id_proto() << " has no time-block";
            continue;
        }

        auto& curr = items.emplace_back(*event,
                                        static_cast<time_t>(event->timeSpan().start()),
                                        static_cast<time_t>(event->timeSpan().end()));

        // Remove expired entries
        std::erase_if(current, [&curr](const auto& v) {
            return v->end <= curr.start;
        });

        // Find an available column for curr
        // First, mark used cols
        // If the user has more than 8 overlapping things to do, well - too bad
        std::array<bool, 8> cols = {};
        for(const auto *c : current) {
            if (c->col < cols.size()) {
                cols[c->col] = true;
            }
        }

        // Get leftmost free column
        for(auto c: cols) {
            if (!c) {
                break;
            }
            ++curr.col;
        }

        current.push_back(&curr);

        // Any item at this point has at least current.size() cols
        std::ranges::for_each(current, [&](auto& v) {
            v->overlap = max<int>(v->overlap, current.size());
        });
    }

    // Now, make sure any items that overlap has the same number of columns
    // as the ones they overlap with
    for(auto it = items.begin(); it != items.end(); /*++it*/) {

        vector<Item *> block{&*it};
        auto last_ends = it->end;
        auto max_overlaps = it->overlap;
        auto it2 = it;
        while(++it2 != items.end()) {
            if (last_ends <= it2->start) {
                // End of a block of overlapping items
                break;
            }
            block.push_back(&*it2);
            last_ends = max(last_ends, it2->end);
            max_overlaps = max(max_overlaps, it2->overlap);
        }
        for(auto *b : block) {
            b->cols = max_overlaps;
        }
        it = it2;
    }

    // Try to optimize unused space by making the item span more columns
    for(auto it = items.begin(); it != items.end(); ++it) {
        if (it->overlap < it->cols) {
            auto overlaps_with = views::filter(items, [it](const Item& item) {
                // Does item overlap with item?
                return item.start < it->end && item.end > it->start;
            });

            // Get a bitmap over the used columns
            std::array<bool, 8> cols = {};
            for(const auto& c : overlaps_with) {
                LOG_DEBUG << it->event.timeBlock().name() << " overlaps with " << c.event.timeBlock().name();
                cols[c.col] = true;
            }

            const auto orig_col = it->col;

            // Se if we can expand the current item left or right
            while(it->col > 0) {
                if (cols[it->col - 1]) {
                    break;
                }
                --it->col, ++it->span_cols;
            }

            for(auto col = orig_col + 1; col < min<size_t>(it->cols, cols.size()); ++col) {
                if (cols[col]) {
                    break;
                }
                ++it->span_cols;
            }
        }
    }

    return items;
}

} //anon ns

CalendarDayModel::CalendarDayModel(QDate date, QObject& component, CalendarModel& calendar, int index, QObject *parent)
    : QObject(parent)    
    , date_(date)
    , index_{index}
    , component_{component}
    , calendar_{calendar}
{
    connect(&ServerComm::instance(), &ServerComm::globalSettingsChanged, this, &CalendarDayModel::setWorkHours);
    setWorkHours();
}

CalendarDayModel::~CalendarDayModel()
{
    LOG_TRACE_N << "Destroying day model for " << date_.toString() << " with index=" << index_;
}

void CalendarDayModel::createTimeBox(QString name, QString category, int start, int end)
{
    nextapp::pb::TimeBlock tb;
    tb.setName(name);
    tb.setCategory(category);

    if (start >= end) {
        qWarning() << "Invalid timebox start/end";
        return;
    }

    nextapp::pb::TimeSpan ts;
    ts.setStart(date_.startOfDay().addSecs(start * 60).toSecsSinceEpoch());
    ts.setEnd(date_.startOfDay().addSecs(end * 60).toSecsSinceEpoch());
    tb.setTimeSpan(ts);

    ServerComm::instance().addTimeBlock(tb);
}

nextapp::pb::CalendarEvent CalendarDayModel::event(int index) const noexcept {
    LOG_TRACE_N << "index: " << index;

    if (index < 0 || index >= events_.size())
        return {};
    return *events_[index];
}

void CalendarDayModel::addCalendarEvents()
{
    auto* ctl = qobject_cast<QQuickItem*>(&component_);
    const auto height = ctl->property("height").toInt();
    const auto width = ctl->property("width").toInt();
    const auto left = 0;
    const auto avail_width = width - left;

    const double heigth_per_minute = height / 1440.0;

    timx_boxes_pool_.prepare();
    const auto items = calculatePlacement(events_);

    for (const auto& item : items) {
        const auto now = time({});

        if (item.event.hasTimeBlock()) {
            const auto& tb = item.event.timeBlock();
            if (auto *object = timx_boxes_pool_.get(this, ctl)) {
                auto name = tb.name();
                if (name.isEmpty() && !tb.category().isEmpty()) {
                    name = ActionCategoriesModel::instance().getName(tb.category());
                }
                object->setProperty("name", name);
                object->setProperty("uuid", tb.id_proto());
                object->setProperty("start", NextAppCore::toTime(item.start));
                object->setProperty("end", NextAppCore::toTime(item.end));
                object->setProperty("category", tb.category());
                const auto duration = (item.end - item.start);
                object->setProperty("duration", toHourMin(duration));

                // Calculate the position and size of the time box
                const auto when = QDateTime::fromSecsSinceEpoch(tb.timeSpan().start());
                auto x = left + ((avail_width / item.cols) * (item.col));
                auto y = floor(heigth_per_minute * (when.time().hour() * 60 + when.time().minute()));
                auto w = (avail_width / item.cols) * item.span_cols;
                auto h = floor(heigth_per_minute * ((tb.timeSpan().end() - tb.timeSpan().start()) / 60));

                auto* item = qobject_cast<QQuickItem*>(object);
                item->setHeight(h);
                item->setX(x);
                item->setY(y);
                item->setWidth(w);

                object->setProperty("model", QVariant::fromValue(this));
                //object->setProperty("timeBlock", QVariant::fromValue(tb));

                LOG_TRACE_N << "Setting height " << h << " for " << tb.name().toStdString()
                            << " start=" << QDateTime::fromSecsSinceEpoch(tb.timeSpan().start()).toString().toStdString()
                            << ", end=" << QDateTime::fromSecsSinceEpoch(tb.timeSpan().end()).toString().toStdString();
            }
        }
    }

    timx_boxes_pool_.makeReady();
}

void CalendarDayModel::moveEvent(const QString &eventId, time_t start, time_t end)
{
    if ((start && end && (start >= end)) || (!start && !end)) {
        LOG_WARN_N << "Invalid timebox start/end";
        return;
    }
    auto it = std::ranges::find_if(events_, [&eventId](const auto& event) {
        return event->id_proto() == eventId;
    });
    if (it == events_.end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return;
    }

    auto& event = *it;
    auto ts = event->timeSpan();
    if (start) {
        LOG_TRACE_N << "Moving start from " << QDateTime::fromSecsSinceEpoch(ts.start()).toString() << " to " << QDateTime::fromSecsSinceEpoch(start).toString();
        ts.setStart(start);
    }
    if (end) {
        LOG_TRACE_N << "Moving end from " << QDateTime::fromSecsSinceEpoch(ts.end()).toString() << " to " << QDateTime::fromSecsSinceEpoch(end).toString();
        ts.setEnd(end);
    }

    auto tb = event->timeBlock();
    tb.setTimeSpan(ts);

    ServerComm::instance().updateTimeBlock(tb);
}

void CalendarDayModel::deleteEvent(const QString &eventId)
{
    // Find the event
    auto it = std::ranges::find_if(events_, [&eventId](const auto& event) {
        return event->id_proto() == eventId;
    });

    if (it == events_.end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return;
    }

    if ((*it)->hasTimeBlock()) {
        calendar_.deleteTimeBlock(eventId);
    } else {
        LOG_WARN_N << "I don't know how to delete this event";
        assert(false);
    }
}

nextapp::pb::TimeBlock CalendarDayModel::tbById(const QString &eventId) const
{
    auto it = std::ranges::find_if(events_, [&eventId](const auto& event) {
        return event->id_proto() == eventId;
    });

    if (it != events_.end() && (*it)->hasTimeBlock()) {
        return (*it)->timeBlock();
    }

    return {};
}

void CalendarDayModel::updateTimeBlock(const nextapp::pb::TimeBlock &tb)
{
    ServerComm::instance().updateTimeBlock(tb);
}

bool CalendarDayModel::addAction(const QString &eventId, const QString &action)
{
    // Relay to the main calendar model
    return calendar_.addAction(eventId, action);
}

void CalendarDayModel::removeAction(const QString &eventId, const QString &action)
{
    // Relay to the main calendar model
    return calendar_.removeAction(eventId, action);
}

TimeBoxActionsModel *CalendarDayModel::getTimeBoxActionsModel(const QString &eventId, QObject *tbItem)
{
    const auto uuid = toQuid(eventId);
    auto model = new TimeBoxActionsModel(uuid, this, tbItem);
    QQmlEngine::setObjectOwnership(model, QQmlEngine::JavaScriptOwnership);
    return model;
}

void CalendarDayModel::moveEventToDay(const QString &eventId, time_t start)
{
    // Delegate to the main calendar model
    calendar_.moveEventToDay(eventId, start);
}

nextapp::pb::TimeBlock *CalendarDayModel::lookupTimeBlock(const QUuid &eventId) const
{
    const auto id = eventId.toString(QUuid::WithoutBraces);
    auto it = std::ranges::find_if(events_, [&id](const auto& event) {
        return event->id_proto() == id;
    });

    if (it != events_.end() && (*it)->hasTimeBlock()) {
        return &(*it)->timeBlock();
    }

    return {};
}

int CalendarDayModel::size() const noexcept  {
    if (!valid_) {
        return 0;
    }
    return events_.size();
}

void CalendarDayModel::setValid(bool valid, bool signalAlways )
{
    if (valid_ == valid && !signalAlways) {
        return;
    }
    valid_ = valid;
    emit validChanged();
}

bool CalendarDayModel::hasEvent(const QString &id) const noexcept
{
    return std::ranges::any_of(events_, [&](const auto& event) {
        return event->id_proto() == id;
    });
}

void CalendarDayModel::setDate(QDate date) {
    date_ = date;
    emit whenChanged();
    LOG_TRACE_N << "Updated date to " << date_.toString().toStdString()
                << " for day with index=" << index_;
}

void CalendarDayModel::setToday(bool today) {
    if (today_ == today) [[likely]] {
        return;
    }
    today_ = today;
    emit todayChanged();
}

int CalendarDayModel::roundToMinutes() const noexcept {
    return 5;
}

void CalendarDayModel::setWorkHours()
{
    const auto& gs = ServerComm::instance().globalSettings();

    const auto old_start = work_start_;
    const auto old_end = work_end_;

    if (gs.hasDefaultWorkHours()) {
        work_start_ = gs.defaultWorkHours().start() / 60;
        work_end_ = gs.defaultWorkHours().end() / 60;

        if (work_start_ >= work_end_ || work_start_ < 0 || work_end_ > 1440) {
            LOG_DEBUG_N << "Ignoring invalid work-hours.";
            work_start_ = work_end_ = 0;
        }

    } else {
        work_start_ = work_end_ = 0;

    }

    if (old_start != work_start_ || old_end != work_end_) {
        emit workHoursChanged();
    }
}

QObject *CalendarDayModel::Pool::get(CalendarDayModel *parent, QObject *ctl)
{
    if (end_ >= pool_.size()) {
        if (!component_factory_) {
            component_factory_.emplace(&NextAppCore::instance()->engine(), QUrl(path_));
        }

        QVariantMap properties;
        properties["visible"] = false;
        properties["parent"] = QVariant::fromValue(ctl);
        properties["model"] = QVariant{};

        auto *object = component_factory_->createWithInitialProperties(properties);
        if (!object) {
            LOG_ERROR_N << "Failed to create a QML component: " << path_;
            QList<QQmlError> errors = component_factory_->errors();
            for (const auto &error : errors) {
                LOG_ERROR_N << error.toString().toStdString();
            }
            return {};
        }
        object->setParent(parent);
        pool_.emplace_back(object);
        ++end_;
        return object;
    }

    return pool_[end_++];
}

void CalendarDayModel::Pool::makeReady()
{
    auto i = 0;
    for(auto *object : pool_) {
        object->setProperty("visible", ++i <= end_);
    }
}
