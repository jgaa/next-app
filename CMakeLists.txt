cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED NEXTAPP_VERSION)
    set(NEXTAPP_VERSION 0.0.3)
endif()

project(next-app
    DESCRIPTION "GTD application"
    HOMEPAGE_URL https://github.com/jgaa/next-app
    VERSION ${NEXTAPP_VERSION}
    LANGUAGES CXX)

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(NEXTAPP_WITH_TESTS "Enable Tests" ON)

add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Doxygen)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.83)
endif()

#include(cmake/3rdparty.cmake)

add_subdirectory(dependencies/logfault)
include_directories(${NEXTAPP_ROOT}/dependencies/logfault/include)

set(MYSQLPOOL_EMBEDDED ON)
set(MYSQLPOOL_LOGGER logfault)
set(MYSQLPOOL_LOG_LEVEL_STR trace)
set(MYSQLPOOL_DBUSER NEXTAPP_DBUSER NEXTAPP_DBUSER)
set(MYSQLPOOL_DBPASSW NEXTAPP_DBPASSW)
set(MYSQLPOOL_DATABASE NEXTAPP_DATABASE)
set(MYSQLPOOL_HOST NEXTAPP_DBHOST)
set(MYSQLPOOL_PORT NEXTAPP_DBPORT)
set(DEFAULT_MYSQLPOOL_DBUSER nextapp)
set(DEFAULT_MYSQLPOOL_DATABASE nextapp)
set(MYSQLPOOL_WITH_TESTS OFF)
set(MYSQLPOOL_WITH_EXAMPLES OFF)
add_subdirectory(dependencies/mysqlpool-cpp)
include_directories(${NEXTAPP_ROOT}/dependencies/mysqlpool-cpp/include)

set(USE_SYSTEM_TZ_DB ON)
set(BUILD_TZ_LIB ON)
add_subdirectory(dependencies/date)
include_directories(${NEXTAPP_ROOT}/dependencies/date/include)


add_subdirectory(src/proto)
add_subdirectory(src/backend)
