#pragma once

#include <set>
#include <deque>


#include <QAbstractListModel>
#include <QStringListModel>
#include <QUuid>
#include <QSettings>
#include <QDate>

#include "qcorotask.h"

#include "nextapp.qpb.h"

// A query to the database gives us the uuid. The action itself is lazily fetched from the cache.
struct ActionData {
    ActionData() = default;
    ActionData(QString uuid)
        : uuid{uuid}{}
    ActionData(QString uuid, std::shared_ptr<nextapp::pb::ActionInfo> action)
        : uuid{uuid}, action{std::move(action)} {}

    QUuid uuid;
    std::shared_ptr<nextapp::pb::ActionInfo> action;
};

class ActionPrx : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:

    enum class State {
        FETCHING,
        VALID,
        FAILED
    };

    Q_ENUM(State)

    Q_PROPERTY(nextapp::pb::Action action READ getAction NOTIFY actionChanged)
    Q_PROPERTY(bool valid READ getValid NOTIFY validChanged)
    Q_PROPERTY(State state MEMBER state_ NOTIFY stateChanged FINAL)

    ActionPrx(QString actionUuid);
    ActionPrx();

    nextapp::pb::Action getAction() const {
        return action_;
    }

    bool getValid() const noexcept {
        return state_ == State::VALID;
    }

signals:
    void actionChanged();
    void validChanged();
    void stateChanged();

private:
    QCoro::Task<void> fetch();

    void setState(State state) {
        if (state != state_) {
            state_ = state;
            emit stateChanged();

            if (state != State::FETCHING) {
                emit validChanged();
            }
        }
    }

    State state_{State::FETCHING};
    QUuid uuid_;
    nextapp::pb::Action action_;
};

class ActionsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    struct Pagination {
        unsigned next_offset{};
        unsigned page = first_page_val_;
        bool more = false;

        void reset() {
            next_offset = 0;
            more = false;
            page = first_page_val_;
        }

        bool isFirstPage() const noexcept {
            return page == first_page_val_;
        }

        bool hasMore() const noexcept {
            return more;
        }

        void increment(unsigned rows = 0) noexcept {
            if (rows == 0) {
                rows = pageSize();
            }
            ++page;
            next_offset += rows;
        }

        unsigned nextOffset() const noexcept{
            return next_offset;
        }

        uint pageSize() const noexcept{
            return page_size_;
        }

    private:
        static constexpr int first_page_val_  = 1;
        uint page_size_ = QSettings{}.value("pagination/page_size", 100).toInt();
    };

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
        ReviewedRole, // dummy
    };

    enum Shortcuts {
        TODAY,
        TOMORROW,
        THIS_WEEKEND,
        NEXT_MONDAY,
        THIS_WEEK,
        AFTER_ONE_WEEK,
        NEXT_WEEK,
        THIS_MONTH,
        NEXT_MONTH,
        THIS_QUARTER,
        NEXT_QUARTER,
        THIS_YEAR,
        NEXT_YEAR,
    };

public:
    enum FetchWhat {
        FW_ACTIVE,
        FW_TODAY,
        FW_TODAY_AND_OVERDUE,
        FW_TOMORROW,
        FW_CURRENT_WEEK,
        FW_NEXT_WEEK,
        FW_CURRENT_MONTH,
        FW_NEXT_MONTH,
        FW_SELECTED_NODE,
        FW_SELECTED_NODE_AND_CHILDREN,
        FW_FAVORITES,
        FW_ON_CALENDAR,
        FW_UNASSIGNED,
        FW_ON_HOLD,
        FW_COMPLETED,
    };

    enum Sorting {
        SORT_DEFAULT,
        SORT_PRI_START_DATE_NAME,
        SORT_PRI_DUE_DATE_NAME,
        SORT_START_DATE_NAME,
        SORT_DUE_DATE_NAME,
        SORT_NAME,
        SORT_CREATED_DATE,
        SORT_CREATED_DATE_DESC,
        SORT_COMPLETED_DATE,
        SORT_COMPLETED_DATE_DESC,
    };

    Q_ENUM(FetchWhat)
    Q_ENUM(Sorting)

    Q_PROPERTY(bool isVisible READ isVisible WRITE setIsVisible NOTIFY isVisibleChanged)
    Q_PROPERTY(FetchWhat mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(Sorting sort MEMBER sort_ WRITE setSort NOTIFY sortChanged FINAL)

    Q_PROPERTY(nextapp::pb::GetActionsFlags flags READ flags WRITE setFlags NOTIFY flagsChanged)

    ActionsModel(QObject *parent = {});

    Q_INVOKABLE void addAction(const nextapp::pb::Action& action);
    Q_INVOKABLE void updateAction(const nextapp::pb::Action& action);
    Q_INVOKABLE void deleteAction(const QString& uuid);
    Q_INVOKABLE nextapp::pb::Action newAction();
    Q_INVOKABLE ActionPrx *getAction(QString uuid);
    Q_INVOKABLE void markActionAsDone(const QString& actionUuid, bool done);
    Q_INVOKABLE void markActionAsFavorite(const QString& actionUuid, bool favorite);
    static Q_INVOKABLE QString toName(nextapp::pb::ActionKindGadget::ActionKind kind);
    static Q_INVOKABLE QString formatWhen(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt);
    static Q_INVOKABLE QString formatDue(const nextapp::pb::Due& due);
    Q_INVOKABLE QString whenListElement(uint64_t when,
                                        nextapp::pb::ActionDueKindGadget::ActionDueKind dt,
                                        nextapp::pb::ActionDueKindGadget::ActionDueKind btn);
    Q_INVOKABLE QStringListModel *getDueSelections(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt);
    Q_INVOKABLE nextapp::pb::Due adjustDue(time_t start, nextapp::pb::ActionDueKindGadget::ActionDueKind kind) const;
    Q_INVOKABLE nextapp::pb::Due setDue(time_t start, time_t until, nextapp::pb::ActionDueKindGadget::ActionDueKind kind) const;
    Q_INVOKABLE nextapp::pb::Due changeDue(int shortcut, const nextapp::pb::Due& fromDue) const;
    Q_INVOKABLE bool moveToNode(const QString& actionUuid, const QString& nodeUuid);
    Q_INVOKABLE void refresh();

   //QCoro::Task<void> fetch(nextapp::pb::GetActionsReq& filter);
    //void receivedActions(const std::shared_ptr<nextapp::pb::Actions>& actions, bool more, bool first);
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);
    void receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions>& sessions);
    void doUpdate(const nextapp::pb::WorkSession& work, nextapp::pb::Update::Operation op);
    FetchWhat mode() const noexcept { return mode_; }
    void setMode(FetchWhat mode);
    bool isVisible() const { return is_visible_; }
    void setIsVisible(bool isVisible);
    nextapp::pb::GetActionsFlags flags() const noexcept { return flags_; }
    void setFlags(nextapp::pb::GetActionsFlags flags);
    void setSort(Sorting sort);

    static nextapp::pb::ActionKindGadget::ActionKind toKind(const nextapp::pb::ActionInfo& action);

    // QAbstractItemModel interface
public:
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void fetchMore(const QModelIndex &parent) override;
    bool canFetchMore(const QModelIndex &parent) const override;

signals:
    void modeChanged();
    void isVisibleChanged();
    void flagsChanged();
    void sortChanged();

private:
    QCoro::Task<void> fetchIf(bool restart = true);
    void selectedChanged();
    void actionChanged(const QUuid &uuid);
    void actionDeleted(const QUuid &uuid);
    void actionAdded(const std::shared_ptr<nextapp::pb::ActionInfo>& ai);

    //QList<nextapp::pb::ActionInfo> actions_;
    std::deque<ActionData> actions_;
    std::set<QUuid> worked_on_;
    FetchWhat mode_ = FW_TODAY_AND_OVERDUE;
    bool is_visible_ = false;
    nextapp::pb::GetActionsFlags flags_{};
    Pagination pagination_;
    Sorting sort_{SORT_DEFAULT};
    QDate current_calendar_date_;
    bool valid_{false};

    // QAbstractItemModel interface
public:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
};
