#pragma once

#include <thread>
#include <string>
#include <cstdint>

namespace nextapp {

struct DbConfig {
    std::string host = "localhost";
    uint16_t port = 3306; // Default for mysql
    size_t max_connections = 2;

    std::string username;
    std::string password;
    std::string database = "nextapp";
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
