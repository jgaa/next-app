#pragma once

#include <thread>
#include <string>
#include <vector>
#include <cstdint>
#include "nextapp/util.h"
#include "mysqlpool/conf.h"
#include "yahat/HttpServer.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(4,std::thread::hardware_concurrency()), 16);
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

    /*! Disable metrics password
     *
     * If the password is disabled, the metrics endpoint will be available without authentication.
     * This is not recommended for production use, unless it is protected behind a reverse proxy or
     * other authentication mechanism, or is only accessible from IP addresses that are trusted.
     */
    bool no_metrics_password = false;

    /*! Enable the embedded HTTP server.
     *
     *  Currently this is only used for the /metrics endpoint.
     */
    bool enable_http = false;
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
        db.database = getEnv("SIGNUP_DB_DATABASE", "signup");
        db.username = getEnv("SIGNUP_DB_USER", "signup");
        db.password = getEnv("SIGNUP_DB_PASSWORD", "");

        http.http_port = "9013";
        http.num_http_threads = 2;
        http.http_endpoint = "localhost";
        http.auto_handle_cors = false;
    }

    ServerConfig svr;
    GrpcConfig grpc_signup{"localhost:10322"};
    GrpcConfig grpc_nextapp{"https://localhost:10321"};
    ServerOptions options;
    Cluster cluster;
    jgaa::mysqlpool::DbConfig db;
    yahat::HttpConfig http;
};

} // ns
