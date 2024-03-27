
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

time_t toTimeT(const boost::mysql::datetime& from) {
    if (from.valid()) {
        uint64_t when = from.as_time_point().time_since_epoch().count();
        when /= 1000000LL;
        return when;
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

} // ns
