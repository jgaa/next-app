cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED NEXTAPP_VERSION)
    set(NEXTAPP_VERSION 0.0.2)
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

include(cmake/3rdparty.cmake)

add_subdirectory(src/proto)
add_subdirectory(src/backend)
