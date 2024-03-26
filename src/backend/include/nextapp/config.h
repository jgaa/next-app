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
    /// Delete the work-events when a work session is marked as done.
    bool delete_work_events_from_db = false;
};

struct Config {
    ServerConfig svr;
    jgaa::mysqlpool::DbConfig db;
    GrpcConfig grpc;
    ServerOptions options;
};

} // ns
