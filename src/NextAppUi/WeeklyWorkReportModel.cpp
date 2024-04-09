
#include <algorithm>
#include <ranges>

#include "WeeklyWorkReportModel.h"
#include "NextAppCore.h"
#include "ServerComm.h"
#include "MainTreeModel.h"

#include "util.h"
#include "logging.h"

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

        if (visible && ! initialized_) {
            start();
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
    request.setStart(time({}));
    request.setKind(nextapp::pb::WorkSummaryKindGadget::WSK_WEEK);

    ServerComm::instance().getDetailedWorkSummary(request, uuid_);
}

void WeeklyWorkReportModel::receivedDetailedWorkSummary(const nextapp::pb::DetailedWorkSummary &summary, const ServerComm::MetaData &meta)
{
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
        QDate qdate{date.year(), date.month(), date.mday()};
        const int day = qdate.dayOfWeek() -1;
        if (day >= 0) {
            ns.duration[day] += item.duration();
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
            switch (section) {
            case 0: return tr("List");
            case 1: return tr("Mon");
            case 2: return tr("Tue");
            case 3: return tr("Wed");
            case 4: return tr("Thu");
            case 5: return tr("Fri");
            case 6: return tr("Sat");
            case 7: return tr("Sat");
            case 8: return tr("SUM");
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
