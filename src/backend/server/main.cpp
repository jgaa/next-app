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
        std::string log_level_console = "info";
        bool bootstrap = false;

        general.add_options()
            ("help,h", "Print help and exit")
            ("version,v", "Print version and exit")
            ("log-to-console,C",
             po::value(&log_level_console)->default_value(log_level_console),
             "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
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
            ("db-max-connections",
             po::value(&config.db.max_connections)->default_value(config.db.max_connections),
             "Max concurrent connections to the database server")
            ;

        po::options_description cmdline_options;
        cmdline_options.add(general).add(bs).add(svr).add(db);
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
                      << "Build date " <<__DATE__ << endl;
            return -3;
        }

        if (auto level = toLogLevel(log_level_console)) {
            logfault::LogManager::Instance().AddHandler(
                make_unique<logfault::StreamHandler>(clog, *level));
        }

        LOG_TRACE_N << LogEvent::LE_TEST << "Getting ready...";

        if (bootstrap) {
            try {
                Server server{config};
                server.bootstrap(bootstrap_opts);
                return 0; // Done
            } catch (const exception& ex) {
                LOG_ERROR << "Caught exception during bootstrap: " << ex.what();
                return -4;
            }
        }

    }

    LOG_INFO << appname << ' ' << APP_VERSION << " starting up.";

    try {
        Server server{config};
        server.init();
        server.run();
    } catch (const exception& ex) {
        LOG_ERROR << "Caught exception: " << ex.what() << endl;
        return -5;
    }

    LOG_INFO << appname << " done! ";
} // main
