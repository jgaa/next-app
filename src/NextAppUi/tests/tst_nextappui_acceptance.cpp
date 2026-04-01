#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include "AcceptanceHarness.h"

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
    if (!result.ok()) {
        QTest::qFail(result.stderr_text.constData(), __FILE__, __LINE__);
        return {};
    }
    const auto json = QJsonDocument::fromJson(result.stdout_text);
    if (!json.isObject()) {
        QTest::qFail("Helper output was not a JSON object", __FILE__, __LINE__);
        return {};
    }
    return json.object();
}

QJsonObject runHelperJson(const AcceptanceDevice& device,
                          const QStringList& arguments,
                          int timeout_ms = 240000)
{
    return requireJsonObject(device.runHelper(arguments, timeout_ms));
}

void compareReplicaState(const QJsonObject& expected, const QJsonObject& actual)
{
    QCOMPARE(expected.value(QStringLiteral("hash")).toString(),
             actual.value(QStringLiteral("hash")).toString());
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
    if (!ready.value(QStringLiteral("synced")).toBool()) {
        QTest::qFail("wait-ready did not report synced", __FILE__, __LINE__);
        return {};
    }
    return ready;
}

void compareAllReady(const QVector<AcceptanceDevice>& devices,
                     const QVector<int>& included_indexes)
{
    QVERIFY(!included_indexes.isEmpty());
    const auto baseline = waitReady(devices.at(included_indexes.front()));
    for (int i = 1; i < included_indexes.size(); ++i) {
        compareReplicaState(baseline, waitReady(devices.at(included_indexes.at(i))));
    }
}

void runTenantScenario(const AcceptancePaths& paths,
                       const BackendFixture& fixture,
                       int tenant_index,
                       int device_count)
{
    QVERIFY(device_count >= 2);

    const auto tenant_name = tenantName(tenant_index);
    auto devices = createDevices(paths, tenant_name, device_count);

    const auto signup = signUpFirstDevice(devices.at(0),
                                          fixture,
                                          tenant_index,
                                          tenant_index == 1 ? QStringLiteral("Freelancer") : QString{});
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
    void backendFixtureForcedFullSyncConvergesLaggingDeviceWhenEnabled();
    void backendFixtureReplicatesAcrossTenantMatrixWhenEnabled();
    void backendFixtureStartsRealBackendWhenEnabled();
};

void tst_NextAppUiAcceptance::backendFixtureCreatesRunLayout()
{
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
    const auto paths = AcceptancePaths::create();
    AcceptanceDevice device{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};

    const auto result = device.runHelper({QStringLiteral("prepare")}, 120000);
    QVERIFY2(result.ok(), result.stderr_text.constData());

    const auto json = QJsonDocument::fromJson(result.stdout_text);
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
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    BackendFixture fixture{AcceptancePaths::create(), backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();

    AcceptanceDevice device{AcceptancePaths::create(), QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    const auto signup_result = device.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);

    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto json = QJsonDocument::fromJson(signup_result.stdout_text);
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
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto signup_json = QJsonDocument::fromJson(signup_result.stdout_text);
    QVERIFY(signup_json.isObject());
    const auto signup_object = signup_json.object();
    QVERIFY(signup_object.value(QStringLiteral("synced")).toBool());

    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());

    const auto otp_json = QJsonDocument::fromJson(otp_result.stdout_text);
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    QVERIFY(otp_object.value(QStringLiteral("otpReady")).toBool());
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    const auto add_json = QJsonDocument::fromJson(add_result.stdout_text);
    QVERIFY(add_json.isObject());
    const auto add_object = add_json.object();
    QCOMPARE(add_object.value(QStringLiteral("command")).toString(), QStringLiteral("add-device-with-otp"));
    QVERIFY(add_object.value(QStringLiteral("online")).toBool());
    QVERIFY(add_object.value(QStringLiteral("synced")).toBool());

    const auto wait1 = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    const auto wait2 = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait1.ok(), wait1.stderr_text.constData());
    QVERIFY2(wait2.ok(), wait2.stderr_text.constData());

    const auto wait1_json = QJsonDocument::fromJson(wait1.stdout_text);
    const auto wait2_json = QJsonDocument::fromJson(wait2.stdout_text);
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
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());
    const auto otp_json = QJsonDocument::fromJson(otp_result.stdout_text);
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    const auto batch_a = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("A"),
    }, 240000);
    QVERIFY2(batch_a.ok(), batch_a.stderr_text.constData());
    const auto batch_a_json = QJsonDocument::fromJson(batch_a.stdout_text);
    QVERIFY(batch_a_json.isObject());
    const auto batch_a_object = batch_a_json.object();
    QVERIFY(batch_a_object.value(QStringLiteral("actionSubmitted")).toBool());

    const auto wait_writer_a = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_a.ok(), wait_writer_a.stderr_text.constData());
    const auto wait_writer_a_json = QJsonDocument::fromJson(wait_writer_a.stdout_text);
    QVERIFY(wait_writer_a_json.isObject());
    const auto wait_writer_a_object = wait_writer_a_json.object();
    QVERIFY(wait_writer_a_object.value(QStringLiteral("synced")).toBool());

    const auto wait_after_a = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_after_a.ok(), wait_after_a.stderr_text.constData());
    const auto wait_after_a_json = QJsonDocument::fromJson(wait_after_a.stdout_text);
    QVERIFY(wait_after_a_json.isObject());
    const auto wait_after_a_object = wait_after_a_json.object();
    QVERIFY(wait_after_a_object.value(QStringLiteral("synced")).toBool());
    QCOMPARE(wait_writer_a_object.value(QStringLiteral("hash")).toString(),
             wait_after_a_object.value(QStringLiteral("hash")).toString());
    QCOMPARE(wait_writer_a_object.value(QStringLiteral("numActions")).toInt(),
             wait_after_a_object.value(QStringLiteral("numActions")).toInt());

    const auto disconnect = device2.runHelper({QStringLiteral("disconnect")}, 120000);
    QVERIFY2(disconnect.ok(), disconnect.stderr_text.constData());
    const auto disconnect_json = QJsonDocument::fromJson(disconnect.stdout_text);
    QVERIFY(disconnect_json.isObject());
    QVERIFY(disconnect_json.object().value(QStringLiteral("disconnected")).toBool());

    const auto batch_b = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("B"),
    }, 240000);
    QVERIFY2(batch_b.ok(), batch_b.stderr_text.constData());
    const auto batch_b_json = QJsonDocument::fromJson(batch_b.stdout_text);
    QVERIFY(batch_b_json.isObject());
    const auto batch_b_object = batch_b_json.object();
    QVERIFY(batch_b_object.value(QStringLiteral("actionSubmitted")).toBool());

    const auto wait_writer_b = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_b.ok(), wait_writer_b.stderr_text.constData());
    const auto wait_writer_b_json = QJsonDocument::fromJson(wait_writer_b.stdout_text);
    QVERIFY(wait_writer_b_json.isObject());
    const auto wait_writer_b_object = wait_writer_b_json.object();
    QVERIFY(wait_writer_b_object.value(QStringLiteral("synced")).toBool());
    QVERIFY(wait_writer_b_object.value(QStringLiteral("numActions")).toInt()
            >= wait_writer_a_object.value(QStringLiteral("numActions")).toInt());

    const auto reconnect = device2.runHelper({QStringLiteral("reconnect")}, 240000);
    QVERIFY2(reconnect.ok(), reconnect.stderr_text.constData());
    const auto reconnect_json = QJsonDocument::fromJson(reconnect.stdout_text);
    QVERIFY(reconnect_json.isObject());
    const auto reconnect_object = reconnect_json.object();
    QVERIFY(reconnect_object.value(QStringLiteral("synced")).toBool());

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

void tst_NextAppUiAcceptance::backendFixtureForcedFullSyncConvergesLaggingDeviceWhenEnabled()
{
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();

    AcceptanceDevice device1{paths, QStringLiteral("tenant-a"), QStringLiteral("device-1")};
    AcceptanceDevice device2{paths, QStringLiteral("tenant-a"), QStringLiteral("device-2")};

    const auto signup_result = device1.runHelper({
        QStringLiteral("signup-first-device"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-name"), QStringLiteral("Acceptance User"),
        QStringLiteral("--user-email"), QStringLiteral("acceptance-user@example.test"),
        QStringLiteral("--company"), QStringLiteral("Acceptance Tenant"),
    }, 240000);
    QVERIFY2(signup_result.ok(), signup_result.stderr_text.constData());

    const auto otp_result = device1.runHelper({QStringLiteral("request-otp")}, 240000);
    QVERIFY2(otp_result.ok(), otp_result.stderr_text.constData());
    const auto otp_json = QJsonDocument::fromJson(otp_result.stdout_text);
    QVERIFY(otp_json.isObject());
    const auto otp_object = otp_json.object();
    const auto otp = otp_object.value(QStringLiteral("otp")).toString();
    const auto email = otp_object.value(QStringLiteral("email")).toString();
    QVERIFY(!otp.isEmpty());
    QVERIFY(!email.isEmpty());

    const auto add_result = device2.runHelper({
        QStringLiteral("add-device-with-otp"),
        QStringLiteral("--signup-url"), fixture.signupPublicUrl(),
        QStringLiteral("--user-email"), email,
        QStringLiteral("--otp"), otp,
    }, 240000);
    QVERIFY2(add_result.ok(), add_result.stderr_text.constData());

    const auto batch_a = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("A"),
    }, 240000);
    QVERIFY2(batch_a.ok(), batch_a.stderr_text.constData());

    const auto wait_writer_a = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    const auto wait_after_a = device2.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_a.ok(), wait_writer_a.stderr_text.constData());
    QVERIFY2(wait_after_a.ok(), wait_after_a.stderr_text.constData());

    const auto wait_writer_a_json = QJsonDocument::fromJson(wait_writer_a.stdout_text);
    const auto wait_after_a_json = QJsonDocument::fromJson(wait_after_a.stdout_text);
    QVERIFY(wait_writer_a_json.isObject());
    QVERIFY(wait_after_a_json.isObject());
    QCOMPARE(wait_writer_a_json.object().value(QStringLiteral("hash")).toString(),
             wait_after_a_json.object().value(QStringLiteral("hash")).toString());

    const auto disconnect = device2.runHelper({QStringLiteral("disconnect")}, 120000);
    QVERIFY2(disconnect.ok(), disconnect.stderr_text.constData());

    const auto batch_b = device1.runHelper({
        QStringLiteral("apply-scripted-batch"),
        QStringLiteral("--batch"), QStringLiteral("B"),
    }, 240000);
    QVERIFY2(batch_b.ok(), batch_b.stderr_text.constData());

    const auto wait_writer_b = device1.runHelper({QStringLiteral("wait-ready")}, 240000);
    QVERIFY2(wait_writer_b.ok(), wait_writer_b.stderr_text.constData());
    const auto wait_writer_b_json = QJsonDocument::fromJson(wait_writer_b.stdout_text);
    QVERIFY(wait_writer_b_json.isObject());
    const auto wait_writer_b_object = wait_writer_b_json.object();
    QVERIFY(wait_writer_b_object.value(QStringLiteral("synced")).toBool());

    const auto full_sync = device2.runHelper({QStringLiteral("force-full-sync")}, 240000);
    QVERIFY2(full_sync.ok(), full_sync.stderr_text.constData());
    const auto full_sync_json = QJsonDocument::fromJson(full_sync.stdout_text);
    QVERIFY(full_sync_json.isObject());
    const auto full_sync_object = full_sync_json.object();
    QVERIFY(full_sync_object.value(QStringLiteral("fullResyncRequested")).toBool());
    QVERIFY(full_sync_object.value(QStringLiteral("synced")).toBool());

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
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    const auto tenant_count = matrixTenantCount();
    const auto device_count = matrixDeviceCount();
    if (tenant_count <= 0 || device_count < 2) {
        QSKIP("Acceptance matrix requires NEXTAPP_ACCEPTANCE_TENANTS >= 1 and NEXTAPP_ACCEPTANCE_DEVICES_PER_TENANT >= 2.");
    }

    const auto paths = AcceptancePaths::create();
    BackendFixture fixture{paths, backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();

    for (int tenant_index = 0; tenant_index < tenant_count; ++tenant_index) {
        runTenantScenario(paths, fixture, tenant_index, device_count);
    }
}

void tst_NextAppUiAcceptance::backendFixtureStartsRealBackendWhenEnabled()
{
    if (!qEnvironmentVariableIsSet("NEXTAPP_ACCEPTANCE_RUN_BACKEND")) {
        QSKIP("Set NEXTAPP_ACCEPTANCE_RUN_BACKEND=1 to enable real-container acceptance smoke tests.");
    }

    BackendFixture fixture{AcceptancePaths::create(), backendOptionsFromEnv()};
    if (!fixture.dockerAvailable()) {
        QSKIP("Docker is not available for the acceptance backend fixture.");
    }

    fixture.start();
    QVERIFY(fixture.isRunning());
    QVERIFY(!fixture.runId().isEmpty());
    QVERIFY(fixture.nextappPublicUrl().startsWith(QStringLiteral("https://")));
    QVERIFY(fixture.signupPublicUrl().startsWith(QStringLiteral("http://")));

    fixture.stop();
    QVERIFY(!fixture.isRunning());
}

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT")) {
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArrayLiteral("1200000"));
    }

    QCoreApplication app(argc, argv);
    tst_NextAppUiAcceptance test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_nextappui_acceptance.moc"
