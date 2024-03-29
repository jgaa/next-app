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


#include "util.h"
#include "nextapp.qpb.h"


class WorkSessionsModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON


    enum Roles {
        UuidRole = Qt::UserRole + 1,
        ActionRole,
        StartRole,
        EndRole,
        DurationRole,
        PausedRole,
        StateRole,
        VersionRole,
        TouchedRole
    };

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
        {
        }

        Session& operator=(const nextapp::pb::WorkSession& session)
        {
            this->session = session;
            this->id = toQuid(session.id_proto());
            return *this;
        }

        QUuid id;
        nextapp::pb::WorkSession session;
    };

    struct id_tag {};
    struct ordered_tag {};
    using sessions_t = boost::multi_index::multi_index_container<
        Session,
        boost::multi_index::indexed_by<
            boost::multi_index::sequenced<
                boost::multi_index::tag<ordered_tag>>,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<id_tag>,
                boost::multi_index::member<Session, QUuid, &Session::id>
                >
            >
        >;

    explicit WorkSessionsModel(QObject *parent = nullptr);

    void start();
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void fetch();
    void receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex & = QModelIndex()) const override {
        return 5;
    }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    inline auto& session_by_id() const { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() const { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_id() { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() { return sessions_.get<ordered_tag>(); }


    sessions_t sessions_;

};
