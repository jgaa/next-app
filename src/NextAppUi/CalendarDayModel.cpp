
#include <QQmlComponent>

#include "NextAppCore.h"
#include "CalendarDayModel.h"
#include "ServerComm.h"

using namespace std;

CalendarDayModel::CalendarDayModel(QDate date, QObject& component, QObject *parent)
    : QObject(parent)    
    , date_(date)
    , component_{component}
{
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
    return events_[index];
}

void CalendarDayModel::addCalendarEvents()
{
    const auto height = component_.property("height").toInt();
    const auto width = component_.property("width").toInt();
    const auto left = component_.property("leftMargin").toInt();
    const auto avail_width = width - left;

    const double heigth_per_minute = height / 1440.0;

    timx_boxes_pool_.prepare();

    struct Overlap {
        time_t until = 0;
        int num = 1;
        int col = 1;
    };

    std::vector<Overlap> overlaps;

    for (auto& event : events_) {
        // Remove obsolete overlap entries
        std::erase_if(overlaps, [&event](const auto& v) {
            return v.until < static_cast<time_t>(event.timeSpan().start());
        });

        // Find lowest available column
        static constexpr size_t max_cols = 8;
        std::array<bool, max_cols> cols = {};

        for(auto& overlap : overlaps) {
            assert(overlap.col > 0);
            cols[std::min<size_t>(overlap.col -1, max_cols -1)] = true;
        }
        int col = 1;
        for(auto c: cols) {
            if (!c) {
                break;
            }
            ++col;
        }

        if (auto overlap = event.overlapWithOtherEvents()) {
            overlaps.emplace_back(static_cast<time_t>(event.timeSpan().end()), overlap + 1, col);
        }

        Overlap dominant = {};
        if (auto it = std::ranges::max_element(overlaps, {}, &Overlap::num); it != overlaps.end()) {
            dominant = *it;
        }

        const auto now = time({});

        if (event.hasTimeBlock()) {
            const auto& tb = event.timeBlock();
            if (auto *object = timx_boxes_pool_.get(&component_)) {
                object->setProperty("name", tb.name());
                object->setProperty("uuid", tb.id_proto());
                object->setProperty("start", NextAppCore::toDateAndTime(tb.timeSpan().start(), now));
                object->setProperty("end", NextAppCore::toDateAndTime(tb.timeSpan().end(), now));

                // Calculate the position and size of the time box
                const auto when = QDateTime::fromSecsSinceEpoch(tb.timeSpan().start());
                auto x = left + ((avail_width / dominant.num) * (col - 1));
                auto y = floor(heigth_per_minute * (when.time().hour() * 60 + when.time().minute()));
                auto w = avail_width / dominant.num;
                auto h = floor(heigth_per_minute * ((tb.timeSpan().end() - tb.timeSpan().start()) / 60));

                object->setProperty("x", x);
                object->setProperty("y", y);
                object->setProperty("width", w);
                object->setProperty("height", h);
                object->setProperty("model", QVariant::fromValue(this));
                //object->setProperty("timeBlock", QVariant::fromValue(tb));

                LOG_TRACE_N << "Setting height " << h << " for " << tb.name().toStdString();

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
        return event.id_proto() == eventId;
    });
    if (it == events_.end()) {
        LOG_WARN_N << "No event found with id: " << eventId;
        return;
    }

    auto& event = *it;
    auto ts = event.timeSpan();
    if (start) {
        LOG_TRACE_N << "Moving start from " << QDateTime::fromSecsSinceEpoch(ts.start()).toString() << " to " << QDateTime::fromSecsSinceEpoch(start).toString();
        ts.setStart(start);
    }
    if (end) {
        LOG_TRACE_N << "Moving end from " << QDateTime::fromSecsSinceEpoch(ts.end()).toString() << " to " << QDateTime::fromSecsSinceEpoch(end).toString();
        ts.setEnd(end);
    }

    auto tb = event.timeBlock();
    tb.setTimeSpan(ts);

    ServerComm::instance().updateTimeBlock(tb);
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

QObject *CalendarDayModel::Pool::get(QObject *parent)
{
    auto& engine = NextAppCore::engine();

    if (end_ <= pool_.size()) {
        if (!component_factory_) {
            component_factory_.emplace(&engine, QUrl(path_));
        }

        QVariantMap properties;
        properties["visible"] = false;
        properties["parent"] = QVariant::fromValue(parent);
        properties["model"] = QVariant::fromValue(parent);

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
        //object->setProperty("parent", QVariant::fromValue(parent));
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
