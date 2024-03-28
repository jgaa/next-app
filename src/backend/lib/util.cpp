

#include "nextapp/util.h"
#include "nextapp/logging.h"
#include "nextapp/UserContext.h"

using namespace std;

std::ostream& operator << (std::ostream& out, const std::optional<std::string>& v) {
    if (v) {
        return out << *v;
    }

    return out << "[empty]";
}

namespace nextapp {

std::string getEnv(const char *name, std::string def) {
    if (auto var = std::getenv(name)) {
        return var;
    }

    return def;
}

boost::uuids::uuid newUuid()
{
    static boost::uuids::random_generator uuid_gen_;
    return uuid_gen_();
}

string newUuidStr()
{
    return boost::uuids::to_string(newUuid());
}

const string& validatedUuid(const string& uuid) {
    using namespace boost::uuids;

    try {
        auto result = string_generator()(uuid);
        return uuid;
    } catch(const runtime_error&) {

    }

    throw runtime_error{"invalid uuid"};
}

optional<string> toAnsiDate(const nextapp::pb::Date& date) {

    if (date.year() == 0) {
        return {};
    }

    return format("{:0>4d}-{:0>2d}-{:0>2d}", date.year(), date.month() + 1, date.mday());
}

optional<string> toAnsiTime(time_t time, const std::chrono::time_zone& ts) {
    using namespace std::chrono;

    if (time == 0) {
        return {};
    }

    const auto when = round<seconds>(system_clock::from_time_t(time));
    const auto zoned = zoned_time{&ts, when};
    auto out = format("{:%F %T}", zoned);
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

    const auto start_day = l_day + (days{ymw.weekday().c_encoding()} * -1) + days{6} + start_of_week_offset;
    const auto end_day = start_day + days{7};

    const auto local_start = zoned_time{&tz, start_day};
    const auto local_end = zoned_time{&tz, end_day};

    return {system_clock::to_time_t(local_start.get_sys_time()),
            system_clock::to_time_t(local_end.get_sys_time())};
}


} // ns
