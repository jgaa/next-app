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
#include <QSettings>

#include <qcorotask.h>

#include "ServerComm.h"
#include "util.h"
#include "nextapp.qpb.h"
#include "nextapp.h"

/*! Base class for Work Session models.
 *
 */

class WorkModelBase: public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isVisible READ isVisible WRITE setIsVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
public:
    enum Roles {
        UuidRole = Qt::UserRole + 1,
        IconRole,
        ActiveRole,
        HasNotesRole,
        FromRole,
        ToRole,
        PauseRole,
        DurationRole,
        NameRole,
        StartedRole,
        ActionRole
    };

    enum Cols {
        FROM,
        TO,
        PAUSE,
        USED,
        NAME
    };

    struct Session {
        Session() = default;
        Session(const Session&) = default;
        Session(Session&&) = default;
        Session& operator=(const Session&) = default;
        Session& operator=(Session&&) = default;

        Session(const std::shared_ptr<nextapp::pb::WorkSession>& session)
            : session{session}
            , id{toQuid(session->id_proto())}
            , action{toQuid(session->action())}
        {
        }

        Session& operator=(const std::shared_ptr<nextapp::pb::WorkSession>& session)
        {
            this->session = session;
            this->id = toQuid(session->id_proto());
            this->action = toQuid(session->action());
            return *this;
        }

        std::shared_ptr<nextapp::pb::WorkSession> session;
        QUuid id;
        QUuid action;
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
                >
            ,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<action_tag>,
                boost::multi_index::member<Session, QUuid, &Session::action>
                >
            >
        >;


    WorkModelBase(QObject *parent);

    Q_INVOKABLE bool sessionExists(const QString& sessionId);
    Q_INVOKABLE nextapp::pb::WorkSession getSession(const QString& sessionId);
    Q_INVOKABLE nextapp::pb::WorkSession createSession(const QString& actionId, const QString& name);
    Q_INVOKABLE bool update(const nextapp::pb::WorkSession& session);

    const nextapp::pb::WorkSession *lookup(const QUuid& id) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override {
        return sessions_.size();
    }
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

    bool isVisible() const { return is_visible_; }
    void setIsVisible(bool isVisible);

    bool active() const noexcept { return is_active_; }
    void setActive(bool active);

    const QUuid& uuid() const noexcept { return uuid_; }

    std::vector<QUuid> getAllActionIds(bool withDuration = false) const;

signals:
    void visibleChanged();
    void activeChanged();

protected:
    inline const auto& session_by_id() const { return sessions_.get<id_tag>(); }
    inline const auto& session_by_ordered() const { return sessions_.get<ordered_tag>(); }
    inline const auto& session_by_action() const { return sessions_.get<action_tag>(); }
    inline auto& session_by_id() { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_action() { return sessions_.get<action_tag>(); }

    sessions_t sessions_;
    bool is_visible_ = false;
    bool is_active_ = false;
    const QUuid uuid_ = QUuid::createUuid();
};
