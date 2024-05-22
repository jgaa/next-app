#pragma once

#include <QObject>
#include <QQmlEngine>
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
    QML_SINGLETON
    Q_ENUMS(CalendarMode)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(CalendarMode mode READ mode NOTIFY modeChanged)

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

    Q_INVOKABLE CalendarDayModel* getDayModel(QObject *obj, int year, int month, int day);
    Q_INVOKABLE void set(CalendarMode mode, int year, int month, int day);

    void setValid(bool value);

signals:
    void validChanged();
    void eventChanged(const QString& eventId);
    void resetModel();
    void modeChanged();

private:
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void onCalendarEventUpdated(const nextapp::pb::CalendarEvents& events, nextapp::pb::Update::Operation op);
    void fetchIf();
    void setOnline(bool online);
    void onReceivedCalendarData(nextapp::pb::CalendarEvents& data);
    void updateDayModels();
    void sort();


    bool valid_ = false;
    bool online_ = false;
    QDate first_;
    QDate last_;
    CalendarMode mode_ = CM_UNSET;
    nextapp::pb::CalendarEvents all_events_;
    std::unordered_map<const QObject *, std::unique_ptr<CalendarDayModel>> day_models_;
};
