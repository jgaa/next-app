#pragma once

#include <thread>
#include <string>
#include <cstdint>
#include "nextapp/util.h"
#include "mysqlpool/conf.h"
#include "nextapp/certs.h"
#include "yahat/HttpServer.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(2,std::thread::hardware_concurrency()), 8);
    size_t time_block_max_actions = 24;
    uint32_t session_timeout_sec = 60 * 5;
    uint32_t session_timer_interval_sec = 15;
};

struct GrpcConfig {
    std::string address = "127.0.0.1:10321";
    std::string tls_mode = "ca"; // ca | none

    // Keepalive options
    unsigned keepalive_time_sec = 10;
    unsigned keepalive_timeout_sec = 20;
    unsigned min_recv_ping_interval_without_data_sec = 5;
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

    /*! Number of messages to batch in a stream */
    size_t stream_batch_size = 250;

    /*! DNS names in the self-signed server cert for grpc */
    std::vector<std::string> server_cert_dns_names;

    /*! The max number of items that can be updated in a batch
     *
     *  see for example rpc UpdateActions
     */
    size_t max_batch_updates = 100;

    /*! Number of milliseconds to wait between publishing each notification for mass notifications
     *
     * This is to prevent the server from spending all it's resources on the notifications.
     */
    unsigned notification_delay_ms = 5;
};

struct Config {
    Config() {
        db.timer_interval_ms = 30000;
        db.max_connections = 16;
        db.username = "nextapp";
        db.database = "nextapp";

        http.http_port = "9012";
        http.num_http_threads = 2;
        http.http_endpoint = "localhost";
        http.auto_handle_cors = false;
    }

    ServerConfig svr;
    jgaa::mysqlpool::DbConfig db;
    GrpcConfig grpc;
    ServerOptions options;
    CaOptions ca;

    yahat::HttpConfig http;
    bool enable_http = true;
};

} // ns
