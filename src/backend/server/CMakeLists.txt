project(nextappd
    DESCRIPTION "GTD application daemon"
    VERSION ${NEXTAPP_VERSION}
    HOMEPAGE_URL https://github.com/jgaa/next-app
    LANGUAGES CXX
    )

add_executable(${PROJECT_NAME}
    main.cpp
    )

target_compile_options(${PROJECT_NAME} PRIVATE -g)
target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
#target_compile_definitions(${PROJECT_NAME} PRIVATE BOOST_STACKTRACE_USE_ADDR2LINE)

add_dependencies(${PROJECT_NAME} logfault)

find_program(ADDR2LINE_EXECUTABLE addr2line)
if(NOT ADDR2LINE_EXECUTABLE)
    message(FATAL_ERROR "addr2line not found. Boost.Stacktrace may not be able to symbolize stack traces.")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${NEXTAPP_DEPENDS}
    ${BOOST_LIBRARIES}
    nalib
    )

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${NEXTAPP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    $<INSTALL_INTERFACE:include>
    )

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}App
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})  # This is for Windows

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_compile_definitions(${PROJECT_NAME} PUBLIC -DAPP_VERSION=\"${NEXTAPP_VERSION}\")
target_compile_definitions(${PROJECT_NAME} PRIVATE ${VALDGRIND_ARGS})
