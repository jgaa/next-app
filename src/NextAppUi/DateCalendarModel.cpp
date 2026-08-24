#include "DateCalendarModel.h"

namespace {
constexpr int kWeeks = 6;
constexpr int kColumns = 8; // ISO week number plus seven days
}

DateCalendarModel::DateCalendarModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int DateCalendarModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : kWeeks * kColumns;
}

QVariant DateCalendarModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const auto row = index.row() / kColumns;
    const auto column = index.row() % kColumns;
    if (column == 0) {
        switch (role) {
        case IsWeekNumberRole: return true;
        case WeekNumberRole: return isoWeekNumberForRow(row);
        case WeekDateRole: return isoWeekStartForRow(row);
        default: return {};
        }
    }

    const auto date = firstGridDate().addDays(row * 7 + column - 1);
    switch (role) {
    case IsWeekNumberRole: return false;
    case DateRole: return date;
    case YearRole: return date.year();
    case MonthRole: return date.month();
    case DayRole: return date.day();
    case InCurrentMonthRole: return date.year() == year_ && date.month() == month_;
    case IsTodayRole: return date == QDate::currentDate();
    default: return {};
    }
}

QHash<int, QByteArray> DateCalendarModel::roleNames() const
{
    return {
        {IsWeekNumberRole, "isWeekNumber"}, {WeekNumberRole, "weekNumber"},
        {WeekDateRole, "weekDate"},
        {DateRole, "date"}, {YearRole, "year"}, {MonthRole, "month"},
        {DayRole, "day"}, {InCurrentMonthRole, "inCurrentMonth"}, {IsTodayRole, "isToday"},
    };
}

QStringList DateCalendarModel::weekdayNames() const
{
    QStringList names;
    for (auto offset = 0; offset < 7; ++offset) {
        const auto day = (first_day_of_week_ - 1 + offset) % 7 + 1;
        // Keep headers compact while preserving the application's locale.
        names.append(locale_.standaloneDayName(day, QLocale::ShortFormat).left(2));
    }
    return names;
}

void DateCalendarModel::setYear(int year)
{
    if (year > 0 && year != year_) {
        year_ = year;
        resetCalendar();
    }
}

void DateCalendarModel::setMonth(int month)
{
    if (month >= 1 && month <= 12 && month != month_) {
        month_ = month;
        resetCalendar();
    }
}

void DateCalendarModel::setFirstDayOfWeek(int day)
{
    if (day >= Qt::Monday && day <= Qt::Sunday && day != first_day_of_week_) {
        first_day_of_week_ = day;
        resetCalendar();
        emit localeChanged(); // weekday order changed as well
    }
}

void DateCalendarModel::setLocale(const QLocale& locale)
{
    if (locale != locale_) {
        locale_ = locale;
        emit localeChanged();
    }
}

QDate DateCalendarModel::firstGridDate() const
{
    const QDate first(year_, month_, 1);
    const int days_before = (first.dayOfWeek() - first_day_of_week_ + 7) % 7;
    return first.addDays(-days_before);
}

int DateCalendarModel::isoWeekNumberForRow(int row) const
{
    return isoWeekStartForRow(row).weekNumber();
}

QDate DateCalendarModel::isoWeekStartForRow(int row) const
{
    const auto row_start = firstGridDate().addDays(row * 7);
    return row_start.addDays((Qt::Monday - row_start.dayOfWeek() + 7) % 7);
}

void DateCalendarModel::resetCalendar()
{
    beginResetModel();
    endResetModel();
    emit calendarChanged();
}
