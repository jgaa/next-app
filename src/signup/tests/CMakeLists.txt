if (NOT GTEST_LIBRARIES)
    if (TARGET GTest::gtest)
        set(GTEST_LIBRARIES GTest::gtest)
    elseif(TARGET gtest::gtest)
        set(GTEST_LIBRARIES gtest::gtest)
    else()
        message(FATAL_ERROR "Neither GTest::gtest nor gtest::gtest targets were found")
    endif()
endif()

if (TARGET GTest::gtest_main)
    set(GTEST_MAIN_LIBRARIES GTest::gtest_main)
elseif(TARGET gtest::gtest_main)
    set(GTEST_MAIN_LIBRARIES gtest::gtest_main)
else()
    message(FATAL_ERROR "Neither GTest::gtest_main nor gtest::gtest_main targets were found")
endif()

add_executable(signup_server_tests signup_server_tests.cpp)

set_property(TARGET signup_server_tests PROPERTY CXX_STANDARD 20)

target_compile_definitions(signup_server_tests PRIVATE NEXTAPP_WITH_TESTS)

add_dependencies(signup_server_tests sulib)

target_include_directories(signup_server_tests
    PRIVATE
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${SIGNUP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(signup_server_tests
    ${GTEST_LIBRARIES}
    ${GTEST_MAIN_LIBRARIES}
    ${NEXTAPP_DEPENDS}
    sulib
)

add_test(NAME signup_server_tests COMMAND signup_server_tests)
