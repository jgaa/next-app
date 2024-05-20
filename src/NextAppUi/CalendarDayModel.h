#pragma once

#include <span>
#include <QObject>
#include <QQmlEngine>
#include "nextapp.qpb.h"

class CalendarDayModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(int year READ year CONSTANT)
    Q_PROPERTY(int month READ month CONSTANT)
    Q_PROPERTY(int day READ day CONSTANT)
    Q_PROPERTY(int size READ size NOTIFY validChanged)

public:
    using events_t = std::span<const nextapp::pb::CalendarEvent>;
    CalendarDayModel(QDate date, QObject* parent = nullptr);

    // start and end are minuts into the day
    Q_INVOKABLE void createTimeBox(QString name, QString category, int start, int end);
    Q_INVOKABLE nextapp::pb::CalendarEvent event(int index) const noexcept;

    int size() const noexcept;

    bool valid() const noexcept {
        return valid_;
    }
    void setValid(bool valid);

    const events_t& events() const noexcept {
        return events_;
    }

    events_t& events() noexcept {
        return events_;
    }

    QDate date() const noexcept {
        return date_;
    }

    int year() const noexcept {
        return date_.year();
    }

    int month() const noexcept {
        return date_.month();
    }

    int day() const noexcept {
        return date_.day();
    }

signals:
    void validChanged();
    void eventChanged(const QString& eventId);
    void resetModel();

private:
    const QDate date_;
    bool valid_ = false;
    events_t events_;
};
