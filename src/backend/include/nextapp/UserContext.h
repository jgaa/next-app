
#pragma once

#include <string>
#include <chrono>

#include "mysqlpool/mysqlpool.h"
#include "nextapp.pb.h"
#include "nextapp/logging.h"

namespace nextapp {

class UserContext {
public:
    UserContext() = default;

    UserContext(const std::string& tenantUuid, const std::string& userUuid, const std::string_view timeZone,
                bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options& dbOptions);

    UserContext(const std::string& tenantUuid, const std::string& userUuid,
                const pb::UserGlobalSettings& settings, boost::uuids::uuid sessionId = newUuid());

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
        return !settings_.firstdayofweekismonday();
    }

    const jgaa::mysqlpool::Options& dbOptions() const noexcept {
        return db_options_;
    }

    const boost::uuids::uuid& sessionId() const noexcept {
        return sessionid_;
    }

    const pb::UserGlobalSettings& settings() const noexcept {
        return settings_;
    }

private:
    static boost::uuids::uuid newUuid();

    std::string user_uuid_;
    std::string tenant_uuid_;
    const std::chrono::time_zone* tz_{};
    jgaa::mysqlpool::Options db_options_;
    const boost::uuids::uuid sessionid_ = newUuid();
    pb::UserGlobalSettings settings_;
};



} // ns
