
#include "nextapp/UserContext.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"

using namespace std;

namespace nextapp {

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid, const std::string_view timeZone, bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options &dbOptions)
    : user_uuid_{userUuid},
    tenant_uuid_{tenantUuid},
    db_options_{dbOptions} {

    if (timeZone.empty()) {
        tz_ = std::chrono::current_zone();
    } else {
        tz_ = std::chrono::locate_zone(timeZone);
        settings_.set_timezone(string{tz_->name()});
        settings_.set_firstdayofweekismonday(!sundayIsFirstWeekday);
        if (tz_ == nullptr) {
            LOG_DEBUG << "UserContext: Invalid timezone: " << timeZone;
            throw std::invalid_argument("Invalid timezone: " + std::string{timeZone});
        }
    }
}

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid, const pb::UserGlobalSettings &settings)
    : user_uuid_{userUuid}, tenant_uuid_{tenantUuid}, settings_(settings) {
    try {
        tz_ = std::chrono::locate_zone(settings_.timezone());
    } catch (const std::exception& e) {
        tz_ = std::chrono::current_zone();
        LOG_WARN << "Failed to locate timezone " << settings_.timezone()
                 << ". Using " << tz_->name() << " instead.";

        // TODO: Send a notice to the user about their time zone setting.
    }

    assert(tz_ != nullptr);
    db_options_.time_zone = tz_->name();
    db_options_.reconnect_and_retry_query = true;
}

boost::uuids::uuid UserContext::newUuid()
{
    return ::nextapp::newUuid();
}



} // ns
