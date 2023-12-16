INCLUDE(FindProtobuf)
FIND_PACKAGE(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
set(GRPC_LIB gRPC::grpc gRPC::grpc++)
set(GRPC_SRC_FILES nextapp.proto)

set(PROTO_FILES
   ${GRPC_SRC_FILES}
)

add_library(proto ${PROTO_FILES})
target_link_libraries(proto
    PUBLIC
        protobuf::libprotobuf
        ${GRPC_LIB}
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

#
# Compile protobuf and grpc files in proto target to cpp
#
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET proto LANGUAGE cpp)

if (NSBLAST_CLUSTER)
    protobuf_generate(TARGET proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")
endif()

set(PROTO_GENERATED_INCLUDE_PATH "${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
