project(nalib VERSION ${NEXTAPP_VERSION} LANGUAGES CXX)

#ADD_SUBDIRECTORY(proto)

add_library(${PROJECT_NAME}
    ${NEXTAPP_BACKEND}/include/nextapp/nextappd.h
    ${NEXTAPP_BACKEND}/include/nextapp/logging.h
    ${NEXTAPP_BACKEND}/include/nextapp/db.h
    ${NEXTAPP_BACKEND}/include/nextapp/errors.h
    ${NEXTAPP_BACKEND}/include/nextapp/config.h
    ${NEXTAPP_BACKEND}/include/nextapp/Server.h
    ${NEXTAPP_BACKEND}/include/nextapp/GrpcServer.h
    util.cpp
    db/db.cpp
    Server.cpp
    grpc/GrpcServer.cpp
)

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

add_dependencies(${PROJECT_NAME} logfault proto ${GRPC_LIBS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
#        $<BUILD_INTERFACE:${Protobuf_LIBRARIES}>
        $<BUILD_INTERFACE:${OPENSSL_LIBRARIES}>
        $<BUILD_INTERFACE:${Boost_LIBRARIES}>
        $<BUILD_INTERFACE:${stdc++fs}>
        $<BUILD_INTERFACE:Threads::Threads>
        $<BUILD_INTERFACE:proto>
    )

message(STATUS "Components to pack (${PROJECT_NAME}): ${CPACK_COMPONENTS_ALL}")
