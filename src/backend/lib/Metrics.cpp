#include "nextapp/Metrics.h"
#include "nextapp/Server.h"

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

    void LogMessage(const logfault::Message& msg) LOGFAULT_NOEXCEPT override {
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
    using namespace yahat;
    yahat::Metrics::labels_t default_labels;
    using lbl = yahat::Metrics::label_t;

    if (auto region = std::getenv("NEXTAPP_REGION")) {
        default_labels.emplace_back("region", region);
    }

    // Create the metrics objects
    errors_ = metrics_.AddCounter("nextapp_logged_errors", "Number of errors logged", {}, default_labels);
    warnings_ = metrics_.AddCounter("nextapp_logged_warnings", "Number of warnings logged", {}, default_labels);
    asio_worker_threads_ = metrics_.AddGauge("nextapp_worker_threads", "Number of ASIO worker threads", {},
                                            combine(default_labels, lbl{"kind", "asio"}));
    session_subscriptions_ = metrics_.AddGauge("nextapp_session_subscriptions", "Number of update subscriptions", {},
                                            combine(default_labels, lbl{"kind", "update"}));
    data_streams_days_ = metrics_.AddGauge("nextapp_data_streams_days", "Number of data streams for days", {},
                                            combine(default_labels, lbl{"kind", "days"}));
    data_streams_nodes_ = metrics_.AddGauge("nextapp_data_streams_nodes", "Number of data streams for nodes", {},
                                            combine(default_labels, lbl{"kind", "nodes"}));
    data_streams_actions_ = metrics_.AddGauge("nextapp_data_streams_actions", "Number of data streams for actions", {},
                                            combine(default_labels, lbl{"kind", "actions"}));
    sessions_user_ = metrics_.AddGauge("nextapp_sessions", "Number of user sessions", {},
                                            combine(default_labels, lbl{"kind", "user"}));
    sessions_admin_ = metrics_.AddGauge("nextapp_sessions", "Number of admin sessions", {},
                                            combine(default_labels, lbl{"kind", "admin"}));
    users_ = metrics_.AddGauge("nextapp_users", "Number of users", {},
                                            combine(default_labels, lbl{"kind", "users"}));
    tenants_ = metrics_.AddGauge("nextapp_tenants", "Number of tenants", {},
                                            combine(default_labels, lbl{"kind", "tenants"}));
    devices_ = metrics_.AddGauge("nextapp_devices", "Number of devices", {},
                                            combine(default_labels, lbl{"kind", "devices"}));

    const std::vector quantiles = {0.5, 0.9, 0.95, 0.99};
    grpc_request_latency_ = metrics_.AddSummary("nextapp_grpc_request_latency", "gRPC request latency", {},
                                                combine(default_labels, lbl{"kind", "grpc-unary"}), quantiles);

    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::ERROR, errors_));
    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::WARN, warnings_));

// Macro to detect compiler and version
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#if defined(__clang__)
#define COMPILER_NAME "Clang"
#define COMPILER_VERSION TOSTRING(__clang_major__ ) "." TOSTRING(__clang_minor__) "." TOSTRING(__clang_patchlevel__)
#elif defined(__GNUC__)
#define COMPILER_NAME "GCC"
#define COMPILER_VERSION TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__)
#elif defined(_MSC_VER)
#define COMPILER_NAME "MSVC"
#define COMPILER_VERSION TOSTRING(_MSC_VER)
#else
#define COMPILER_NAME "Unknown Compiler"
#define COMPILER_VERSION "Unknown Version"
#endif

    metrics_.AddInfo("nextapp_build", "Build information", {},
        combine(default_labels, lbl{"version", NEXTAPP_VERSION},
            lbl{"build_date", __DATE__},
            lbl{"build_time", __TIME__},
            lbl{"platform", BOOST_PLATFORM},
            lbl{"compiler", COMPILER_NAME},
            lbl{"compiler_version", COMPILER_VERSION},
            lbl{"branch", GIT_BRANCH}));
}


} // ns nextapp::lib
