#pragma once
#include <format>


#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>


namespace nextapp::logging {

template <typename T>
std::string_view logName(const T *self) noexcept {
    static const auto name = boost::typeindex::type_id_runtime(*self).pretty_name();
    return name;
}

// One unique entry for each warn or error log event
enum class LogEvent {
    LE_TEST = 0x0001,
    LE_DATABASE_FAILED_TO_CONNECT = 0x0002,
    LE_DATABASE_FAILED_TO_RESOLVE = 0x0003,
    LE_IOTHREAD_THREW             = 0x0004,
};

} // ns

#define LOGFAULT_USE_TID_AS_NAME 1

#include "logfault/logfault.h"

#define LOG_ERROR   LFLOG_ERROR
#define LOG_WARN    LFLOG_WARN
#define LOG_INFO    LFLOG_INFO
#define LOG_DEBUG   LFLOG_DEBUG
#define LOG_TRACE   LFLOG_TRACE

#define LOG_ERROR_N   LFLOG_ERROR  << __PRETTY_FUNCTION__ << ' '
#define LOG_WARN_N    LFLOG_WARN   << __PRETTY_FUNCTION__ << ' '
#define LOG_INFO_N    LFLOG_INFO   << __PRETTY_FUNCTION__ << ' '
#define LOG_DEBUG_N   LFLOG_DEBUG  << __PRETTY_FUNCTION__ << ' '
#define LOG_TRACE_N   LFLOG_TRACE  << __PRETTY_FUNCTION__ << ' '

inline std::ostream& operator << (std::ostream& out, const ::nextapp::logging::LogEvent ev) {
    return out << std::format("lid={:0>4x} ", static_cast<uint32_t>(ev));
}
