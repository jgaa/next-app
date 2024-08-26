INCLUDE(FindProtobuf)
FIND_PACKAGE(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(utf8_range CONFIG REQUIRED)

set(GRPC_SRC_FILES nextapp.proto signup.proto)

set(PROTO_FILES
   ${GRPC_SRC_FILES}
   common.proto
)

add_library(proto ${PROTO_FILES}
    absl_fuckup.cpp)
target_link_libraries(proto
    PUBLIC
    protobuf::libprotobuf protobuf::libprotobuf-lite
    gRPC::grpc gRPC::grpc++
    absl::log_internal_check_op
    utf8_range::utf8_validity
    absl::log_internal_message
    absl::log_internal_check_op
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

#
# Compile protobuf and grpc files in proto target to cpp
#
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET proto LANGUAGE cpp)
protobuf_generate(TARGET proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

set(PROTO_GENERATED_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
