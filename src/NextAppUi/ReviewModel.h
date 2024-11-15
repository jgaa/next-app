#pragma once

#include <stack>
#include <vector>

#include <QObject>
#include <QUuid>

#include "nextapp.qpb.h"
#include "qcorotask.h"

class ReviewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active MEMBER active_ WRITE setActive NOTIFY activeChanged);
    Q_PROPERTY(QUuid currentUuid MEMBER current_uuid_ NOTIFY currentUuidChanged);

public:
    enum class State {
        PENDING,
        FETCHING,
        READY,
        ERROR
    };

    Q_ENUM(State);
    Q_PROPERTY(state MEMBER state_ NOTIFY stateChanged);

    class Item {
        public:
        enum class State {
            PENDING,
            DONE
        };

        Item(const QUuid& actionId, const QUuid& nodeId)
            : id_{id}, node_id_{nodeId} {}

        State state() const noexcept { return state_; }
        bool done() const noexcept { return state_ == State::DONE; }

        //std::shared_ptr<nextapp::pb::ActionInfo> action;
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
            return items_[current_ix_]->id_;
        }

        void reserve(uint size) { items_.reserve(size); }
        void add(const QUuid& id);
        bool setCurrent(uint ix);

    private:
        std::map<QUuid, Item> by_quuid_;
        std::vector<Item *> items_; // Ordered list
        uint current_ix_{0};
    };

    explicit ReviewModel(QObject *parent = nullptr);
    static ReviewModel& instance();

    void setActive(bool active);

    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE bool first();
    Q_INVOKABLE bool back();

signals:
    void activeChanged();
    void currentUuidChanged();
    void stateChanged();
    void modelReset();

private:
    void setCurrentUuid(const QUuid& uuid);
    void setState(State state);

    QCoro::Task<void> fetchIf();

    QUuid current_uuid_;
    std::stack<QUuid> history_; // For back navigation
    bool active_{false}; // True if the review is active in the UI.
    State state_{State::PENDING};
    Cache cache_;
};
