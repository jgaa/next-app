#pragma once

#include <map>
#include <QObject>
#include <QUuid>

#include "qcorotask.h"

class ActionsWorkedOnTodayCache : public QObject
{
    Q_OBJECT

public:
    struct Info {
        bool current{false};
    };

    ActionsWorkedOnTodayCache();

    static ActionsWorkedOnTodayCache *instance();

    bool contains(const QUuid& uuid) const noexcept {
        return actions_.contains(uuid);
    }

    std::optional<Info> get(const QUuid& uuid) const noexcept {
        if (auto it = actions_.find(uuid); it != actions_.end()) {
            return it->second;
        }
        return {};
    };

signals:
    void modelReset();

private:
    QCoro::Task<void> init();

    std::map<QUuid, Info> actions_;
    bool is_initializing_{false};
    bool pending_initialize_{false};
};
