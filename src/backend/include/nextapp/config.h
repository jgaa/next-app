#pragma once

#include <thread>
#include <string>
#include <cstdint>
#include "nextapp/util.h"
#include "mysqlpool/conf.h"
#include "nextapp/certs.h"
#include "yahat/HttpServer.h"
#include "cpp-push/cpp-push.h"

namespace nextapp {

struct ServerConfig {
    size_t io_threads = std::min<size_t>(std::max<size_t>(4,std::thread::hardware_concurrency()), 16);
    size_t time_block_max_actions = 24;
    uint32_t session_timeout_sec = 60 * 5;
    uint32_t session_timer_interval_sec = 15;
};

struct GrpcConfig {
    std::string address = "127.0.0.1:10321";
    std::string tls_mode = "ca"; // ca | none

    // Keepalive options
    bool disable_keepalive = false;
    unsigned keepalive_time_sec = 10;
    unsigned keepalive_timeout_sec = 20;
    unsigned min_recv_ping_interval_without_data_sec = 5;
    unsigned max_ping_strikes = 7;
};

struct PaymentOptions {
    /*! Enable Plan
     *
     *  Enables tenant limits and payed plans.
     */
    bool enable_plan = false;

    /*! Payment service gRPC endpoint URL.
     *
     *  Examples:
     *  - http://127.0.0.1:10421
     *  - https://payments.internal:10443
     */
    std::string service_url;
    std::string product_id = "nextapp";
    std::string return_url;
    std::string cancel_url;
    std::string success_url;

    /*! TLS PEM files used when connecting to the payment service over https. */
    std::string tls_ca;
    std::string tls_cert;
    std::string tls_key;

    /*! Interval in seconds for syncing plans with the payment service.
     *
     *  This is to make sure we have up to date information about the plans, and can enforce limits correctly.
     *
     *  0 disables the feature and plans will only be synced when the server starts up.
     */
    uint32_t plan_sync_interval_seconds = 60 * 60 * 3;

    uint32_t grace_period_days = 7;
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

    /* Time interval for updating costly metrics values
     *
     * The value is in minutes.
     *
     * To disable the timer, set this to 0.
     */
    size_t metrics_timer_minutes = 5;

    /*! Maximum size for feedback log entries
     *
     *  This is to prevent excessively large feedback entries from being stored in the database.
     *  The size is in bytes.
     */
    size_t max_feedback_log_size = 32 * 1024;
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
    jgaa::cpp_push::Config push;
    bool push_enabled = false;
    PaymentOptions payment;
};

} // ns
