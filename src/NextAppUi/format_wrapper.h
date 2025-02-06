#pragma once

#if defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
#include <format>
namespace nextapp {
using ::std::format;
}
#else
#include <fmt/core.h>
#include <fmt/format.h>
namespace nextapp {
using ::fmt::format;
}
#endif
