
#include <algorithm>
#include <ranges>

#include <QDateTime>

#include "WeeklyWorkReportModel.h"
#include "WorkSessionsModel.h"
#include "NextAppCore.h"
#include "ServerComm.h"
#include "MainTreeModel.h"

#include "util.h"
#include "logging.h"

using namespace std;

void WeeklyWorkReportModel::setStartTime(time_t when)
{
    if (startTime_ != when) {
        startTime_ = when;

        const auto st = QDateTime::fromSecsSinceEpoch(when);
        LOG_DEBUG_N << "Setting start time to " << st.toString();

        emit startTimeChanged();
        fetch();
    }
}

WeeklyWorkReportModel::WeeklyWorkReportModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    LOG_DEBUG_N << "Creating WeeklyWorkReportModel " << uuid_.toString();
    connect(NextAppCore::instance(), &NextAppCore::onlineChanged, this, &WeeklyWorkReportModel::setOnline);
    connect(NextAppCore::instance(), &NextAppCore::allBaseModelsCreated, this, [this]() {
        emit isAvailableChanged();
    });
}

void WeeklyWorkReportModel::setIsVisible(bool visible) {
    LOG_DEBUG_N << "Setting visible to " << visible;
    if (visible_ != visible) {
        visible_ = visible;
        emit isVisibleChanged();

        if (visible) {
            if (!initialized_) {
                start();
            } else if (online_ && refresh_when_activated_) {
                fetch();
            }
        }
    }
}

void WeeklyWorkReportModel::setOnline(bool online) {
    LOG_DEBUG << "Setting online to " << online;
    online_ = online;
    emit isAvailableChanged();
    if (online && visible_ && initialized_) {
        fetch();
    }
}

void WeeklyWorkReportModel::start()
{
    LOG_DEBUG_N << "Starting...";
    initialized_ = true;

    connect(&ServerComm::instance(), &ServerComm::receivedDetailedWorkSummary,
            this, &WeeklyWorkReportModel::receivedDetailedWorkSummary);
    connect(&ServerComm::instance(), &ServerComm::onUpdate,
            this, &WeeklyWorkReportModel::onUpdate);
    connect(&WorkSessionsModel::instance(), &WorkSessionsModel::updatedDuration,
            this, &WeeklyWorkReportModel::onUpdatedDuration);

    if (online_) {
        fetch();
    }
}

void WeeklyWorkReportModel::fetch()
{
    LOG_DEBUG_N << "Fetching...";
    if (!online_) {
        LOG_WARN_N << "Not online";
        return;
    }
    nextapp::pb::DetailedWorkSummaryRequest request;
    request.setStart(startTime_);
    request.setKind(nextapp::pb::WorkSummaryKindGadget::WSK_WEEK);

    ServerComm::instance().getDetailedWorkSummary(request, uuid_);
}

void WeeklyWorkReportModel::receivedDetailedWorkSummary(const nextapp::pb::DetailedWorkSummary &summary, const ServerComm::MetaData &meta)
{
    // Days are 1 - 7 where Monday is 1
    // We map them so we get the index into our array of day values
    // The leftmost entries in the arrays below are supposed to never be used, as Qt::Monday=1
    // is the lowest valid value.
    static constexpr auto monday_first = std::to_array<char>({0, 0, 1, 2, 3, 4, 5, 6});
    static constexpr auto sunday_first = std::to_array<char>({0, 1, 2, 3, 4, 5, 6, 0});

    const auto days_of_week = ServerComm::instance().getGlobalSettings().firstDayOfWeekIsMonday() ? monday_first : sunday_first;

    if (meta.requester != uuid_) {
        return;
    }

    LOG_DEBUG_N << "Received detailed work summary";

    beginResetModel();
    ScopedExit scoped{[this] { endResetModel(); }};
    nodes_.clear();
    sorted_nodes_.clear();

    // Build the list of nodes and their totals
    for(const auto& item : summary.items()) {
        auto& ns = nodes_[toQuid(item.node())];
        auto &date = item.date();
        QDate qdate{date.year(), date.month() + 1, date.mday()};
        const int day = qdate.dayOfWeek();
        if (day > 0) {
            assert(day <= Qt::Sunday);
            assert(day >= Qt::Monday);
            const auto wday = days_of_week[day];
            ns.duration[wday] += item.duration();
        }
    }

    // TODO: Build a list of actions so we can handle update events
    //          form the work sessions and update the totals in real time.

    // Resolve the names of the nodes
    for(auto& [uuid, ns] : nodes_) {
        ns.name = MainTreeModel::instance()->nodeNameFromQuuid(uuid, true);
    }

    // Create a sorted list of nodes
    sorted_nodes_.reserve(nodes_.size());
    for(const auto& [uuid, ns] : nodes_) {
        sorted_nodes_.push_back(uuid);
    }
    // Sort the list
    std::ranges::sort(sorted_nodes_, [this](const QUuid& a, const QUuid& b) {
        return nodes_[a].name < nodes_[b].name;
    });
}

int WeeklyWorkReportModel::rowCount(const QModelIndex &parent) const
{
    return sorted_nodes_.size() + 1;
}

int WeeklyWorkReportModel::columnCount(const QModelIndex &parent) const
{
    return 9;
}

void WeeklyWorkReportModel::onUpdate(const std::shared_ptr<nextapp::pb::Update>& update) {
    if (update->hasWork() || update->hasUserGlobalSettings()) {

        // TODO: Optimize so we can use the update logic in the WorkSessionsModel
        needRefresh();
    }

    if (update->hasAction()) {
        if (update->op() == nextapp::pb::Update::Operation::DELETED || update->op() == nextapp::pb::Update::Operation::MOVED) {
            needRefresh();
        }
    }
}

void WeeklyWorkReportModel::onUpdatedDuration()
{
    needRefresh();
}

void WeeklyWorkReportModel::needRefresh()
{
    if (isVisible()) {
        fetch();
    } else {
        refresh_when_activated_ = true;
    }
}

QVariant WeeklyWorkReportModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 || row > nodes_.size()) {
        return {};
    }

    const auto col = index.column();
    if (col < 0 || col > 8) {
        return {};
    }

    auto sum = [this] (unsigned day) {
        int sum = 0;
        for(const auto& [uuid, ns] : nodes_) {
            sum += ns.duration[day];
        }

        return sum;
    };

    auto total = [this] {
        int sum = 0;
        for(const auto& [uuid, ns] : nodes_) {
            sum += std::accumulate(ns.duration.begin(), ns.duration.end(), 0);
        }

        return sum;
    };

    if (role == Qt::DisplayRole) {
        if (row == nodes_.size()) {
            if (col == 0) {
                return tr("Total");
            }
            if (col == 8) {
                return toHourMin(total());
            }
            return toHourMin(sum(col - 1));
        } else {
            if (auto it = nodes_.find(sorted_nodes_[row]); it != nodes_.end()) {
                if (col == 0) {
                    return it->second.name;
                }
                if (col == 8) {
                    return toHourMin(std::accumulate(it->second.duration.begin(), it->second.duration.end(), 0));
                }
                return toHourMin(it->second.duration[col - 1]);
            }
        }
    }

    if (role == SummaryRole) {
        return row == nodes_.size();
    }

    return {};
}

QVariant WeeklyWorkReportModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (ServerComm::instance().getGlobalSettings().firstDayOfWeekIsMonday()) {
                switch (section) {
                case 0: return tr("List");
                case 1: return tr("Mon");
                case 2: return tr("Tue");
                case 3: return tr("Wed");
                case 4: return tr("Thu");
                case 5: return tr("Fri");
                case 6: return tr("Sat");
                case 7: return tr("Sun");
                case 8: return tr("SUM");
                }
            } else  { // sunday is the first day of the week
                switch (section) {
                case 0: return tr("List");
                case 1: return tr("Sun");
                case 2: return tr("Mon");
                case 3: return tr("Tue");
                case 4: return tr("Wed");
                case 5: return tr("Thu");
                case 6: return tr("Fri");
                case 7: return tr("Sat");
                case 8: return tr("SUM");
                }
            }
        }
    }
    return {};
}

QHash<int, QByteArray> WeeklyWorkReportModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {SummaryRole, "summary"}
    };
}

WeeklyWorkReportModel::WeekSelection WeeklyWorkReportModel::weekSelection() const
{
    return week_selection_;
}

void WeeklyWorkReportModel::setWeekSelection(WeekSelection when)
{
    LOG_DEBUG << "Setting week selection to " << when;
    if (week_selection_ == when) {
        return;
    }
    week_selection_ = when;
    if (week_selection_ == THIS_WEEK) {
        setStartTime(time({}));
    } else if (week_selection_ == LAST_WEEK) {
        setStartTime(time({}) - 7 * 24 * 60 * 60);
    } else {
        return ; // Assume that the model has set a specific time
    }
    emit weekSelectionChanged();
}

