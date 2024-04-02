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

#include "util.h"
#include "nextapp.qpb.h"


class WorkSessionsModel : public QAbstractTableModel
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
    struct Session {
        Session() = default;
        Session(const Session&) = default;
        Session(Session&&) = default;
        Session& operator=(const Session&) = default;
        Session& operator=(Session&&) = default;

        Session(const nextapp::pb::WorkSession& session)
            : session{session}
            , id{toQuid(session.id_proto())}
            , action{toQuid(session.action())}
        {
        }

        Session& operator=(const nextapp::pb::WorkSession& session)
        {
            this->session = session;
            this->id = toQuid(session.id_proto());
            this->action = toQuid(session.action());
            return *this;
        }

        QUuid id;
        QUuid action;
        nextapp::pb::WorkSession session;
    };

    struct id_tag {};
    struct ordered_tag {};
    struct action_tag {};
    using sessions_t = boost::multi_index::multi_index_container<
        Session,
        boost::multi_index::indexed_by<
            boost::multi_index::sequenced<
                boost::multi_index::tag<ordered_tag>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<id_tag>,
                boost::multi_index::member<Session, QUuid, &Session::id>
                >,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<action_tag>,
                boost::multi_index::member<Session, QUuid, &Session::action>
                >
            >
        >;

    explicit WorkSessionsModel(QObject *parent = nullptr);

    Q_INVOKABLE void startWork(const QString& actionId);
    Q_INVOKABLE bool isActive(const QString& sessionId) const;
    Q_INVOKABLE void pause(const QString& sessionId);
    Q_INVOKABLE void resume(const QString& sessionId);
    Q_INVOKABLE void done(const QString& sessionId);
    Q_INVOKABLE void touch(const QString& sessionId);
    Q_INVOKABLE bool sessionExists(const QString& sessionId);

    void start();

    static WorkSessionsModel& instance() noexcept {
        assert(instance_ != nullptr);
        return *instance_;
    }

    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void fetch();
    void receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);

    bool canAddNew() const noexcept {
        return true;
    }

    bool actionIsInSessionList(const QUuid& actionId) const;

    const nextapp::pb::WorkSession *lookup(const QUuid& id) const;

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex & = QModelIndex()) const override {
        return 5;
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Q_UNUSED(index)
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }

signals:
    void canAddNewChanged();
    void somethingChanged();

private:
    void onTimer();
    void updateSessionsDurations();
    inline auto& session_by_id() const { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() const { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_action() const { return sessions_.get<action_tag>(); }
    inline auto& session_by_id() { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_action() { return sessions_.get<action_tag>(); }


    sessions_t sessions_;
    static WorkSessionsModel* instance_;
    QTimer *timer_ = {};
};
