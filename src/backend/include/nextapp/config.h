#pragma once

#include <thread>
#include <string>
#include <cstdint>
#include "nextapp/util.h"
#include "mysqlpool/conf.h"
#include "nextapp/certs.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(2,std::thread::hardware_concurrency()), 8);
    size_t time_block_max_actions = 24;
};

struct GrpcConfig {
    std::string address = "127.0.0.1:10321";
    std::string tls_mode = "ca"; // ca | none

    // Keepalive options
    unsigned keepalive_time_sec = 10;
    unsigned keepalive_timeout_sec = 20;
    unsigned min_recv_ping_interval_without_cata_sec = 5;
    unsigned max_ping_strikes = 7;
};

struct ServerOptions {
    /*! Print protobuf messages to the log as json
     *  - 1 enable
     *  - 2 enable and format in readable form
     */
    int log_protobuf_messages = 0;

    /*! Maximum page size for paginated results */
    size_t max_page_size = 250;

    /*! DNS names in the self-signed server cert for grpc */
    std::vector<std::string> server_cert_dns_names;
};

struct Config {
    Config() {
        db.timer_interval_ms = 30000;
        db.max_connections = 64;
    }

    ServerConfig svr;
    jgaa::mysqlpool::DbConfig db;
    GrpcConfig grpc;
    ServerOptions options;
    CaOptions ca;
};

} // ns
