#pragma once

#include <cassert>

#include "yahat/Metrics.h"

namespace nextapp {

class Server;

class Metrics
{
public:
    using gauge_t = yahat::Metrics::Gauge<uint64_t>;
    using counter_t = yahat::Metrics::Counter<uint64_t>;
    using gauge_scoped_t = yahat::Metrics::Scoped<gauge_t>;
    using counter_scoped_t = yahat::Metrics::Scoped<counter_t>;
    using summary_t = yahat::Metrics::Summary<double>;
    using summary_scoped_t = yahat::Metrics::Scoped<summary_t>;

    Metrics(Server& server);

    yahat::Metrics& metrics() {
        return metrics_;
    }

    // Access to various metrics objects
    counter_t& errors() {
        return *errors_;
    }

    counter_t& warnings() {
        return *warnings_;
    }

    gauge_t& asio_worker_threads() {
        return *asio_worker_threads_;
    }

    gauge_t& session_subscriptions() {
        return *session_subscriptions_;
    }

    gauge_t& data_streams_days() {
        return *data_streams_days_;
    }

    gauge_t& data_streams_nodes() {
        return *data_streams_nodes_;
    }

    gauge_t& data_streams_actions() {
        return *data_streams_actions_;
    }

    gauge_t& sessions_user() {
        return *sessions_user_;
    }

    gauge_t& sessions_admin() {
        return *sessions_admin_;
    }

    summary_t& grpc_request_latency() {
        return *grpc_request_latency_;
    }


private:
    Server& server_;
    yahat::Metrics metrics_;

    counter_t * errors_{};
    counter_t * warnings_{};
    gauge_t * sessions_user_{};
    gauge_t * sessions_admin_{};
    gauge_t * session_subscriptions_{};
    gauge_t * data_streams_days_{};
    gauge_t * data_streams_nodes_{};
    gauge_t * data_streams_actions_{};
    gauge_t * asio_worker_threads_{};
    summary_t * grpc_request_latency_{};
};


} // ns nextapp

