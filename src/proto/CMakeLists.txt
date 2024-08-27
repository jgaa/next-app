INCLUDE(FindProtobuf)

find_package(Protobuf CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

message("Using protobuf ${Protobuf_VERSION}")

find_package(gRPC CONFIG REQUIRED)
if (Protobuf_VERSION VERSION_GREATER 22.0)
    message("Using new protobuf version that depends on absl")

    find_package(absl CONFIG REQUIRED)
    find_package(utf8_range CONFIG REQUIRED)

    set(extra_proto_sources absl_fuckup.cpp)
    set(extra_prot_libs
        utf8_range::utf8_validity
        absl::log_internal_message
        absl::log_internal_check_op
    )
endif()

set(GRPC_SRC_FILES nextapp.proto signup.proto)

set(PROTO_FILES
   ${GRPC_SRC_FILES}
   common.proto
)

add_library(proto ${PROTO_FILES} ${extra_proto_sources})

target_link_libraries(proto
    PUBLIC
    protobuf::libprotobuf protobuf::libprotobuf-lite
    gRPC::grpc gRPC::grpc++
    ${extra_prot_libs}
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

#
# Compile protobuf and grpc files in proto target to cpp
#
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET proto LANGUAGE cpp)
protobuf_generate(TARGET proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

set(PROTO_GENERATED_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
