project(signup)

find_package(Threads REQUIRED)
find_package(Doxygen)

set(SIGNUP_BACKEND ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.83)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED MODULE COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    json
    )

set(SIGNUP_LIBS = ${Boost_LIBRARIES} ZLIB Threads::Threads)

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

add_subdirectory(lib)
add_subdirectory(server)

# if (SIGNUP_WITH_TESTS)
#     find_package(GTest REQUIRED)
#     enable_testing()
#     add_subdirectory(tests)
# endif()
