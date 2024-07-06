project(sulib VERSION ${SIGNUP_VERSION} LANGUAGES CXX)


add_library(${PROJECT_NAME}
    ${SIGNUP_BACKEND}/include/signup/GrpcServer.h
    ${SIGNUP_BACKEND}/include/signup/Server.h
    ${SIGNUP_BACKEND}/include/signup/config.h
    ${NEXTAPP_ROOT}/include/nextapp/logging.h
    ${NEXTAPP_ROOT}/include/nextapp/util.h
    Server.cpp
    GrpcServer.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${Boost_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NEXTAPP_ROOT}/include>
    $<BUILD_INTERFACE:${SIGNUP_BACKEND}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${PROTO_GENERATED_INCLUDE_PATH}>
    $<INSTALL_INTERFACE:include>
    PRIVATE $Boost_INCLUDE_DIR
    PRIVATE src)

message("PROTO_GENERATED_INCLUDE_PATH: ${PROTO_GENERATED_INCLUDE_PATH}")

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

add_dependencies(${PROJECT_NAME} mysqlpool logfault common proto ${GRPC_LIBS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${OPENSSL_LIBRARIES}>
        $<BUILD_INTERFACE:${Boost_LIBRARIES}>
        $<BUILD_INTERFACE:Threads::Threads>
        $<BUILD_INTERFACE:common>
        $<BUILD_INTERFACE:proto>
    )

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")
