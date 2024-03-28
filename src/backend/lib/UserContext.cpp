
#include "nextapp/UserContext.h"
#include "nextapp/logging.h"
#include "nextapp/util.h"

using namespace std;

namespace nextapp {

UserContext::UserContext(const std::string &tenantUuid, const std::string &userUuid, const std::string_view timeZone, bool sundayIsFirstWeekday, const jgaa::mysqlpool::Options &dbOptions)
    : user_uuid_{userUuid},
    tenant_uuid_{tenantUuid},
    sunday_is_first_weekday_{sundayIsFirstWeekday},
    db_options_{dbOptions} {
    if (timeZone.empty()) {
        tz_ = std::chrono::current_zone();
    } else {
        tz_ = std::chrono::locate_zone(timeZone);
        if (tz_ == nullptr) {
            LOG_DEBUG << "UserContext: Invalid timezone: " << timeZone;
            throw std::invalid_argument("Invalid timezone: " + std::string{timeZone});
        }
    }
}

boost::uuids::uuid UserContext::newUuid()
{
    return ::nextapp::newUuid();
}



} // ns
