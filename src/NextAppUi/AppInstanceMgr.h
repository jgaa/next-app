#pragma once

#include <array>

#include <QSharedMemory>

class AppInstanceMgr
{
    static constexpr uint max_instances = 10;

    struct InstanceInfo {
        uint activeInstances{};
        std::array<qint64, max_instances> pids;
    };

public:
    AppInstanceMgr();
    ~AppInstanceMgr();

    static AppInstanceMgr *instance();

    QString name() const noexcept { return name_; }
    uint instanceId() const noexcept { return instance_id_; }

    bool init();
    void close();

private:
    // TODO: We should probably use a hash aagainst the server we use
    //       to support multiple servers on the same device.
#ifdef _DEBUG
    const QString SHARED_MEMORY_KEY = "NextApp_InstanceTracker_dbg";
#else
    const QString SHARED_MEMORY_KEY = "NextApp_InstanceTracker";
#endif
#ifndef __ANDROID__
    QSharedMemory shared_memory_{SHARED_MEMORY_KEY};
#endif
    QString name_{"singleton"};
    bool closed_{true};
    uint instance_id_{1}; // Default to 1, also on systems that don't support multiple instances.
};
