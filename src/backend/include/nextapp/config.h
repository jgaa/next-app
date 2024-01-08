#pragma once

#include <thread>
#include <string>
#include <cstdint>
#include "nextapp/util.h"

namespace nextapp {

struct DbConfig {
    std::string host = "localhost";
    uint16_t port = 3306; // Default for mysql
    size_t max_connections = 2;

    std::string username = getEnv("NA_DBUSER","nextapp");
    std::string password = getEnv("NA_DBPASSWD");
    std::string database = getEnv("NA_DATABASE", "nextapp");

    unsigned retry_connect = 20;
    unsigned retry_connect_delay_ms = 2000;
};

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(2,std::thread::hardware_concurrency()), 8);
};

struct GrpcConfig {
    std::string address = "127.0.0.1:10321";
};

struct Config {
    ServerConfig svr;
    DbConfig db;
    GrpcConfig grpc;
};

} // ns
