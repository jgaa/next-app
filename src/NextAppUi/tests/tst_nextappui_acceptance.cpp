
#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include "AcceptanceHarness.h"
#include "logging.h"

namespace {

BackendFixture::Options backendOptionsFromEnv()
{
    auto options = BackendFixture::Options{};
    options.pull_images = qEnvironmentVariableIntValue("NEXTAPP_ACCEPTANCE_PULL") > 0;
    if (qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_TAG")) {
        const auto tag = qEnvironmentVariable("NEXTAPP_ACCEPTANCE_TAG");
        options.nextapp_tag = tag;
        options.signup_tag = tag;
    }
    if (qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_REPOSITORY")) {
        options.repository = qEnvironmentVariable("NEXTAPP_ACCEPTANCE_REPOSITORY");
    }
    return options;
}

int matrixTenantCount()
{
    const auto requested = qEnvironmentVariableIntValue("NEXTAPP_ACCEPTANCE_TENANTS");
    return requested > 0 ? requested : 3;
}

int matrixDeviceCount()
{
    const auto requested = qEnvironmentVariableIntValue("NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT");
    return requested > 0 ? requested : 5;
}

QJsonObject requireJsonObject(const CommandResult& result)
{
    const auto failWithOutput = [&](const char *reason) -> QJsonObject {
        const auto message = QStringLiteral("%1\nexit_code: %2\nexit_status: %3\ntimed_out: %4\nstdout:\n%5\nstderr:\n%6")
                                 .arg(QString::fromUtf8(reason),
                                      QString::number(result.exit_code),
                                      QString::number(static_cast<int>(result.exit_status)),
                                      result.timed_out ? QStringLiteral("true") : QStringLiteral("false"),
                                      QString::fromUtf8(result.jsonText()),
                                      QString::fromUtf8(result.stderr_text));
        QTest::qFail(message.toUtf8().constData(), __FILE__, __LINE__);
        return {};
    };

    const auto json = QJsonDocument::fromJson(result.jsonText());
    if (!result.ok()) {
        return failWithOutput("Helper command failed");
    }
    if (!json.isObject()) {
        return failWithOutput("Helper output was not a JSON object");
    }
    return json.object();
}

QJsonObject runHelperJson(const AcceptanceDevice& device,
                          const QStringList& arguments,
                          int timeout_ms = 240000)
{
    return requireJsonObject(device.runHelper(arguments, timeout_ms));
}

QString jsonObjectCompact(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString describeNodeSnapshotDifference(const QJsonArray& expected_nodes,
                                       const QJsonArray& actual_nodes)
{
    const auto max_size = std::max(expected_nodes.size(), actual_nodes.size());
    for (qsizetype index = 0; index < max_size; ++index) {
        if (index >= expected_nodes.size()) {
            return QStringLiteral("extra actual node at index %1\nactual_node=%2")
                .arg(index)
                .arg(jsonObjectCompact(actual_nodes.at(index).toObject()));
        }
        if (index >= actual_nodes.size()) {
            return QStringLiteral("missing actual node at index %1\nexpected_node=%2")
                .arg(index)
                .arg(jsonObjectCompact(expected_nodes.at(index).toObject()));
        }

        const auto expected_node = expected_nodes.at(index).toObject();
        const auto actual_node = actual_nodes.at(index).toObject();
        if (expected_node == actual_node) {
            continue;
        }

        const auto expected_uuid = expected_node.value(QStringLiteral("uuid")).toString();
        const auto actual_uuid = actual_node.value(QStringLiteral("uuid")).toString();
        if (expected_uuid != actual_uuid) {
            return QStringLiteral("node uuid mismatch at index %1\nexpected_uuid=%2\nactual_uuid=%3\nexpected_node=%4\nactual_node=%5")
                .arg(index)
                .arg(expected_uuid, actual_uuid, jsonObjectCompact(expected_node), jsonObjectCompact(actual_node));
        }

        QStringList differing_fields;
        for (auto it = expected_node.begin(); it != expected_node.end(); ++it) {
            if (actual_node.value(it.key()) != it.value()) {
                differing_fields.push_back(it.key());
            }
        }
        for (auto it = actual_node.begin(); it != actual_node.end(); ++it) {
            if (!expected_node.contains(it.key())) {
                differing_fields.push_back(it.key());
            }
        }
        differing_fields.removeDuplicates();

        return QStringLiteral("node field mismatch at index %1 uuid=%2\ndiffering_fields=%3\nexpected_node=%4\nactual_node=%5")
            .arg(index)
            .arg(expected_uuid, differing_fields.join(QStringLiteral(",")),
                 jsonObjectCompact(expected_node), jsonObjectCompact(actual_node));
    }

    return QStringLiteral("node snapshots differ, but no differing row was identified");
}

void compareReplicaState(const QJsonObject& expected, const QJsonObject& actual)
{
    if (expected.isEmpty() || actual.isEmpty()) {
        return;
    }

    const auto expected_table_hashes = expected.value(QStringLiteral("tableHashes")).toObject();
    const auto actual_table_hashes = actual.value(QStringLiteral("tableHashes")).toObject();
    const QStringList shared_tables{
        QStringLiteral("Nodes"),
        QStringLiteral("Action Categories"),
        QStringLiteral("Actions"),
        QStringLiteral("Days"),
        QStringLiteral("Day Colors"),
        QStringLiteral("Work Sessions"),
        QStringLiteral("Time Blocks"),
    };

    if (expected_table_hashes.isEmpty() || actual_table_hashes.isEmpty()) {
        QTest::qFail("compareReplicaState missing tableHashes", __FILE__, __LINE__);
        return;
    }

    for (const auto& table_name : shared_tables) {
        const auto expected_hash = expected_table_hashes.value(table_name).toString();
        const auto actual_hash = actual_table_hashes.value(table_name).toString();
        if (expected_hash != actual_hash) {
            auto message = QStringLiteral("compareReplicaState table mismatch: %1\nexpected_hash=%2\nactual_hash=%3")
                               .arg(table_name, expected_hash, actual_hash);
            if (table_name == QStringLiteral("Nodes")) {
                const auto expected_nodes = expected.value(QStringLiteral("nodeSnapshot")).toArray();
                const auto actual_nodes = actual.value(QStringLiteral("nodeSnapshot")).toArray();
                if (!expected_nodes.isEmpty() && !actual_nodes.isEmpty()) {
                    message += QStringLiteral("\n%1").arg(
                        describeNodeSnapshotDifference(expected_nodes, actual_nodes));
                } else {
                    message += QStringLiteral("\nnodeSnapshot missing for Nodes comparison");
                }
            }
            QTest::qFail(message.toUtf8().constData(), __FILE__, __LINE__);
            return;
        }
    }

    QCOMPARE(expected.value(QStringLiteral("numNodes")).toInt(),
             actual.value(QStringLiteral("numNodes")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numActionCategories")).toInt(),
             actual.value(QStringLiteral("numActionCategories")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numActions")).toInt(),
             actual.value(QStringLiteral("numActions")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numDays")).toInt(),
             actual.value(QStringLiteral("numDays")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numDayColors")).toInt(),
             actual.value(QStringLiteral("numDayColors")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numWorkSessions")).toInt(),
             actual.value(QStringLiteral("numWorkSessions")).toInt());
    QCOMPARE(expected.value(QStringLiteral("numTimeBlocks")).toInt(),
             actual.value(QStringLiteral("numTimeBlocks")).toInt());
}

void requireReplayReconnect(const QJsonObject& reconnect_object)
{
    QVERIFY(reconnect_object.value(QStringLiteral("requestedLiveReplay")).toBool());
    QVERIFY(!reconnect_object.value(QStringLiteral("usedDurableSync")).toBool());
    QVERIFY(!reconnect_object.value(QStringLiteral("usedFullSync")).toBool());
    QVERIFY(!reconnect_object.value(QStringLiteral("serverInstanceChanged")).toBool());
}

void requireNormalSyncReconnect(const QJsonObject& reconnect_object)
{
    QVERIFY(!reconnect_object.value(QStringLiteral("usedFullSync")).toBool());
    QVERIFY(reconnect_object.value(QStringLiteral("usedDurableSync")).toBool());
}

void requireFullSyncReconnect(const QJsonObject& reconnect_object)
{
    QVERIFY(reconnect_object.value(QStringLiteral("usedFullSync")).toBool());
    QVERIFY(reconnect_object.value(QStringLiteral("usedDurableSync")).toBool());
}

QString tenantName(int tenant_index)
{
    return QStringLiteral("tenant-%1").arg(QChar(u'a' + tenant_index));
}

QString deviceName(int device_index)
{
    return QStringLiteral("device-%1").arg(device_index + 1);
}

QString tenantEmail(int tenant_index)
{
    return QStringLiteral("acceptance-user-%1@example.test").arg(tenant_index + 1);
}

QString tenantCompany(int tenant_index)
{
    return QStringLiteral("Acceptance Tenant %1").arg(tenant_index + 1);
}

class StdoutCapture final {
public:
    StdoutCapture()
    {
        if (::fflush(stdout) != 0) {
            return;
        }

        if (::pipe(pipe_fds_.data()) != 0) {
            pipe_fds_ = {-1, -1};
            return;
        }

        original_stdout_ = ::dup(STDOUT_FILENO);
        if (original_stdout_ < 0) {
            cleanupPipe();
            return;
        }

        if (::dup2(pipe_fds_[1], STDOUT_FILENO) < 0) {
            cleanupOriginalStdout();
            cleanupPipe();
            return;
        }

        active_ = true;
        reader_ = std::thread([this] { readLoop(); });
    }

    ~StdoutCapture()
    {
        stop();
    }

    void stop()
    {
        if (!active_) {
            return;
        }

        ::fflush(stdout);
        ::dup2(original_stdout_, STDOUT_FILENO);
        cleanupOriginalStdout();
        cleanupPipeWriteEnd();

        if (reader_.joinable()) {
            reader_.join();
        }

        cleanupPipeReadEnd();
        active_ = false;
    }

    [[nodiscard]] QString text() const
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        return QString::fromUtf8(captured_);
    }

private:
    void readLoop()
    {
        std::array<char, 4096> buffer{};
        for (;;) {
            const auto nread = ::read(pipe_fds_[0], buffer.data(), buffer.size());
            if (nread > 0) {
                {
                    const std::lock_guard<std::mutex> lock{mutex_};
                    captured_.append(buffer.data(), nread);
                }
                if (original_stdout_ >= 0) {
                    const auto ignored = ::write(original_stdout_, buffer.data(), static_cast<size_t>(nread));
                    (void)ignored;
                }
                continue;
            }

            if (nread == 0) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            break;
        }
    }

    void cleanupOriginalStdout()
    {
        if (original_stdout_ >= 0) {
            ::close(original_stdout_);
            original_stdout_ = -1;
        }
    }

    void cleanupPipeWriteEnd()
    {
        if (pipe_fds_[1] >= 0) {
            ::close(pipe_fds_[1]);
            pipe_fds_[1] = -1;
        }
    }

    void cleanupPipeReadEnd()
    {
        if (pipe_fds_[0] >= 0) {
            ::close(pipe_fds_[0]);
            pipe_fds_[0] = -1;
        }
    }

    void cleanupPipe()
    {
        cleanupPipeWriteEnd();
        cleanupPipeReadEnd();
    }

    std::array<int, 2> pipe_fds_{-1, -1};
    int original_stdout_{-1};
    bool active_{false};
    std::thread reader_;
    mutable std::mutex mutex_;
    QByteArray captured_;
};

struct FailureSummary final {
    QString header;
    QStringList details;
};

QVector<FailureSummary> summarizeFailures(const QString& output)
{
    const QRegularExpression outcome_re{
        QStringLiteral(R"(^(PASS|FAIL!|XFAIL|XPASS|SKIP|QFATAL)\s+:)")
    };

    QVector<FailureSummary> failures;
    FailureSummary current;
    bool collecting = false;

    const auto lines = output.split(u'\n');
    for (const auto& raw_line : lines) {
        const auto line = raw_line.trimmed();
        if (line.isEmpty()) {
            if (collecting && !current.details.isEmpty()) {
                collecting = false;
                failures.append(current);
                current = {};
            }
            continue;
        }

        const auto match = outcome_re.match(line);
        if (match.hasMatch()) {
            if (collecting) {
                failures.append(current);
                current = {};
                collecting = false;
            }

            if (match.captured(1) == QStringLiteral("FAIL!")) {
                collecting = true;
                current.header = line;
            }
            continue;
        }

        if (!collecting) {
            continue;
        }

        if (line.startsWith(QStringLiteral("Loc: ["))
            || line.startsWith(QStringLiteral("Totals:"))
            || line.startsWith(QStringLiteral("*********"))) {
            failures.append(current);
            current = {};
            collecting = false;
            continue;
        }

        current.details.append(line);
    }

    if (collecting) {
        failures.append(current);
    }

    return failures;
}

void printFailureSummary(const QVector<FailureSummary>& failures)
{
    if (failures.isEmpty()) {
        return;
    }

    std::cout << "\nFailure summary:\n";
    for (const auto& failure : failures) {
        std::cout << "  " << failure.header.toStdString() << "\n";
        const auto limit = std::min<qsizetype>(failure.details.size(), 6);
        for (qsizetype i = 0; i < limit; ++i) {
            std::cout << "    " << failure.details.at(i).toStdString() << "\n";
        }
        if (failure.details.size() > limit) {
            std::cout << "    ...\n";
        }
    }
    std::cout.flush();
}

void logTestStart(const char *test_name)
{
    LOG_DEBUG_N << "Starting acceptance test: " << test_name;
}

QVector<AcceptanceDevice> createDevices(const AcceptancePaths& paths,
                                        const QString& tenant_name,
                                        int device_count)
{
    QVector<AcceptanceDevice> devices;
    devices.reserve(device_count);
    for (int index = 0; index < device_count; ++index) {
        devices.emplaceBack(paths, tenant_name, deviceName(index));
    }
    return devices;
}

QJsonObject signUpFirstDevice(const AcceptanceDevice& device,
                              const BackendFixture& fixture,
                              int tenant_index,
                              const QString& template_name = {})
{
    LOG_DEBUG_N << "Signing up first device for tenant index " << tenant_index
                << (template_name.isEmpty()
                        ? QString{}
                        : QStringLiteral(" using template %1").arg(template_name));

    QStringList arguments{
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User %1").arg(tenant_index + 1),
        QStringLiteral("--user-email"), tenantEmail(tenant_index),
        QStringLiteral("--company"), tenantCompany(tenant_index),
    };
    if (!template_name.isEmpty()) {
        arguments.append(QStringLiteral("--template-name"));
        arguments.append(template_name);
    }

    const auto signup = runHelperJson(device, arguments);
    if (signup.isEmpty()) {
        return {};
    }
    if (signup.value(QStringLiteral("command")).toString() != QStringLiteral("signup-first-device")) {
        QTest::qFail("signup-first-device returned unexpected command", __FILE__, __LINE__);
        return {};
    }
    if (!signup.value(QStringLiteral("online")).toBool()) {
        QTest::qFail("signup-first-device did not reach online", __FILE__, __LINE__);
        return {};
    }
    if (!signup.value(QStringLiteral("synced")).toBool()) {
        QTest::qFail("signup-first-device did not finish sync", __FILE__, __LINE__);
        return {};
    }
    if (signup.value(QStringLiteral("numDayColors")).toInt() <= 0) {
        QTest::qFail("signup-first-device did not receive day_colors", __FILE__, __LINE__);
        return {};
    }
    if (!template_name.isEmpty()) {
        if (!signup.value(QStringLiteral("templateApplied")).toBool()) {
            QTest::qFail("signup-first-device did not apply requested template", __FILE__, __LINE__);
            return {};
        }
        if (signup.value(QStringLiteral("templateName")).toString() != template_name) {
            QTest::qFail("signup-first-device reported unexpected template name", __FILE__, __LINE__);
            return {};
        }
        if (signup.value(QStringLiteral("numNodes")).toInt() <= 0) {
            QTest::qFail("signup-first-device did not populate nodes from template", __FILE__, __LINE__);
            return {};
        }
    }
    return signup;
}

void addSecondaryDevice(const AcceptanceDevice& source_device,
                        const AcceptanceDevice& target_device,
                        const BackendFixture& fixture)
{
    LOG_DEBUG_N << "Adding secondary device via OTP for tenant "
                << source_device.name() << ": "
                << target_device.name();

    const auto otp_json = runHelperJson(source_device, {QStringLiteral("request-otp")});
    QVERIFY(otp_json.value(QStringLiteral("otpReady")).toBool());
    const auto otp = otp_json.value(QStringLiteral("otp")).toString();
    const auto email = otp_json.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    const auto added = runHelperJson(target_device, {
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    });
    QCOMPARE(added.value(QStringLiteral("command")).toString(), QStringLiteral("add-device-with-otp"));
    QVERIFY(added.value(QStringLiteral("online")).toBool());
    QVERIFY(added.value(QStringLiteral("synced")).toBool());
}

QJsonObject waitReady(const AcceptanceDevice& device)
{
    const auto ready = runHelperJson(device, {QStringLiteral("wait-ready")});
    if (ready.isEmpty()) {
        return {};
    }
    if (!ready.value(QStringLiteral("synced")).toBool()) {
        QTest::qFail("wait-ready did not report synced", __FILE__, __LINE__);
        return {};
    }
    if (!ready.value(QStringLiteral("haveDbInfo")).toBool()) {
        QTest::qFail("wait-ready did not report DB info", __FILE__, __LINE__);
        return {};
    }
    if (ready.value(QStringLiteral("hash")).toString().isEmpty()) {
        QTest::qFail("wait-ready reported empty DB hash", __FILE__, __LINE__);
        return {};
    }
    return ready;
}

void compareAllReady(const QVector<AcceptanceDevice>& devices,
                     const QVector<int>& included_indexes)
{
    QVERIFY(!included_indexes.isEmpty());
    LOG_DEBUG_N << "Comparing replica state across " << included_indexes.size()
                << " ready devices.";
    const auto baseline = waitReady(devices.at(included_indexes.front()));
    if (baseline.isEmpty()) {
        return;
    }
    for (int i = 1; i < included_indexes.size(); ++i) {
        const auto current = waitReady(devices.at(included_indexes.at(i)));
        if (current.isEmpty()) {
            return;
        }
        compareReplicaState(baseline, current);
    }
}

void runTenantScenario(const AcceptancePaths& paths,
                       const BackendFixture& fixture,
                       int tenant_index,
                       int device_count)
{
    QVERIFY(device_count >= 2);
    LOG_DEBUG_N << "Running tenant scenario for tenant index " << tenant_index
                << " with " << device_count << " devices.";

    const auto tenant_name = tenantName(tenant_index);
    auto devices = createDevices(paths, tenant_name, device_count);

    const auto signup = signUpFirstDevice(devices.at(0),
                                          fixture,
                                          tenant_index,
                                          tenant_index == 1 ? QStringLiteral("Freelancer") : QString{});
    if (signup.isEmpty()) {
        return;
    }
    if (tenant_index == 1) {
        QVERIFY(signup.value(QStringLiteral("numNodes")).toInt() > 0);
    }
    for (int index = 1; index < devices.size(); ++index) {
        addSecondaryDevice(devices.at(0), devices.at(index), fixture);
    }

    QVector<int> all_indexes;
    all_indexes.reserve(devices.size());
    for (int index = 0; index < devices.size(); ++index) {
        all_indexes.append(index);
    }
    compareAllReady(devices, all_indexes);

    const int writer_a = tenant_index % devices.size();
    const int offline_device = (tenant_index + devices.size() - 1) % devices.size();
    const int writer_b = (offline_device + 1) % devices.size();
    LOG_DEBUG_N << "Tenant scenario roles for " << tenant_name
                << ": writer_a=" << writer_a
                << ", offline_device=" << offline_device
                << ", writer_b=" << writer_b;

    const auto batch_a = runHelperJson(devices.at(writer_a), {
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("A-%1").arg(tenant_index + 1),
    });
    QVERIFY(batch_a.value(QStringLiteral("actionSubmitted")).toBool());

    compareAllReady(devices, all_indexes);

    const auto disconnected = runHelperJson(devices.at(offline_device), {QStringLiteral("disconnect")}, 120000);
    QVERIFY(disconnected.value(QStringLiteral("disconnected")).toBool());

    const auto batch_b = runHelperJson(devices.at(writer_b), {
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("B-%1").arg(tenant_index + 1),
    });
    QVERIFY(batch_b.value(QStringLiteral("actionSubmitted")).toBool());

    QVector<int> connected_indexes;
    connected_indexes.reserve(devices.size() - 1);
    for (int index = 0; index < devices.size(); ++index) {
        if (index != offline_device) {
            connected_indexes.append(index);
        }
    }
    compareAllReady(devices, connected_indexes);

    const auto full_sync = runHelperJson(devices.at(offline_device), {QStringLiteral("force-full-sync")});
    QVERIFY(full_sync.value(QStringLiteral("fullResyncRequested")).toBool());
    QVERIFY(full_sync.value(QStringLiteral("synced")).toBool());

    compareAllReady(devices, all_indexes);
}

} // namespace

class tst_NextAppUiAcceptance final : public QObject {
    Q_OBJECT

private slots:
    void backendFixtureCreatesRunLayout();
    void acceptanceDevicePrepareCreatesIsolatedClientWorkspace();
    void backendFixtureSignsUpFirstDeviceWhenEnabled();
    void backendFixtureAddsSecondDeviceWithOtpWhenEnabled();
    void backendFixtureReplicatesScriptedBatchAcrossReconnectWhenEnabled();
    void backendFixtureFallsBackToNormalSyncWhenReplayUnavailableWhenEnabled();
    void backendFixtureReconnectsAfterServerRestartWithNormalSyncWhenEnabled();
    void backendFixtureServerEnforcedFullSyncConvergesLaggingDeviceWhenEnabled();
    void backendFixtureForcedFullSyncConvergesLaggingDeviceWhenEnabled();
    void backendFixtureReplicatesAcrossTenantMatrixWhenEnabled();
    void backendFixtureStartsRealBackendWhenEnabled();
};

void tst_NextAppUiAcceptance::backendFixtureCreatesRunLayout()
{
    logTestStart(__func__);
    const auto paths = AcceptancePaths::create();

    QVERIFY(QDir{paths.run_root}.exists());
    QVERIFY(QDir{paths.backend_root}.exists());
    QVERIFY(QDir{paths.certs_root}.exists());
    QVERIFY(QDir{paths.artifacts_root}.exists());
    QVERIFY(QDir{paths.devices_root}.exists());

    const auto device_root = paths.ensureDeviceRoot(QStringLiteral("tenant-a"), QStringLiteral("device-1"));
    QVERIFY(QDir{device_root}.exists());
}

void tst_NextAppUiAcceptance::acceptanceDevicePrepareCreatesIsolatedClientWorkspace()
{
    logTestStart(__func__);
    const auto paths = AcceptancePaths::create();
    AcceptanceDevice device{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};

    LOG_DEBUG_N << "Preparing isolated client workspace for tenant-a/device-1.";
    const auto result = device.runHelper({QStringLiteral("prepare")}, 120000);
    QVERIFY2(result.ok(), result.stderr_text.constData());

    const auto json = QJsonDocument::fromJson(result.jsonText());
    QVERIFY(json.isObject());

    const auto object = json.object();
    QCOMPARE(object.value(QStringLiteral("command")).toString(), QStringLiteral("prepare"));
    QCOMPARE(object.value(QStringLiteral("deviceName")).toString(), QStringLiteral("device-1"));
    QVERIFY(!object.value(QStringLiteral("dbPath")).toString().isEmpty());
    QVERIFY(QFileInfo::exists(object.value(QStringLiteral("dbPath")).toString()));
    QVERIFY(QDir{device.devicePaths().config_root}.exists());
    QVERIFY(QDir{device.devicePaths().data_root}.exists());
}

void tst_NextAppUiAcceptance::backendFixtureSignsUpFirstDeviceWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    BackendFixture fixture{AcceptancePaths::create(), backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for first-device signup test.";
    fixture.start();

    AcceptanceDevice device{AcceptancePaths::create(), QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    LOG_DEBUG_N << "Running first-device signup helper for tenant-a/device-1.";
    const auto signup_result = device.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);

    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto json = QJsonDocument::fromJson(signup_result.jsonText());
    QVERIFY(json.isObject());

    const auto object = json.object();
    QCOMPARE(object.value(QStringLiteral("command")).toString(), QStringLiteral("signup-first-device"));
    QVERIFY(object.value(QStringLiteral("online")).toBool());
    QVERIFY(object.value(QStringLiteral("synced")).toBool());
    QVERIFY(!object.value(QStringLiteral("hash")).toString().isEmpty());
    QVERIFY(object.value(QStringLiteral("numDayColors")).toInt() > 0);
    QVERIFY(object.value(QStringLiteral("numActionCategories")).toInt() >= 0);
    QVERIFY(object.value(QStringLiteral("numNodes")).toInt() >= 0);
}

void tst_NextAppUiAcceptance::backendFixtureAddsSecondDeviceWithOtpWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for add-device OTP test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    LOG_DEBUG_N << "Signing up primary device before OTP flow.";
    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto signup_json = QJsonDocument::fromJson(signup_result.jsonText());
    QVERIFY(signup_json.isObject());
    const auto signup_object = signup_json.object();
    QVERIFY(signup_object.value(QStringLiteral("synced")).toBool());

    LOG_DEBUG_N << "Requesting OTP from primary device.";
    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());

    const auto otp_json = QJsonDocument::fromJson(otp_result.jsonText());
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    QVERIFY(otp_object.value(QStringLiteral("otpReady")).toBool());
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    LOG_DEBUG_N << "Adding secondary device with OTP.";
    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    const auto add_json = QJsonDocument::fromJson(add_result.jsonText());
    QVERIFY(add_json.isObject());
    const auto add_object = add_json.object();
    QCOMPARE(add_object.value(QStringLiteral("command")).toString(), QStringLiteral("add-device-with-otp"));
    QVERIFY(add_object.value(QStringLiteral("online")).toBool());
    QVERIFY(add_object.value(QStringLiteral("synced")).toBool());

    LOG_DEBUG_N << "Waiting for both devices to converge after OTP add.";
    const auto wait1 = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    const auto wait2 = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait1.ok(), wait1.stderr_text.constData());
    QVERIFY2(wait2.ok(), wait2.stderr_text.constData());

    const auto wait1_json = QJsonDocument::fromJson(wait1.jsonText());
    const auto wait2_json = QJsonDocument::fromJson(wait2.jsonText());
    QVERIFY(wait1_json.isObject());
    QVERIFY(wait2_json.isObject());

    const auto wait1_object = wait1_json.object();
    const auto wait2_object = wait2_json.object();
    QVERIFY(wait1_object.value(QStringLiteral("synced")).toBool());
    QVERIFY(wait2_object.value(QStringLiteral("synced")).toBool());
    QCOMPARE(wait1_object.value(QStringLiteral("hash")).toString(),
             wait2_object.value(QStringLiteral("hash")).toString());
    QCOMPARE(wait1_object.value(QStringLiteral("numNodes")).toInt(),
             wait2_object.value(QStringLiteral("numNodes")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numActionCategories")).toInt(),
             wait2_object.value(QStringLiteral("numActionCategories")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numActions")).toInt(),
             wait2_object.value(QStringLiteral("numActions")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numDays")).toInt(),
             wait2_object.value(QStringLiteral("numDays")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numDayColors")).toInt(),
             wait2_object.value(QStringLiteral("numDayColors")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numWorkSessions")).toInt(),
             wait2_object.value(QStringLiteral("numWorkSessions")).toInt());
    QCOMPARE(wait1_object.value(QStringLiteral("numTimeBlocks")).toInt(),
             wait2_object.value(QStringLiteral("numTimeBlocks")).toInt());
}

void tst_NextAppUiAcceptance::backendFixtureReplicatesScriptedBatchAcrossReconnectWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for reconnect replication test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    LOG_DEBUG_N << "Signing up first device for reconnect replication test.";
    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    LOG_DEBUG_N << "Requesting OTP for secondary device.";
    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());
    const auto otp_json = QJsonDocument::fromJson(otp_result.jsonText());
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    LOG_DEBUG_N << "Adding secondary device before reconnect scenario.";
    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    LOG_DEBUG_N << "Applying first scripted batch before disconnect.";
    const auto batch_a = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("A"),
    }, 240000);
    QVERIFY2(batch_a.ok(), batch_a.stderr_text.constData());
    const auto batch_a_json = QJsonDocument::fromJson(batch_a.jsonText());
    QVERIFY(batch_a_json.isObject());
    const auto batch_a_object = batch_a_json.object();
    QVERIFY(batch_a_object.value(QStringLiteral("actionSubmitted")).toBool());

    const auto wait_writer_a = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_a.ok(), wait_writer_a.stderr_text.constData());
    const auto wait_writer_a_json = QJsonDocument::fromJson(wait_writer_a.jsonText());
    QVERIFY(wait_writer_a_json.isObject());
    const auto wait_writer_a_object = wait_writer_a_json.object();
    QVERIFY(wait_writer_a_object.value(QStringLiteral("synced")).toBool());

    const auto wait_after_a = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_after_a.ok(), wait_after_a.stderr_text.constData());
    const auto wait_after_a_json = QJsonDocument::fromJson(wait_after_a.jsonText());
    QVERIFY(wait_after_a_json.isObject());
    const auto wait_after_a_object = wait_after_a_json.object();
    QVERIFY(wait_after_a_object.value(QStringLiteral("synced")).toBool());
    QCOMPARE(wait_writer_a_object.value(QStringLiteral("hash")).toString(),
             wait_after_a_object.value(QStringLiteral("hash")).toString());
    QCOMPARE(wait_writer_a_object.value(QStringLiteral("numActions")).toInt(),
             wait_after_a_object.value(QStringLiteral("numActions")).toInt());

    LOG_DEBUG_N << "Disconnecting secondary device.";
    const auto disconnect = device2.runHelper({QStringLiteral("disconnect")}, 120000);
    QVERIFY2(disconnect.ok(), disconnect.stderr_text.constData());
    const auto disconnect_json = QJsonDocument::fromJson(disconnect.jsonText());
    QVERIFY(disconnect_json.isObject());
    QVERIFY(disconnect_json.object().value(QStringLiteral("disconnected")).toBool());

    LOG_DEBUG_N << "Applying second scripted batch while secondary device is offline.";
    const auto batch_b = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("B"),
    }, 240000);
    QVERIFY2(batch_b.ok(), batch_b.stderr_text.constData());
    const auto batch_b_json = QJsonDocument::fromJson(batch_b.jsonText());
    QVERIFY(batch_b_json.isObject());
    const auto batch_b_object = batch_b_json.object();
    QVERIFY(batch_b_object.value(QStringLiteral("actionSubmitted")).toBool());

    const auto wait_writer_b = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_b.ok(), wait_writer_b.stderr_text.constData());
    const auto wait_writer_b_json = QJsonDocument::fromJson(wait_writer_b.jsonText());
    QVERIFY(wait_writer_b_json.isObject());
    const auto wait_writer_b_object = wait_writer_b_json.object();
    QVERIFY(wait_writer_b_object.value(QStringLiteral("synced")).toBool());
    QVERIFY(wait_writer_b_object.value(QStringLiteral("numActions")).toInt()
            >= wait_writer_a_object.value(QStringLiteral("numActions")).toInt());

    LOG_DEBUG_N << "Reconnecting secondary device and validating replay path.";
    const auto reconnect = device2.runHelper({QStringLiteral("reconnect")}, 240000);
    QVERIFY2(reconnect.ok(), reconnect.stderr_text.constData());
    const auto reconnect_json = QJsonDocument::fromJson(reconnect.jsonText());
    QVERIFY(reconnect_json.isObject());
    const auto reconnect_object = reconnect_json.object();
    QVERIFY(reconnect_object.value(QStringLiteral("synced")).toBool());
    requireReplayReconnect(reconnect_object);

    QCOMPARE(wait_writer_b_object.value(QStringLiteral("hash")).toString(),
             reconnect_object.value(QStringLiteral("hash")).toString());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numNodes")).toInt(),
             reconnect_object.value(QStringLiteral("numNodes")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numActionCategories")).toInt(),
             reconnect_object.value(QStringLiteral("numActionCategories")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numActions")).toInt(),
             reconnect_object.value(QStringLiteral("numActions")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numDays")).toInt(),
             reconnect_object.value(QStringLiteral("numDays")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numDayColors")).toInt(),
             reconnect_object.value(QStringLiteral("numDayColors")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numWorkSessions")).toInt(),
             reconnect_object.value(QStringLiteral("numWorkSessions")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numTimeBlocks")).toInt(),
             reconnect_object.value(QStringLiteral("numTimeBlocks")).toInt());
}

void tst_NextAppUiAcceptance::backendFixtureFallsBackToNormalSyncWhenReplayUnavailableWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for replay-unavailable fallback test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    signUpFirstDevice(device1, fixture, 0);
    addSecondaryDevice(device1, device2, fixture);
    compareReplicaState(waitReady(device1), waitReady(device2));

    LOG_DEBUG_N << "Disconnecting lagging device before large replay-miss batch.";
    const auto disconnected = runHelperJson(device2, {QStringLiteral("disconnect")}, 120000);
    QVERIFY(disconnected.value(QStringLiteral("disconnected")).toBool());

    LOG_DEBUG_N << "Applying large scripted batch set to force replay-unavailable fallback.";
    const auto large_batch = runHelperJson(device1, {
        QStringLiteral("apply-scripted-batches"),
        QStringLiteral("--batch"), QStringLiteral("ReplayMiss"),
        QStringLiteral("--count"), QStringLiteral("400"),
    }, 600000);
    QVERIFY(large_batch.value(QStringLiteral("actionSubmitted")).toBool());
    QCOMPARE(large_batch.value(QStringLiteral("batchCount")).toInt(), 400);

    LOG_DEBUG_N << "Reconnecting lagging device after replay-unavailable setup.";
    const auto reconnect = runHelperJson(device2, {QStringLiteral("reconnect")}, 600000);
    QVERIFY(reconnect.value(QStringLiteral("synced")).toBool());
    QVERIFY(reconnect.value(QStringLiteral("replayUnavailableFallback")).toBool());
    QVERIFY(!reconnect.value(QStringLiteral("requestedLiveReplay")).toBool());
    QVERIFY(!reconnect.value(QStringLiteral("serverInstanceChanged")).toBool());
    requireNormalSyncReconnect(reconnect);

    compareReplicaState(waitReady(device1), reconnect);
}

void tst_NextAppUiAcceptance::backendFixtureReconnectsAfterServerRestartWithNormalSyncWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for server-restart reconnect test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    signUpFirstDevice(device1, fixture, 0);
    addSecondaryDevice(device1, device2, fixture);
    compareReplicaState(waitReady(device1), waitReady(device2));

    LOG_DEBUG_N << "Disconnecting lagging device before backend restart.";
    const auto disconnected = runHelperJson(device2, {QStringLiteral("disconnect")}, 120000);
    QVERIFY(disconnected.value(QStringLiteral("disconnected")).toBool());

    LOG_DEBUG_N << "Applying batch before restarting nextappd.";
    const auto batch = runHelperJson(device1, {
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("RestartA"),
    }, 240000);
    QVERIFY(batch.value(QStringLiteral("actionSubmitted")).toBool());

    LOG_DEBUG_N << "Restarting nextappd backend service.";
    fixture.restartNextappd();

    LOG_DEBUG_N << "Reconnecting lagging device after server restart.";
    const auto reconnect = runHelperJson(device2, {QStringLiteral("reconnect")}, 600000);
    QVERIFY(reconnect.value(QStringLiteral("synced")).toBool());
    QVERIFY(reconnect.value(QStringLiteral("serverInstanceChanged")).toBool());
    QVERIFY(!reconnect.value(QStringLiteral("usedFullSync")).toBool());
    QVERIFY(!reconnect.value(QStringLiteral("requestedLiveReplay")).toBool());
    requireNormalSyncReconnect(reconnect);

    compareReplicaState(waitReady(device1), reconnect);
}

void tst_NextAppUiAcceptance::backendFixtureServerEnforcedFullSyncConvergesLaggingDeviceWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for server-enforced full sync test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    signUpFirstDevice(device1, fixture, 0);
    addSecondaryDevice(device1, device2, fixture);
    compareReplicaState(waitReady(device1), waitReady(device2));

    LOG_DEBUG_N << "Disconnecting lagging device before structural delete.";
    const auto disconnected = runHelperJson(device2, {QStringLiteral("disconnect")}, 120000);
    QVERIFY(disconnected.value(QStringLiteral("disconnected")).toBool());

    LOG_DEBUG_N << "Applying structural delete expected to trigger server-enforced resync.";
    const auto structural_delete = runHelperJson(device1, {
        QStringLiteral("apply-structural-node-delete"),
        QStringLiteral("--batch"), QStringLiteral("ResyncDelete"),
    }, 600000);
    QVERIFY(structural_delete.value(QStringLiteral("resyncRequested")).toBool());

    LOG_DEBUG_N << "Reconnecting lagging device after server-enforced resync trigger.";
    const auto reconnect = runHelperJson(device2, {QStringLiteral("reconnect")}, 600000);
    QVERIFY(reconnect.value(QStringLiteral("synced")).toBool());
    requireFullSyncReconnect(reconnect);

    compareReplicaState(waitReady(device1), reconnect);
}

void tst_NextAppUiAcceptance::backendFixtureForcedFullSyncConvergesLaggingDeviceWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for client-forced full sync test.";
    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    LOG_DEBUG_N << "Signing up primary device before forced full sync scenario.";
    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    LOG_DEBUG_N << "Requesting OTP for forced full sync scenario.";
    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());
    const auto otp_json = QJsonDocument::fromJson(otp_result.jsonText());
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    LOG_DEBUG_N << "Adding secondary device before forced full sync scenario.";
    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    LOG_DEBUG_N << "Applying first batch before disconnect.";
    const auto batch_a = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("A"),
    }, 240000);
    QVERIFY2(batch_a.ok(), batch_a.stderr_text.constData());

    const auto wait_writer_a = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    const auto wait_after_a = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_a.ok(), wait_writer_a.stderr_text.constData());
    QVERIFY2(wait_after_a.ok(), wait_after_a.stderr_text.constData());

    const auto wait_writer_a_json = QJsonDocument::fromJson(wait_writer_a.jsonText());
    const auto wait_after_a_json = QJsonDocument::fromJson(wait_after_a.jsonText());
    QVERIFY(wait_writer_a_json.isObject());
    QVERIFY(wait_after_a_json.isObject());
    QCOMPARE(wait_writer_a_json.object().value(QStringLiteral("hash")).toString(),
             wait_after_a_json.object().value(QStringLiteral("hash")).toString());

    LOG_DEBUG_N << "Disconnecting lagging device before second batch.";
    const auto disconnect = device2.runHelper({QStringLiteral("disconnect")}, 120000);
    QVERIFY2(disconnect.ok(), disconnect.stderr_text.constData());

    LOG_DEBUG_N << "Applying second batch before forcing full sync.";
    const auto batch_b = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("B"),
    }, 240000);
    QVERIFY2(batch_b.ok(), batch_b.stderr_text.constData());

    const auto wait_writer_b = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_b.ok(), wait_writer_b.stderr_text.constData());
    const auto wait_writer_b_json = QJsonDocument::fromJson(wait_writer_b.jsonText());
    QVERIFY(wait_writer_b_json.isObject());
    const auto wait_writer_b_object = wait_writer_b_json.object();
    QVERIFY(wait_writer_b_object.value(QStringLiteral("synced")).toBool());

    LOG_DEBUG_N << "Forcing full sync on lagging device.";
    const auto full_sync = device2.runHelper({QStringLiteral("force-full-sync")}, 240000);
    QVERIFY2(full_sync.ok(), full_sync.stderr_text.constData());
    const auto full_sync_json = QJsonDocument::fromJson(full_sync.jsonText());
    QVERIFY(full_sync_json.isObject());
    const auto full_sync_object = full_sync_json.object();
    QVERIFY(full_sync_object.value(QStringLiteral("fullResyncRequested")).toBool());
    QVERIFY(full_sync_object.value(QStringLiteral("synced")).toBool());
    requireFullSyncReconnect(full_sync_object);

    QCOMPARE(wait_writer_b_object.value(QStringLiteral("hash")).toString(),
             full_sync_object.value(QStringLiteral("hash")).toString());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numNodes")).toInt(),
             full_sync_object.value(QStringLiteral("numNodes")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numActionCategories")).toInt(),
             full_sync_object.value(QStringLiteral("numActionCategories")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numActions")).toInt(),
             full_sync_object.value(QStringLiteral("numActions")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numDays")).toInt(),
             full_sync_object.value(QStringLiteral("numDays")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numDayColors")).toInt(),
             full_sync_object.value(QStringLiteral("numDayColors")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numWorkSessions")).toInt(),
             full_sync_object.value(QStringLiteral("numWorkSessions")).toInt());
    QCOMPARE(wait_writer_b_object.value(QStringLiteral("numTimeBlocks")).toInt(),
             full_sync_object.value(QStringLiteral("numTimeBlocks")).toInt());
}

void tst_NextAppUiAcceptance::backendFixtureReplicatesAcrossTenantMatrixWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
        LOG_WARN_N << "Skipping backend fixture tenant matrix replication test. Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable.";
    }

    const auto tenant_count = matrixTenantCount();
    const auto device_count = matrixDeviceCount();
    if (tenant_count <= 0 || device_count < 2) {
        QSKIP("Acceptance matrix requires NEXTAPP_ACCEPTANCE_TENANTS >= 1 and NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT >= 2.");
    }

    LOG_DEBUG_N << "Running backend fixture tenant matrix replication test with "
                << tenant_count << " tenants and " << device_count << " devices per tenant.";

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture for tenant matrix replication test.";
    fixture.start();

    for (int tenant_index = 0; tenant_index < tenant_count; ++tenant_index) {
        runTenantScenario(paths, fixture, tenant_index, device_count);
    }
}

void tst_NextAppUiAcceptance::backendFixtureStartsRealBackendWhenEnabled()
{
    logTestStart(__func__);
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
        LOG_WARN_N << "Skipping backend fixture real backend smoke test. Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable.";
    }

    BackendFixture fixture{AcceptancePaths::create(), backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    LOG_DEBUG_N << "Starting backend fixture real backend smoke test.";
    fixture.start();
    LOG_DEBUG_N << "Backend fixture started.";
    QVERIFY(fixture.isRunning());
    LOG_DEBUG_N << "Backend fixture is running.";
    QVERIFY(!fixture.runId().isEmpty());
    LOG_DEBUG_N << "Backend fixture run ID: " << fixture.runId();
    QVERIFY(fixture.nextappPublicUrl().startsWith(QStringLiteral("https://")));
    QVERIFY(fixture.signupPublicUrl().startsWith(QStringLiteral("http://")));

    LOG_DEBUG_N  << "Stopping backend fixture.";
    fixture.stop();
    LOG_DEBUG_N  << "Backend fixture stopped.";
    QVERIFY(!fixture.isRunning());
}

int main(int argc, char** argv)
{
    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::StreamHandler>(std::clog, logfault::LogLevel::DEBUGGING));

    LOG_INFO_N << "Starting NextApp UI acceptance tests.";

    if (!qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT")) {
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArrayLiteral("1200000"));
    }

    QCoreApplication app(argc, argv);
    tst_NextAppUiAcceptance test;
    StdoutCapture stdout_capture;
    const auto rc = QTest::qExec(&test, argc, argv);
    stdout_capture.stop();
    printFailureSummary(summarizeFailures(stdout_capture.text()));
    return rc;
}

#include "tst_nextappui_acceptance.moc"
