#pragma once
#include <span>
#include <QObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QStringLiteral>
#include <QQuickItem>
#include "nextapp.qpb.h"

class CalendarModel;

class CalendarDayModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(time_t when READ when NOTIFY whenChanged)
    Q_PROPERTY(int size READ size NOTIFY validChanged)
    Q_PROPERTY(int roundToMinutes READ roundToMinutes CONSTANT)

public:
    struct Pool {
        Pool(QString path) : path_(std::move(path)) {}

        void prepare() {
            end_ = 0;
        }

        QObject* get(QObject *parent);

        void makeReady();

        std::vector<QObject*> pool_;
        size_t end_ = 0;
        std::optional<QQmlComponent> component_factory_;
        const QString path_;
    };


    using events_t = std::span<const nextapp::pb::CalendarEvent>;
    CalendarDayModel(QDate date, QObject& component, CalendarModel& calendar, int index,  QObject* parent = nullptr);
    ~CalendarDayModel();

    // start and end are minuts into the day
    Q_INVOKABLE void createTimeBox(QString name, QString category, int start, int end);
    Q_INVOKABLE nextapp::pb::CalendarEvent event(int index) const noexcept;
    Q_INVOKABLE void addCalendarEvents();
    Q_INVOKABLE void moveEvent(const QString& eventId, time_t start, time_t end);
    Q_INVOKABLE void deleteEvent(const QString& eventId);
    Q_INVOKABLE nextapp::pb::TimeBlock tbById(const QString& eventId) const;
    Q_INVOKABLE void updateTimeBlock(const nextapp::pb::TimeBlock& tb);

    // Called after a drop operation, potentially on another day
    Q_INVOKABLE void moveEventToDay(const QString& eventId, time_t start);

    int size() const noexcept;

    bool valid() const noexcept {
        return valid_;
    }
    void setValid(bool valid, bool signalAlways = false);

    const events_t& events() const noexcept {
        return events_;
    }

    events_t& events() noexcept {
        return events_;
    }

    time_t when() const {
        if (date_.isValid()) [[likely]] {
            return date_.startOfDay().toSecsSinceEpoch();
        }

        return 0;
    }

    QDate date() const noexcept {
        return date_;
    }

    void setDate(QDate date);

    int index() const noexcept {
        return index_;
    }

    // int year() const noexcept {
    //     return date_.year();
    // }

    // int month() const noexcept {
    //     return date_.month();
    // }

    // int day() const noexcept {
    //     return date_.day();
    // }

    int roundToMinutes() const noexcept;

signals:
    void validChanged();
    void whenChanged();
    void eventChanged(const QString& eventId);
    void resetModel();

private:
    QDate date_;
    const int index_;
    bool valid_ = false;
    events_t events_;
    QObject& component_;
    Pool timx_boxes_pool_{QStringLiteral("qrc:/qt/qml/NextAppUi/qml/calendar/TimeBlock.qml")};
    CalendarModel& calendar_;
};
