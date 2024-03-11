####### basic_tests

add_executable(basic_tests basic_tests.cpp)

set_property(TARGET basic_tests PROPERTY CXX_STANDARD 20)

add_dependencies(basic_tests nalib)

target_include_directories(basic_tests
    PRIVATE
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${NEXTAPP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
    )

target_link_libraries(basic_tests
    ${GTEST_LIBRARIES}
    ${NEXTAPP_DEPENDS}
    nalib
)

add_test(NAME basic_tests COMMAND basic_tests)
