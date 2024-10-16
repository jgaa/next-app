#pragma once

#include "WorkModelBase.h"

/*! A model for work sessions
 *
 *  This model can be used for a small window or all work sessions
 *  for the current user in the database.
 */

class WorkModel: public WorkModelBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

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


    enum FetchWhat {
        TODAY,
        YESTERDAY,
        CURRENT_WEEK,
        LAST_WEEK,
        CURRENT_MONTH,
        LAST_MONTH,
        SELECTED_LIST
    };

    Q_ENUM(FetchWhat)

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


    // Fetch all work sessions for the current user.
    Q_INVOKABLE void fetchSome(FetchWhat what);
    Q_INVOKABLE void setDebug(bool enable) { enable_debug_ = enable;}
    Q_INVOKABLE void setSorting(Sorting sorting);

    Q_INVOKABLE nextapp::pb::WorkSession getSession(const QString& sessionId);
    Q_INVOKABLE nextapp::pb::WorkSession createSession(const QString& actionId, const QString& name);
    Q_INVOKABLE bool update(const nextapp::pb::WorkSession& session);

    explicit WorkModel(QObject *parent = nullptr);

    void fetchMore(const QModelIndex &parent) override;
    bool canFetchMore(const QModelIndex &parent) const override;

    Outcome updateOutcome(nextapp::pb::WorkSession &work);

signals:
    void somethingChanged();
    void activeChanged();

protected:
    void selectedChanged();
    void replace(const nextapp::pb::WorkSessions& sessions);
    void sort();
    QCoro::Task<void> fetchIf();
    QCoro::Task<void> doFetchSome(FetchWhat what, bool firstPage = true);

    Sorting sorting_ = SORT_TOUCHED;
    bool enable_debug_ = false;
    QString currentTreeNode_;
    bool skipped_node_fetch_ = false;
    FetchWhat fetch_what_ = TODAY;
    Pagination pagination_;
    bool exclude_done_ = false; // Needed if we show the current work session list
    bool started_ = false;
};
