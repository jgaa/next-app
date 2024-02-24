

#include "nextapp/util.h"
#include "nextapp/logging.h"

namespace nextapp {

std::string getEnv(const char *name, std::string def) {
    if (auto var = std::getenv(name)) {
        return var;
    }

    return def;
}

} // ns
