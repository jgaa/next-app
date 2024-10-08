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

    void LogMessage(const logfault::Message& msg) override {
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
    errors_ = metrics_.AddCounter("nextapp_logged_errors", "Number of errors logged", {});
    warnings_ = metrics_.AddCounter("nextapp_logged_warnings", "Number of warnings logged", {});
    asio_worker_threads_ = metrics_.AddGauge("nextapp_worker_threads", "Number of ASIO worker threads", {}, {{"kind", "asio"}});
    session_subscriptions_ = metrics_.AddGauge("nextapp_session_subscriptions", "Number of update subscriptions", {}, {{"kind", "update"}});
    data_streams_days_ = metrics_.AddGauge("nextapp_data_streams_days", "Number of data streams for days", {}, {{"kind", "days"}});
    data_streams_nodes_ = metrics_.AddGauge("nextapp_data_streams_nodes", "Number of data streams for nodes", {}, {{"kind", "nodes"}});
    data_streams_actions_ = metrics_.AddGauge("nextapp_data_streams_actions", "Number of data streams for actions", {}, {{"kind", "actions"}});
    sessions_user_ = metrics_.AddGauge("nextapp_sessions", "Number of user sessions", {}, {{"kind", "user"}});
    sessions_admin_ = metrics_.AddGauge("nextapp_sessions", "Number of admin sessions", {}, {{"kind", "admin"}});

    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::ERROR, errors_));
    logfault::LogManager::Instance().AddHandler(std::make_unique<LogHandler>(logfault::LogLevel::WARN, warnings_));
}


} // ns nextapp::lib
