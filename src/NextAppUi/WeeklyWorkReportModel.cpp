
#include <algorithm>
#include <ranges>

#include <QDateTime>

#include "WeeklyWorkReportModel.h"
#include "DbStore.h"
#include "WorkSessionsModel.h"
#include "WorkCache.h"
#include "NextAppCore.h"
#include "ServerComm.h"
#include "MainTreeModel.h"
#include "ActionCategoriesModel.h"

#include "util.h"
#include "logging.h"

using namespace std;

void WeeklyWorkReportModel::setStartTime(time_t when)
{
    if (startTime_ != when) {
        startTime_ = when;

        const auto st = QDateTime::fromSecsSinceEpoch(when);
        LOG_TRACE_N << "Setting start time to " << st.toString();

        emit startTimeChanged();
        fetch();
    }
}

void WeeklyWorkReportModel::refresh()
{
    fetch();
}

WeeklyWorkReportModel::WeeklyWorkReportModel(QObject *parent)
    : WeeklyWorkReportModel(*NextAppCore::instance(), parent)
{
}

WeeklyWorkReportModel::WeeklyWorkReportModel(RuntimeServices& runtime, QObject *parent)
    : QAbstractTableModel(parent)
    , runtime_{runtime}
{
    LOG_TRACE_N << "Creating WeeklyWorkReportModel " << uuid_.toString();
    if (auto *core = dynamic_cast<NextAppCore*>(&runtime_)) {
        connect(core, &NextAppCore::onlineChanged, this, &WeeklyWorkReportModel::setOnline);
        connect(core, &NextAppCore::allBaseModelsCreated, this, [this]() {
            emit isAvailableChanged();
        });
    }
}

void WeeklyWorkReportModel::setIsVisible(bool visible) {
    LOG_TRACE_N << "Setting visible to " << visible;
    if (visible_ != visible) {
        visible_ = visible;
        emit isVisibleChanged();

        if (visible) {
            if (!initialized_) {
                start();
            } else if (refresh_when_activated_) {
                fetch();
            }
        }
    }
}

void WeeklyWorkReportModel::setOnline(bool online) {
    LOG_TRACE << "Setting online to " << online;
    online_ = online;
    emit isAvailableChanged();
    if (visible_ && initialized_) {
        fetch();
    }
}

void WeeklyWorkReportModel::start()
{
    LOG_TRACE_N << "Starting...";
    initialized_ = true;

    connect(std::addressof(runtime_.serverComm()), &ServerCommAccess::onUpdate,
            this, &WeeklyWorkReportModel::onUpdate);
    connect(&WorkSessionsModel::instance(), &WorkSessionsModel::updatedDuration,
            this, &WeeklyWorkReportModel::onUpdatedDuration);

    fetch();
}

void WeeklyWorkReportModel::fetch()
{
    LOG_TRACE_N << "Fetching...";
    QCoro::connect(fetchFromLocalCache(++fetch_generation_), this, [] {});
}

QCoro::Task<void> WeeklyWorkReportModel::fetchFromLocalCache(uint64_t generation)
{
    // Days are 1 - 7 where Monday is 1
    // We map them so we get the index into our array of day values
    // The leftmost entries in the arrays below are supposed to never be used, as Qt::Monday=1
    // is the lowest valid value.
    static constexpr auto monday_first = std::to_array<char>({0, 0, 1, 2, 3, 4, 5, 6});
    static constexpr auto sunday_first = std::to_array<char>({0, 1, 2, 3, 4, 5, 6, 0});

    const auto days_of_week = runtime_.serverComm().getGlobalSettings().firstDayOfWeekIsMonday() ? monday_first : sunday_first;
    const auto start_date = getFirstDayOfWeek(runtime_.serverComm().getGlobalSettings(),
                                             QDateTime::fromSecsSinceEpoch(startTime_).date());
    const auto end_date = start_date.addDays(7);
    const auto start = start_date.startOfDay();
    const auto end = end_date.startOfDay();

    auto& db = runtime_.db();
    QList<QVariant> params;
    params << static_cast<uint>(nextapp::pb::WorkSession::State::DONE);
    params << start;
    params << end;
    const auto res = co_await db.legacyQuery(R"(SELECT a.node, a.category, DATE(w.start_time), CAST(SUM(w.duration) AS INTEGER)
FROM work_session w
INNER JOIN action a ON a.id = w.action
WHERE w.state = ?
  AND w.start_time >= ?
  AND w.start_time < ?
GROUP BY a.node, a.category, DATE(w.start_time))", &params);
    if (generation != fetch_generation_) {
        co_return;
    }
    if (!res) {
        LOG_ERROR_N << "Failed to fetch weekly work report from local cache: " << res.error();
        co_return;
    }

    std::map<QUuid, NodeSummary> nodes;

    const auto add_duration = [&nodes, &days_of_week](const QUuid& node, const QDate& qdate, uint32_t duration) {
        const int day = qdate.dayOfWeek();
        if (day > 0) {
            assert(day <= Qt::Sunday);
            assert(day >= Qt::Monday);
            const auto wday = days_of_week[day];
            nodes[node].duration[wday] += duration;
        }
    };

    // Build the list of nodes and their totals from completed sessions stored locally.
    for (const auto& row : *res) {
        if (row.size() < 4) {
            LOG_WARN_N << "Ignoring invalid weekly work report row";
            continue;
        }

        auto qdate = row.at(2).toDate();
        if (!qdate.isValid()) {
            qdate = QDate::fromString(row.at(2).toString(), Qt::ISODate);
        }
        if (!qdate.isValid()) {
            LOG_WARN_N << "Ignoring weekly work report row with invalid date: " << row.at(2).toString();
            continue;
        }

        if (const auto group = resolveGroup(row.at(0).toString(), row.at(1).toString())) {
            add_duration(*group, qdate, row.at(3).toUInt());
        }
    }

    // Active sessions are kept fresh in WorkCache; the persisted duration is only updated when saved.
    for (const auto& work : WorkCache::instance()->getActive()) {
        if (!work || !work->start()) {
            continue;
        }

        const auto work_start = QDateTime::fromSecsSinceEpoch(work->start());
        if (work_start < start || work_start >= end) {
            continue;
        }

        if (const auto action = co_await db.query("SELECT node, category FROM action WHERE id = ?", work->action())) {
            if (generation != fetch_generation_) {
                co_return;
            }
            if (!action->rows.empty() && action->rows.front().size() >= 2) {
                const auto& row = action->rows.front();
                if (const auto group = resolveGroup(row.at(0).toString(), row.at(1).toString())) {
                    add_duration(*group, work_start.date(), work->duration());
                }
            }
        } else {
            LOG_WARN_N << "Unable to resolve action node for active work session " << work->id_proto();
        }
    }

    // Resolve the names of the nodes
    for(auto& [uuid, ns] : nodes) {
        ns.name = groupName(uuid);
    }

    std::vector<QUuid> sorted_nodes;
    sorted_nodes.reserve(nodes.size());
    for(const auto& [uuid, ns] : nodes) {
        sorted_nodes.push_back(uuid);
    }
    std::ranges::sort(sorted_nodes, [&nodes](const QUuid& a, const QUuid& b) {
        return nodes[a].name < nodes[b].name;
    });

    beginResetModel();
    ScopedExit scoped{[this] { endResetModel(); }};
    nodes_ = std::move(nodes);
    sorted_nodes_ = std::move(sorted_nodes);
    refresh_when_activated_ = false;
    co_return;
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
        if (update->op() == nextapp::pb::Update::Operation::DELETED
            || update->op() == nextapp::pb::Update::Operation::MOVED
            || grouping_ == CATEGORY) {
            needRefresh();
        }
    }

    if (grouping_ == CATEGORY && update->hasActionCategory()) {
        needRefresh();
    }

    if (grouping_ != CATEGORY && update->hasNode()) {
        needRefresh();
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
                return toHourMin(total(), false);
            }
            return toHourMin(sum(col - 1), false);
        } else {
            if (auto it = nodes_.find(sorted_nodes_[row]); it != nodes_.end()) {
                if (col == 0) {
                    return it->second.name;
                }
                if (col == 8) {
                    return toHourMin(std::accumulate(it->second.duration.begin(), it->second.duration.end(), 0), false);
                }
                return toHourMin(it->second.duration[col - 1], false);
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
            const auto first_header = [this] {
                switch (grouping_) {
                case LIST:
                    return tr("List");
                case PROJECT:
                    return tr("Project");
                case ORGANIZATION:
                    return tr("Organization");
                case PERSON:
                    return tr("Person");
                case CATEGORY:
                    return tr("Category");
                }
                return tr("List");
            };
            if (runtime_.serverComm().getGlobalSettings().firstDayOfWeekIsMonday()) {
                switch (section) {
                case 0: return first_header();
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
                case 0: return first_header();
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
    LOG_TRACE << "Setting week selection to " << when;
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

WeeklyWorkReportModel::Grouping WeeklyWorkReportModel::grouping() const noexcept
{
    return grouping_;
}

void WeeklyWorkReportModel::setGrouping(Grouping grouping)
{
    if (grouping_ == grouping) {
        return;
    }

    grouping_ = grouping;
    emit groupingChanged();
    emit headerDataChanged(Qt::Horizontal, 0, 0);
    needRefresh();
}

std::optional<QUuid> WeeklyWorkReportModel::resolveGroup(const QString& node_id, const QString& category_id) const
{
    if (grouping_ == CATEGORY) {
        return QUuid{category_id};
    }

    auto *tree = MainTreeModel::instance();
    std::unique_ptr<nextapp::pb::Node> node{tree->nodeFromUuid(node_id)};
    if (!node) {
        return std::nullopt;
    }

    if (grouping_ == LIST) {
        return QUuid{node->uuid()};
    }

    nextapp::pb::Node::Kind wanted = nextapp::pb::Node::Kind::PROJECT;
    switch (grouping_) {
    case PROJECT:
        wanted = nextapp::pb::Node::Kind::PROJECT;
        break;
    case ORGANIZATION:
        wanted = nextapp::pb::Node::Kind::ORGANIZATION;
        break;
    case PERSON:
        wanted = nextapp::pb::Node::Kind::PERSON;
        break;
    case LIST:
    case CATEGORY:
        break;
    }

    for (auto current = std::move(node); current; ) {
        if (current->kind() == wanted) {
            return QUuid{current->uuid()};
        }
        if (current->parent().isEmpty()) {
            break;
        }
        current.reset(tree->nodeFromUuid(current->parent()));
    }

    return std::nullopt;
}

QString WeeklyWorkReportModel::groupName(const QUuid& uuid) const
{
    if (grouping_ == CATEGORY) {
        return uuid.isNull()
            ? ActionCategoriesModel::instance().getName(QString{})
            : ActionCategoriesModel::instance().getName(uuid.toString(QUuid::WithoutBraces));
    }

    return MainTreeModel::instance()->nodeNameFromQuuid(uuid, true);
}
