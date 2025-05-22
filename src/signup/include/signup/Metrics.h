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


    summary_t& grpc_request_latency() {
        return *grpc_request_latency_;
    }

    counter_t& unauthorized_admin_requests() {
        return *unauthorized_admin_requests_;
    }

    gauge_t& nextapp_connections() {
        return *nextapp_connections_;
    }

    gauge_t& nextapp_servers() {
        return *nextapp_servers_;
    }

    gauge_t& nextapp_instances_up() {
        return *nextapp_instances_up_;
    }

    gauge_t& regions() {
        return *regions_;
    }

private:
    Server& server_;
    yahat::Metrics metrics_;

    counter_t * errors_{};
    counter_t * warnings_{};
    gauge_t * asio_worker_threads_{};
    summary_t * grpc_request_latency_{};
    counter_t * unauthorized_admin_requests_{};
    gauge_t * nextapp_connections_{};
    gauge_t * nextapp_instances_up_{};
    gauge_t * nextapp_servers_{};
    gauge_t * regions_{};
};


} // ns nextapp

