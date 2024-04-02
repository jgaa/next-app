#pragma once

#include <QString>
#include <QUuid>

[[nodiscard]] QString toValidQuid(const QString& str);
[[nodiscard]] QUuid toQuid(const QString& str);
[[nodiscard]] QString toHourMin(const int duration);
[[nodiscard]] int parseDuration(const QString& value);

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
