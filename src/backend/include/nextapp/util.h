#pragma once

#include <string>
#include <cstddef>
#include <locale>
#include <optional>

#include <boost/json.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>

#include "google/protobuf/util/json_util.h"

#include "nextapp.pb.h"
#include "logging.h"

namespace nextapp {

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


template <class T, class V>
concept range_of = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;

std::string getEnv(const char *name, std::string def = {});

template <range_of<char> T>
std::string toUpper(const T& input)
{
    static const std::locale loc{"C"};
    std::string v;
    v.reserve(input.size());

    for(const auto ch : input) {
        v.push_back(std::toupper(ch, loc));
    }

    return v;
}

boost::uuids::uuid newUuid();
std::string newUuidStr();
const std::string& validatedUuid(const std::string& uuid);

template <typename T>
concept ProtoMessage = std::is_base_of_v<google::protobuf::Message, T>;

template <ProtoMessage T>
std::string toJson(const T& obj, int mode = 1) {
    if (!mode) {
        return {};
    }

    std::string str;
    google::protobuf::util::JsonOptions opts;
    opts.always_print_primitive_fields = true;
    opts.add_whitespace = mode == 2;
    auto res = google::protobuf::util::MessageToJsonString(obj, &str, opts);
    if (!res.ok()) {
        LOG_DEBUG << "Failed to convert object to json: "
                  << typeid(T).name() << ": "
                  << res.ToString();
        throw std::runtime_error{"Failed to convert object to json"};
    }
    return str;
}

template <typename T>
concept ProtoStringStringMap = std::is_same_v<std::remove_cv<T>, std::remove_cv<::google::protobuf::Map<std::string, std::string>>>;


template <ProtoStringStringMap T>
std::string toJson(const T& map, int mode = 1) {
    if (!mode) {
        return {};
    }

    boost::json::object o;

    for(const auto [key, value] : map) {
        o[key] = value;
    }

    return boost::json::serialize(o);
}

std::optional<std::string> toAnsiDate(const nextapp::pb::Date& date);
std::optional<std::string> toAnsiTime(std::time_t time, const std::chrono::time_zone& ts);

template <typename T>
std::optional<std::string> toStringOrNull(const T& val) {
    if (val.empty()) {
        return {};
    }

    return std::string{val};
}

struct TimePeriod {
    time_t start = 0;
    time_t end = 0;
};

class UserContext;

TimePeriod toTimePeriodDay(time_t when, const UserContext& uctx);
TimePeriod toTimePeriodWeek(time_t when, const UserContext& uctx);


} // ns

std::ostream& operator << (std::ostream& out, const std::optional<std::string>& v);
