
#include <openssl/evp.h>

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

// Modified from ChatGPT generated code
string Base64Encode(const std::span<uint8_t> in)
{
    static constexpr std::string_view base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    string base64_data;
    base64_data.reserve(((in.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < in.size(); i += 3) {
        const auto a = in[i];
        const auto b = i + 1 < in.size() ? in[i + 1] : 0;
        const auto c = i + 2 < in.size() ? in[i + 2] : 0;

        const auto index1 = (a >> 2) & 0x3f;
        const auto index2 = ((a & 0x3) << 4) | ((b >> 4) & 0xf);
        const auto index3 = ((b & 0xf) << 2) | ((c >> 6) & 0x3);
        const auto index4 = c & 0x3f;

        base64_data.push_back(base64_chars[index1]);
        base64_data.push_back(base64_chars[index2]);
        base64_data.push_back(i + 1 < in.size() ? base64_chars[index3] : '=');
        base64_data.push_back(i + 2 < in.size() ? base64_chars[index4] : '=');
    }

    return base64_data;
}


// Impl. based from https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
string sha256(const std::span<uint8_t> in, bool encodeToBase64)
{
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const ScopedExit bye{[context] {
        EVP_MD_CTX_free(context);
    }};

    if (context != nullptr) {
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 0) {
            if (EVP_DigestUpdate(context, in.data(), in.size()) != 0) {
                array<uint8_t, EVP_MAX_MD_SIZE> hash{};
                unsigned int lengthOfHash = 0;
                if (EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 0) {
                    if (encodeToBase64) {
                        return Base64Encode({hash.data(), lengthOfHash});
                    }
                    return {reinterpret_cast<const char *>(hash.data()), lengthOfHash};
                }
            }
        }
    }
    throw runtime_error{"sha256 failed!"};
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

} // ns
