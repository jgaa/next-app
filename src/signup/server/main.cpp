/* This is an experiment where I play with gRPC for C++.
 *
 * This is an example of a possible server-implementation, using
 * the gRPC "Async" interface that is generated with the protobuf
 * utility.
 *
 * The protofile is the same that Google use in their general documentation
 * for gRPC.
 *
 *
 * This file is free and open source code, released under the
 * GNU GENERAL PUBLIC LICENSE version 3.
 *
 * Copyright 2023 by Jarle (jgaa) Aase. All rights reserved.
 */

#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "signup/config.h"
#include "nextapp/logging.h"
#include "signup/Server.h"

using namespace std;
using namespace nextapp;
using nextapp::logging::LogEvent;

namespace {

optional<logfault::LogLevel> toLogLevel(string_view name) {
    if (name.empty() || name == "off" || name == "false") {
        return {};
    }

    if (name == "debug") {
        return logfault::LogLevel::DEBUGGING;
    }

    if (name == "trace") {
        return logfault::LogLevel::TRACE;
    }

    return logfault::LogLevel::INFO;
}

template <typename T>
void handleSignals(auto& signals, bool& done, T& service) {
    signals.async_wait([&](const boost::system::error_code& ec, int signalNumber) {

        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "handleSignals: Handler aborted.";
                return;
            }
            LOG_WARN << "handleSignals - Received error: " << ec.message();
            return;
        }

        LOG_INFO << "handleSignals - Received signal #" << signalNumber;
        if (signalNumber == SIGHUP) {
            LOG_WARN << "handleSignals - Ignoring SIGHUP. Note - config is not re-loaded.";
        } else if (signalNumber == SIGQUIT || signalNumber == SIGINT) {
            if (!done) {
                LOG_INFO << "handleSignals - Stopping the service.";
                service.stop();
                done = true;
            }
            return;
        } else {
            LOG_WARN << "handleSignals - Ignoring signal #" << signalNumber;
        }

        handleSignals(signals, done, service);
    });
}

} // anon ns

int main(int argc, char* argv[]) {
    try {
        locale loc("");
    } catch (const exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    Config config;
    Server::BootstrapOptions bootstrap_opts;

    const auto appname = filesystem::path(argv[0]).stem().string();

    {
        namespace po = boost::program_options;
        po::options_description general("Options");
        const string default_config_file = "/etc/nextapp/signupd.conf";
        string config_file;
        string log_level_console = "info";
        string log_level = "info";
        string log_file;
        bool trunc_log = false;
        bool json_logging_to_console = false;
        bool json_logging_to_file = false;

        auto init_logging_and_config_file = [&](auto& cmdline_options, auto& vm) {
            if (!config_file.empty()) {
                if (filesystem::exists(config_file)) {
                    try {
                        ifstream config_file_stream(config_file);
                        po::store(po::parse_config_file(config_file_stream, cmdline_options), vm);
                        po::notify(vm);
                    } catch (const exception& ex) {
                        cerr << appname << " Failed to load configuration file '" << config_file << "': " << ex.what() << endl;
                        return -4;
                    }
                } else {
                    if (config_file == default_config_file) {
                        cerr << appname << " Default configuration file '" << config_file << "' does not exist." << endl;
                    } else {
                        cerr << appname << " Configuration file '" << config_file << "' does not exist." << endl;
                        return -4;
                    }
                }
            }

            if (auto level = toLogLevel(log_level_console)) {
                if (json_logging_to_console) {
                    logfault::LogManager::Instance().AddHandler(
                        make_unique<logfault::JsonHandler>(clog, *level, 0xffff));
                } else {
                    logfault::LogManager::Instance().AddHandler(
                        make_unique<logfault::StreamHandler>(clog, *level));
                }
            }

            if (!log_file.empty()) {
                if (auto level = toLogLevel(log_level)) {
                    if (json_logging_to_file) {
                        logfault::LogManager::Instance().AddHandler(
                            make_unique<logfault::JsonHandler>(log_file, *level, trunc_log, 0xffff));
                    } else {
                        logfault::LogManager::Instance().AddHandler(
                            make_unique<logfault::StreamHandler>(log_file, *level, trunc_log));
                    }
                }
            }

            return 0;
        };

        general.add_options()
            ("help,h", "Print help and exit")
            ("version,v", "Print version and exit")
            ("config,c",
             po::value(&config_file)->default_value(default_config_file),
             "Configuration file to use")
            ("log-to-console,C",
             po::value(&log_level_console)->default_value(log_level_console),
             "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
            ("log-as-json-to-console", po::bool_switch(&json_logging_to_console),
             "Logs to the console using json format.")
            ("log-level,l",
             po::value<string>(&log_level)->default_value(log_level),
             "Log-level; one of 'info', 'debug', 'trace'.")
            ("log-file,L",
             po::value<string>(&log_file),
             "Log-file to write a log to. Default is to use only the console.")
            ("truncate-log-file,T",
              po::bool_switch(&trunc_log),
             "Truncate the log-file if it already exists.")
            ("log-as-json-to-file", po::bool_switch(&json_logging_to_file),
             "Logs to the file using json format.")
            ("log-messages",
             po::value(&config.options.log_protobuf_messages)->default_value(config.options.log_protobuf_messages),
             "Log data-messages to the log in Json format.\n0=disable, 1=enable, 2=enable and format in readable form.\n"
             "This mostly applies for debug and trace level log-messages.")
            ;

        po::options_description bs("Bootstrap");
        bs.add_options()
            ("drop-database", po::bool_switch(&bootstrap_opts.drop_old_db),
             "Delete the existing database if it exists.")
            ("root-db-user",
             po::value(&bootstrap_opts.db_root_user)->default_value(bootstrap_opts.db_root_user),
             "Mysql user to use when logging into the mysql server")
            ("root-db-passwd",
             po::value(&bootstrap_opts.db_root_passwd),
             "Mysql password to use when logging into the mysql server. If unset, the password is read from the environment variable SIGNUP_ROOT_DBPASSWD.")
            ;

        po::options_description cluster("Cluster");
        cluster.add_options()
            ("nextapp-address,n", po::value(&config.grpc_nextapp.address)->default_value(config.grpc_nextapp.address),
             "Protocol, address and port to use for gRPC client-connectiopn to NextApp")
            ("nextapp-public-url", po::value(&config.cluster.nextapp_public_url),
             "Public URL for nextappd. If unset, uses the value specified with `--nextapp-address`")
            ("grpc-client-ca-cert", po::value(&config.grpc_nextapp.ca_cert),
             "Path to the CA certificate")
            ("grpc-client-cert", po::value(&config.grpc_nextapp.server_cert),
             "Path to the server certificate")
            ("grpc-client-key", po::value(&config.grpc_nextapp.server_key),
             "Path to the server key")
            ;

        po::options_description svr("Server");
        svr.add_options()
            ("io-threads", po::value(&config.svr.io_threads)->default_value(config.svr.io_threads),
             "Number of worker-threads to start for IO")
            ("grpc-address,g", po::value(&config.grpc_signup.address)->default_value(config.grpc_signup.address),
             "Address and port to use for gRPC")
            ("grpc-tls-mode", po::value(&config.grpc_signup.tls_mode)->default_value(config.grpc_signup.tls_mode),
             "TLS mode; one of 'ca' or 'none'. Ca will use a self-signed server cert.")
            ("grpc-server-ca-cert", po::value(&config.grpc_signup.ca_cert),
             "Path to the CA certificate")
            ("grpc-server-cert", po::value(&config.grpc_signup.server_cert),
             "Path to the server certificate")
            ("grpc-server-key", po::value(&config.grpc_signup.server_key),
             "Path to the server key")
            ("timer-interval", po::value(&config.options.timer_interval_sec)->default_value(config.options.timer_interval_sec),
             "Number seconds between the internal timer wakes up.")
            ("retry-connect-delay", po::value(&config.options.retry_connect_to_nextappd_secs)->
                default_value(config.options.retry_connect_to_nextappd_secs),
             "Number seconds to wait before re-trying the initial connect to nextappd. 0 to disale retrying.")
            ;

        po::options_description info("Info");
        info.add_options()
            ("welcome", po::value(&config.cluster.welcome_path),
             "Path to the welcome page")
            ("eula", po::value(&config.cluster.eula_path),
             "Path to the EULA page")
            ;

        po::options_description metrics("Metrics");
        metrics.add_options()
            ("enable-metrics", po::bool_switch(&config.options.enable_http),
             "Enable /metrics HTTP endpoint for monitoring")
            ("disable-metrics-password", po::bool_switch(&config.options.no_metrics_password),
             "Disable password for the /metrics endpoint.")
            ("metrics-endpoint", po::value(&config.http.metrics_target)->default_value(config.http.metrics_target),
             "Scrape endpoint for metrics.")
            ("metrics-port", po::value(&config.http.http_port)->default_value(config.http.http_port),
             "Port to listen on for metrics.")
            ("metrics-host", po::value(&config.http.http_endpoint)->default_value(config.http.http_endpoint),
             "Host to listen on for metrics.")
            ("metrics-tls-cert", po::value(&config.http.http_tls_cert)->default_value(config.http.http_tls_cert),
             "TLS (PAM) cert to use (enables HTTPS).")
            ("metrics-tls-key", po::value(&config.http.http_tls_key)->default_value(config.http.http_tls_key),
             "TLS key to use (enables HTTPS).")
            ;


        po::options_description db("Database");
        db.add_options()
            ("db-user",
             po::value(&config.db.username),
             "Mysql user to use when logging into the mysql server")
            ("db-passwd",
             po::value(&config.db.password),
             "Mysql password to use when logging into the mysql server. If unset, the password is read from the environment variable SIGNUP_DB_PASSWD.")
            ("db-host",
             po::value(&config.db.host)->default_value(config.db.host),
             "Hostname or IP address for the database server")
            ("db-port",
             po::value(&config.db.port)->default_value(config.db.port),
             "Port number for the database server")
            ("db-min-connections",
             po::value(&config.db.min_connections)->default_value(config.db.min_connections),
             "Max concurrent connections to the database server")
            ("db-max-connections",
             po::value(&config.db.max_connections)->default_value(config.db.max_connections),
             "Max concurrent connections to the database server")
            ("db-retry-connect",
             po::value(&config.db.retry_connect)->default_value(config.db.retry_connect),
             "Retry connect to the database-server # times on startup. Useful when using containers, where nextappd may be running before the database is ready.")
            ("db-retry-delay",
             po::value(&config.db.retry_connect_delay_ms)->default_value(config.db.retry_connect_delay_ms),
             "Milliseconds to wait between connection retries")
            ;

        po::options_description cmdline_options;

        // Handle `bootstrap` mode
        if (argc > 1) {
            if (argv[1] == "bootstrap"s) {
                cmdline_options.add(general).add(bs).add(db).add(cluster);
                po::variables_map vm;
                try {
                    // Shift arguments left to remove "bootstrap"
                    vector<char*> new_argv(argv, argv + argc); // Copy argv
                    new_argv.erase(new_argv.begin() + 1); // Remove second element (index 1)

                    po::store(po::command_line_parser(new_argv.size(), new_argv.data()).options(cmdline_options).run(), vm);
                    po::notify(vm);
                } catch (const exception& ex) {
                    cerr << appname
                         << " Failed to parse command-line arguments: " << ex.what() << endl;
                    return -1;
                }

                if (vm.count("help")) {
                    cout <<appname << " [options]"
                         << cmdline_options << endl << endl
                         << "If Cluster options are provided, the Nextappd instance "
                         << "is added to the runtime-configuration." << endl
                         << "If the evvironment variable SIGNUP_ADMIN_PASSWORD is unset, "
                         << "a random password will be generated for the admin user." << endl;
                    return -2;
                }

                if (auto err = init_logging_and_config_file(cmdline_options, vm)) {
                    return err;
                }

                if (config.db.password.empty()) {
                    LOG_ERROR << "Database password is not set. Please set the SIGNUP_DB_PASSWD environment variable or use --db-passwd option.";
                    return -4;
                }

                try {
                    Server server{config};
                    server.bootstrap(bootstrap_opts);
                    return 0; // Done
                } catch (const exception& ex) {
                    LOG_ERROR << "Caught exception during bootstrap: " << ex.what();
                    return -5;
                }
            }
        }

        cmdline_options.add(general).add(svr).add(info).add(metrics).add(db);
        po::variables_map vm;
        try {
            po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
            po::notify(vm);
        } catch (const exception& ex) {
            cerr << appname
                 << " Failed to parse command-line arguments: " << ex.what() << endl;
            return -1;
        }

        if (vm.count("help")) {
            cout <<appname << " [options]";
            cout << cmdline_options << endl;
            cout << "To bootstrap the instance:" << endl;
            cout << "  " << appname << " bootstrap [bootstrap-options] [--help]" << endl;
            return -2;
        }

        if (vm.count("version")) {
            cout << appname << ' ' << APP_VERSION << endl
                      << "Using C++ standard " << __cplusplus << endl
                      << "Boost " << BOOST_LIB_VERSION << endl
                      << "Platform " << BOOST_PLATFORM << endl
                      << "Compiler " << BOOST_COMPILER << endl
                      << "Build date " << __DATE__ << endl;
            return -3;
        }

        if (auto err = init_logging_and_config_file(cmdline_options, vm)) {
            return err;
        }

        LOG_TRACE_N << "Getting ready...";
    }

    LOG_INFO << appname << ' ' << APP_VERSION << " starting up.";

    try {
        Server server{config};
        server.init();
        server.run();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception: " << ex.what() << endl;
        return -101;
    }

    LOG_INFO << appname << " done! ";
} // main
