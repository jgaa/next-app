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

#include "ServerComm.h"
#include "util.h"
#include "nextapp.qpb.h"

/*! A model for work sessions
 *
 *  This model can be used for a small window or all work sessions
 *  for the current user in the database.
 */

class WorkModel: public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isVisible READ isVisible WRITE setIsVisible NOTIFY isVisibleChanged)

    struct Pagination {
        int page_size = 0;
        time_t prev = 0;
        size_t page = first_page_val_;
        bool more = false;

        void reset() {
            page_size = QSettings{}.value("pagination/page_size", 100).toInt();
            prev = 0;
            page = first_page_val_;
            more = false;
        }

        int pageSize() const noexcept {
            return std::max(page_size, 20);
        }

        void increment() noexcept {
            ++page;
        };

        bool isFirstPage() const noexcept {
            return page == first_page_val_;
        }

        bool hasMore() const noexcept {
            return more;
        }
    private:
        static constexpr int first_page_val_  = 1;
    };
public:
    enum Roles {
        UuidRole = Qt::UserRole + 1,
        IconRole,
        ActiveRole,
    };

    enum Cols {
        FROM,
        TO,
        PAUSE,
        USED,
        NAME
    };

    enum FetchWhat {
        TODAY,
        YESTERDAY,
        CURRENT_WEEK,
        LAST_WEEK,
        CURRENT_MONTH,
        LAST_MONTH,
        SELECTED_LIST
    };

    enum Sorting {
        SORT_TOUCHED,
        SORT_START_TIME,
        SORT_START_TIME_DESC
    };

    struct Outcome {
        bool duration = false;
        bool paused = false;
        bool end = false;
        bool start = false;
        bool name = false;

        bool changed() const noexcept {
            return duration || paused || end || start || name;
        }
    };

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
                >
            ,
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<action_tag>,
                boost::multi_index::member<Session, QUuid, &Session::action>
                >
            >
        >;

    // Fetch all work sessions for the current user.
    Q_INVOKABLE void fetchAll();
    Q_INVOKABLE void fetchSome(FetchWhat what);
    Q_INVOKABLE void setDebug(bool enable) { enable_debug_ = enable;}
    Q_INVOKABLE void setSorting(Sorting sorting);

    void doFetchSome(FetchWhat what, bool firstPage = true);


    // If we need to start the model from QML
    Q_INVOKABLE void doStart();

    bool isVisible() const { return is_visible_; }
    void setIsVisible(bool isVisible);


    explicit WorkModel(QObject *parent = nullptr);

    virtual void start();

    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

    // These are the active sessions.
    void receivedCurrentWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);

    // These are the sessions that have been received from the server in response to a request.
    void receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions, const ServerComm::MetaData meta);

    const nextapp::pb::WorkSession *lookup(const QUuid& id) const;

    // QAbstractItemModel interface
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
    void fetchMore(const QModelIndex &parent) override;
    bool canFetchMore(const QModelIndex &parent) const override;

    Outcome updateOutcome(nextapp::pb::WorkSession &work);

    const QUuid& uuid() const noexcept { return uuid_; }

signals:
    void somethingChanged();
    void isVisibleChanged();


protected:
    void selectedChanged();
    void replace(const nextapp::pb::WorkSessions& sessions);
    void sort();

    inline auto& session_by_id() const { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() const { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_action() const { return sessions_.get<action_tag>(); }
    inline auto& session_by_id() { return sessions_.get<id_tag>(); }
    inline auto& session_by_ordered() { return sessions_.get<ordered_tag>(); }
    inline auto& session_by_action() { return sessions_.get<action_tag>(); }

    sessions_t sessions_;
    const QUuid uuid_ = QUuid::createUuid();
    std::once_flag start_once_;
    Sorting sorting_ = SORT_TOUCHED;
    bool enable_debug_ = false;
    QString currentTreeNode_;
    bool is_visible_ = false;
    bool skipped_node_fetch_ = false;
    FetchWhat fetch_what_ = TODAY;
    Pagination pagination_;

};
