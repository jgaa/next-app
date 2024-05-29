
#include "shared_grpc_server.h"

namespace nextapp::grpc {

::nextapp::pb::Date toDate(const boost::mysql::date& from) {

    ::nextapp::pb::Date date;
    if (from.valid()) {
        date.set_year(from.year());
        date.set_month(from.month() -1); // Our range is 0 - 11, the db's range is 1 - 12
        date.set_mday(from.day());
    }
    return date;
}

::nextapp::pb::Date toDate(const boost::mysql::datetime& from) {
    ::nextapp::pb::Date date;
    if (from.valid()) {
        date.set_year(from.year());
        date.set_month(from.month() -1); // Our range is 0 - 11, the db's range is 1 - 12
        date.set_mday(from.day());
    }
    return date;
}

// FOR TIMESTAMP
time_t toTimeT(const boost::mysql::datetime& from, const chrono::time_zone& tz) {
    if (from.valid()) {
        auto tp = from.as_time_point();
        chrono::seconds seconds{chrono::system_clock::to_time_t(tp)};
        chrono::zoned_time ztime{&tz, chrono::local_seconds{seconds}};
        auto when = ztime.get_sys_time();
        return chrono::system_clock::to_time_t(when);
    }

    return {};
}

// For DATETIME
time_t toTimeT(const boost::mysql::datetime& from) {
    if (from.valid()) {
        auto tp = from.as_time_point();
        return chrono::system_clock::to_time_t(tp);
    }

    return {};
}


void setError(pb::Status& status, pb::Error err, const std::string& message) {

    status.set_error(err);
    if (message.empty()) {
        status.set_message(pb::Error_Name(err));
    } else {
        status.set_message(message);
    }

    LOG_DEBUG << "Setting error " << status.message() << " on request.";
}

// TODO: Add the time, adjusted for local time zone.
std::time_t addDays(std::time_t input, int n)
{
    auto today = floor<date::days>(chrono::system_clock::from_time_t(input));
    today += date::days(n);
    return chrono::system_clock::to_time_t(today);
}

std::shared_ptr<pb::Update> newUpdate(pb::Update::Operation op)
{
    auto update = std::make_shared<pb::Update>();
    update->set_op(op);
    return update;
}

pb::Date toDate(const time_t when, const chrono::time_zone &tz)
{
    const auto ymd = toYearMonthDay(when, tz);

    if (ymd.ok()) [[likely]]{
        pb::Date date;
        date.set_year(static_cast<int32_t>(ymd.year()));
        date.set_month(static_cast<uint32_t>(ymd.month()));
        date.set_mday(static_cast<uint32_t>(ymd.day()));
        return date;
    }

    return {};
}

std::chrono::year_month_day toYearMonthDay(const time_t when, const chrono::time_zone &tz)
{
    using namespace std::chrono;

    auto tp = system_clock::from_time_t(when);
    chrono::zoned_time ztime{&tz, tp};
    const auto ymd = year_month_day{floor<days>(ztime.get_local_time())};

    if (!ymd.ok()) {
        LOG_DEBUG_N << "when is not a valid date: " << when
                    << " with timezone " << tz.name();
        return {};
    }

    return ymd;
}

} // ns
