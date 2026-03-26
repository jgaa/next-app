#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>

class QProcessEnvironment;

struct AcceptancePaths final {
    QString run_root;
    QString backend_root;
    QString certs_root;
    QString logs_root;
    QString artifacts_root;
    QString devices_root;

    static AcceptancePaths create();
    QString ensureDeviceRoot(const QString& tenant_name, const QString& device_name) const;
};

struct CommandResult final {
    int exit_code{-1};
    QProcess::ExitStatus exit_status{QProcess::NormalExit};
    bool timed_out{false};
    QByteArray stdout_text;
    QByteArray stderr_text;

    [[nodiscard]] bool ok() const noexcept {
        return !timed_out
            && exit_status == QProcess::NormalExit
            && exit_code == 0;
    }
};

class ProcessRunner final {
public:
    static CommandResult run(const QString& program,
                             const QStringList& arguments,
                             const QProcessEnvironment& environment,
                             const QString& working_directory,
                             int timeout_ms);
};

struct AcceptanceDevicePaths final {
    QString root;
    QString config_root;
    QString data_root;
    QString home_root;

    static AcceptanceDevicePaths create(const AcceptancePaths& paths, const QString& tenant_name, const QString& device_name);
};

class AcceptanceDevice final {
public:
    AcceptanceDevice(AcceptancePaths paths, QString tenant_name, QString device_name);

    [[nodiscard]] QString name() const noexcept;
    [[nodiscard]] QString helperExecutablePath() const;
    [[nodiscard]] const AcceptanceDevicePaths& devicePaths() const noexcept;
    [[nodiscard]] CommandResult runHelper(const QStringList& arguments, int timeout_ms = 120000) const;

private:
    QProcessEnvironment environment() const;

    AcceptancePaths paths_;
    QString tenant_name_;
    QString device_name_;
    AcceptanceDevicePaths device_paths_;
};

class BackendFixture final {
public:
    struct Options final {
        QString repository{QStringLiteral("ghcr.io/jgaa")};
        QString nextapp_tag{QStringLiteral("latest")};
        QString signup_tag{QStringLiteral("latest")};
        QString mariadb_image{QStringLiteral("mariadb:latest")};
        QString host{QStringLiteral("127.0.0.1")};
        bool pull_images{false};
        int startup_timeout_ms{120000};
    };

    explicit BackendFixture(AcceptancePaths paths);
    BackendFixture(AcceptancePaths paths, Options options);
    ~BackendFixture();

    [[nodiscard]] bool dockerAvailable() const;
    void start();
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] QString runId() const noexcept;
    [[nodiscard]] QString signupPublicUrl() const;
    [[nodiscard]] QString nextappPublicUrl() const;

private:
    void allocatePorts();
    void ensureStopped() const;
    void pullImagesIfNeeded();
    void bootstrapNextappd();
    void createSignupClientCert();
    void startMariaDb();
    void startNextappd();
    void bootstrapSignupd();
    void startSignupd();
    void waitForMariaDbReady();
    void waitForTcpPort(int port, const char *service_name) const;
    CommandResult runDocker(const QStringList& arguments,
                            const QString& step_name,
                            int timeout_ms,
                            bool allow_failure = false) const;
    void collectLogs() const;
    void writeCommandArtifacts(const QString& step_name, const CommandResult& result) const;
    [[nodiscard]] QString nextappImage() const;
    [[nodiscard]] QString signupImage() const;
    [[nodiscard]] QString randomSecret() const;
    [[nodiscard]] QString sanitizeStepName(const QString& step_name) const;

    AcceptancePaths paths_;
    Options options_;
    QString run_id_;
    QString mariadb_container_;
    QString nextappd_container_;
    QString signupd_container_;
    QString root_db_password_;
    QString nextapp_db_password_;
    int mariadb_port_{0};
    int nextapp_port_{0};
    int signup_port_{0};
    bool running_{false};
};
