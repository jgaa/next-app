project(common VERSION ${NEXTAPP_VERSION} LANGUAGES CXX)

#ADD_SUBDIRECTORY(proto)

add_library(${PROJECT_NAME}
    ${NEXTAPP_ROOT}/include/nextapp/util.h
    ${NEXTAPP_ROOT}/include/nextapp/error_mapping.h
    ${NEXTAPP_ROOT}/include/nextapp/AsyncServerWriteReactor.h
    util.cpp
    error_mapping.cpp
)

# clang complains about unknown g++ warnings enabled by the date library
target_compile_options(${PROJECT_NAME} PRIVATE -Wno-unknown-warning-option)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${NEXTAPP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${PROTO_GENERATED_INCLUDE_PATH}>
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    PRIVATE src)

message("PROTO_GENERATED_INCLUDE_PATH: ${PROTO_GENERATED_INCLUDE_PATH}")

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

add_dependencies(${PROJECT_NAME} proto logfault)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${Boost_LIBRARIES}>
        $<BUILD_INTERFACE:Threads::Threads>
        $<BUILD_INTERFACE:proto>
    )

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")
