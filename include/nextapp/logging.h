#pragma once

#include <array>
#include <string_view>

#include <optional>
#include <string_view>

#define LOGFAULT_USE_TID_AS_NAME 1

#include "logfault/logfault.h"

#define LOG_ERROR   LFLOG_ERROR
#define LOG_WARN    LFLOG_WARN
#define LOG_INFO    LFLOG_INFO
#define LOG_DEBUG   LFLOG_DEBUG
#define LOG_TRACE   LFLOG_TRACE

inline std::optional<logfault::LogLevel> toLogLevel(std::string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    using ll_t = std::pair<std::string_view, logfault::LogLevel>;
    static constexpr std::array<ll_t, 9> levels = {
        ll_t{"", logfault::LogLevel::DISABLED},
        ll_t{"off", logfault::LogLevel::DISABLED},
        ll_t{"false", logfault::LogLevel::DISABLED},
        ll_t{"error",  logfault::LogLevel::ERROR},
        ll_t{"warning",  logfault::LogLevel::WARN},
        ll_t{"warn",  logfault::LogLevel::WARN},
        ll_t{"info",  logfault::LogLevel::INFO},
        ll_t{"debug",  logfault::LogLevel::DEBUGGING},
        ll_t{"trace",  logfault::LogLevel::TRACE}
    };

    if (auto it = std::find_if(levels.begin(), levels.end(), [name](const ll_t& lv) {
        return lv.first == name;
            }); it != levels.end()) {
        return it->second;
    }

    throw std::runtime_error{std::string{"Unknown log-level: "} + std::string{name}};
}
