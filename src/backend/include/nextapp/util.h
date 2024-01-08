#pragma once

#include <string>
#include <cstddef>

namespace nextapp {

    std::string getEnv(const char *name, std::string def = {});

}
