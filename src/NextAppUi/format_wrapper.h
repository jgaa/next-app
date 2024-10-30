#pragma once

#pragma once

#if __cpp_lib_format // Check if <format> is available
#   include <format>
#   define NA_FORMAT std::format
#else
#include <fmt/core.h>
#include <fmt/format.h>

#define NA_FORMAT fmt::format
// template <typename... Args>
// inline auto format(Args&&... args) {
//     return fmt::format(std::forward<Args>(args)...);
// }
#endif
