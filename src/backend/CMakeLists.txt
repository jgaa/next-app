project(backend)

find_package(Threads REQUIRED)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.82)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    )

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

add_subdirectory(lib)
add_subdirectory(server)

if (NEXTAPP_WITH_TESTS)
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

