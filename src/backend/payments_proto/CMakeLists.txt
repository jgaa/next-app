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
    payments/v1/payments.proto
    payments/v1/notifications.proto
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

if (TARGET gRPC::grpc_cpp_plugin)
    get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
else()
    find_program(grpc_cpp_plugin_location grpc_cpp_plugin REQUIRED)
endif()

if (TARGET protobuf::protoc)
    get_target_property(protobuf_protoc_location protobuf::protoc LOCATION)
    set(payments_proto_protoc_dep protobuf::protoc)
else()
    set(protobuf_protoc_location ${Protobuf_PROTOC_EXECUTABLE})
    set(payments_proto_protoc_dep ${protobuf_protoc_location})
endif()

function(nextapp_generate_payments_proto proto_rel_path)
    get_filename_component(proto_dir ${proto_rel_path} DIRECTORY)
    get_filename_component(proto_name_we ${proto_rel_path} NAME_WLE)

    set(proto_abs_path ${PAYMENTS_PROTO_IMPORT_ROOT}/${proto_rel_path})
    set(proto_cpp_h ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}/${proto_name_we}.pb.h)
    set(proto_cpp_cc ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}/${proto_name_we}.pb.cc)
    set(proto_grpc_h ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}/${proto_name_we}.grpc.pb.h)
    set(proto_grpc_cc ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}/${proto_name_we}.grpc.pb.cc)

    add_custom_command(
        OUTPUT ${proto_cpp_h} ${proto_cpp_cc}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}
        COMMAND ${protobuf_protoc_location}
        ARGS
            --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
            -I ${PAYMENTS_PROTO_IMPORT_ROOT}
            ${proto_rel_path}
        WORKING_DIRECTORY ${PAYMENTS_PROTO_IMPORT_ROOT}
        DEPENDS ${proto_abs_path} ${payments_proto_protoc_dep}
        COMMENT "Running cpp protocol buffer compiler on ${proto_abs_path}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${proto_grpc_h} ${proto_grpc_cc}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${proto_dir}
        COMMAND ${protobuf_protoc_location}
        ARGS
            --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
            --plugin=protoc-gen-grpc=${grpc_cpp_plugin_location}
            -I ${PAYMENTS_PROTO_IMPORT_ROOT}
            ${proto_rel_path}
        WORKING_DIRECTORY ${PAYMENTS_PROTO_IMPORT_ROOT}
        DEPENDS ${proto_abs_path} ${payments_proto_protoc_dep}
        COMMENT "Running grpc protocol buffer compiler on ${proto_abs_path}"
        VERBATIM
    )

    set(PAYMENTS_PROTO_GENERATED_CPP
        ${PAYMENTS_PROTO_GENERATED_CPP}
        ${proto_cpp_h}
        ${proto_cpp_cc}
        PARENT_SCOPE
    )
    set(PAYMENTS_PROTO_GENERATED_GRPC
        ${PAYMENTS_PROTO_GENERATED_GRPC}
        ${proto_grpc_h}
        ${proto_grpc_cc}
        PARENT_SCOPE
    )
endfunction()

foreach(payments_proto_file IN LISTS PAYMENTS_PROTO_FILES)
    nextapp_generate_payments_proto(${payments_proto_file})
endforeach()

add_library(${PROJECT_NAME} STATIC
    ${PAYMENTS_PROTO_GENERATED_CPP}
    ${PAYMENTS_PROTO_GENERATED_GRPC}
    ${extra_proto_sources}
)

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
