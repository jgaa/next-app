#pragma once

#include <stack>
#include <vector>

#include <QAbstractListModel>
#include <QUuid>

#include "nextapp.qpb.h"
#include "qcorotask.h"

/*! This cache provices a lazy model for the review of actions.
 *
 *  It will show a window over the actions for the current review tree item, and
 *  it has a concept of "current review" action.
 *
 *  The cache is filled with the uuids of the actions to review. The
 *  actions themselves are fetched lazily from the cache.
 */

class ReviewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum class State {
        PENDING,
        FETCHING,
        FILLING,
        READY,
        ERROR
    };

    // Same as ActionsModels as we feed the same QML view
    enum Roles {
        NameRole = Qt::UserRole + 1,
        UuidRole,
        PriorityRole,
        StatusRole,
        NodeRole,
        CreatedDateRole,
        DueTypeRole,
        DueByTimeRole,
        CompletedRole,
        CompletedTimeRole,
        SectionRole,
        SectionNameRole,
        DueRole,
        FavoriteRole,
        HasWorkSessionRole,
        ListNameRole,
        CategoryRole,
        ReviewedRole,
    };

    Q_ENUM(State)

    Q_PROPERTY(bool active MEMBER active_ WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString actionUuid MEMBER action_uuid_ NOTIFY actionUuidChanged)
    Q_PROPERTY(QString nodeUuid MEMBER node_uuid_ NOTIFY nodeUuidChanged)
    Q_PROPERTY(int selected MEMBER selected_ NOTIFY selectedChanged)
    Q_PROPERTY(State state MEMBER state_ NOTIFY stateChanged)
    Q_PROPERTY(nextapp::pb::Action action READ action NOTIFY actionChanged)

    class Item {
        public:
        enum class State {
            PENDING,
            DONE
        };

        Item(const QUuid& actionId, const QUuid& nodeId)
            : id_{actionId}, node_id_{nodeId} {}

        State state() const noexcept { return state_; }
        bool done() const noexcept { return state_ == State::DONE; }
        const QUuid& uuid() const noexcept { return id_; }
        void markDone() noexcept { state_ = State::DONE; }

        std::shared_ptr<nextapp::pb::ActionInfo> action;
        const QUuid id_;
        QUuid node_id_;
        State state_{State::PENDING};
    };

    class Cache {
    public:
        Cache() = default;

        uint size() const noexcept { return items_.size(); }
        bool valid() const noexcept { return !items_.empty(); }

        uint currentIx() const noexcept {
            assert(valid());
            return current_ix_;
        }

        const QUuid& currentId() noexcept {
            assert(!items_.empty());
            assert(current_ix_ < items_.size());
            return items_[current_ix_].id_;
        }

        void reserve(uint size) { items_.reserve(size); }
        void clear() {
            by_quuid_.clear();
            items_.clear();
            current_ix_ = 0;
            current_window_ = {};
            node_changed_ = false;
        }
        void add(const QUuid& actionId, const QUuid& nodeId);
        bool setCurrent(uint ix);

        auto& currentWindow() noexcept {
            return current_window_;
        }

        const auto& currentWindow() const noexcept {
            return current_window_;
        }

        bool nodeChanged() const noexcept {
            return node_changed_;
        }

        auto& at(int ix) {
            return items_.at(ix);
        }

        auto& current() {
            assert(!items_.empty());
            return at(currentIx());
        }

        bool empty() const noexcept {
            return items_.empty();
        }

        int pos(const QUuid& uuid) const;

        /*! Get the row (in the current window) for the given index.
         *
         *  @param ix Index in the cache.
         *  @return Row index in the current window or -1 if the index is outside the window.
         */
        int rowAtIx(int ix) const;

        int startOfWindowIx() const noexcept {
            return start_of_window_ix_;
        }

    private:
        std::map<QUuid, uint /* index */> by_quuid_;
        std::vector<Item> items_; // Ordered list
        std::vector<Item *> current_window_{};
        uint current_ix_{0};
        uint start_of_window_ix_{0};
        bool node_changed_{};
    };

    explicit ReviewModel(QObject *parent = nullptr);
    static ReviewModel& instance();

    void setActive(bool active);

    Q_INVOKABLE bool next();
    Q_INVOKABLE bool nextList();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool first();
    Q_INVOKABLE bool back();
    Q_INVOKABLE void selectByUuid(const QString& uuid);

    // QAbstractItemModel interface
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setSelected(int ix);
    void setNodeUuid(const QString& uuid);
    nextapp::pb::Action action();

signals:
    void activeChanged();
    void nodeUuidChanged();
    void actionUuidChanged();
    void stateChanged();
    void modelReset();
    void selectedChanged();
    void actionChanged();

private:
    int findNext(bool forward, int from = -1, bool nextList = false);
    bool moveToIx(uint ix);
    void setActionUuid(const QUuid& uuid);
    void setState(State state);
    QCoro::Task<void> changeNode();
    QCoro::Task<void> fetchIf();
    QCoro::Task<void> fetchAction();
    void signalChanged(int row);
    //void markAsReviewed()

    QString node_uuid_;
    QString action_uuid_;
    std::shared_ptr<nextapp::pb::Action> action_;
    int selected_{-1};
    std::stack<QUuid> history_; // For back navigation
    bool active_{false}; // True if the review is active in the UI.
    State state_{State::PENDING};
    Cache cache_;
};
