#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
// #include <QAudioOutput>
// #include <QMediaPlayer>

#include "CalendarCache.h"
#include "CalendarDayModel.h"

#include "nextapp.qpb.h"

/* A model for calendar data
 *
 * This model can be used for a small window or all calendar data
 * for the current user in the database.
 *
 * The object is *valid* when it's online and has received the calendar data.
 */
class CalendarModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_ENUMS(CalendarMode)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(CalendarMode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(time_t target READ target NOTIFY targetChanged)

    enum AudioEventType {
        AE_PRE,
        AE_START,
        AE_SOON_ENDING,
        AE_END,
    };

public:
    enum CalendarMode {
        CM_UNSET,
        CM_DAY,
        CM_WEEK,
        CM_MONTH
    };

    CalendarModel();

    bool valid() const noexcept {
        return valid_ && online_;
    }

    CalendarMode mode() const noexcept {
        return mode_;
    }

    /*! Get a window over a single day.
     *
     *  This is used bt QML components to render a day.
     *
     *  @param obj The object that will use the model. Used to manually delete the models when the object is destroyed.
     *  @param index The index of the day in the current window. Starts at 0.
     */
    Q_INVOKABLE CalendarDayModel* getDayModel(QObject *obj, int index);
    Q_INVOKABLE void set(CalendarMode mode, int year, int month, int day, bool primaryForActionList = false);
    Q_INVOKABLE void moveEventToDay(const QString& eventId, time_t start);
    Q_INVOKABLE void deleteTimeBlock(const QString& eventId);
    Q_INVOKABLE time_t getDate(int index);
    Q_INVOKABLE QString getDateStr(int index);
    Q_INVOKABLE void goPrev();
    Q_INVOKABLE void goNext();
    Q_INVOKABLE void goToday();
    Q_INVOKABLE bool addAction(const QString& eventId, const QString& action);
    Q_INVOKABLE void removeAction(const QString& eventId, const QString& action);
    Q_INVOKABLE CategoryUseModel *getCategoryUseModel();

    nextapp::pb::CalendarEvent *lookup(const QString& eventId);

    void setValid(bool value);

    time_t target() const noexcept {
        return target_.startOfDay().toSecsSinceEpoch();
    }

    void setTarget(QDate target);

signals:
    void validChanged();
    void eventChanged(const QString& eventId);
    void resetModel();
    void modeChanged();
    void targetChanged();

private:
    QCoro::Task<void> onCalendarEventRemoved(const QUuid id);
    QCoro::Task<void> onCalendarEventAddedOrUpdated(const QUuid id);
    QCoro::Task<void> fetchIf();
    void setOnline(bool online);
    void updateDayModels();
    void sort();
    void updateDayModelsDates();
    void alignDates();
    void onMinuteTimer();
    void updateToday();
    void updateIfPrimary();
    CategoryUseModel::list_t getCategoryUsage();

    CategoryUseModel *category_use_model_{};
    bool valid_ = false;
    bool online_ = false;
    QDate first_;
    QDate last_;
    QDate target_;
    CalendarMode mode_ = CM_UNSET;
    QList<std::shared_ptr<nextapp::pb::CalendarEvent>> all_events_;
    std::unordered_map<const QObject *, std::unique_ptr<CalendarDayModel>> day_models_;
    std::unique_ptr<QTimer> minute_timer_;
    bool is_primary_ = false;
};
