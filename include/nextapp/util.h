#pragma once

#include <string>
#include <cstddef>
#include <locale>
#include <optional>
#include <ranges>
#include <filesystem>
#include <span>
#include <string_view>

#include <boost/json.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/functional/hash.hpp>

#include "google/protobuf/util/json_util.h"

#include "nextapp.pb.h"
#include "nextapp/logging.h"

namespace nextapp {

using span_t = std::span<const char>;

uint64_t getRandomNumber64();
uint32_t getRandomNumber32();
uint16_t getRandomNumber16();
std::string getRandomStr(size_t len);
std::string getRandomStr(size_t len, std::string_view chars);

bool isValidEmail(const std::string& email);

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

template <range_of<char> T>
std::string_view to_view(const T& v) {
    return std::string_view(v.data(), v.size());
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

template <typename T>
std::optional<std::string> toStringOrNull(const T& val) {
    if (val.empty()) {
        return {};
    }

    return std::string{val};
}

std::vector<char> base64Decode(const std::string_view in);
std::string Base64Encode(const span_t in);

std::string sha256(span_t what, bool encodeToBase64);

} // ns

std::ostream& operator << (std::ostream& out, const std::optional<std::string>& v);
