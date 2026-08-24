#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QLocale>
#include <qqmlregistration.h>

// A small, locale-aware calendar grid for QML controls.  It deliberately keeps
// ISO week numbering independent from the user's preferred first weekday.
class DateCalendarModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int year READ year WRITE setYear NOTIFY calendarChanged)
    Q_PROPERTY(int month READ month WRITE setMonth NOTIFY calendarChanged)
    Q_PROPERTY(int firstDayOfWeek READ firstDayOfWeek WRITE setFirstDayOfWeek NOTIFY calendarChanged)
    Q_PROPERTY(QLocale locale READ locale WRITE setLocale NOTIFY localeChanged)
    Q_PROPERTY(QStringList weekdayNames READ weekdayNames NOTIFY localeChanged)

public:
    enum Role {
        IsWeekNumberRole = Qt::UserRole + 1,
        WeekNumberRole,
        WeekDateRole,
        DateRole,
        YearRole,
        MonthRole,
        DayRole,
        InCurrentMonthRole,
        IsTodayRole,
    };
    Q_ENUM(Role)

    explicit DateCalendarModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int year() const noexcept { return year_; }
    int month() const noexcept { return month_; }
    int firstDayOfWeek() const noexcept { return first_day_of_week_; }
    QLocale locale() const { return locale_; }
    QStringList weekdayNames() const;

    void setYear(int year);
    void setMonth(int month);
    void setFirstDayOfWeek(int day);
    void setLocale(const QLocale& locale);

signals:
    void calendarChanged();
    void localeChanged();

private:
    QDate firstGridDate() const;
    int isoWeekNumberForRow(int row) const;
    QDate isoWeekStartForRow(int row) const;
    void resetCalendar();

    int year_ = QDate::currentDate().year();
    int month_ = QDate::currentDate().month(); // QDate's 1-based month
    int first_day_of_week_ = Qt::Monday;
    QLocale locale_ = QLocale{QLocale::English};
};
