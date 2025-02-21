#pragma once

#include <thread>
#include <string>
#include <vector>
#include <cstdint>
//#include "nextapp/util.h"
#include "mysqlpool/conf.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(2,std::thread::hardware_concurrency()), 8);
    size_t time_block_max_actions = 6;
};

struct GrpcConfig {
    std::string address;
    std::string tls_mode = "cert"; // cert | none
    std::string ca_cert;
    std::string server_cert;
    std::string server_key;

    // Keepalive options
    unsigned keepalive_time_sec = 10;
    unsigned keepalive_timeout_sec = 20;
};

struct ServerOptions {
    /*! Print protobuf messages to the log as json
     *  - 1 enable
     *  - 2 enable and format in readable form
     */
    int log_protobuf_messages = 0;

    /*! Maximum page size for paginated results */
    size_t max_page_size = 250;

    std::string fqdn = "localhost";

    unsigned timer_interval_sec = 120;

    unsigned retry_connect_to_nextappd_secs = 2; // 0 to disable
    unsigned max_retry_time_to_nextapp_secs = 60; //
};

struct Cluster {
    // When new tenants sign up, they are assigned to a backend in the list
    // on a round-robin basis.
    //std::vector<std::string> backends; // host:port

    std::string welcome_path;
    std::string eula_path;
    std::string nextapp_public_url;
    unsigned timer_interval_sec = 120;
};

struct Config {
    Config() {
        db.database = "signup";
        db.username = "signup";
    }

    ServerConfig svr;
    GrpcConfig grpc_signup{"localhost:10322"};
    GrpcConfig grpc_nextapp{"https://localhost:10321"};
    ServerOptions options;
    Cluster cluster;
    jgaa::mysqlpool::DbConfig db;
};

} // ns
