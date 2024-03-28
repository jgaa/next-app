#pragma once

#include <thread>
#include <string>
#include <cstdint>
#include "nextapp/util.h"
#include "mysqlpool/conf.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(2,std::thread::hardware_concurrency()), 8);
};

struct GrpcConfig {
    std::string address = "127.0.0.1:10321";
};

struct ServerOptions {
    /*! Print protobuf messages to the log as json
     *  - 1 enable
     *  - 2 enable and format in readable form
     */
    int log_protobuf_messages = 0;
};

struct Config {
    ServerConfig svr;
    jgaa::mysqlpool::DbConfig db;
    GrpcConfig grpc;
    ServerOptions options;
};

} // ns
