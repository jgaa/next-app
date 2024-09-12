#pragma once

#include <QString>
#include <QUuid>
#include <QDate>

[[nodiscard]] QString toValidQuid(const QString& str);
[[nodiscard]] QUuid toQuid(const QString& str);
[[nodiscard]] QString toHourMin(const int duration, bool showEmpty = true);
[[nodiscard]] int parseDuration(const QString& value);
[[nodiscard]] time_t parseDateOrTime(const QString& str, const QDate& defaultDate = QDate::currentDate());
[[nodiscard]] QString toJson(const QObject& o);

QDate getFirstDayOfWeek(const QDate& when = QDate::currentDate());

// BOOST_SCOPE_EXIT confuses Clang-Tidy :/
template <typename T>
struct ScopedExit {
    explicit ScopedExit(T&& fn)
        : fn_{std::move(fn)} {}

    ScopedExit(const ScopedExit&) = delete;
    ScopedExit(ScopedExit&&) = delete;

    ~ScopedExit() {
        fn_();
    }

    ScopedExit& operator =(const ScopedExit&) = delete;
    ScopedExit& operator =(ScopedExit&&) = delete;
private:
    T fn_;
};

bool isToday(time_t when);
bool isYesterday(time_t when);
bool isCurrentWeek(time_t when);
bool isLastWeek(time_t when);
bool isCurrentMonth(time_t when);
bool isLastMonth(time_t when);
bool isLastYear(time_t when);
