project(backend)

find_package(Threads REQUIRED)
find_package(Doxygen)

set(NEXTAPP_BACKEND ${CMAKE_CURRENT_SOURCE_DIR})

set(NEXTAPP_LIBS = ${BOOST_LIBRARIES} stdc++fs ZLIB Threads::Threads)

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
