####### time_date_tests

add_executable(time_date_tests time_date_tests.cpp)

set_property(TARGET time_date_tests PROPERTY CXX_STANDARD 20)

add_dependencies(time_date_tests nalib)

target_include_directories(time_date_tests
    PRIVATE
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${NEXTAPP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
    )

target_link_libraries(time_date_tests
    ${GTEST_LIBRARIES}
    ${NEXTAPP_DEPENDS}
    nalib
)

add_test(NAME time_date_tests COMMAND time_date_tests)
