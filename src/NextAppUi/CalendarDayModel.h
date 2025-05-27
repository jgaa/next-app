#pragma once
#include <span>
#include <QObject>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QStringLiteral>
#include <QQuickItem>
#include <QAbstractListModel>
#include <QUuid>

#include "TimeBoxActionsModel.h"
#include "CategoryUseModel.h"
#include "nextapp.qpb.h"

class CalendarModel;
class CalendarDayModel;

class CalendarDayModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(time_t when READ when NOTIFY whenChanged)
    Q_PROPERTY(int size READ size NOTIFY validChanged)
    Q_PROPERTY(int roundToMinutes READ roundToMinutes CONSTANT)
    Q_PROPERTY(bool today READ today NOTIFY todayChanged)
    Q_PROPERTY(int now READ now NOTIFY timeChanged)
    Q_PROPERTY(QString timeStr READ timeStr NOTIFY timeChanged)
    Q_PROPERTY(int workHoursStart MEMBER work_start_ NOTIFY workHoursChanged)
    Q_PROPERTY(int workHoursEnd MEMBER work_end_ NOTIFY workHoursChanged)

public:
    struct Pool {
        Pool(QString path) : path_(std::move(path)) {}

        void prepare() {
            end_ = 0;
        }

        QObject* get(CalendarDayModel *parent, QObject *ctl);

        void makeReady();

        std::vector<QObject*> pool_;
        size_t end_ = 0;
        std::optional<QQmlComponent> component_factory_;
        const QString path_;
    };


    using events_t = std::span<const std::shared_ptr<nextapp::pb::CalendarEvent>>;
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
    Q_INVOKABLE bool addAction(const QString& eventId, const QString& action);
    Q_INVOKABLE void removeAction(const QString& eventId, const QString& action);
    Q_INVOKABLE TimeBoxActionsModel *getTimeBoxActionsModel(const QString& eventId, QObject *tbItem);

    // Called after a drop operation, potentially on another day
    Q_INVOKABLE void moveEventToDay(const QString& eventId, time_t start);

    nextapp::pb::TimeBlock *lookupTimeBlock(const QUuid& eventId) const;

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

    bool hasEvent(const QString& id) const noexcept;

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

    bool today() const noexcept {
        return today_;
    }

    int now() {
        // return the current minute in the day
        const auto current = QDateTime::currentDateTime();
        return current.time().hour() * 60 + current.time().minute();
    }

    QString timeStr() {
        const auto current = QDateTime::currentDateTime();
        return current.time().toString(QStringLiteral("hh:mm"));
    }

    void setToday(bool today);

    int roundToMinutes() const noexcept;

    void emitTimeChanged() {
        emit timeChanged();
    }

signals:
    void validChanged();
    void whenChanged();
    void eventChanged(const QString& eventId);
    void resetModel();
    void timeChanged();
    void todayChanged();
    void workHoursChanged();

private:
    void setWorkHours();

    QDate date_;
    bool today_ = false;
    const int index_;
    bool valid_ = false;
    events_t events_;
    int work_start_ = 0; // Minutes since midnight
    int work_end_ = 0; // Minutes since midnight
    QObject& component_;
    Pool timx_boxes_pool_{QStringLiteral("qrc:/qt/qml/NextAppUi/qml/calendar/TimeBlockArea.qml")};
    CalendarModel& calendar_;
};
