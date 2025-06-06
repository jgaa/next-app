#pragma once

#include <string>
#include <cstddef>
#include <locale>
#include <optional>
#include <ranges>
#include <filesystem>
#include <span>
#include <string_view>
#include <format>

#include <boost/json.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/functional/hash.hpp>

#include "google/protobuf/util/json_util.h"

#include "nextapp.pb.h"
#include "nextapp/logging.h"

#include "google/protobuf/port_def.inc"
namespace nextapp {
    static auto constexpr protobuf_version = PROTOBUF_VERSION;
} // ns
#include "google/protobuf/port_undef.inc"

namespace nextapp {

template<typename T>
concept SmartPointer = std::is_same_v<T, std::unique_ptr<typename T::element_type>> ||
                       std::is_same_v<T, std::shared_ptr<typename T::element_type>>;

template <typename T>
concept ProtoMessage = std::is_base_of_v<google::protobuf::Message, T>;

template <typename T>
concept ProtoMessagePtr = (std::is_same_v<T, std::unique_ptr<typename T::element_type>> ||
                          std::is_same_v<T, std::shared_ptr<typename T::element_type>>) &&
                          ProtoMessage<typename T::element_type>;

using span_t = std::span<const char>;

uint64_t getRandomNumber64();
uint32_t getRandomNumber32();
uint16_t getRandomNumber16();
std::string getRandomStr(size_t len);
std::string getRandomStr(size_t len, std::string_view chars);

bool isValidEmail(const std::string& email);

template <typename T>
std::optional<std::string> getValueFromKey(const T& values, std::string_view key) {
    for(auto& kv : values) {
        if (kv.key() == key) {
            return kv.value();
        }
    }

    return {};
}

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

template <typename V, typename T>
concept AtomicLike =
    requires(V v, T t) {
        { v.load(std::memory_order_relaxed) } -> std::convertible_to<T>;
        { v.compare_exchange_weak(t, t, std::memory_order_relaxed, std::memory_order_relaxed) } -> std::same_as<bool>;
    };

template <range_of<char> T>
auto pb_adapt(const T& v) {
    if constexpr (protobuf_version >= 4021050) {
        return std::string_view(v.data(), v.size());
    } else {
        return std::string(v.data(), v.size());
    }
}

inline std::string_view trim(std::string_view sv) {
    auto begin = sv.find_first_not_of(" \t\n\r\f\v");
    auto end = sv.find_last_not_of(" \t\n\r\f\v");

    if (begin == std::string_view::npos) return {}; // string was all whitespace
    return sv.substr(begin, end - begin + 1);
}

template <range_of<char> T>
std::vector<std::string_view> split(const T& input, char delimiter) {
    std::vector<std::string_view> result;

    auto first = std::ranges::begin(input);
    auto last = std::ranges::end(input);

    auto start = first;
    for (auto it = first; it != last; ++it) {
        if (*it == delimiter) {
            std::string_view segment(&*start, std::distance(start, it));
            result.push_back(trim(segment));
            start = std::next(it);
        }
    }

    // Add the final segment
    if (start != last) {
        std::string_view segment(&*start, std::distance(start, last));
        result.push_back(trim(segment));
    } else {
        // if delimiter is at the end, preserve empty segment
        result.emplace_back("");
    }

    return result;
}

std::string getEnv(const char *name, std::string def = {});

/*! Read a file into a string
     *
     *  (A funtion like this is part of the Rust standard library because it is so common...)
     */
std::string readFileToBuffer(const std::filesystem::path& path);

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

template <range_of<char> T>
std::string toLower(const T& input)
{
    static const std::locale loc{"C"};
    std::string v;
    v.reserve(input.size());

    for(const auto ch : input) {
        v.push_back(std::tolower(ch, loc));
    }

    return v;
}

boost::uuids::uuid newUuid();
std::string newUuidStr();
const std::string& validatedUuid(const std::string& uuid);
boost::uuids::uuid toUuid(std::string_view uuid);

struct UuidHash {
    size_t operator()(const boost::uuids::uuid& uuid) const {
        return boost::hash_value(uuid);
    }
};

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

template <typename T>
std::optional<std::string> toStringOrNull(const T& val) {
    if (val.empty()) {
        return {};
    }

    return std::string{val};
}

template <typename T>
auto toStringIfValue(const T& row, size_t col) -> decltype(row[0].as_string()) {
    const auto &r = row.at(col);
    if (r.is_null()) {
        return {};
    };

    return r.as_string();
}

template <typename T>
int64_t toIntIfValue(const T& row, size_t col) {
    const auto &r = row.at(col);
    if (r.is_null()) {
        return {};
    };

    return r.as_int64();
}


std::vector<char> base64Decode(const std::string_view in);
std::string Base64Encode(const span_t in);

std::string sha256(span_t what, bool encodeToBase64);

template<typename T>
std::string formatDuration(const T& elapsed) {
    using namespace std::chrono;

    // Convert the duration to seconds for easier manipulation
    auto total_seconds = duration_cast<seconds>(elapsed).count();

    // Compute days, hours, minutes, and seconds
    auto days = total_seconds / (24 * 3600);
    total_seconds %= (24 * 3600);
    auto hours = total_seconds / 3600;
    total_seconds %= 3600;
    auto minutes = total_seconds / 60;
    auto seconds = total_seconds % 60;

    // Create the result string and reserve memory for the max length
    std::string result;
    result.reserve(16);

    // Build the formatted string with only relevant parts
    if (days > 0) {
        result += std::format("{:02}d ", days);
    }
    if (hours > 0 || !result.empty()) { // Show hours if non-zero or if days are already added
        result += std::format("{:02}h ", hours);
    }
    if (minutes > 0 || !result.empty()) { // Show minutes if non-zero or if higher units are added
        result += std::format("{:02}m ", minutes);
    }
    result += std::format("{:02}s", seconds); // Always include seconds

    return result;
}

/*! Atomic set if greater
 *
 *  Set the value of v to new_value if v < new_value
 */
template <typename T, typename V>
requires AtomicLike<V, T>
static void atomicSetIfGreater(V& v, T new_value) noexcept {
    T current = v.load(std::memory_order_relaxed);
    while (current < new_value &&
           !v.compare_exchange_weak(
               current, new_value,
               std::memory_order_relaxed,  // success
               std::memory_order_relaxed   // failure
               )) {
    }
}


} // ns

std::ostream& operator << (std::ostream& out, const std::optional<std::string>& v);
std::ostream& operator << (std::ostream& o, const nextapp::pb::KeyValue& v);
