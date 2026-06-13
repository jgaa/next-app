#include "AcceptanceHarness.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUuid>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

#include "logging.h"

namespace {

constexpr QFileDevice::Permissions kOpenDirectoryPermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
    QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

QString nextRunId()
{
    return QStringLiteral("run-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss")))
        .arg(QCoreApplication::applicationPid());
}

int reserveEphemeralPort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }

    return server.serverPort();
}

QProcessEnvironment mergedEnvironment(const QProcessEnvironment& environment)
{
    auto merged = QProcessEnvironment::systemEnvironment();
    for (const auto& key : environment.keys()) {
        merged.insert(key, environment.value(key));
    }
    return merged;
}

QString artifactPath(const AcceptancePaths& paths, const QString& name, const QString& suffix)
{
    return QStringLiteral("%1/%2.%3")
        .arg(paths.artifacts_root, name, suffix);
}

QString acceptanceHelperExecutablePath()
{
    return QFileInfo{QCoreApplication::applicationFilePath()}
        .dir()
        .filePath(QStringLiteral("nextappui_acceptance_device"));
}

QString summarizeOutput(const QByteArray& text, int max_chars = 400)
{
    auto summary = QString::fromUtf8(text);
    summary.replace(QChar{'\n'}, QStringLiteral("\\n"));
    summary.replace(QChar{'\r'}, QStringLiteral("\\r"));
    if (summary.size() > max_chars) {
        summary = summary.left(max_chars) + QStringLiteral("...");
    }
    return summary;
}

QString formatArguments(const QStringList& arguments)
{
    return arguments.join(QChar{' '});
}

} // namespace

AcceptancePaths AcceptancePaths::create()
{
    const auto temp_root = QDir::tempPath();
    const auto run_id = nextRunId();
    const auto run_root = QStringLiteral("%1/nextapp-acceptance/%2").arg(temp_root, run_id);

    AcceptancePaths paths{
        .run_root = run_root,
        .backend_root = run_root + QStringLiteral("/backend"),
        .certs_root = run_root + QStringLiteral("/backend/certs"),
        .logs_root = run_root + QStringLiteral("/backend/logs"),
        .artifacts_root = run_root + QStringLiteral("/artifacts"),
        .devices_root = run_root + QStringLiteral("/devices"),
    };

    QDir{}.mkpath(paths.certs_root);
    QDir{}.mkpath(paths.logs_root);
    QDir{}.mkpath(paths.artifacts_root);
    QDir{}.mkpath(paths.devices_root);
    QFile::setPermissions(paths.backend_root, kOpenDirectoryPermissions);
    QFile::setPermissions(paths.certs_root, kOpenDirectoryPermissions);
    QFile::setPermissions(paths.logs_root, kOpenDirectoryPermissions);

    return paths;
}

QString AcceptancePaths::ensureDeviceRoot(const QString& tenant_name, const QString& device_name) const
{
    const auto path = QStringLiteral("%1/%2/%3").arg(devices_root, tenant_name, device_name);
    QDir{}.mkpath(path);
    return path;
}

AcceptanceDevicePaths AcceptanceDevicePaths::create(const AcceptancePaths& paths,
                                                    const QString& tenant_name,
                                                    const QString& device_name)
{
    const auto root = QStringLiteral("%1/%2/%3").arg(paths.devices_root, tenant_name, device_name);
    AcceptanceDevicePaths device_paths{
        .root = root,
        .config_root = root + QStringLiteral("/config"),
        .data_root = root + QStringLiteral("/data"),
        .home_root = root + QStringLiteral("/home"),
    };

    QDir{}.mkpath(device_paths.config_root);
    QDir{}.mkpath(device_paths.data_root);
    QDir{}.mkpath(device_paths.home_root);
    return device_paths;
}

CommandResult ProcessRunner::run(const QString& program,
                                 const QStringList& arguments,
                                 const QProcessEnvironment& environment,
                                 const QString& working_directory,
                                 int timeout_ms)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(mergedEnvironment(environment));
    if (!working_directory.isEmpty()) {
        process.setWorkingDirectory(working_directory);
    }

    process.start();
    if (!process.waitForStarted()) {
        return {
            .exit_code = -1,
            .exit_status = QProcess::CrashExit,
            .timed_out = false,
            .result_text = {},
            .stdout_text = {},
            .stderr_text = process.errorString().toUtf8(),
        };
    }

    const auto finished = process.waitForFinished(timeout_ms);
    if (!finished) {
        process.kill();
        process.waitForFinished();
    }

    return {
        .exit_code = finished ? process.exitCode() : -1,
        .exit_status = process.exitStatus(),
        .timed_out = !finished,
        .result_text = {},
        .stdout_text = process.readAllStandardOutput(),
        .stderr_text = process.readAllStandardError(),
    };
}

AcceptanceDevice::AcceptanceDevice(AcceptancePaths paths, QString tenant_name, QString device_name)
    : paths_{std::move(paths)}
    , tenant_name_{std::move(tenant_name)}
    , device_name_{std::move(device_name)}
    , device_paths_{AcceptanceDevicePaths::create(paths_, tenant_name_, device_name_)}
{
}

QString AcceptanceDevice::name() const noexcept
{
    return device_name_;
}

QString AcceptanceDevice::helperExecutablePath() const
{
    return QFileInfo{QCoreApplication::applicationFilePath()}
        .dir()
        .filePath(QStringLiteral("nextappui_acceptance_device"));
}

const AcceptanceDevicePaths& AcceptanceDevice::devicePaths() const noexcept
{
    return device_paths_;
}

CommandResult AcceptanceDevice::runHelper(const QStringList& arguments, int timeout_ms) const
{
    int result_fds[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, result_fds) != 0) {
        return {
            .exit_code = -1,
            .exit_status = QProcess::CrashExit,
            .timed_out = false,
            .result_text = {},
            .stdout_text = {},
            .stderr_text = QByteArrayLiteral("Failed to create result socketpair"),
        };
    }

    QStringList helper_args{
        QStringLiteral("--workspace-root"), device_paths_.root,
        QStringLiteral("--device-name"), device_name_,
        QStringLiteral("--result-fd"), QString::number(result_fds[1]),
    };
    helper_args.append(arguments);

    LOG_DEBUG_N << "Running acceptance helper for " << tenant_name_ << "/" << device_name_
                << " timeout_ms=" << timeout_ms
                << " program=" << helperExecutablePath()
                << " args=" << formatArguments(helper_args);

    QProcess process;
    process.setProgram(helperExecutablePath());
    process.setArguments(helper_args);
    process.setProcessEnvironment(mergedEnvironment(environment()));
    process.start();

    if (!process.waitForStarted()) {
        ::close(result_fds[0]);
        ::close(result_fds[1]);
        return {
            .exit_code = -1,
            .exit_status = QProcess::CrashExit,
            .timed_out = false,
            .result_text = {},
            .stdout_text = {},
            .stderr_text = process.errorString().toUtf8(),
        };
    }

    ::close(result_fds[1]);
    QElapsedTimer timer;
    timer.start();
    QByteArray stdout_text;
    QByteArray stderr_text;

    const auto flushChildOutput = [&]() {
        if (const auto out = process.readAllStandardOutput(); !out.isEmpty()) {
            stdout_text += out;
            std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
            std::fflush(stdout);
        }
        if (const auto err = process.readAllStandardError(); !err.isEmpty()) {
            stderr_text += err;
            std::fwrite(err.constData(), 1, static_cast<size_t>(err.size()), stderr);
            std::fflush(stderr);
        }
    };

    bool finished = false;
    while (!finished) {
        process.waitForReadyRead(50);
        flushChildOutput();
        finished = process.state() == QProcess::NotRunning;
        if (!finished && timer.elapsed() >= timeout_ms) {
            process.kill();
            process.waitForFinished();
            flushChildOutput();
            break;
        }
    }

    QByteArray result_payload;
    char buffer[4096];
    while (true) {
        const auto bytes_read = ::read(result_fds[0], buffer, sizeof(buffer));
        if (bytes_read > 0) {
            result_payload.append(buffer, bytes_read);
            continue;
        }
        if (bytes_read == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        break;
    }
    ::close(result_fds[0]);

    CommandResult result{
        .exit_code = finished ? process.exitCode() : -1,
        .exit_status = process.exitStatus(),
        .timed_out = !finished,
        .result_text = result_payload,
        .stdout_text = stdout_text,
        .stderr_text = stderr_text,
    };

    if (result.ok()) {
        LOG_DEBUG_N << "Acceptance helper completed for " << tenant_name_ << "/" << device_name_
                    << " exit_code=" << result.exit_code
                    << " result=" << summarizeOutput(result.result_text)
                    << " stdout=" << summarizeOutput(result.stdout_text);
    } else {
        LOG_WARN_N << "Acceptance helper failed for " << tenant_name_ << "/" << device_name_
                   << " exit_code=" << result.exit_code
                   << " exit_status=" << static_cast<int>(result.exit_status)
                   << " timed_out=" << result.timed_out
                   << " result=" << summarizeOutput(result.result_text)
                   << " stdout=" << summarizeOutput(result.stdout_text)
                   << " stderr=" << summarizeOutput(result.stderr_text);
    }

    return result;
}

QProcessEnvironment AcceptanceDevice::environment() const
{
    const auto app_name = QStringLiteral("nextapp-acceptance-%1-%2")
        .arg(device_name_)
        .arg(QString::number(qHash(device_paths_.root), 16));

    QProcessEnvironment env;
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("dummy"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), device_paths_.config_root);
    env.insert(QStringLiteral("XDG_DATA_HOME"), device_paths_.data_root);
    env.insert(QStringLiteral("HOME"), device_paths_.home_root);
    env.insert(QStringLiteral("NEXTAPP_ORG"), QStringLiteral("NextAppAcceptance"));
    env.insert(QStringLiteral("NEXTAPP_NAME"), app_name);
    return env;
}

BackendFixture::BackendFixture(AcceptancePaths paths)
    : BackendFixture(std::move(paths), Options{})
{
}

BackendFixture::BackendFixture(AcceptancePaths paths, Options options)
    : paths_{std::move(paths)}
    , options_{std::move(options)}
    , run_id_{QFileInfo(paths_.run_root).fileName()}
    , mariadb_container_{QStringLiteral("na-acceptance-mariadb-%1").arg(QCoreApplication::applicationPid())}
    , nextappd_container_{QStringLiteral("na-acceptance-nextappd-%1").arg(QCoreApplication::applicationPid())}
    , signupd_container_{QStringLiteral("na-acceptance-signupd-%1").arg(QCoreApplication::applicationPid())}
    , root_db_password_{randomSecret()}
    , nextapp_db_password_{randomSecret()}
{
}

BackendFixture::~BackendFixture()
{
    stop();
}

bool BackendFixture::dockerAvailable() const
{
    const auto result = ProcessRunner::run(
        QStringLiteral("docker"),
        {QStringLiteral("version"), QStringLiteral("--format"), QStringLiteral("{{.Server.Version}}")},
        {},
        {},
        10000);
    return result.ok();
}

void BackendFixture::start()
{
    if (running_) {
        LOG_WARN_N << "Backend fixture is already running. Ignoring start() call.";
        return;
    }

    LOG_DEBUG_N << "Starting backend fixture with run ID: " << run_id_;
    LOG_DEBUG_N << "calling allocatePorts()";
    allocatePorts();
    LOG_DEBUG_N << "calling ensureStopped()";
    ensureStopped();
    LOG_DEBUG_N << "calling pullImagesIfNeeded()";
    pullImagesIfNeeded();
    LOG_DEBUG_N << "calling inspectImages()";
    inspectImages();
    LOG_DEBUG_N << "calling startMariaDb()";
    startMariaDb();
    LOG_DEBUG_N << "calling waitForMariaDbReady()";
    waitForMariaDbReady();
    LOG_DEBUG_N << "calling bootstrapNextappd()";
    bootstrapNextappd();
    LOG_DEBUG_N << "calling createSignupClientCert()";
    createSignupClientCert();
    LOG_DEBUG_N << "calling startNextappd()";
    startNextappd();
    LOG_DEBUG_N << "calling waitForTcpPort(nextapp_port_, \"nextappd\")";
    waitForTcpPort(nextapp_port_, "nextappd");
    LOG_DEBUG_N << "calling bootstrapSignupd()";
    bootstrapSignupd();
    LOG_DEBUG_N << "calling  startSignupd()";
    startSignupd();
    LOG_DEBUG_N << "calling waitForTcpPort(signup_port_, \"signupd\")";
    waitForTcpPort(signup_port_, "signupd");
    LOG_DEBUG_N << "calling waitForSignupdReady()";
    waitForSignupdReady();
    LOG_INFO_N << "Backend fixture started successfully with run ID: " << run_id_;
    running_ = true;
}

void BackendFixture::stop()
{
    if (!running_) {
        ensureStopped();
        return;
    }

    collectLogs();
    ensureStopped();
    running_ = false;
}

void BackendFixture::restartNextappd()
{
    QVERIFY2(running_, "Cannot restart nextappd before the backend fixture is running");
    collectLogs();
    QVERIFY2(runDocker({QStringLiteral("rm"), QStringLiteral("-f"), nextappd_container_},
                       QStringLiteral("restart-rm-nextappd"),
                       options_.startup_timeout_ms).ok(),
             "Failed to stop nextappd container for restart");
    startNextappd();
    waitForTcpPort(nextapp_port_, "nextappd");
}

bool BackendFixture::isRunning() const noexcept
{
    return running_;
}

QString BackendFixture::runId() const noexcept
{
    return run_id_;
}

QString BackendFixture::signupPublicUrl() const
{
    return QStringLiteral("http://%1:%2").arg(options_.host).arg(signup_port_);
}

QString BackendFixture::nextappPublicUrl() const
{
    return QStringLiteral("https://%1:%2").arg(options_.host).arg(nextapp_port_);
}

void BackendFixture::allocatePorts()
{
    if (mariadb_port_ != 0) {
        return;
    }

    mariadb_port_ = reserveEphemeralPort();
    nextapp_port_ = reserveEphemeralPort();
    signup_port_ = reserveEphemeralPort();

    QVERIFY2(mariadb_port_ != 0, "Failed to reserve MariaDB port");
    QVERIFY2(nextapp_port_ != 0, "Failed to reserve nextappd port");
    QVERIFY2(signup_port_ != 0, "Failed to reserve signupd port");
}

void BackendFixture::ensureStopped() const
{
    runDocker({QStringLiteral("rm"), QStringLiteral("-f"), signupd_container_},
              QStringLiteral("rm-signupd"),
              15000,
              true);
    runDocker({QStringLiteral("rm"), QStringLiteral("-f"), nextappd_container_},
              QStringLiteral("rm-nextappd"),
              15000,
              true);
    runDocker({QStringLiteral("rm"), QStringLiteral("-f"), mariadb_container_},
              QStringLiteral("rm-mariadb"),
              15000,
              true);
}

void BackendFixture::pullImagesIfNeeded()
{
    if (!options_.pull_images) {
        return;
    }

    QVERIFY2(runDocker({QStringLiteral("pull"), options_.mariadb_image},
                       QStringLiteral("pull-mariadb"),
                       options_.startup_timeout_ms).ok(),
             "Failed to pull MariaDB image");
    QVERIFY2(runDocker({QStringLiteral("pull"), nextappImage()},
                       QStringLiteral("pull-nextappd"),
                       options_.startup_timeout_ms).ok(),
             "Failed to pull nextappd image");
    QVERIFY2(runDocker({QStringLiteral("pull"), signupImage()},
                       QStringLiteral("pull-signupd"),
                       options_.startup_timeout_ms).ok(),
             "Failed to pull signupd image");
}

void BackendFixture::inspectImages() const
{
    LOG_DEBUG_N << "Acceptance backend images:"
                << " nextappd=" << nextappImage()
                << " signupd=" << signupImage()
                << " mariadb=" << options_.mariadb_image;

    runDocker({QStringLiteral("image"), QStringLiteral("inspect"), nextappImage()},
              QStringLiteral("inspect-nextappd-image"),
              15000,
              true);
    runDocker({QStringLiteral("image"), QStringLiteral("inspect"), signupImage()},
              QStringLiteral("inspect-signupd-image"),
              15000,
              true);
    runDocker({QStringLiteral("image"), QStringLiteral("inspect"), options_.mariadb_image},
              QStringLiteral("inspect-mariadb-image"),
              15000,
              true);
}

void BackendFixture::bootstrapNextappd()
{
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--name"), nextappd_container_ + QStringLiteral("-bootstrap"),
        QStringLiteral("--link"), mariadb_container_,
        QStringLiteral("--env"), QStringLiteral("NEXTAPP_DBPASSW=%1").arg(nextapp_db_password_),
        QStringLiteral("--env"), QStringLiteral("NEXTAPP_ROOT_DBPASSW=%1").arg(root_db_password_),
        QStringLiteral("-v"), QStringLiteral("%1:/logs").arg(paths_.logs_root),
        nextappImage(),
        QStringLiteral("bootstrap"),
        QStringLiteral("-c"), QStringLiteral(""),
        QStringLiteral("-C"), QStringLiteral("trace"),
        QStringLiteral("-l"), QStringLiteral("trace"),
        QStringLiteral("-L"), QStringLiteral("/logs/nextappd-bootstrap.log"),
        QStringLiteral("-T"),
        QStringLiteral("--db-host"), mariadb_container_,
        QStringLiteral("--server-fqdn"), options_.host,
        QStringLiteral("--server-fqdn"), nextappd_container_,
    }, QStringLiteral("bootstrap-nextappd"), options_.startup_timeout_ms).ok(),
    "nextappd bootstrap failed");
}

void BackendFixture::createSignupClientCert()
{
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--name"), nextappd_container_ + QStringLiteral("-clicert"),
        QStringLiteral("--link"), mariadb_container_,
        QStringLiteral("--env"), QStringLiteral("NEXTAPP_DBPASSW=%1").arg(nextapp_db_password_),
        QStringLiteral("--env"), QStringLiteral("NEXTAPP_ROOT_DBPASSW=%1").arg(root_db_password_),
        QStringLiteral("-v"), QStringLiteral("%1:/certs").arg(paths_.certs_root),
        QStringLiteral("-v"), QStringLiteral("%1:/logs").arg(paths_.logs_root),
        nextappImage(),
        QStringLiteral("create-client-cert"),
        QStringLiteral("-c"), QStringLiteral(""),
        QStringLiteral("-C"), QStringLiteral("trace"),
        QStringLiteral("-l"), QStringLiteral("trace"),
        QStringLiteral("-L"), QStringLiteral("/logs/nextappd-create-client-cert.log"),
        QStringLiteral("-T"),
        QStringLiteral("--db-host"), mariadb_container_,
        QStringLiteral("--admin-cert"),
        QStringLiteral("--cert-name"), QStringLiteral("/certs/signup"),
    }, QStringLiteral("create-signupd-cert"), options_.startup_timeout_ms).ok(),
    "Failed to create signupd client certificate");
}

void BackendFixture::startMariaDb()
{
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--detach"),
        QStringLiteral("--name"), mariadb_container_,
        QStringLiteral("-p"), QStringLiteral("%1:%2:3306").arg(options_.host).arg(mariadb_port_),
        QStringLiteral("--env"), QStringLiteral("MARIADB_ROOT_PASSWORD=%1").arg(root_db_password_),
        options_.mariadb_image,
    }, QStringLiteral("start-mariadb"), options_.startup_timeout_ms).ok(),
    "Failed to start MariaDB container");
}

void BackendFixture::startNextappd()
{
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--detach"),
        QStringLiteral("--name"), nextappd_container_,
        QStringLiteral("-v"), QStringLiteral("%1:/certs").arg(paths_.certs_root),
        QStringLiteral("-v"), QStringLiteral("%1:/logs").arg(paths_.logs_root),
        QStringLiteral("-p"), QStringLiteral("%1:%2:10321").arg(options_.host).arg(nextapp_port_),
        QStringLiteral("--link"), mariadb_container_,
        QStringLiteral("--env"), QStringLiteral("NEXTAPP_DBPASSW=%1").arg(nextapp_db_password_),
        nextappImage(),
        QStringLiteral("-c"), QStringLiteral(""),
        QStringLiteral("-C"), QStringLiteral("trace"),
        QStringLiteral("-l"), QStringLiteral("trace"),
        QStringLiteral("-L"), QStringLiteral("/logs/nextappd.log"),
        QStringLiteral("-T"),
        QStringLiteral("--db-host"), mariadb_container_,
        QStringLiteral("-g"), QStringLiteral("0.0.0.0:10321"),
    }, QStringLiteral("start-nextappd"), options_.startup_timeout_ms).ok(),
    "Failed to start nextappd");
}

void BackendFixture::bootstrapSignupd()
{
    const auto doc_root = QStringLiteral("%1/doc").arg(QStringLiteral(NEXTAPP_TEST_SOURCE_DIR));
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--name"), signupd_container_ + QStringLiteral("-bootstrap"),
        QStringLiteral("-v"), QStringLiteral("%1:/certs").arg(paths_.certs_root),
        QStringLiteral("-v"), QStringLiteral("%1:/logs").arg(paths_.logs_root),
        QStringLiteral("-v"), QStringLiteral("%1:/doc:ro").arg(doc_root),
        QStringLiteral("--link"), nextappd_container_,
        QStringLiteral("--link"), mariadb_container_,
        signupImage(),
        QStringLiteral("bootstrap"),
        QStringLiteral("-C"), QStringLiteral("trace"),
        QStringLiteral("-l"), QStringLiteral("trace"),
        QStringLiteral("-L"), QStringLiteral("/logs/signupd-bootstrap.log"),
        QStringLiteral("-T"),
        QStringLiteral("--root-db-passwd"), root_db_password_,
        QStringLiteral("--db-passwd"), nextapp_db_password_,
        QStringLiteral("--db-host"), mariadb_container_,
        QStringLiteral("--nextapp-address"), QStringLiteral("https://%1:10321").arg(nextappd_container_),
        QStringLiteral("--nextapp-public-url"), nextappPublicUrl(),
        QStringLiteral("--grpc-client-ca-cert"), QStringLiteral("/certs/signup-ca.pem"),
        QStringLiteral("--grpc-client-cert"), QStringLiteral("/certs/signup-cert.pem"),
        QStringLiteral("--grpc-client-key"), QStringLiteral("/certs/signup-key.pem"),
    }, QStringLiteral("bootstrap-signupd"), options_.startup_timeout_ms).ok(),
    "signupd bootstrap failed");
}

void BackendFixture::startSignupd()
{
    const auto doc_root = QStringLiteral("%1/doc").arg(QStringLiteral(NEXTAPP_TEST_SOURCE_DIR));
    QVERIFY2(runDocker({
        QStringLiteral("run"),
        QStringLiteral("--rm"),
        QStringLiteral("--detach"),
        QStringLiteral("--name"), signupd_container_,
        QStringLiteral("-v"), QStringLiteral("%1:/doc:ro").arg(doc_root),
        QStringLiteral("-v"), QStringLiteral("%1:/logs").arg(paths_.logs_root),
        QStringLiteral("-p"), QStringLiteral("%1:%2:10322").arg(options_.host).arg(signup_port_),
        QStringLiteral("--link"), nextappd_container_,
        QStringLiteral("--link"), mariadb_container_,
        signupImage(),
        QStringLiteral("-C"), QStringLiteral("trace"),
        QStringLiteral("-l"), QStringLiteral("trace"),
        QStringLiteral("-L"), QStringLiteral("/logs/signupd.log"),
        QStringLiteral("-T"),
        QStringLiteral("--db-passwd"), nextapp_db_password_,
        QStringLiteral("--db-host"), mariadb_container_,
        QStringLiteral("--grpc-address"), QStringLiteral("0.0.0.0:10322"),
        QStringLiteral("--grpc-tls-mode"), QStringLiteral("none"),
        QStringLiteral("--welcome"), QStringLiteral("/doc/sample-welcome.html"),
        QStringLiteral("--eula"), QStringLiteral("/doc/sample-eula.html"),
        QStringLiteral("-c"), QStringLiteral(""),
    }, QStringLiteral("start-signupd"), options_.startup_timeout_ms).ok(),
    "Failed to start signupd");
}

void BackendFixture::waitForMariaDbReady()
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < options_.startup_timeout_ms) {
        const auto result = runDocker({
            QStringLiteral("exec"),
            mariadb_container_,
            QStringLiteral("mariadb-admin"),
            QStringLiteral("--host=127.0.0.1"),
            QStringLiteral("--port=3306"),
            QStringLiteral("--user=root"),
            QStringLiteral("--password=%1").arg(root_db_password_),
            QStringLiteral("ping"),
        }, QStringLiteral("wait-mariadb-ready"), 10000, true);

        if (result.ok()) {
            return;
        }

        QTest::qWait(1000);
    }

    QFAIL("MariaDB did not become ready in time");
}

void BackendFixture::waitForSignupdReady() const
{
    const auto probe_root = QStringLiteral("%1/_probe-signupd").arg(paths_.devices_root);
    QDir{}.mkpath(probe_root + QStringLiteral("/config"));
    QDir{}.mkpath(probe_root + QStringLiteral("/data"));
    QDir{}.mkpath(probe_root + QStringLiteral("/home"));

    QProcessEnvironment env;
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("dummy"));
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), probe_root + QStringLiteral("/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), probe_root + QStringLiteral("/data"));
    env.insert(QStringLiteral("HOME"), probe_root + QStringLiteral("/home"));
    env.insert(QStringLiteral("NEXTAPP_ORG"), QStringLiteral("NextAppAcceptance"));
    env.insert(QStringLiteral("NEXTAPP_NAME"), QStringLiteral("nextapp-acceptance-probe"));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < options_.startup_timeout_ms) {
        LOG_DEBUG_N << "Probing signupd readiness via acceptance helper.";
        const auto result = ProcessRunner::run(
            acceptanceHelperExecutablePath(),
            {
                QStringLiteral("--workspace-root"), probe_root,
                QStringLiteral("--device-name"), QStringLiteral("probe"),
                QStringLiteral("probe-signup-server"),
                QStringLiteral("--signup-url"), signupPublicUrl(),
            },
            env,
            {},
            30000);

        if (result.ok()) {
            LOG_DEBUG_N << "signupd readiness probe succeeded.";
            return;
        }

        LOG_DEBUG_N << "signupd readiness probe failed"
                    << " exit_code=" << result.exit_code
                    << " exit_status=" << static_cast<int>(result.exit_status)
                    << " timed_out=" << result.timed_out
                    << " stdout=" << summarizeOutput(result.stdout_text)
                    << " stderr=" << summarizeOutput(result.stderr_text);

        QTest::qWait(500);
    }

    QFAIL("signupd did not become ready in time");
}

void BackendFixture::waitForTcpPort(int port, const char *service_name) const
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < options_.startup_timeout_ms) {
        QTcpSocket socket;
        socket.connectToHost(options_.host, static_cast<quint16>(port));
        if (socket.waitForConnected(1000)) {
            socket.disconnectFromHost();
            return;
        }

        QTest::qWait(500);
    }

    QFAIL(qPrintable(QStringLiteral("%1 did not become reachable on port %2")
                         .arg(QString::fromLatin1(service_name))
                         .arg(port)));
}

CommandResult BackendFixture::runDocker(const QStringList& arguments,
                                        const QString& step_name,
                                        int timeout_ms,
                                        bool allow_failure) const
{
    const auto result = ProcessRunner::run(
        QStringLiteral("docker"),
        arguments,
        {},
        {},
        timeout_ms);
    writeCommandArtifacts(step_name, result);
    if (!allow_failure && !result.ok()) {
        const auto error = QStringLiteral("Docker step failed: %1\nstdout:\n%2\nstderr:\n%3")
                               .arg(step_name,
                                    QString::fromUtf8(result.stdout_text),
                                    QString::fromUtf8(result.stderr_text));
        QTest::qFail(qPrintable(error), __FILE__, __LINE__);
    }
    return result;
}

void BackendFixture::collectLogs() const
{
    runDocker({QStringLiteral("logs"), nextappd_container_},
              QStringLiteral("logs-nextappd"),
              15000,
              true);
    runDocker({QStringLiteral("logs"), signupd_container_},
              QStringLiteral("logs-signupd"),
              15000,
              true);
    runDocker({QStringLiteral("logs"), mariadb_container_},
              QStringLiteral("logs-mariadb"),
              15000,
              true);
}

void BackendFixture::writeCommandArtifacts(const QString& step_name, const CommandResult& result) const
{
    const auto safe_name = sanitizeStepName(step_name);

    QFile stdout_file{artifactPath(paths_, safe_name, QStringLiteral("stdout.log"))};
    if (stdout_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        stdout_file.write(result.stdout_text);
    }

    QFile stderr_file{artifactPath(paths_, safe_name, QStringLiteral("stderr.log"))};
    if (stderr_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        stderr_file.write(result.stderr_text);
    }
}

QString BackendFixture::nextappImage() const
{
    return QStringLiteral("%1/nextappd:%2").arg(options_.repository, options_.nextapp_tag);
}

QString BackendFixture::signupImage() const
{
    return QStringLiteral("%1/signupd:%2").arg(options_.repository, options_.signup_tag);
}

QString BackendFixture::randomSecret() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString BackendFixture::sanitizeStepName(const QString& step_name) const
{
    QString safe = step_name;
    for (auto& ch : safe) {
        if (!ch.isLetterOrNumber() && ch != QChar{'-'} && ch != QChar{'_'}) {
            ch = QChar{'_'};
        }
    }
    return safe;
}
