#include "signup/Metrics.h"
#include "signup/Server.h"

#include "nextapp/logging.h"

using namespace std;
using namespace std::string_literals;

namespace nextapp {

namespace {

// We use a log-handler to count error and warning log events.
class LogHandler : public logfault::Handler {
public:
    LogHandler(logfault::LogLevel level, Metrics::counter_t *counter)
        : Handler(level),  level_{level}, counter_{counter} {
        if (!counter_) {
            throw std::invalid_argument("counter_ must not be nullptr");
        }
    };

    void LogMessage(const logfault::Message& msg) noexcept override {
        if (msg.level_ == level_) {
            counter_->inc();
        }
    }

private:
    const logfault::LogLevel level_;
    Metrics::counter_t *counter_;
};
}

Metrics::Metrics(Server& server)
    : server_{server}
{

    // Create the metrics objects
    errors_ = metrics_.AddCounter("signup_logged_errors", "Number of errors logged", {});
    warnings_ = metrics_.AddCounter("signup_logged_warnings", "Number of warnings logged", {});
    asio_worker_threads_ = metrics_.AddGauge("signup_worker_threads", "Number of ASIO worker threads", {}, {{"kind", "asio"}});

    const std::vector quantiles = {0.5, 0.9, 0.95, 0.99};
    grpc_request_latency_ = metrics_.AddSummary("signup_grpc_request_latency", "gRPC request latency", {},
                                                {{"kind", "grpc-unary"}}, quantiles);
    unauthorized_admin_requests_ = metrics_.AddCounter("signup_unauthorized_admin_requests", "Number of unauthorized admin requests", {});
    nextapp_connections_ = metrics_.AddGauge("signup_nextapp_connections", "Number of connection s to nextapp servers", {});
    nextapp_servers_ = metrics_.AddGauge("signup_nextapp_servers", "Number of nextapp servers", {});
    regions_ = metrics_.AddGauge("signup_regions", "Number of regions", {});

    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::ERROR, errors_));
    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::WARN, warnings_));
}


} // ns nextapp::lib
