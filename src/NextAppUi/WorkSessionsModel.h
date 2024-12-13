#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/uuid/uuid.hpp>

#include <QAbstractTableModel>
#include <QStringListModel>
#include <QUuid>
#include <QTimer>

#include "WorkModelBase.h"
#include "WorkCache.h"
#include "util.h"
#include "nextapp.qpb.h"


/*! A model for the active work sessions

  Also the static interface to anything work related
*/
class WorkSessionsModel : public WorkModelBase
{
    Q_OBJECT
    QML_ELEMENT


    enum Roles {
        UuidRole = Qt::UserRole + 1,
        IconRole,
        ActiveRole,
    };

    Q_PROPERTY(bool canAddNew READ canAddNew NOTIFY canAddNewChanged);
public:

    explicit WorkSessionsModel(QObject *parent = nullptr);

    Q_INVOKABLE void startWork(const QString& actionId);
    Q_INVOKABLE void startWorkSetActive(const QString& actionId);
    Q_INVOKABLE void deleteWork(const QString& actionId);
    Q_INVOKABLE bool isActive(const QString& sessionId) const;
    Q_INVOKABLE bool isStarted(const QString& sessionId) const;
    Q_INVOKABLE void pause(const QString& sessionId);
    Q_INVOKABLE void resume(const QString& sessionId);
    Q_INVOKABLE void done(const QString& sessionId);
    Q_INVOKABLE void touch(const QString& sessionId);
    Q_INVOKABLE void finishAction(const QString& sessionId);
    Q_INVOKABLE void addCalendarEvent(const QString& eventId);

    //void start() override;

    static WorkSessionsModel& instance() noexcept {
        assert(instance_ != nullptr);
        return *instance_;
    }

    bool canAddNew() const noexcept {
        return true;
    }

    bool actionIsInSessionList(const QUuid& actionId) const;

    void fetchIf();

signals:
    void canAddNewChanged();
    void updatedDuration();

private:
    void onDurationChanged(const WorkCache::active_duration_changes_t& changes);
    void fetch();

    static WorkSessionsModel* instance_;
};
