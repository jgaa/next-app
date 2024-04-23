#include <map>
#include <chrono>
#include <iostream>

#include <boost/json.hpp>
#include <boost/algorithm/string.hpp>

#include "date/date.h"
#include "date/tz.h"

#include "nextapp/GrpcServer.h"
#include "nextapp/Server.h"
#include "nextapp/util.h"

using namespace std;
using namespace std::literals;
using namespace std::chrono_literals;
using namespace std;
namespace json = boost::json;
namespace asio = boost::asio;

namespace nextapp::grpc {

// Use this so that we don't forget to set the operation
std::shared_ptr<nextapp::pb::Update> newUpdate(nextapp::pb::Update::Operation op);

template <typename T>
concept ActionType = std::is_same_v<T, pb::ActionInfo> || std::is_same_v<T, pb::Action>;


::nextapp::pb::Date toDate(const boost::mysql::date& from);
::nextapp::pb::Date toDate(const boost::mysql::datetime& from);
std::time_t toTimeT(const boost::mysql::datetime& from, const chrono::time_zone& tz);
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
    return std::move(blob);
}

} // ns
