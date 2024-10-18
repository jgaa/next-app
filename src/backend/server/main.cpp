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

#include "nextapp/config.h"
#include "nextapp/logging.h"
#include "nextapp/Server.h"
#include "nextapp/errors.h"
#include "nextapp/GrpcServer.h"

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
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    Config config;
    Server::BootstrapOptions bootstrap_opts;

    const auto appname = filesystem::path(argv[0]).stem().string();

    {
        namespace po = boost::program_options;
        po::options_description general("Options");
        const string default_config_file = "/etc/nextapp/nextappd.conf";
        string config_file;
        string log_level_console = "info";
        string log_level = "info";
        string log_file;
        string client_certs;
        string client_cert_uuid;
        bool trunc_log = false;
        bool bootstrap = false;
        bool admin_cert = false;
        bool create_server_cert = false;

        general.add_options()
            ("help,h", "Print help and exit")
            ("version,v", "Print version and exit")
            ("config,c",
             po::value(&config_file)->default_value(default_config_file),
             "Configuration file to use")
            ("log-to-console,C",
             po::value(&log_level_console)->default_value(log_level_console),
             "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
            ("log-level,l",
             po::value<string>(&log_level)->default_value(log_level),
             "Log-level; one of 'info', 'debug', 'trace'.")
            ("log-file,L",
             po::value<string>(&log_file),
             "Log-file to write a log to. Default is to use only the console.")
            ("truncate-log-file,T",
              po::bool_switch(&trunc_log),
             "Truncate the log-file if it already exists.")
            ("log-messages",
             po::value(&config.options.log_protobuf_messages)->default_value(config.options.log_protobuf_messages),
             "Log data-messages to the log in Json format.\n0=disable, 1=enable, 2=enable and format in readable form.\n"
             "This mostly applies for debug and trace level log-messages.")
            ;

        po::options_description bs("Bootstrap");
        bs.add_options()
            ("bootstrap", po::bool_switch(&bootstrap),
             "Bootstrap the system. Creates the database and system tenant. Exits when done. "
             "The databse credentials must be for the system (root) user of the database.")
            ("drop-database", po::bool_switch(&bootstrap_opts.drop_old_db),
             "Tells the server to delete the existing database.")
            ("root-db-user",
              po::value(&bootstrap_opts.db_root_user)->default_value(bootstrap_opts.db_root_user),
             "Mysql user to use when logging into the mysql server")
            ("root-db-passwd",
             po::value(&bootstrap_opts.db_root_passwd),
             "Mysql password to use when logging into the mysql server")
            ;

        po::options_description svr("Server");
        svr.add_options()
            ("io-threads", po::value(&config.svr.io_threads)->default_value(config.svr.io_threads),
             "Number of worker-threads to start for IO")
            ("grpc-address,g", po::value(&config.grpc.address)->default_value(config.grpc.address),
             "Address and port to use for gRPC")
            ("grpc-tls-mode", po::value(&config.grpc.tls_mode)->default_value(config.grpc.tls_mode),
             "TLS mode; one of 'ca' or 'none'. Ca will use a self-signed server cert.")
            ("session-timeout", po::value(&config.svr.session_timeout_sec)->default_value(config.svr.session_timeout_sec),
             "Client-session timeout in seconds. Client sessions are removed after this period." )
            ;

        po::options_description metrics("Metrics");
        metrics.add_options()
            ("enable-metrics", po::bool_switch(&config.enable_http))
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

        po::options_description ca("Certs");
        ca.add_options()
            ("cert-lifetime", po::value(&config.ca.lifetime_days_certs)->default_value(config.ca.lifetime_days_certs),
             "Life-time in days for self signed TLS certificates")
            ("cert-key-size", po::value(&config.ca.key_bytes)->default_value(config.ca.key_bytes),
             "Size of keys in self-signed carts (in bytes)")
            ("server-fqdn", po::value(&config.options.server_cert_dns_names),
             "The fqdn to use in the server-certificate. Can be repeated to allow multiple aliases. "
             "The server cert is created if it is missing when the server starts, for example during initial bootstrap. "
             "It is also created when the --create-server-cert option is used.")
            ("create-server-cert", po::bool_switch(&create_server_cert),
             "Re-creates the serverts TLS cert and key. Exits when done.")
            ("create-client-cert", po::value(&client_certs),
             "Create a client-certificate and key and store it files beginning with [name]. "
             "The files will be overwritten if they exists. "
             "Exits when done.")
            ("with-user-uuid", po::value(&client_cert_uuid),
             "Used with --create-client-cert to create a client-certificate for a specific user.")
            ("admin-cert", po::bool_switch(&admin_cert),
             "Used with --create-client-cert to create a client-certificate for the default admin user. For example for signupd.")
            ;

        po::options_description db("Database");
        db.add_options()
            ("db-user",
              po::value(&config.db.username),
              "Mysql user to use when logging into the mysql server")
            ("db-passwd",
             po::value(&config.db.password),
             "Mysql password to use when logging into the mysql server")
            ("db-name",
              po::value(&config.db.database)->default_value(config.db.database),
             "Database to use")
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
        cmdline_options.add(general).add(bs).add(svr).add(metrics).add(ca).add(db);
        po::variables_map vm;
        try {
            po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
            po::notify(vm);
        } catch (const std::exception& ex) {
            cerr << appname
                 << " Failed to parse command-line arguments: " << ex.what() << endl;
            return -1;
        }

        if (vm.count("help")) {
            std::cout <<appname << " [options]";
            std::cout << cmdline_options << std::endl;
            return -2;
        }

        if (vm.count("version")) {
            std::cout << appname << ' ' << APP_VERSION << endl
                      << "Using C++ standard " << __cplusplus << endl
                      << "Boost " << BOOST_LIB_VERSION << endl
                      << "Platform " << BOOST_PLATFORM << endl
                      << "Compiler " << BOOST_COMPILER << endl
                      << "Build date " << __DATE__ << endl;
            return -3;
        }

        if (!config_file.empty()) {
            if (filesystem::exists(config_file)) {
                try {
                    std::ifstream config_file_stream(config_file);
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
            logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, *level));
        }

        if (!log_file.empty()) {
            if (auto level = toLogLevel(log_level)) {
                logfault::LogManager::Instance().AddHandler(
                    make_unique<logfault::StreamHandler>(log_file, *level, trunc_log));
            }
        }

        LOG_TRACE_N << "Getting ready...";

        if (bootstrap) {
            LOG_INFO << appname << ' ' << APP_VERSION << ".";
            try {
                Server server{config};
                server.bootstrap(bootstrap_opts);
                return 0; // Done
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception during bootstrap: " << ex.what();
                return -4;
            }
        }

        if (!client_certs.empty()) {
            LOG_INFO << appname << ' ' << APP_VERSION << ".";
            try {
                Server server{config};
                boost::uuids::uuid userUuid;
                if (admin_cert) {
                    userUuid = toUuid(nextapp::Server::system_user);
                } else if (!client_cert_uuid.empty()){
                    userUuid = toUuid(client_cert_uuid);
                } else {
                    LOG_ERROR << "You must specify --with-user-uuid=uuid or --admin-cert to create a client-certificate.";
                    return -7;
                }
                server.createClientCert(client_certs, userUuid);
                return 0; // Done
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception during client-certificate creation: " << ex.what();
                return -6;
            }
        }

        if (create_server_cert) {
            LOG_INFO << appname << ' ' << APP_VERSION << ".";
            try {
                Server server{config};
                server.recreateServerCert();
                return 0; // Done
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception during server-certificate creation: " << ex.what();
                return -8;
            }
        }
    }

    LOG_INFO << appname << ' ' << APP_VERSION << " starting up.";

    try {
        Server server{config};
        server.init();
        server.run();
    } catch (const nextapp::aborted& ex) {
        LOG_INFO << "Server was aborted: " << ex.what();
        return -100;
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception: " << ex.what() << endl;
        return -101;
    }

    LOG_INFO << appname << " done! ";
} // main
