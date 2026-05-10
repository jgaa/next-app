project(payments_proto)

find_package(Protobuf CONFIG)
if(NOT Protobuf_FOUND)
  message("Falling back to: find_package(Protobuf)")
  find_package(Protobuf REQUIRED)
endif()

message("Using protobuf ${Protobuf_VERSION}")

find_package(gRPC CONFIG REQUIRED)

set(PAYMENTS_PROTO_IMPORT_ROOT ${NEXTAPP_ROOT}/dependencies/payments-pub/proto)
set(PAYMENTS_PROTO_FILES
    ${PAYMENTS_PROTO_IMPORT_ROOT}/payments/v1/payments.proto
    ${PAYMENTS_PROTO_IMPORT_ROOT}/payments/v1/notifications.proto
)

if (Protobuf_VERSION VERSION_GREATER 22.0)
    message("Using new protobuf version that depends on absl")

    find_package(absl CONFIG REQUIRED)
    find_package(utf8_range CONFIG REQUIRED)

    set(extra_proto_sources ${NEXTAPP_ROOT}/src/proto/absl_fix.cpp)
    set(extra_prot_libs
        utf8_range::utf8_validity
        absl::log_internal_message
        absl::log_internal_check_op
    )
endif()

add_library(${PROJECT_NAME} STATIC ${PAYMENTS_PROTO_FILES} ${extra_proto_sources})

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        protobuf::libprotobuf
        protobuf::libprotobuf-lite
        gRPC::grpc
        gRPC::grpc++
        ${extra_prot_libs}
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${PAYMENTS_PROTO_IMPORT_ROOT}>
        $<INSTALL_INTERFACE:include>
)

if (TARGET gRPC::grpc_cpp_plugin)
    get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
else()
    find_program(grpc_cpp_plugin_location grpc_cpp_plugin REQUIRED)
endif()

protobuf_generate(
    TARGET ${PROJECT_NAME}
    LANGUAGE cpp
    IMPORT_DIRS ${PAYMENTS_PROTO_IMPORT_ROOT}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
protobuf_generate(
    TARGET ${PROJECT_NAME}
    LANGUAGE grpc
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    IMPORT_DIRS ${PAYMENTS_PROTO_IMPORT_ROOT}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}"
)
