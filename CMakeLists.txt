cmake_minimum_required(VERSION 3.22)

if (NOT DEFINED NEXTAPP_VERSION)
    set(NEXTAPP_VERSION 0.8.3) # MajorVersion.MinorVersion.PatchVersion
endif()

project(next-app
    DESCRIPTION "GTD application"
    HOMEPAGE_URL https://github.com/jgaa/next-app
    VERSION ${NEXTAPP_VERSION}
    LANGUAGES CXX)

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# Linux
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX_BUILD ON)
  message(STATUS "Linux build")
endif()

# Windows
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(WINDOWS_BUILD ON)
  message(STATUS "Windows build")
endif()

# Android
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(ANDROID_BUILD ON)
  message(STATUS "Android build")
endif()

# Apple platforms (macOS vs. iOS)
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # CMAKE_OSX_SYSROOT will be something like "macosx", "iphoneos", or "iphonesimulator"
  if (CMAKE_OSX_SYSROOT MATCHES "^(iphoneos|iphonesimulator)")
    set(IOS_BUILD ON)
    message(STATUS "iOS build")
  else()
    set(MACOS_BUILD ON)
    message(STATUS "macOS build")
  endif()
endif()


option(NEXTAPP_WITH_TESTS "Enable Tests" OFF)
option(NEXTAPP_BOOST_USE_STATIC_LIBS "Link statically with boost libraries" ON)
option(NEXTAPP_WITH_UI "Include the UI project in the build" ON)
option(NEXTAPP_WITH_BACKEND "Include the backend server project in the build" ${LINUX_BUILD})
option(NEXTAPP_WITH_SIGNUP "Include the signup server project in the build" ${LINUX_BUILD})
option(NEXTAPP_USE_VALGRIND "Add support to use valgrind to run tests" OFF)

if (BOOST_USE_VALGRIND)
    set(BOOST_USE_VALGRIND ON CACHE INTERNAL "")
    set(BOOST_ASIO_DISABLE_HANDLER_ALLOC_HOOKS ON CACHE INTERNAL "")
    add_definitions(-DBOOST_USE_VALGRIND)
    add_definitions(-DBOOST_ASIO_DISABLE_HANDLER_ALLOC_HOOKS)
endif()

if (NEXTAPP_BOOST_USE_STATIC_LIBS)
    message("Using static boost libraries")
    set(Boost_USE_STATIC_LIBS ON CACHE INTERNAL "")
else()
    set(Boost_USE_STATIC_LIBS OFF CACHE INTERNAL "")
endif()

add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

# Get Git commit ID
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get Git branch name
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Add definitions to the compiler
add_definitions(-DGIT_COMMIT_ID=\"${GIT_COMMIT_ID}\")
add_definitions(-DGIT_BRANCH=\"${GIT_BRANCH}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

# Get the current Git branch name
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Git branch: ${GIT_BRANCH}")
add_compile_definitions(GIT_BRANCH="${GIT_BRANCH}")

find_package(Threads REQUIRED)
if (NOT ANDROID)
    find_package(OpenSSL REQUIRED)
endif()
find_package(Doxygen)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.85)
endif()

add_subdirectory(dependencies/logfault)
include_directories(${NEXTAPP_ROOT}/dependencies/logfault/include)

if (NEXTAPP_WITH_BACKEND OR NEXTAPP_WITH_SIGNUP)
    find_package(ZLIB REQUIRED)

    add_subdirectory(src/proto)
    add_subdirectory(src/common)

    set(YAHAT_ENABLE_METRICS ON)
    set(YAHAT_WITH_EXAMPLES OFF)
    set(YAHAT_WITH_TESTS OFF)
    set(USE_LOGFAULT ON)
    add_subdirectory(dependencies/yahat-cpp)
    include_directories(${NEXTAPP_ROOT}/dependencies/yahat-cpp/include)

    set(MYSQLPOOL_EMBEDDED ON CACHE INTERNAL "")
    #set(LOGFAULT_ROOT ${NEXTAPP_ROOT}/dependencies/logfault CACHE INTERNAL "")
    set(MYSQLPOOL_LOGGER logfault CACHE INTERNAL "")
    set(MYSQLPOOL_LOG_LEVEL_STR trace CACHE INTERNAL "")
    set(MYSQLPOOL_DBUSER NEXTAPP_DBUSER NEXTAPP_DBUSER CACHE INTERNAL "")
    set(MYSQLPOOL_DBPASSW NEXTAPP_DBPASSW CACHE INTERNAL "")
    set(MYSQLPOOL_DATABASE NEXTAPP_DATABASE CACHE INTERNAL "")
    set(MYSQLPOOL_DBHOST NEXTAPP_DBHOST CACHE INTERNAL "")
    set(MYSQLPOOL_DBPORT NEXTAPP_DBPORT CACHE INTERNAL "")
    # set(DEFAULT_MYSQLPOOL_DBUSER nextapp CACHE INTERNAL "")
    # set(DEFAULT_MYSQLPOOL_DATABASE nextapp CACHE INTERNAL "")
    set(MYSQLPOOL_WITH_TESTS OFF CACHE INTERNAL "")
    set(MYSQLPOOL_WITH_EXAMPLES OFF CACHE INTERNAL "")
    add_subdirectory(dependencies/mysqlpool-cpp)
    include_directories(${NEXTAPP_ROOT}/dependencies/mysqlpool-cpp/include)

endif()

if (NEXTAPP_WITH_BACKEND)
    message("Building backend server")

    set(USE_SYSTEM_TZ_DB ON CACHE INTERNAL "")
    set(BUILD_TZ_LIB ON CACHE INTERNAL "")
    add_subdirectory(dependencies/date)
    include_directories(${NEXTAPP_ROOT}/dependencies/date/include)

    add_subdirectory(src/backend)
endif()

if (NEXTAPP_WITH_SIGNUP)
    message("Building signup server")
    add_subdirectory(src/signup)
endif()

if (NEXTAPP_WITH_UI)
    message("Building UI")
    add_subdirectory(src/NextAppUi)
endif()
