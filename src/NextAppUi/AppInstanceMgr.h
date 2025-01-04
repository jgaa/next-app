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

    void init();
    void close();

private:
#ifdef _DEBUG
    const QString SHARED_MEMORY_KEY = "NextApp_InstanceTracker_dbg";
#else
    const QString SHARED_MEMORY_KEY = "NextApp_InstanceTracker";
#endif
    QSharedMemory shared_memory_{SHARED_MEMORY_KEY};
    QString name_{"singleton"};
    bool closed_{true};
};
