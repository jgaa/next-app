

#include "nextapp/util.h"
#include "nextapp/logging.h"

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



} // ns
