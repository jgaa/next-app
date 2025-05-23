#pragma once

#include <map>
#include <chrono>
#include <iostream>

#include <boost/json.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "date/date.h"
#include "date/tz.h"

#include "nextapp/GrpcServer.h"
#include "nextapp/Server.h"
#include "nextapp/errors.h"
#include "nextapp/util.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

namespace nextapp {
class UserContext;
}

namespace nextapp::grpc {

template <typename T>
concept ActionType = std::is_same_v<T, pb::ActionInfo> || std::is_same_v<T, pb::Action>;

std::optional<std::string> toAnsiDate(const nextapp::pb::Date& date);
std::optional<std::string> toAnsiTime(std::time_t time, const std::chrono::time_zone& ts, bool required = false);
std::optional<std::string> toAnsiTime(std::time_t time, bool required = false);

struct TimePeriod {
    time_t start = 0;
    time_t end = 0;
};

TimePeriod toTimePeriod(time_t when, const UserContext& uctx, nextapp::pb::WorkSummaryKind kind);
TimePeriod toTimePeriodDay(time_t when, const UserContext& uctx);
TimePeriod toTimePeriodWeek(time_t when, const UserContext& uctx);

std::string prefixNames(const std::string_view cols, const std::string_view prefix);


::nextapp::pb::Date toDate(const boost::mysql::date& from);
::nextapp::pb::Date toDate(const boost::mysql::datetime& from);
::nextapp::pb::Date toDate(const time_t when, const chrono::time_zone& tz);
std::chrono::year_month_day toYearMonthDay(const time_t when, const chrono::time_zone& tz);
int64_t toMsTimestamp(const boost::mysql::datetime& from, const chrono::time_zone& tz);
std::string toMsDateTime(uint64_t msSinceEpoc, const chrono::time_zone& tz, bool roundUp = true);

// For TIMESTAMP
std::time_t toTimeT(const boost::mysql::datetime& from, const chrono::time_zone& tz);

// fOR Datetime
std::time_t toTimeT(const boost::mysql::datetime& from);
void setError(pb::Status& status, pb::Error err, const std::string& message = {});

template <typename T>
time_t getDueTime(const T& start, const auto& ts, pb::ActionDueKind kind) {
    using namespace date;

    auto t_local = start.get_local_time();
    auto start_date = year_month_day{floor<days>(t_local)};

    // Days from epoch in local time
    local_days ldays{start_date};

    // Time of day in local time
    const hh_mm_ss time{t_local - floor<days>(t_local)};

    switch(kind) {
    case pb::ActionDueKind::DATETIME: {
        return chrono::system_clock::to_time_t(start.get_sys_time());
    }
    default:
        ; // pass
    }

    return {};
}

template <typename T>
auto toTimet(const T& when, const auto *ts) {
    const auto zoned = date::make_zoned(ts, when);
    return chrono::system_clock::to_time_t(zoned.get_sys_time());
}

std::time_t addDays(std::time_t input, int n);

static constexpr auto quarters = to_array({date::January, date::January, date::January,
                                           date::April, date::April, date::April,
                                           date::July, date::July, date::July,
                                           date::October, date::October, date::October});

struct SqlFilter {

    SqlFilter(bool addWhere = true)
        : add_where_{addWhere} {}

    template <typename T>
    void add(const T& what) {
        if (add_where_ && where_.empty()) {
            where_ = "WHERE ";
        } else {
            where_ += " AND ";
        }

        where_ += what;
    }

    std::string_view where() const noexcept {
        return where_;
    }

private:
    bool add_where_ = true;
    std::string where_;
};

template <ProtoMessage T>
auto toBlob(const T& msg) {
    boost::mysql::blob blob;
    blob.resize(msg.ByteSizeLong());
    if (!msg.SerializeToArray(blob.data(), blob.size())) {
        throw runtime_error{"Failed to serialize protobuf message"};
    }
    return blob;
}

template <range_of<char> T>
auto toBlob(const T& buffer) {
    boost::mysql::blob blob;
    blob.resize(buffer.size());
    std::copy(buffer.begin(), buffer.end(), blob.begin());
    return blob;
}

template<typename T>
std::optional<nextapp::pb::KeyValue> KeyValueFromBlob(const T& row) {
    if (row.is_blob()) {
        auto data = row.as_blob();
        pb::KeyValue kv;
        if (kv.ParseFromArray(data.data(), data.size())) {
            return kv;
        }
    }
    return std::nullopt;
}


} // ns
