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

#include "WorkModel.h"
#include "util.h"
#include "nextapp.qpb.h"


/*! A model for the active work sessions

  Also the static interface to anything work related
*/
class WorkSessionsModel : public WorkModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON


    enum Roles {
        UuidRole = Qt::UserRole + 1,
        IconRole,
        ActiveRole,
        // ActionRole,
        // StartRole,
        // EndRole,
        // DurationRole,
        // PausedRole,
        // StateRole,
        // VersionRole,
        // TouchedRole
    };

    Q_PROPERTY(bool canAddNew READ canAddNew NOTIFY canAddNewChanged);
public:

    explicit WorkSessionsModel(QObject *parent = nullptr);

    Q_INVOKABLE void startWork(const QString& actionId);
    Q_INVOKABLE void deleteWork(const QString& actionId);
    Q_INVOKABLE bool isActive(const QString& sessionId) const;
    Q_INVOKABLE void pause(const QString& sessionId);
    Q_INVOKABLE void resume(const QString& sessionId);
    Q_INVOKABLE void done(const QString& sessionId);
    Q_INVOKABLE void touch(const QString& sessionId);
    Q_INVOKABLE void finishAction(const QString& sessionId);

    void start() override;

    static WorkSessionsModel& instance() noexcept {
        assert(instance_ != nullptr);
        return *instance_;
    }

    void fetch();

    bool canAddNew() const noexcept {
        return true;
    }

    bool actionIsInSessionList(const QUuid& actionId) const;

signals:
    void canAddNewChanged();
    void updatedDuration();

private:
    void onTimer();
    void updateSessionsDurations();

    static WorkSessionsModel* instance_;
    QTimer *timer_ = {};
};
