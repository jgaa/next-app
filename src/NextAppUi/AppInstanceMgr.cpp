
#include <QCoreApplication>
#include <QThread>
#include <QSettings>
#include <QGuiApplication>

#include "AppInstanceMgr.h"

#include "logging.h"
#include "util.h"


using namespace std;
using namespace std::chrono_literals;

AppInstanceMgr::AppInstanceMgr() {
}

AppInstanceMgr::~AppInstanceMgr()
{
    close();
}

AppInstanceMgr *AppInstanceMgr::instance()
{
    static AppInstanceMgr instance;
    return &instance;
}

bool AppInstanceMgr::init()
{
#ifndef __ANDROID__
    assert(closed_);
    closed_ = false;
    QSettings settings;
    const auto conf_instances = settings.value("client/maxInstances", 1).toUInt();
    const auto instances = min(conf_instances, max_instances);

    // Somehow, both create() and attach() some times fails, so we need to retry.
    for(int i = 0; i < 10; ++i) {
        if (shared_memory_.create(sizeof(InstanceInfo))) {
            break;
        }

        if (shared_memory_.attach()) {
            break;
        }

        LOG_TRACE_N << "Failed to attach to shared memory. Will retry.";
        QThread::sleep(80ms);
    }

    if (!shared_memory_.isAttached()) {
        LOG_ERROR_N << "Failed to attach to shared memory!";
        qFatal("Failed to attach to shared memory!");
    }

    shared_memory_.lock();
    ScopedExit unlock([&] { shared_memory_.unlock(); });

    InstanceInfo *info = static_cast<InstanceInfo*>(shared_memory_.data());

    // Find an available instance slot or re-use an existing one
    for (uint i = 0; i < instances; ++i) {
        if (info->pids[i] == 0) {  // Unused slot
            info->pids[i] = QCoreApplication::applicationPid();
            info->activeInstances++;
            shared_memory_.unlock();
            name_ = QString("/instance_%1").arg(i + 1);
            LOG_INFO_N << "Instance name: " << name_ << ". Active instances: "
                       << info->activeInstances << " of " << instances;
            instance_id_ = i + 1;
            return true;
        }
    }

    LOG_ERROR_N << "No available instance slots!";
    return false;
#else
    return true;
#endif // __ANDROID__
}

void AppInstanceMgr::close()
{
#ifndef __ANDROID__
    if (!closed_) {
        LOG_DEBUG_N << "Cleaning up instance " << name();

        shared_memory_.lock();
        ScopedExit unlock([&] { shared_memory_.unlock(); });
        InstanceInfo *info = static_cast<InstanceInfo *>(shared_memory_.data());
        for (int i = 0; i < 10; ++i) {
            if (info->pids[i] == QCoreApplication::applicationPid()) {
                info->pids[i] = 0;
                --info->activeInstances;
                assert(info->activeInstances >= 0);
                break;
            }
        }

        LOG_DEBUG_N << "Successfully cleaned up instance " << name();
        closed_ = true;
    }
#endif // __ANDROID__
}

QString AppInstanceMgr::getKey()
{
    QString key = "nextapp";
    if (auto* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance())) {
        key = app->applicationName();
    }

    auto full_key = QString{"InstanceTracker_"} + key;
    return full_key;
}
