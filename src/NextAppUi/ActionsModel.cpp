
#include <memory>
#include <QDate>
#include <QUuid>
#include <QTimeZone>
#include <QDateTime>
#include <QSettings>
#include <QMimeData>
#include <QIODevice>

#include "ActionsModel.h"
#include "ServerComm.h"
#include "MainTreeModel.h"
//#include "WorkSessionsModel.h"
#include "ActionInfoCache.h"
#include "ActionsOnCurrentCalendar.h"
#include "ActionsWorkedOnTodayCache.h"
#include "ActionCategoriesModel.h"
#include "NextAppCore.h"

#include "logging.h"
#include "util.h"
#include "DbStore.h"

using namespace std;
using namespace std::string_literals;
using namespace nextapp;
using a_status_t = nextapp::pb::ActionStatusGadget::ActionStatus;

namespace {

QDateTime before(QDateTime when) {
    return when.addSecs(-1);
}

template <typename T, typename U>
int findCurrentRow(const T& list, const U& key) {
    const QUuid id{key};

    auto it = std::ranges::find_if(list, [&id](const auto& a) {
        return a.uuid == id;
    });

    if (it != list.end()) {
        auto row = ranges::distance(list.begin(), it);
        return row;
    }

    return -1;
}



static constexpr auto quarters = to_array<int8_t>({1, 1, 1, 4, 4, 4, 7, 7, 7, 10, 10, 10});

template <ProtoMessage T>
pb::ActionKindGadget::ActionKind toKindT(const T& action) {
    switch(action.status()) {
        case a_status_t::ACTIVE:
            if (action.hasDue() && action.due().hasDue() && action.due().due()) {
                const auto now = QDateTime::currentDateTime();
                const auto due = QDateTime::fromSecsSinceEpoch(action.due().due());
                const auto today = now.date();
                auto due_date = due.date(); //.addDays((due.time().hour() == 0 && due.time().second() == 0 && due.time().minute() == 0) ? -1 : 0);

                if (due_date < today) {
                    return pb::ActionKindGadget::ActionKind::AC_OVERDUE;
                }

                if (action.due().kind() != pb::ActionDueKindGadget::ActionDueKind::DATETIME) {
                    if (due_date == today) {
                        return pb::ActionKindGadget::ActionKind::AC_TODAY;
                    }
                };

                switch(action.due().kind()) {
                    case pb::ActionDueKindGadget::ActionDueKind::DATETIME:
                        if (action.due().due() < now.toSecsSinceEpoch()) {
                            return pb::ActionKindGadget::ActionKind::AC_OVERDUE;
                        }
                        if (due_date == today) {
                            return pb::ActionKindGadget::ActionKind::AC_TODAY;
                        }
                        break;
                    case pb::ActionDueKindGadget::ActionDueKind::DATE:
                        break;
                    case pb::ActionDueKindGadget::ActionDueKind::WEEK: {
                        const auto week = getFirstDayOfWeek();
                        if (due_date >= week && due_date < week.addDays(7)) {
                            return pb::ActionKindGadget::ActionKind::AC_ACTIVE;
                        }
                    }
                    case pb::ActionDueKindGadget::ActionDueKind::MONTH: {
                        if (today.year() == due_date.year() && today.month() == due_date.month()) {
                            return pb::ActionKindGadget::ActionKind::AC_ACTIVE;
                        }
                    }
                    case pb::ActionDueKindGadget::ActionDueKind::QUARTER:
                        if (today.year() == due_date.year()) {
                            const auto quarter = quarters.at(due_date.month() - 1);
                            if (today.month() >= quarter && today.month() < quarter + 3) {
                                return pb::ActionKindGadget::ActionKind::AC_ACTIVE;
                            }
                        }
                        break;
                    case pb::ActionDueKindGadget::ActionDueKind::YEAR:
                        if (today.year() == due_date.year()) {
                            return pb::ActionKindGadget::ActionKind::AC_ACTIVE;
                        }
                        break;
                    case pb::ActionDueKindGadget::ActionDueKind::UNSET:
                        return pb::ActionKindGadget::ActionKind::AC_UNSCHEDULED;

                    case pb::ActionDueKindGadget::ActionDueKind::SPAN_HOURS:
                    case pb::ActionDueKindGadget::ActionDueKind::SPAN_DAYS:
                        break; // Not handled here...
                } // action.kind()

                if (action.due().hasStart()) {
                    const auto start_day = QDateTime::fromSecsSinceEpoch(action.due().start()).date();
                    if (start_day > today) {
                        return pb::ActionKindGadget::ActionKind::AC_UPCOMING;
                    }
                    return pb::ActionKindGadget::ActionKind::AC_ACTIVE;
                }

                if (due_date > today) {
                    return pb::ActionKindGadget::ActionKind::AC_UPCOMING;
                }
            }
            return pb::ActionKindGadget::ActionKind::AC_UNSET;
        case a_status_t::DONE:
            return pb::ActionKindGadget::ActionKind::AC_DONE;
        case a_status_t::ONHOLD:
            return pb::ActionKindGadget::ActionKind::AC_ON_HOLD;
        case a_status_t::DELETED:
            return pb::ActionKindGadget::ActionKind::AC_UNSET;
    }

    assert(false); // We should probably not get here...
    return pb::ActionKindGadget::ActionKind::AC_UNSET;
}

template <typename T>
concept ActionType = std::is_same_v<T, pb::ActionInfo> || std::is_same_v<T, pb::Action>;

template <ActionType T, ActionType U>
int comparePriName(const T& left, const U& right) {
    if (left.priority() != right.priority()) {
        return left.priority() - right.priority();
    }

    return left.name().compare(right.name(), Qt::CaseInsensitive);
}

int compare(const pb::Due& left, const pb::Due& right) {
    if (left.kind() != right.kind()) {
        return static_cast<int>(left.kind()) - static_cast<int>(right.kind());
    }

    if (left.hasStart() && right.hasStart()) {
        return static_cast<int>(left.start()) - static_cast<int>(right.start());
    }

    if (left.hasStart() || right.hasStart()) {
        return left.hasStart() ? -1 : 1;
    }

    if (left.hasDue() && right.hasDue()) {
        return left.due() - right.due();
    }

    if (left.hasDue() || right.hasDue()) {
        return left.hasDue() ? -1 : 1;
    }

    return 0;
}

template <ActionType T, ActionType U>
int64_t compare(const T& left, const U& right) {
    if (left.kind() != right.kind()) {
        // Lowest is most significalt
        return left.kind() - right.kind();
    }

    assert(left.kind() == right.kind()) ;
    switch(left.kind()) {
    case pb::ActionKindGadget::ActionKind::AC_OVERDUE:
    case pb::ActionKindGadget::ActionKind::AC_UNSCHEDULED:
    case pb::ActionKindGadget::ActionKind::AC_UNSET:
        return comparePriName(left, right);
    case pb::ActionKindGadget::ActionKind::AC_TODAY:
        if (left.due().due() && right.due().due()) {
            return left.due().due() - right.due().due();
        }
        return left.name().compare(right.name(), Qt::CaseInsensitive);
    case pb::ActionKindGadget::ActionKind::AC_ACTIVE:
    case pb::ActionKindGadget::ActionKind::AC_UPCOMING:
        if (const auto cmp = compare(left.due(), right.due())) {
            return cmp;
        }
        return comparePriName(left, right);
    case pb::ActionKindGadget::ActionKind::AC_DONE:
        if (left.completedTime() != right.completedTime()) {
            return left.completedTime() - right.completedTime();
        }
        return left.name().compare(right.name(), Qt::CaseInsensitive);
    }

    return 0;
}

template <ActionType T, ActionType U>
bool comparePred(const T& left, const U& right) {
    return compare(left, right) < 0LL;
}

template <ActionType T, ActionType U>
int findInsertRow(const T& action, const QList<U>& list) {
    int row = 0;
    // assume that list is already sorted

    bool prev_was_target = false;
    const U *prev = {};
    for(const auto& a: list) {
        if (comparePred(action, a)) {
            break;
        }
        prev_was_target = a.id_proto() == action.id_proto();
        ++row;
    }

    if (prev_was_target) {
        --row; // Assume exactely the same sorting order
        assert(row >= 0);
    }
    return row;
}

} // anon ns

ActionsModel::ActionsModel(QObject *parent)
{
    flags_.setActive(true);
    flags_.setDone(false);
    flags_.setUnscheduled(true);
    flags_.setUpcoming(true);

    connect(std::addressof(ServerComm::instance()), &ServerComm::onUpdate, this, &ActionsModel::onUpdate);
    connect(std::addressof(ServerComm::instance()), &ServerComm::receivedCurrentWorkSessions, this, &ActionsModel::receivedWorkSessions);
    connect(MainTreeModel::instance(), &MainTreeModel::selectedChanged, this, &ActionsModel::selectedChanged);
    connect(ActionInfoCache::instance(), &ActionInfoCache::actionChanged, this, &ActionsModel::actionChanged);
    connect(ActionInfoCache::instance(), &ActionInfoCache::actionDeleted, this, &ActionsModel::actionDeleted);
    connect(ActionInfoCache::instance(), &ActionInfoCache::actionAdded, this, &ActionsModel::actionAdded);
    connect(ActionInfoCache::instance(), &ActionInfoCache::cacheReloaded, this, &ActionsModel::cacheReloaded);

    connect(ActionsOnCurrentCalendar::instance(), &ActionsOnCurrentCalendar::modelReset, this, [this] {
        if (valid_) {
            refreshVisibleItems();
        }
    });

    connect(ActionsOnCurrentCalendar::instance(), &ActionsOnCurrentCalendar::actionAdded, this, [this](const QUuid& action) {
        if (valid_) {
            if (const auto row = findCurrentRow(actions_, action) ; row < 0) {
                const auto cix = index(row);
                emit dataChanged(cix, cix);
            }
        }
    });

    connect(ActionsOnCurrentCalendar::instance(), &ActionsOnCurrentCalendar::actionRemoved, this, [this](const QUuid& action) {
        if (valid_) {
            if (const auto row = findCurrentRow(actions_, action) ; row >= 0) {
                const auto cix = index(row);
                emit dataChanged(cix, cix);
            }
        }
    });

    connect(NextAppCore::instance(), &NextAppCore::propertyChanged, this, [this](const QString& name) {
        if (mode_ == FetchWhat::FW_ON_CALENDAR && name == "primaryForActionList") {
            QMetaObject::invokeMethod(this, [this] {
                fetchIf();
            }, Qt::QueuedConnection);
        }
    });

    connect(ActionsWorkedOnTodayCache::instance(), &ActionsWorkedOnTodayCache::modelReset, this, [this] {
        if (valid_) {
            refreshVisibleItems();
        }
    });

    connect(NextAppCore::instance(), &NextAppCore::currentDateChanged, this, [this]() {
        fetchIf();
    });
}

void ActionsModel::addAction(const nextapp::pb::Action &action)
{
    ServerComm::instance().addAction(action);
}

void ActionsModel::updateAction(const nextapp::pb::Action &action)
{
    const auto& p = action.dynamicPriority();
    if (p.hasPriority()) {
        auto pri = p.priority();
    }
    if (p.hasUrgencyImportance()) {
        auto& ui = p.urgencyImportance();
        auto urgency = ui.urgency();
        auto importance = ui.importance();
    }
    ServerComm::instance().updateAction(action);
}

void ActionsModel::deleteAction(const QString &uuid)
{
    ServerComm::instance().deleteAction(uuid);
}

nextapp::pb::Action ActionsModel::newAction()
{
    nextapp::pb::Action action;
    nextapp::pb::Priority p;
    p.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_NORMAL);
    action.setDynamicPriority(p);
    return action;
}

ActionPrx *ActionsModel::getAction(QString uuid)
{
    if (uuid.isEmpty()) {
        return new ActionPrx{};
    }

    if (uuid == "today") {
        auto prx = make_unique<ActionPrx>();
        auto& action = prx->getActionRef();
        action.setDue(changeDue(TODAY, {}));
        return prx.release();
    }

    if (uuid == "tomorrow") {
        auto prx = make_unique<ActionPrx>();
        auto& action = prx->getActionRef();
        action.setDue(changeDue(TOMORROW, {}));
        return prx.release();
    }

    auto prx = make_unique<ActionPrx>(uuid);
    return prx.release();
}

void ActionsModel::markActionAsDone(const QString &actionUuid, bool done)
{
    ServerComm::instance().markActionAsDone(actionUuid, done);
}

void ActionsModel::markActionAsFavorite(const QString &actionUuid, bool favorite)
{
    ServerComm::instance().markActionAsFavorite(actionUuid, favorite);
}

void ActionsModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    const auto op = update->op();
    if (update->hasWork()) {
        return doUpdate(update->work(), op);
    }
}

void ActionsModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
{
    worked_on_.clear();
    for(const auto& session: sessions->sessions()) {
        worked_on_.insert(toQuid(session.action()));
    }
}

// Send a data changed event for the action referenced in the work session.
void ActionsModel::doUpdate(const nextapp::pb::WorkSession &work, nextapp::pb::Update::Operation op)
{
    static const QList<int> roles = {HasWorkSessionRole};

    const auto& action_id = work.action();
    if (const auto currentRow = findCurrentRow(actions_, action_id) ; currentRow >=0 ) {
        const auto cix = index(currentRow);
        LOG_TRACE << "Emitting data change on " << action_id << " for work session update on row " << currentRow;

        if (op == nextapp::pb::Update::Operation::DELETED || work.hasEnd()) {
            worked_on_.erase(toQuid(action_id));
        } else {
            worked_on_.insert(toQuid(action_id));
        }

        emit dataChanged(cix, cix, roles);
    }
}

void ActionsModel::setMode(FetchWhat mode)
{
    if (mode_ != mode) {
        LOG_DEBUG_N << "Mode changed to " << mode;
        mode_ = mode;
        emit modeChanged();
        fetchIf(true);
    }
}

void ActionsModel::setIsVisible(bool isVisible)
{
    LOG_DEBUG_N << "Visible is now: " << isVisible;
    if (is_visible_ != isVisible) {
        LOG_DEBUG_N << "Visible changed to " << isVisible;
        is_visible_ = isVisible;
        emit isVisibleChanged();
        fetchIf(true);
    }
}

void ActionsModel::setFlags(nextapp::pb::GetActionsFlags flags)
{
    if (flags_ != flags) {
        LOG_DEBUG_N << "Flags changed ";
        flags_ = flags;
        emit flagsChanged();
        fetchIf(true);
    }
}

void ActionsModel::setSort(Sorting sort)
{
    if (sort_ != sort) {
        sort_ = sort;
        emit sortChanged();
        fetchIf(true);
    }
}

void ActionsModel::setMatch(QString match)
{
    if (match_ != match) {
        match_ = match;
        emit matchChanged();
        if (filters_enabled_) {
            fetchIf(true);
        }
    }
}

void ActionsModel::setFiltersEnabled(bool match_enabled)
{
    if (filters_enabled_ != match_enabled) {
        filters_enabled_ = match_enabled;
        emit filtersEnabledChanged();
        fetchIf(true);
    }
}

nextapp::pb::ActionKindGadget::ActionKind ActionsModel::toKind(const nextapp::pb::ActionInfo &action)
{
    return toKindT(action);
}

QString ActionsModel::toName(nextapp::pb::ActionKindGadget::ActionKind kind)
{
    using namespace nextapp::pb::ActionKindGadget;
    switch(kind) {
    case ActionKind::AC_UNSET:
        return tr("Unset");
    case ActionKind::AC_OVERDUE:
        return tr("Overdue");
    case ActionKind::AC_TODAY:
        return tr("Today");
    case ActionKind::AC_ACTIVE:
        return tr("Active");
    case ActionKind::AC_UPCOMING:
        return tr("Upcoming");
    case ActionKind::AC_UNSCHEDULED:
        return tr("No due time set");
    case ActionKind::AC_DONE:
        return tr("Done");
    case ActionKind::AC_ON_HOLD:
        return tr("On Hold");
    }
    assert(false);
    return {};
}

QString ActionsModel::formatWhen(time_t from, time_t to, nextapp::pb::ActionDueKindGadget::ActionDueKind dt)
{
    using namespace nextapp::pb::ActionDueKindGadget;

    if (!from) {
        return tr("No due time set");
    }

    // Helper QDateTime objects in local/system time
    QDateTime fromDT = QDateTime::fromSecsSinceEpoch(from, QTimeZone::systemTimeZone());
    QDate fromDate = fromDT.date();

    QDateTime toDT   = QDateTime::fromSecsSinceEpoch(to, QTimeZone::systemTimeZone());
    QDate toDate     = toDT.date();

    QDateTime currentDT = QDateTime::currentDateTime();
    QDate currentDate   = currentDT.date();

    // -- Helper lambdas for reusability --

    // A helper to differentiate between 'today', 'tomorrow', or show a date label
    auto datename = [&](const QDate &candidate, bool verbose = true) {
        if (candidate == currentDate) {
            return tr("Today");
        }
        else if (candidate == currentDate.addDays(1)) {
            return tr("Tomorrow");
        }
        else if (verbose) {
            // E.g. "Mon 2024-01-31"
            static const array<QString, 8> days = {
                tr("Day"), tr("Mon"), tr("Tue"), tr("Wed"), tr("Thu"), tr("Fri"), tr("Sat"), tr("Sun")
            };

            const auto dow = candidate.dayOfWeek();
            QStringView day_name;
            if (dow >= 0 && dow <= 7) {
                day_name = days.at(dow);
            } else {
                day_name = days.at(0);
            };

            return QString("%1 %2")
                .arg(day_name)
                .arg(candidate.toString(QStringLiteral("yyyy-MM-dd")));
        }
        // Non-verbose: just "2024-01-31"
        return candidate.toString(QStringLiteral("yyyy-MM-dd"));
    };

    // A helper that returns a "short vs. phrase" form depending on whether
    // the fromDate is today or not
    auto select = [&](const QString &formatted,
                      const QString &phrase,
                      const QString &shortp,
                      const QString &prefix = {}) -> QString
    {
        if (fromDate == currentDate) {
            return phrase;
        }
        // e.g. "Week #43 2023"
        if (!prefix.isEmpty()) {
            return shortp + QStringLiteral(" ") + prefix + formatted;
        }
        else {
            return shortp + QStringLiteral(" ") + formatted;
        }
    };

    // -- Switch on the enum to pick the correct output --

    switch(dt)
    {
    case ActionDueKind::DATETIME:
        // e.g. "Time 2024-01-31 13:45"
        return tr("Time") + QStringLiteral(" ")
               + fromDT.toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    case ActionDueKind::DATE:
        // e.g. "Today" or "Day 2024-01-31"
        return datename(fromDate);

    case ActionDueKind::WEEK:
    {
        // QString formatted = fromDT.toString(QStringLiteral("ww yyyy"));
        // return select(formatted, tr("This week"), tr("Week"), QStringLiteral("#"));
        int isoYear = 0;
        int isoWeek = fromDate.weekNumber(&isoYear);
        // isoWeek is typically 1..53

        // Build e.g. "43 2024" or "This week"
        QString formatted = QStringLiteral("%1 %2")
                                .arg(QString::number(isoWeek))
                                .arg(QString::number(isoYear));

        // Then use the same select(...) logic to decide whether to use "This week" or "Week #43 2024"
        return select(formatted,
                      tr("This week"),
                      tr("Week"),
                      QStringLiteral("#"));
    }

    case ActionDueKind::MONTH:
    {
        // e.g. "Month Jan 2024" or "This month"
        // We'll approximate {:%b %Y} => "MMM yyyy"
        QString formatted = fromDT.toString(QStringLiteral("MMM yyyy"));
        return select(formatted, tr("This month"), tr("Month"));
    }

    case ActionDueKind::QUARTER:
    {
        // We must determine the quarter ourselves
        // E.g.: if month = 1..3 => Q1, 4..6 => Q2, etc.
        int month = fromDate.month();
        int quarter = (month - 1) / 3 + 1;   // 1..4
        // e.g. "Quarter Q1 2024" or "This Quarter"
        QString formatted = QString("Q%1 %2")
                                .arg(QString::number(quarter))
                                .arg(fromDT.toString(QStringLiteral("yyyy")));
        return select(formatted, tr("This Quarter"), tr("Quarter"), QString());
    }

    case ActionDueKind::YEAR:
    {
        // e.g. "Year 2024" or "This year"
        QString formatted = fromDT.toString(QStringLiteral("yyyy"));
        return select(formatted, tr("This year"), tr("Year"));
    }

    case ActionDueKind::UNSET:
        // If the enum signals an unset time, just return
        return tr("No due time set");

    case ActionDueKind::SPAN_HOURS:
    {
        // e.g. "2024-01-31 13:45 - 16:10"
        // Or "Today 13:45 - 16:10" if fromDate is today (via datename)
        QString dateLabel = datename(fromDate, /*verbose=*/false);
        return dateLabel + QStringLiteral(" ")
               + fromDT.toString(QStringLiteral("HH:mm"))
               + QStringLiteral(" - ")
               + toDT.toString(QStringLiteral("HH:mm"));
    }

    case ActionDueKind::SPAN_DAYS:
    {
        // e.g. "2024-01-31 - 2024-02-02"
        // Or "Today - Tomorrow"
        QString fromLabel = datename(fromDate, /*verbose=*/false);
        QString toLabel   = datename(toDate, /*verbose=*/false);
        return QString("%1 - %2").arg(fromLabel, toLabel);
    }

    default:
        // Fallback if new enum values appear
        return {};
    }
}

QString ActionsModel::formatDue(const nextapp::pb::Due &due)
{
    auto from = due.hasStart() ? due.start() : 0;
    auto to = due.hasDue() ? due.due() : 0;
    return formatWhen(from, to, due.kind());
}

QString ActionsModel::whenListElement(uint64_t when,
                                      nextapp::pb::ActionDueKindGadget::ActionDueKind dt,
                                      nextapp::pb::ActionDueKindGadget::ActionDueKind btn)
{
    using namespace nextapp::pb::ActionDueKindGadget;

    //if (when == 0 || dt > btn) {
    switch(btn) {
    case ActionDueKind::DATETIME:
        return tr("Date and Time");
    case ActionDueKind::DATE:
        return tr("Date");
    case ActionDueKind::WEEK:
        return tr("Week");
    case ActionDueKind::MONTH:
        return tr("Month");
    case ActionDueKind::QUARTER:
        return tr("Quarter");
    case ActionDueKind::YEAR:
        return tr("Year");
    case ActionDueKind::UNSET:
        return tr("No due time set");
    case ActionDueKind::SPAN_HOURS:
        return tr("Spans hours");
    case ActionDueKind::SPAN_DAYS:
        return tr("Spans days");
    default:
        ;
    }

    return {};

    //return formatWhen(when, btn);
}

QStringListModel *ActionsModel::getDueSelections(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt)
{
    using namespace nextapp::pb::ActionDueKindGadget;
    auto model = new QStringListModel{};

    QStringList list;
    list << whenListElement(when, dt, ActionDueKind::DATETIME);
    list << whenListElement(when, dt, ActionDueKind::SPAN_HOURS);
    list << whenListElement(when, dt, ActionDueKind::DATE);
    list << whenListElement(when, dt, ActionDueKind::SPAN_DAYS);
    list << whenListElement(when, dt, ActionDueKind::WEEK);
    list << whenListElement(when, dt, ActionDueKind::MONTH);
    list << whenListElement(when, dt, ActionDueKind::QUARTER);
    list << whenListElement(when, dt, ActionDueKind::YEAR);
    list << whenListElement(when, dt, ActionDueKind::UNSET);

    model->setStringList(list);
    return model;
}

pb::Due ActionsModel::getEmptyDue() const
{
    pb::Due due;
    due.setKind(pb::ActionDueKindGadget::ActionDueKind::UNSET);
    return due;
}

pb::Due ActionsModel::setDue(time_t start, time_t until, nextapp::pb::ActionDueKindGadget::ActionDueKind kind) const
{
    assert(start <= until);
    if (start > until) {
        start = until;
    }
    pb::Due due;
    due.setKind(kind);
    const auto gs = ServerComm::instance().getGlobalSettings();

    switch(kind) {
    case pb::ActionDueKindGadget::ActionDueKind::SPAN_HOURS:
        due.setStart(start);
        due.setDue(until);
        break;
    case pb::ActionDueKindGadget::ActionDueKind::SPAN_DAYS: {
        auto date = QDateTime::fromSecsSinceEpoch(start).date();
        start = QDateTime{date, QTime{0, 0}}.toSecsSinceEpoch();
        due.setStart(start);
        date = QDateTime::fromSecsSinceEpoch(until).date();
        until = QDateTime{date, QTime{23, 59}}.toSecsSinceEpoch();
        due.setDue(until);
        }
        break;
    default:
        assert(false && "Invalid due kind");
        return {}; // don't crash
    }

    LOG_TRACE << "Setting due: from="
              << QDateTime::fromSecsSinceEpoch(start).toLocalTime().toString()
              << ", to="
              << QDateTime::fromSecsSinceEpoch(until).toLocalTime().toString();

    return due;
}

// #if !defined(ANDROID) && !defined(__APPLE__)
// auto timeZoneOffset(const std::chrono::time_zone *tz, const auto& tp) {

//     const auto ts_offset = tz->get_info(tp).offset;
//     const auto offset = ts_offset.count();
//     return offset;
// }
// #endif

pb::Due ActionsModel::adjustDue(time_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind kind) const
{
    pb::Due due;
    due.setKind(kind);
    const auto gs = ServerComm::instance().getGlobalSettings();

    time_t start = 0;
    time_t end = 0;

    QLocale locale = QLocale::system();
    const Qt::DayOfWeek firstDayOfWeek = gs.firstDayOfWeekIsMonday() ? Qt::Monday : Qt::Sunday;

    // How many days to subtract from any weekday to get to the start of the week
    static constexpr auto sunday_first = to_array<int8_t>({1, 2, 3, 4, 5, 6, 0});
    static constexpr auto monday_first = to_array<int8_t>({0, 1, 2, 3, 4, 5, 6});
    const auto days_offset = firstDayOfWeek == Qt::Sunday ? sunday_first : monday_first;

    auto ts = QTimeZone{gs.timeZone().toLocal8Bit()};
    if (ts.isValid()) {
        LOG_TRACE << "Timezone " << ts.id() << " is valid.";
    } else {
        LOG_WARN << "Timezone " << gs.timeZone() << " is invalid. Using system timezone.";
        ts = QTimeZone::systemTimeZone();
    }
    auto qt_start = QDateTime::fromSecsSinceEpoch(when);
    qt_start.setTimeZone(ts);
    //qt_start.setTimeSpec(Qt::LocalTime);
    const auto tz_name = qt_start.timeZoneAbbreviation();
    due.setTimezone(tz_name.toUtf8().constData());
    auto d_start = qt_start.date().startOfDay();

    switch(kind) {
    case pb::ActionDueKindGadget::ActionDueKind::DATETIME:
        start = when;
        end = when;
        break;
    case pb::ActionDueKindGadget::ActionDueKind::DATE: {
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addDays(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch() - 1;
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::WEEK: {
        const auto day_in_week = qt_start.date().dayOfWeek();
        assert(day_in_week != 0);
        const auto first_day_of_that_week = getFirstDayOfWeek(qt_start.date());
        //const auto start = first_day_of_that_week.startOfDay().toSecsSinceEpoch();
        const auto d_start = first_day_of_that_week.startOfDay();
        const auto d_end = d_start.addDays(7);
        start = d_start.toSecsSinceEpoch();
        end = d_end.toSecsSinceEpoch() - 1;

        // auto day_in_week = qt_start.date().dayOfWeek();
        // assert(day_in_week != 0);
        // // Jump back in time to the start of the week
        // auto offset = days_offset.at(day_in_week - 1) * -1;
        // auto w_start = d_start.addDays(offset);
        // start = w_start.toSecsSinceEpoch();
        // auto d_end = w_start.addDays(7);
        // d_end.setTime(QTime(0,0));
        // end = d_end.toSecsSinceEpoch() - 1;
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::MONTH: {
        auto m_start = d_start;
        m_start.setDate(QDate{d_start.date().year(), d_start.date().month(), 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addMonths(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch() - 1;
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::QUARTER: {
        auto m_start = d_start;
        auto qmonth = quarters.at(d_start.date().month() - 1);
        m_start.setDate(QDate{d_start.date().year(), qmonth, 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addMonths(3);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch() -1;
    }
    case pb::ActionDueKindGadget::ActionDueKind::YEAR: {
        auto y_start = d_start;
        y_start.setDate(QDate{d_start.date().year(), 1, 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addYears(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch() -1;
    }
    break;

    default:
    break;
    }

    due.setStart(start);
    due.setDue(end);

    auto qfrom = QDateTime::fromMSecsSinceEpoch(start * 1000);

    LOG_TRACE << "Setting due: from=" << qfrom.toLocalTime().toString() << ", to=" << QDateTime::fromMSecsSinceEpoch(end * 1000).toLocalTime().toString();

    return due;
}

pb::Due ActionsModel::changeDue(int shortcut, const nextapp::pb::Due &fromDue) const
{


    auto start = QDateTime::currentDateTime().toSecsSinceEpoch();
    auto end = start;
    auto kind = fromDue.kind();

    if (fromDue.hasStart()) {
        start = fromDue.start();
    }
    if (fromDue.hasDue()) {
        end = fromDue.due();
    }

    auto qt_start = QDateTime::fromSecsSinceEpoch(start);
    auto qt_end = QDateTime::fromSecsSinceEpoch(end);
    auto zone = QTimeZone::systemTimeZone();
    qt_start.setTimeZone(zone);
    //qt_start.setTimeSpec(Qt::LocalTime);
    qt_end.setTimeZone(zone);
    //qt_end.setTimeSpec(Qt::LocalTime);

    auto today = QDate::currentDate();

    switch(shortcut) {
    case TODAY:
        start = today.startOfDay().toSecsSinceEpoch();
        end = today.addDays(1).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        break;
    case TOMORROW:
        start = today.addDays(1).startOfDay().toSecsSinceEpoch();
        end = today.addDays(2).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        break;
    case THIS_WEEKEND: {
        auto day_in_week = today.dayOfWeek();
        auto offset = Qt::DayOfWeek::Saturday - day_in_week;
        auto w_start = today.addDays(offset);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(2).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        }
        break;
    case NEXT_MONDAY: {
        auto day_in_week = today.dayOfWeek();
        auto offset = (Qt::DayOfWeek::Sunday - day_in_week) + 1;
        auto w_start = today.addDays(offset);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(1).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        }
        break;
    case THIS_WEEK: {
        auto day_in_week = today.dayOfWeek();
        auto w_start = today.addDays((day_in_week  -1) * -1);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(7).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::WEEK;
        }
        break;
    case AFTER_ONE_WEEK: {
        auto s_date = today.addDays(7);
        auto e_date = today.addDays(8);
        if (kind == pb::ActionDueKindGadget::ActionDueKind::DATETIME) {
            auto s_time = QDateTime{s_date, qt_start.time()};
            auto e_time = QDateTime{s_date, qt_start.time()};
            start = s_time.toSecsSinceEpoch();
            end = e_time.toSecsSinceEpoch();
        } else {
            kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
            start = s_date.startOfDay().toSecsSinceEpoch();
            end = e_date.startOfDay().toSecsSinceEpoch() -1;
        }
        }
        break;
    case NEXT_WEEK: {
        auto day_in_week = today.dayOfWeek();
        auto w_start = today.addDays((day_in_week  -1) * -1);
        start = w_start.addDays(7).startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(14).startOfDay().toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::WEEK;
        }
        break;
    case THIS_MONTH: {
        auto m_start = QDateTime{QDate{today.year(), today.month(), 1}, QTime{0, 0}};
        start = m_start.toSecsSinceEpoch();
        end = m_start.addMonths(1).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::MONTH;
        }
        break;
    case NEXT_MONTH: {
        auto m_start = QDateTime{QDate{today.year(), today.month(), 1}, QTime{0, 0}};
        start = m_start.addMonths(1).toSecsSinceEpoch();
        end = m_start.addMonths(2).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::MONTH;
        }
        break;
    case THIS_QUARTER: {
        auto qmonth = quarters.at(today.month() - 1);
        auto m_start = QDateTime{QDate{today.year(), qmonth, 1}, QTime{0, 0}};
        start = m_start.toSecsSinceEpoch();
        end = m_start.addMonths(3).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::QUARTER;
        }
        break;
    case NEXT_QUARTER: {
        auto qmonth = quarters.at(today.month() - 1);
        auto m_start = QDateTime{QDate{today.year(), qmonth, 1}, QTime{0, 0}};
        start = m_start.addMonths(3).toSecsSinceEpoch();
        end = m_start.addMonths(6).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::QUARTER;
        }
        break;
    case THIS_YEAR: {
        auto y_start = QDateTime{QDate{today.year(), 1, 1}, QTime{0, 0}};
        start = y_start.toSecsSinceEpoch();
        end = y_start.addYears(1).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::YEAR;
        }
        break;
    case NEXT_YEAR: {
        auto y_start = QDateTime{QDate{today.year(), 1, 1}, QTime{0, 0}};
        start = y_start.addYears(1).toSecsSinceEpoch();
        end = y_start.addYears(2).toSecsSinceEpoch() -1;
        kind = pb::ActionDueKindGadget::ActionDueKind::YEAR;
        }
        break;
    }

    pb::Due due;
    due.setStart(start);
    due.setDue(end);
    due.setKind(kind);
    return due;
}

bool ActionsModel::moveToNode(const QString &actionUuid, const QString &nodeUuid)
{
    auto row = findCurrentRow(actions_, actionUuid);
    if (row >= 0) {
        auto& ai= actions_.at(row);
        assert(ai.action);
        assert(ai.action->id_proto() == actionUuid);
        if (ai.action->node() == nodeUuid) {
            LOG_DEBUG_N << "Cannot move to the same node";
            return false;
        }

        ServerComm::instance().moveAction(actionUuid, nodeUuid);

        return true;
    }
    return false;
}

void ActionsModel::refresh()
{
    fetchIf(true);
}

void ActionsModel::batchChangeDue(const nextapp::pb::Due &due, const QStringList &actions)
{
    pb::UpdateActionsReq req;
    req.setDue(due);

    batchUpdateActions(req, actions);
}

void ActionsModel::batchChangeCategory(const QString &category, const QStringList &actions)
{
    pb::UpdateActionsReq req;
    common::Uuid uuid;
    uuid.setUuid(category);
    req.setCategory(uuid);
    batchUpdateActions(req, actions);
}

void ActionsModel::batchChangePriority(int priority, const QStringList &actions)
{
    if (priority < 0 || priority > 7) {
        LOG_WARN_N << "Invalid priority " << priority;
        return;
    }
    const auto pri = static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(priority);
    pb::UpdateActionsReq req;
    req.setPriority(pri);
    batchUpdateActions(req, actions);
}

void ActionsModel::batchChangeDifficulty(int difficulty, const QStringList &actions)
{
    if (difficulty < 0 || difficulty > 5) {
        LOG_WARN_N << "Invalid difficulty " << difficulty;
        return;
    }
    const auto diff = static_cast<nextapp::pb::ActionDifficultyGadget::ActionDifficulty>(difficulty);
    pb::UpdateActionsReq req;
    req.setDifficulty(diff);
    batchUpdateActions(req, actions);
}

void ActionsModel::batchDelete(const QStringList &actions)
{
    for(const auto& id: actions) {
        deleteAction(id);
    }
}

QStringList ActionsModel::tagsToList(const QString &tags)
{
    static const QRegularExpression regex{"[\\s,;]+"};
    QStringList list = tags.split(regex, Qt::SkipEmptyParts);

    // Remove whitespace around each tag and eliminate duplicates
    QSet<QString> uniqueTags;
    for (QString &tag : list) {
        uniqueTags.insert(tag.trimmed());
    }

    // Convert set back to list and sort
    QStringList sortedList = QStringList(uniqueTags.begin(), uniqueTags.end());
    sortedList.sort();
    return sortedList;
}

QString ActionsModel::tagsToString(const QStringList &tags, bool addHash)
{
    if (addHash) {
        QString rval;
        for (auto& tag : tags) {
            if (tag.isEmpty()) {
                continue;
            }
            if (!rval.isEmpty()) {
                rval += " ";
            }
            rval += "#" + tag;
        }
        return rval;
    }
    return tags.join(" ");
}


pb::UrgencyImportance ActionsModel::setUrgencyImportance(double urgency, double importance)
{
    pb::UrgencyImportance ui;
    ui.setUrgency(urgency);
    ui.setImportance(importance);
    return ui;
}

int ActionsModel::rowCount(const QModelIndex &parent) const
{
    return valid_ ? actions_.size() : 0;
}

QVariant ActionsModel::data(const QModelIndex &index, int role) const
{
    if (!valid_ || !index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= actions_.size()) {
        return {};
    }

    auto& data = actions_.at(row);
    if (!data.action) {
        LOG_WARN_N << "Missing action at row " << row;
        return {};
    }
    assert(data.action);

    const auto& action = *data.action;

    switch(role) {
    case NameRole:
        return action.name();
    case UuidRole:
        return action.id_proto();
    case PriorityKindRole:
        if (action.dynamicPriority().hasPriority()) {
            return PriorityKind::PkPriority;
        }
        if (action.dynamicPriority().hasUrgencyImportance()) {
            return PriorityKind::PkDynamic;
        }
        assert(false);
        return {};
    case PriorityRole:
        if (action.dynamicPriority().hasPriority()) {
            return static_cast<int>(action.dynamicPriority().priority());
        }
        return {};
    case ImportanceRole:
        if (action.dynamicPriority().hasUrgencyImportance()) {
            return static_cast<int>(action.dynamicPriority().urgencyImportance().importance());
        }
        return {};
    case UrgencyRole:
        if (action.dynamicPriority().hasUrgencyImportance()) {
            return static_cast<int>(action.dynamicPriority().urgencyImportance().urgency());
        }
        return {};
    case ScoreRole:
        return action.score();
    case StatusRole:
        return static_cast<uint>(action.status());
    case NodeRole:
        return action.node();
    case CreatedDateRole:
        return QDate{action.createdDate().year(), action.createdDate().month(), action.createdDate().mday()}.toString();
    case DueTypeRole:
        return static_cast<uint>(action.due().kind());
    case DueByTimeRole:
        return static_cast<quint64>(action.due().due());
    case CompletedRole:
        return action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DONE;
    case CompletedTimeRole:
        if (action.completedTime()) {
            return QDateTime::fromSecsSinceEpoch(action.completedTime());
        }
        return {};
    case SectionKindRole:
        return static_cast<uint>(toKind(action));
    case SectionNameRole:
        if (sort_ == SORT_CATEGORY_NAME) {
            return ActionCategoriesModel::instance().getName(action.category());
        }
        return toName(toKind(action));
    case DueRole:
        return formatDue(action.due());
    case FavoriteRole:
        return action.favorite();
    case HasWorkSessionRole:
        return worked_on_.contains(toQuid(action.id_proto()));
    case ListNameRole:
        return MainTreeModel::instance()->nodeNameFromUuid(action.node(), true);
    case CategoryRole:
        return action.category();
    case ReviewedRole:
        return false;
    case OnCalendarRole:
        return ActionsOnCurrentCalendar::instance()->contains(data.uuid);
    case WorkedOnTodayRole:
        return ActionsWorkedOnTodayCache::instance()->contains(data.uuid);
    case ScoreColorRole: {
        const QColor c = ActionInfoCache::getScoreColor(action.score());
        return c.name();
        }
    case TagsRole:
        return tagsToString(action.tags(), true);
    }

    return {};
}

QVariant ActionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (section == 0 && orientation == Qt::Horizontal) {
        switch(role) {
        case NameRole:
            return "Name";
        case UuidRole:
            return "Id";
        case PriorityRole:
            return "Priority";
        case StatusRole:
            return "Status";
        case NodeRole:
            return "Node";
        case CreatedDateRole:
            return "CreatedDate";
        case DueTypeRole:
            return "DueType";
        case DueByTimeRole:
            return "DueBy";
        case CompletedTimeRole:
            return "CompletedTime";
        case CompletedRole:
            return "Done";
        case SectionKindRole:
            return "SectionKind";
        case SectionNameRole:
            return "Section";
        case DueRole:
            return "Due";
        case CategoryRole:
            return "Category";
        }
    }

    return {};
}

QHash<int, QByteArray> ActionsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[UuidRole] = "uuid";
    roles[PriorityRole] = "priority";
    roles[StatusRole] = "status";
    roles[NodeRole] = "node";
    roles[CreatedDateRole] = "createdDate";
    roles[DueTypeRole] = "dueType";
    roles[DueByTimeRole] = "dueBy";
    roles[CompletedRole] = "done";
    roles[CompletedTimeRole] = "completedTime";
    roles[SectionKindRole] = "sectionKind";
    roles[SectionNameRole] = "section";
    roles[DueRole] = "due";
    roles[FavoriteRole] = "favorite";
    roles[HasWorkSessionRole] = "hasWorkSession";
    roles[ListNameRole] = "listName";
    roles[CategoryRole] = "category";
    roles[ReviewedRole] = "reviewed";
    roles[OnCalendarRole] = "onCalendar";
    roles[WorkedOnTodayRole] = "workedOnToday";
    roles[ImportanceRole] = "importance";
    roles[UrgencyRole] = "urgency";
    roles[ScoreRole] = "score";
    roles[PriorityKindRole] = "priorityKind";
    roles[ScoreColorRole] = "scoreColor";
    roles[TagsRole] = "tags";
    return roles;
}

void ActionsModel::fetchMore(const QModelIndex &parent)
{
    LOG_DEBUG_N  << "more=" << pagination_.more << ", offset =" << pagination_.nextOffset()
                << ", page = " << pagination_.page;

    if (pagination_.hasMore()) {
        fetchIf(false);
    }
}

bool ActionsModel::canFetchMore(const QModelIndex &parent) const
{
    LOG_DEBUG_N  << "more=" << pagination_.more << ", offset =" << pagination_.nextOffset()
              << ", page = " << pagination_.page;
    return valid_ ? (isVisible() && pagination_.hasMore()) : false;
}

pair<QString, QStringList> extractTagsAndCleanText(const QString &input)
{
    // Regular expression to match tags like #tag
    static const QRegularExpression tagRegex(R"(#(\w+))");

    QStringList tags;
    QString result = input;

    // Find all tag matches
    QRegularExpressionMatchIterator it = tagRegex.globalMatch(input);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        const auto tag = match.captured(1);
        if (!tag.isEmpty()) {
            tags << tag;  // Just the tag name without '#'
        }
        result.replace(match.captured(0), "");  // Remove full match (#tag) from text
    }

    result = result.simplified(); // Trim and remove extra spaces

    return {result, tags};
}

QCoro::Task<void> ActionsModel::fetchIf(bool restart)
{
    static constexpr auto sorting = to_array<string_view>({
        "a.due_by_time, a.score DESC, a.name",
        "a.score DESC, a.start_time, a.name",
        "a.score DESC, a.due_by_time, a.name",
        "a.start_time, a.name",
        "a.due_by_time, a.name",
        "acat.name, a.name",
        "a.name",
        "a.created_date",
        "a.created_date DESC",
        "a.completed_time",
        "a.completed_time DESC",
    });

    static constexpr string_view sort_completed_prefix = "is_completed DESC, a.completed_time DESC,";
    static constexpr string_view sort_has_due_prefix = "has_due, ";
    static constexpr string_view join_action_name = " LEFT JOIN action_category acat ON a.category = acat.id ";
    static constexpr string_view join_tags= " LEFT JOIN tags t ON t.action = a.id ";

    auto join_action_name_if = [&] {
        if (sort_ == SORT_CATEGORY_NAME) {
            return join_action_name;
        }
        return std::string_view{};
    };

    const auto sort_completed = sort_ < SORT_COMPLETED_DATE ? sort_completed_prefix : "";

    bool started_reset = false;

    ScopedExit reset_model([&] {
        if (started_reset) {
            endResetModel();
        }
    });

    auto start_reset = [&] {
        if (!started_reset) {
            beginResetModel();
            started_reset = true;
        }
    };

    if (restart) {
        pagination_.reset();
        actions_.clear();
        valid_ = false;
        start_reset();
    }

    if (!isVisible() ) {
        LOG_DEBUG_N << "Not visible. Skipping fetch. connected="
                    << ServerComm::instance().connected() << ", visible=" << isVisible();
        co_return;
    }

    LOG_DEBUG_N << "Fetching actions. Mode is " << mode_ << " sorting is " << sort_;

    // Set pagination to the request
    const uint offset = pagination_.nextOffset();
    auto date = QDate::currentDate();
    DbStore::param_t params;
    string sql;
    string match;
    QString match_text;
    QStringList match_tags;
    if (!match_.isEmpty() && filters_enabled_) {
        auto [text, tags] = extractTagsAndCleanText(match_);

        if (!text.isEmpty()) {
            match = " AND a.name LIKE ?";
            match_text = "%" + text + "%";
        }

        if (!tags.isEmpty()) {
            match_tags = std::move(tags);
        }
    }

    auto pushMatch = [&] {
        if (!match_text.isEmpty()) {
            params.push_back(match_text);
        }
    };

    auto join_tags_if = [&] -> string {
        if (!match_tags.isEmpty()) {
            //return join_tags;
            string rval{join_tags};
            if (match_tags.size() == 1) {
                rval += " AND t.name=? ";
                params << match_tags.front();
                return rval;
            }

            rval += " AND t.name IN (";
            int num_tags = 0;
            for(const auto tag: match_tags) {
                if (++num_tags > 1) {
                    rval += ",";
                }
                rval += "?";
                params << tag;
            }
            rval += ") ";
            return rval;
        }
        return {};
    };


    // // TODO: Remove this when we consistently end a time-span at the end of a day in stad of the start of the next day.
    // const QDateTime end_of_today = QDateTime{date.addDays(1), QTime{0, 0}}.addSecs(-1);
    // const QDateTime start_of_today = QDateTime{date, QTime{0, 0}};

    switch(mode_) {
    case FetchWhat::FW_ACTIVE:
        // Fetch all active actions with start-time before tomorrow.
        sql = nextapp::format(R"(SELECT a.id FROM action a {} {} WHERE a.status={} AND a.start_time <= ? {}
ORDER BY {}
LIMIT {} OFFSET {})",
                    join_action_name_if(),
                    join_tags_if(),
                    static_cast<uint>(a_status_t::ACTIVE),
                    match,
                    sorting.at(sort_),
                    pagination_.pageSize(), pagination_.nextOffset());
        params.push_back(date.addDays(1).startOfDay());
        pushMatch();
        break;

    case FetchWhat::FW_TODAY:
        // Only show todays actions and actions completed today.
        sql = nextapp::format(R"(SELECT a.id,
CASE WHEN a.completed_time IS NULL THEN 1 ELSE 0 END AS is_completed FROM action a {}
WHERE (a.status={} {} AND a.due_by_time >= ? AND a.due_by_time < ?)
OR a.completed_time >= ? AND a.completed_time < ?
ORDER BY {}
LIMIT {} OFFSET {})",
                    join_action_name_if(),
                    static_cast<uint>(a_status_t::ACTIVE),
                              match,
                    nextapp::format("{}{}", sort_completed, sorting.at(sort_)),
                    pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        params << date.startOfDay();
        params << date.startOfDay().addDays(1);
        params << date.startOfDay();
        params << date.startOfDay().addDays(1);
        break;

    case FetchWhat::FW_TODAY_AND_OVERDUE:
        sql = nextapp::format(R"(SELECT a.id,
CASE WHEN a.completed_time IS NULL THEN 1 ELSE 0 END AS is_completed FROM action a {}
WHERE (a.status={} {} AND a.due_by_time < ?)
OR a.completed_time >= ? AND a.completed_time <= ?
ORDER BY {}
LIMIT {} OFFSET {})",
                    join_action_name_if(),
                    static_cast<uint>(a_status_t::ACTIVE),
                    match,
                    nextapp::format("{}{}", sort_completed, sorting.at(sort_)),
                    pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        params.push_back(date.addDays(1).startOfDay());
        params << date.startOfDay();
        params << date.startOfDay().addDays(1);
        break;

    case FetchWhat::FW_TOMORROW:
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} AND a.due_by_time >= ? AND a.due_by_time < ?
ORDER BY {}
LIMIT {} OFFSET {})",
                    join_action_name_if(),
                    static_cast<uint>(a_status_t::ACTIVE),
                    match,
                    sorting.at(sort_),
                    pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        params << date.startOfDay().addDays(1);
        params << date.startOfDay().addDays(2);
        break;

    case FetchWhat::FW_CURRENT_WEEK: {
        // Fetch all active actions with start-time before tomorrow.
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} AND a.due_by_time >= ? AND a.due_by_time < ?
ORDER BY {}
LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        auto start_of_week = getFirstDayOfWeek(date);
        params.push_back(start_of_week.startOfDay());
        params.push_back(start_of_week.addDays(7).startOfDay());
    } break;

    case FetchWhat::FW_NEXT_WEEK: {
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} AND a.due_by_time >= ? AND a.due_by_time < ?
ORDER BY {}
LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        auto start_of_week = getFirstDayOfWeek(date.addDays(7));
        params.push_back(start_of_week.startOfDay());
        params.push_back(start_of_week.addDays(7).startOfDay());
    } break;

    case FetchWhat::FW_CURRENT_MONTH: {
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} AND a.due_by_time >= ? AND a.due_by_time < ?
ORDER BY {}
LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        auto start_of_month = QDate{date.year(), date.month(), 1};
        params.push_back(start_of_month.startOfDay());
        params.push_back(start_of_month.addMonths(1).startOfDay());
    } break;

    case FetchWhat::FW_NEXT_MONTH: {
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} AND a.due_by_time >= ? AND a.due_by_time <= ?
ORDER BY {}
LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        auto start_of_month = QDate{date.year(), date.month(), 1};
        params.push_back(start_of_month.addMonths(1).startOfDay());
        params.push_back(start_of_month.addMonths(2).startOfDay());
    } break;

    case FetchWhat::FW_SELECTED_NODE:
        sql = nextapp::format(R"(SELECT a.id,
CASE WHEN a.due_by_time IS NULL THEN 1 ELSE 0 END AS has_due
FROM action a {}
JOIN node n ON a.node = n.uuid
WHERE a.status={} {} AND n.uuid = ?
ORDER BY {}
LIMIT {} OFFSET {})",
                    join_action_name_if(),
                    static_cast<uint>(a_status_t::ACTIVE),
                    match,
                    nextapp::format("{}{}", sort_has_due_prefix, sorting.at(sort_)),
                    pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        params.push_back(MainTreeModel::instance()->selected());
        break;

    case FetchWhat::FW_SELECTED_NODE_AND_CHILDREN:
        // This is where ChatGPT shines ;)
        sql = nextapp::format(R"(WITH RECURSIVE node_hierarchy AS (
    -- Base case: Select the node with the given UUID
    SELECT uuid
    FROM node
    WHERE uuid = ?

    -- Recursive case: Select the children of the current node
    UNION ALL
    SELECT n.uuid
    FROM node n
    JOIN node_hierarchy nh ON n.parent = nh.uuid
)
SELECT a.id,
CASE WHEN a.due_by_time IS NULL THEN 1 ELSE 0 END AS has_due
FROM action a {}
JOIN node_hierarchy nh ON a.node = nh.uuid
WHERE a.status={} {}
ORDER BY {}
LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     nextapp::format("{}{}", sort_has_due_prefix, sorting.at(sort_)),
                     pagination_.pageSize(), pagination_.nextOffset());
        params.push_back(MainTreeModel::instance()->selected());
        pushMatch();
        break;

    case FetchWhat::FW_FAVORITES:
        sql = nextapp::format(R"(SELECT a.id,
CASE WHEN a.due_by_time IS NULL THEN 1 ELSE 0 END AS has_due
FROM action a {} WHERE a.favorite = 1 AND a.status={} {} ORDER BY {} LIMIT {} OFFSET {})",
                      join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     nextapp::format("{}{}", sort_has_due_prefix, sorting.at(sort_)),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        break;

    case FetchWhat::FW_ON_CALENDAR:
        // Get the date of the rightside calendar
        if (auto prop = NextAppCore::instance()->getProperty("primaryForActionList"); prop.isValid()) {
            auto cal_date = prop.toDate();
            if (cal_date.isValid()) {
                sql = nextapp::format(R"(SELECT DISTINCT a.id,
CASE WHEN a.due_by_time IS NULL THEN 1 ELSE 0 END AS has_due
FROM action a {}
JOIN time_block_actions tba on a.id=tba.action
JOIN time_block tb on tba.time_block=tb.id
WHERE a.status != {} {}
AND tb.start_time >= ? AND tb.end_time <= ?
ORDER BY {} LIMIT {} OFFSET {})",
                             join_action_name_if(),
                             static_cast<uint>(a_status_t::DELETED),
                             match,
                             nextapp::format("{}{}", sort_has_due_prefix, sorting.at(sort_)),
                             pagination_.pageSize(), pagination_.nextOffset());
                pushMatch();
                params << cal_date;
                params << cal_date.addDays(1);
            }
        }
        break;

    case FetchWhat::FW_UNASSIGNED:
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.due_by_time IS NULL AND a.status={} {} ORDER BY {} LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ACTIVE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        break;

    case FetchWhat::FW_ON_HOLD:
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} ORDER BY {} LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::ONHOLD),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        break;

    case FetchWhat::FW_COMPLETED:
        sql = nextapp::format(R"(SELECT a.id FROM action a {} WHERE a.status={} {} ORDER BY {} LIMIT {} OFFSET {})",
                     join_action_name_if(),
                     static_cast<uint>(a_status_t::DONE),
                     match,
                     sorting.at(sort_),
                     pagination_.pageSize(), pagination_.nextOffset());
        pushMatch();
        break;
    }

    co_await ActionInfoCache::instance()->updateAllScores();
    auto& db = NextAppCore::instance()->db();
    auto result = co_await db.query(QString::fromLatin1(sql), params);
    if (result) {
        // Make a new list of actions with the sorted ID's
        decltype(actions_) new_actions;
        for(const auto& row: *result) {
            const auto& id = row.at(0);
            new_actions.push_back({id.toString()});
        }

        // Send the list to the cache to fill in the data-pointers
        co_await ActionInfoCache::instance()->fill(new_actions);

        // Insert or replace the new list depending on it's page.
        start_reset();
        if (restart) {
            actions_ = std::move(new_actions);
        } else {
            std::copy(new_actions.begin(), new_actions.end(), std::back_inserter(actions_));
        }

        pagination_.more = result->size() == pagination_.pageSize();
        pagination_.increment(result->size());
        valid_ = true;
    } else {
        LOG_ERROR << "Failed to query actions from local db";
        pagination_.more = false;
        valid_ = false;
        start_reset();
        actions_.clear();
    }
}

void ActionsModel::selectedChanged()
{
    if (!isVisible()) {
        return;
    }

    if (mode_ == FetchWhat::FW_SELECTED_NODE || mode_ == FetchWhat::FW_SELECTED_NODE_AND_CHILDREN) {
        fetchIf(true);
    }
}

void ActionsModel::actionChanged(const QUuid &uuid)
{
    if (const auto row = findCurrentRow(actions_, uuid) ; row >= 0) {
        emit dataChanged(index(row), index(row));
    }
}

void ActionsModel::actionDeleted(const QUuid &uuid)
{
    if (const auto row = findCurrentRow(actions_, uuid) ; row >= 0) {
        beginRemoveRows({}, row, row);
        actions_.erase(actions_.begin() + row);
        endRemoveRows();
    }
}

void ActionsModel::actionAdded(const std::shared_ptr<nextapp::pb::ActionInfo> &ai)
{
    if (ai) {
        // Determine if we should add it to the list.
        bool add = false;
        switch(mode_) {
        case FetchWhat::FW_ACTIVE:
            if (ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                add = true;
            }
            break;
        case FetchWhat::FW_TODAY:
            if (ai->hasDue() && ai->due().hasDue()) {
                if (ai->due().due() >= QDateTime::currentDateTime().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE
                    && ai->due().due() < QDateTime::currentDateTime().addDays(1).toSecsSinceEpoch()) {
                    add = true;
                }
            }
            break;
        case FetchWhat::FW_TODAY_AND_OVERDUE:
            if (ai->hasDue() && ai->due().hasDue()) {
                if (ai->due().due() <= QDateTime::currentDateTime().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
            break;
        case FetchWhat::FW_TOMORROW:
            if (ai->hasDue() && ai->due().hasDue()) {
                if (ai->due().due() >= QDateTime::currentDateTime().addDays(1).toSecsSinceEpoch()
                    && ai->due().due() < QDateTime::currentDateTime().addDays(2).toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
            break;
        case FetchWhat::FW_CURRENT_WEEK: {
            if (ai->hasDue() && ai->due().hasDue()) {
                auto start_of_week = getFirstDayOfWeek(QDate::currentDate());
                if (ai->due().due() >= start_of_week.startOfDay().toSecsSinceEpoch()
                    && ai->due().due() < start_of_week.addDays(7).startOfDay().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
        } break;
        case FetchWhat::FW_NEXT_WEEK: {
            if (ai->hasDue() && ai->due().hasDue()) {
                auto start_of_week = getFirstDayOfWeek(QDate::currentDate().addDays(7));
                if (ai->due().due() >= start_of_week.startOfDay().toSecsSinceEpoch()
                    && ai->due().due() < start_of_week.addDays(7).startOfDay().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
        } break;
        case FetchWhat::FW_CURRENT_MONTH: {
            if (ai->hasDue() && ai->due().hasDue()) {
                auto start_of_month = QDate{QDate::currentDate().year(), QDate::currentDate().month(), 1};
                if (ai->due().due() >= start_of_month.startOfDay().toSecsSinceEpoch()
                    && ai->due().due() < start_of_month.addMonths(1).startOfDay().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
        } break;
        case FetchWhat::FW_NEXT_MONTH: {
            if (ai->hasDue() && ai->due().hasDue()) {
                auto start_of_month = QDate{QDate::currentDate().year(), QDate::currentDate().month(), 1};
                if (ai->due().due() >= start_of_month.addMonths(1).startOfDay().toSecsSinceEpoch()
                    && ai->due().due() < start_of_month.addMonths(2).startOfDay().toSecsSinceEpoch()
                    && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                    add = true;
                }
            }
        } break;
        case FetchWhat::FW_SELECTED_NODE:
            if (ai->node() == MainTreeModel::instance()->selected()) {
                add = true;
            }
            break;
        case FetchWhat::FW_SELECTED_NODE_AND_CHILDREN:
            if (MainTreeModel::instance()->isChildOfSelected(toQuid(ai->node()))
                && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                add = true;
            }
            break;
        case FetchWhat::FW_FAVORITES:
            if (ai->favorite()
                && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                add = true;
            }
            break;
        case FetchWhat::FW_UNASSIGNED:
            if ((!ai->hasDue() || !ai->due().hasDue() || ai->due().kind() ==  pb::ActionDueKindGadget::ActionDueKind::UNSET)
                && ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ACTIVE) {
                add = true;
            }
            break;
        case FetchWhat::FW_ON_HOLD:
            if (ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::ONHOLD) {
                add = true;
            }
            break;
        case FetchWhat::FW_COMPLETED:
            if (ai->status() == nextapp::pb::ActionStatusGadget::ActionStatus::DONE) {
                add = true;
            }
            break;
        case FetchWhat::FW_ON_CALENDAR:
            break;
        }

        if (add) {
            beginInsertRows({}, 0, 0);
            actions_.emplace_front(ai->id_proto(), ai);
            endInsertRows();
        }
    }
}

void ActionsModel::refreshVisibleItems()
{
    // TODO: Get the visible items

    auto first = createIndex(0,0);
    auto last = createIndex(actions_.size(), 0);
    emit dataChanged(first, last);
}

void ActionsModel::batchUpdateActions(nextapp::pb::UpdateActionsReq &req, const QStringList &actions)
{
    // QT protobuf don't support mutable sub-members, so we must write
    // with the detail of assambly-langue
    if (actions.empty()) {
        LOG_DEBUG_N << "Empty action list";
        return;
    }

    QList<common::Uuid> uuids;
    for(const auto& action: actions) {
        common::Uuid uuid;
        uuid.setUuid(action);
        uuids.append(uuid);
    }
    req.setActions(uuids);

    ServerComm::instance().updateActions(req);
}

void ActionsModel::cacheReloaded()
{
    fetchIf(true);
}

QStringList ActionsModel::mimeTypes() const
{
     return QStringList() << "application/na.action.list";
}

QMimeData *ActionsModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encodedData;

    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    foreach (const QModelIndex &index, indexes) {
        if (index.isValid()) {
            QString text = data(index, UuidRole).toString();
            stream << text;
        }
    }

    mimeData->setData("application/vnd.text.list", encodedData);
    return mimeData;
}

ActionPrx::ActionPrx(QString actionUuid)
    : uuid_{QUuid{actionUuid}}
{
    if (QUuid{actionUuid}.isNull()) {
        throw runtime_error{"Invalid uuid for action"};
    }

    fetch();
}

// For a new action
ActionPrx::ActionPrx()
    : state_{State::VALID}
{
    nextapp::pb::Priority p;
    p.setPriority(nextapp::pb::ActionPriorityGadget::ActionPriority::PRI_NORMAL);
    action_.setDynamicPriority(p);
    action_.setDifficulty(nextapp::pb::ActionDifficultyGadget::ActionDifficulty::NORMAL);
}

QCoro::Task<void> ActionPrx::fetch()
{
    if (auto a = co_await ActionInfoCache::instance()->getAction(uuid_)) {
        action_ = *a;

        const auto& pri = action_.dynamicPriority();
        if (pri.hasPriority()) {
            const auto& p = pri.priority();
            LOG_TRACE_N << "Action " << uuid_.toString() << " has priority " << static_cast<int>(p);
        }
        if (pri.hasUrgencyImportance()) {
            const auto& ui = pri.urgencyImportance();
            LOG_TRACE_N << "Action " << uuid_.toString() << " has urgency " << static_cast<int>(ui.urgency())
                        << " and importance " << static_cast<int>(ui.importance());
        }
        if (pri.hasScore()) {
            const auto score = pri.score();
            LOG_TRACE_N << "Action " << uuid_.toString() << " has score " << static_cast<int>(score);
        }

        emit actionChanged();
        setState(State::VALID);
    } else {
        LOG_WARN << "Failed to get action " << uuid_.toString();
        setState(State::FAILED);
    }
}
