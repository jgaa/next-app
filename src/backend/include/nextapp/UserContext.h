
#pragma once

#include <string>
#include <chrono>

#include "mysqlpool/mysqlpool.h"

namespace nextapp {

class UserContext {
public:
    UserContext() = default;

    UserContext( const std::string& tenantUuid, const std::string& userUuid, const std::string_view timeZone,
                bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options& dbOptions);

    ~UserContext() = default;

    const std::string& userUuid() const noexcept {
        return user_uuid_;
    }

    const std::string& tenantUuid() const noexcept {
        return tenant_uuid_;
    }

    const std::chrono::time_zone& tz() const noexcept {
        assert(tz_ != nullptr);
        return *tz_;
    }

    bool sundayIsFirstWeekday() const noexcept {
        return sunday_is_first_weekday_;
    }

    const jgaa::mysqlpool::Options& dbOptions() const noexcept {
        return db_options_;
    }

    const boost::uuids::uuid& sessionId() const noexcept {
        return sessionid_;
    }

private:
    static boost::uuids::uuid newUuid();

    std::string user_uuid_;
    std::string tenant_uuid_;
    const std::chrono::time_zone* tz_{};
    bool sunday_is_first_weekday_{true};
    jgaa::mysqlpool::Options db_options_;
    const boost::uuids::uuid sessionid_ = newUuid();
};



} // ns
