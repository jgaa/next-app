project(backend)

find_package(Threads REQUIRED)
find_package(Doxygen)

set(NEXTAPP_BACKEND ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.83)
endif()

option(Boost_USE_STATIC_LIBS "Link statically with boost libraries" ON)
find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    json
    )

set(NEXTAPP_LIBS = ${Boost_LIBRARIES} stdc++fs ZLIB Threads::Threads)

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

add_subdirectory(lib)
add_subdirectory(server)

if (NEXTAPP_WITH_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()
