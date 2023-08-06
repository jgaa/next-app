cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED NEXTAPP_VERSION)
    set(NEXTAPP_VERSION 0.0.1)
endif()

project(next-app
    DESCRIPTION "GTD application"
    HOMEPAGE_URL https://github.com/jgaa/next-app
    VERSION ${NEXTAPP_VERSION}
    LANGUAGES CXX)

option(NEXTAPP_WITH_TESTS "Enable Tests" ON)

add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake/3rdparty.cmake)

add_subdirectory(src/backend)
