
#include "shared_grpc_server.h"
#include "nextapp/UserContext.h"


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


optional<string> toAnsiDate(const nextapp::pb::Date& date) {

    if (date.year() == 0) {
        return {};
    }

    return format("{:0>4d}-{:0>2d}-{:0>2d}", date.year(), date.month() + 1, date.mday());
}

optional<string> toAnsiTime(time_t time, const std::chrono::time_zone& ts, bool required) {
    using namespace std::chrono;

    if (time == 0) {
        if (required) {
            throw server_err{pb::Error::CONSTRAINT_FAILED, "datetime is required"};
        }
        return {};
    }

    const auto when = round<seconds>(system_clock::from_time_t(time));
    const auto zoned = zoned_time{&ts, when};
    auto out = format("{:%F %T}", zoned.get_local_time());
    LOG_TRACE_N << "Local time: " << format("{:%F %T}", zoned.get_local_time())
                << " System time " << format("{:%F %T}", zoned.get_sys_time())
                << " out " << out
                << " time_t=" << time
                << " time zone " << ts.name();
    return out;
}

std::optional<string> toAnsiTime(time_t time, bool required)
{
    using namespace std::chrono;
    if (time == 0) {
        if (required) {
            throw server_err{pb::Error::CONSTRAINT_FAILED, "datetime is required"};
        }
        return {};
    }

    const auto when = round<seconds>(system_clock::from_time_t(time));
    auto out = format("{:%F %T}", when);
    return out;
}

auto getLocalDate(time_t when, const std::chrono::time_zone &tz)
{
    using namespace std::chrono;

    const auto start = floor<days>(system_clock::from_time_t(when));
    auto zoned_ref = zoned_time{&tz, start};
    return year_month_day{floor<days>(zoned_ref.get_local_time())};
}

auto getLocalDays(time_t when, const std::chrono::time_zone &tz) -> std::chrono::local_days
{
    using namespace std::chrono;
    return local_days{getLocalDate(when, tz)};
}

TimePeriod toTimePeriodDay(time_t when, const UserContext& uctx)
{
    using namespace std::chrono;
    const auto& tz = uctx.tz();
    const auto start_day = getLocalDays(when, tz);
    const auto end_day = start_day + days{1};

    const auto local_start = zoned_time{&tz, start_day};
    const auto local_end = zoned_time{&tz, end_day};

    return {system_clock::to_time_t(local_start.get_sys_time()),
            system_clock::to_time_t(local_end.get_sys_time())};
}

TimePeriod toTimePeriodWeek(time_t when, const UserContext& uctx)
{
    using namespace std::chrono;
    const auto& tz = uctx.tz();
    const auto start_of_week_offset = uctx.sundayIsFirstWeekday() ? days(0) : days(1);

    const auto start = floor<days>(system_clock::from_time_t(when));
    auto zoned_ref = zoned_time{&tz, start};
    const auto l_day = floor<days>(zoned_ref.get_local_time());
    const auto ymd = year_month_day{l_day};
    const auto ymw = year_month_weekday{l_day};

    const auto start_day = l_day + (days{ymw.weekday().c_encoding()} * -1) + start_of_week_offset;
    const auto end_day = start_day + days{7};

    const auto local_start = zoned_time{&tz, start_day};
    const auto local_end = zoned_time{&tz, end_day};

    LOG_DEBUG_N << format("local_when = {} local_start = {}, local_end = {}",
                          zoned_ref, local_start, local_end);

    const auto res_start = system_clock::to_time_t(local_start.get_sys_time());
    const auto res_end = system_clock::to_time_t(local_end.get_sys_time());

    LOG_DEBUG_N << format("start = {}, end = {}",
                          chrono::system_clock::from_time_t(res_start),
                          chrono::system_clock::from_time_t(res_end));


    return {res_start, res_end};
}

string prefixNames(const std::string_view cols, const std::string_view prefix) {
    std::ostringstream os;

    size_t col = 0;
    for (auto word : std::views::split(cols, ',')) {
        os << (++col == 1 ? "" : ", ");
        auto w = std::string_view{word.data(), word.size()};
        if (!w.empty() && w.front() == ' ') {
            w = w.substr(1);
        }
        os << prefix << w;
    }
    auto rval = os.str();
    return rval;
}

TimePeriod toTimePeriod(time_t when, const UserContext &uctx, pb::WorkSummaryKind kind)
{
    switch (kind) {
    case pb::WorkSummaryKind::WSK_DAY:
        return toTimePeriodDay(when, uctx);
    case pb::WorkSummaryKind::WSK_WEEK:
        return toTimePeriodWeek(when, uctx);
        break;
    default:
        throw std::runtime_error{"Invalid WorkSummaryRequest.Kind"};
    }
}

int64_t toMsTimestamp(const boost::mysql::datetime& from, const std::chrono::time_zone& tz)
{
    if (from.valid()) {
        using namespace std::chrono;

        // Convert the datetime to a time_point representing local time
        auto when = from.as_time_point();

        // Treat 'when' as a local_time in the specified time zone
        local_time<milliseconds> local_tp{duration_cast<milliseconds>(when.time_since_epoch())};

        // Convert local_time to sys_time (UTC time_point) using the time zone
        sys_time<milliseconds> utc_time = tz.to_sys(local_tp);

        // Get milliseconds since epoch from the UTC time_point
        int64_t when_ms = utc_time.time_since_epoch().count();

        return when_ms;
    }

    return {};
}

// Internally the timestamps are in nanoseconds.
// If we query the date like > msSinceEpoch, we need to round up to the next millisecond.
string toMsDateTime(uint64_t msSinceEpoch, const chrono::time_zone& tz, bool roundUp)
{
    using namespace std::chrono;

    // Convert milliseconds since epoch to a system_clock time_point
    auto tp = time_point<system_clock, milliseconds>{milliseconds{msSinceEpoch + (roundUp ? 1 : 0)}};

    // Create a zoned_time in the specified time zone
    auto zoned = zoned_time{&tz, tp};

    // Get the local_time in the specified time zone
    auto local_tp = zoned.get_local_time();

    // Format the local time without any further time zone conversion
    return format("{:%F %T}", local_tp);
}

} // ns
