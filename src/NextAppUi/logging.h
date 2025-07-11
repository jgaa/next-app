#pragma once

namespace nextapp::logging {

} // ns

#define LOGFAULT_USE_TID_AS_NAME 1
//#define LOGFAULT_USE_QT_LOG 1

#ifdef __ANDROID__
#define LOGFAULT_USE_ANDROID_NDK_LOG 1
#endif

#include "logfault/logfault.h"

#define LOG_ERROR   LFLOG_ERROR
#define LOG_WARN    LFLOG_WARN
#define LOG_INFO    LFLOG_INFO
#define LOG_DEBUG   LFLOG_DEBUG
#define LOG_TRACE   LFLOG_TRACE

#if !defined(__PRETTY_FUNCTION__) && !defined(__GNUC__)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif


#define LOG_ERROR_N   LFLOG_ERROR  << __PRETTY_FUNCTION__ << " - "
#define LOG_WARN_N    LFLOG_WARN   << __PRETTY_FUNCTION__ << " - "
#define LOG_INFO_N    LFLOG_INFO   << __PRETTY_FUNCTION__ << " - "
#define LOG_DEBUG_N   LFLOG_DEBUG  << __PRETTY_FUNCTION__ << " - "
#define LOG_TRACE_N   LFLOG_TRACE  << __PRETTY_FUNCTION__ << " - "

namespace nextapp::logging {

#ifdef __ANDROID__

void initAndroidLogging(logfault::LogLevel level = logfault::LogLevel::DEBUGGING);

#endif

}
