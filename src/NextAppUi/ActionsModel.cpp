
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

#include "logging.h"
#include "util.h"

using namespace std;
using namespace std::string_literals;
using namespace nextapp;

namespace {

static constexpr auto quarters = to_array<int8_t>({1, 1, 1, 4, 4, 4, 7, 7, 7, 10, 10, 10});

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
        return left.kind() - right.kind();
    }

    if (left.hasStart() && right.hasStart()) {
        return left.start() - right.start();
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

pb::ActionInfo toActionInfo(const pb::Action& action) {
    pb::ActionInfo ai;
    ai.setId_proto(action.id_proto());
    ai.setNode(action.node());
    if (action.hasOrigin()) {
        ai.setOrigin(action.origin());
    }
    ai.setPriority(action.priority());
    ai.setStatus(action.status());
    ai.setFavorite(action.favorite());
    ai.setName(action.name());
    ai.setCreatedDate(ai.createdDate());
    ai.due().setKind(action.due().kind());
    if (action.due().hasStart()) {
        ai.due().setStart(action.due().start());
    }
    if (action.due().hasDue()) {
        ai.due().setDue(action.due().due());
    }
    ai.due().setTimezone(action.due().timezone());
    ai.setCompletedTime(ai.completedTime());
    ai.setKind(action.kind());
    ai.setCategory(action.category());
    return ai;
}

void insertAction(QList<pb::ActionInfo>& list, const pb::Action& action, int row) {

    if (row >= list.size()) {
        list.append(toActionInfo(action));
    } else {
        list.insert(row, toActionInfo(action));
    }
}

} // anon ns

ActionsModel::ActionsModel(QObject *parent)
    : actions_{make_shared<nextapp::pb::Actions>()}
{
    flags_.setActive(true);
    flags_.setDone(false);
    flags_.setUnscheduled(true);
    flags_.setUpcoming(true);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedActions,
            this,
            &ActionsModel::receivedActions);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::onUpdate,
            this,
            &ActionsModel::onUpdate);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedCurrentWorkSessions,
            this,
            &ActionsModel::receivedWorkSessions);

    connect(MainTreeModel::instance(),
            &MainTreeModel::selectedChanged,
            this,
            &ActionsModel::selectedChanged);

    connect(&ServerComm::instance(), &ServerComm::connectedChanged, this, [this] {
        fetchIf();
    });

    fetchIf();
}

void ActionsModel::addAction(const nextapp::pb::Action &action)
{
    ServerComm::instance().addAction(action);
}

void ActionsModel::updateAction(const nextapp::pb::Action &action)
{
    ServerComm::instance().updateAction(action);
}

void ActionsModel::deleteAction(const QString &uuid)
{
    ServerComm::instance().deleteAction(uuid);
}

nextapp::pb::Action ActionsModel::newAction()
{
    nextapp::pb::Action action;
    action.setPriority(nextapp::pb::ActionPriorityGadget::PRI_NORMAL);
    return action;
}

ActionPrx *ActionsModel::getAction(QString uuid)
{
    if (uuid.isEmpty()) {
        return new ActionPrx{};
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

void ActionsModel::fetch(nextapp::pb::GetActionsReq &filter)
{
    ServerComm::instance().getActions(filter);
}

void ActionsModel::receivedActions(const std::shared_ptr<nextapp::pb::Actions> &actions,
                                   bool more, bool first)
{
    LOG_TRACE_N << "Received " << actions->actions().size() << " actions. More=" << more << ", first=" << first;
    if (first) {
        pagination_.reset();
    }
    pagination_.increment(actions->actions().size());
    pagination_.more = more;

    beginResetModel();
    if (first) {
        actions_ = actions;
    } else {
        actions_->actions().append(actions->actions().begin(), actions->actions().end());
    }

    std::ranges::sort(actions_->actions(), [](const auto& left, const auto& right) {
        return comparePred(left, right);
    });
    endResetModel();
}

template <ActionType T>
int findCurrentRow(const QList<T>& list, QString id) {

    auto it = std::ranges::find_if(list, [&id](const T& a) {
        return a.id_proto() == id;
    });

    if (it != list.end()) {
        auto row = ranges::distance(list.begin(), it);
        return row;
    }

    return -1;
}

void ActionsModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    const auto op = update->op();
    if (update->hasWork()) {
        return doUpdate(update->work(), op);
    } else if (update->hasAction()) {
        return doUpdate(update->action(), op);
    }
}

void ActionsModel::receivedWorkSessions(const std::shared_ptr<nextapp::pb::WorkSessions> &sessions)
{
    worked_on_.clear();
    for(const auto& session: sessions->sessions()) {
        worked_on_.insert(toQuid(session.action()));
    }
}

void ActionsModel::doUpdate(const nextapp::pb::Action &action, nextapp::pb::Update::Operation op)
{
    if (op == pb::Update::Operation::ADDED || op == pb::Update::Operation::UPDATED) {
        if (action.node() != MainTreeModel::instance()->selected()) {
            return; // Irrelevant
        }
    }

    switch(op) {
    case pb::Update::Operation::ADDED: {
    insert_as_new:
        auto row = findInsertRow(action, actions_->actions());
        beginInsertRows({}, row, row);
        insertAction(actions_->actions(), action, row);
        endInsertRows();
    }
    break;
    case pb::Update::Operation::MOVED:
        // TODO: Optimize so that we only add/remove the moved node, but adhers to the current selection
        fetchIf(true);
        break;
    case pb::Update::Operation::UPDATED: {
        auto row = findInsertRow(action, actions_->actions());
        if (auto currentRow = findCurrentRow(actions_->actions(), action.id_proto()) ; currentRow >=0 ) {
            auto& list = actions_->actions();
            if (list.at(currentRow).version() > action.version()) {
                // We have a newer version already. Ignore
                LOG_DEBUG << "Ignoring update of Action " << action.id_proto() << " \"" << action.name()
                          << "\", version " << action.version() << ": we already have a newer version.";
                return;
            }

            if (row != currentRow) {
                beginMoveRows({}, currentRow, currentRow, {}, min<int>(row, list.size()));
                if (row > currentRow) {
                    --row; // Compensate for the deleted row
                }
                list.removeAt(currentRow);
                insertAction(list, action, row);
                endMoveRows();
                const auto cix = index(row);
                emit dataChanged(cix, cix);
            } else {
                // Update in place
                auto &crow = list[currentRow];
                assert(crow.id_proto() == action.id_proto());
                crow = toActionInfo(action);
                //LOG_TRACE << "Updated action " << action.id_proto() << " \"" << action.name() << "\" in place. favorite=" << crow.favorite();
                const auto cix = index(currentRow);
                emit dataChanged(cix, cix);
            }
        } else {
            // Not found
            LOG_DEBUG << "Did not find updated action  " << action.id_proto() << " \"" << action.name()
                      << "\" in the current list af actions. Inserting it as new.";
            goto insert_as_new;
        }
    }
    break;
    case pb::Update::Operation::DELETED: {
        // The deleted event gives us an empty Action with just the id field containing information.
        if (auto currentRow = findCurrentRow(actions_->actions(), action.id_proto()) ; currentRow >=0 ) {
            beginRemoveRows({}, currentRow, currentRow);
            auto& list = actions_->actions();
            list.removeAt(currentRow);
            endRemoveRows();
        }
    }
    break;
    }
}

// Send a data changed event for the action referenced in the work session.
void ActionsModel::doUpdate(const nextapp::pb::WorkSession &work, nextapp::pb::Update::Operation op)
{
    static const QList<int> roles = {HasWorkSessionRole};

    const auto& action_id = work.action();
    if (const auto currentRow = findCurrentRow(actions_->actions(), action_id) ; currentRow >=0 ) {
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
        fetchIf();
    }
}

void ActionsModel::setIsVisible(bool isVisible)
{
    if (is_visible_ != isVisible) {
        LOG_DEBUG_N << "Visible changed to " << isVisible;
        is_visible_ = isVisible;
        emit isVisibleChanged();
        fetchIf();
    }
}

void ActionsModel::setFlags(nextapp::pb::GetActionsFlags flags)
{
    if (flags_ != flags) {
        LOG_DEBUG_N << "Flags changed ";
        flags_ = flags;
        emit flagsChanged();
        fetchIf();
    }
}

QString ActionsModel::toName(nextapp::pb::ActionKindGadget::ActionKind kind) const
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
    }
    assert(false);
}

QString ActionsModel::formatWhen(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt)
{
#ifdef ANDROID
    return "android";
#else
    using namespace nextapp::pb::ActionDueKindGadget;

    if (!when) {
        return tr("No due time set");
    }

    const std::chrono::time_zone *ts = std::chrono::current_zone();
    const auto tp = round<std::chrono::seconds>(std::chrono::system_clock::from_time_t(when));
    const auto zoned = std::chrono::zoned_time{ts, tp};
    const auto ymd = std::chrono::year_month_day(floor<std::chrono::days>(zoned.get_local_time()));

    const auto current = std::chrono::zoned_time{ts, round<std::chrono::seconds>(std::chrono::system_clock::now())};
    const auto current_ymd = std::chrono::year_month_day(floor<std::chrono::days>(current.get_local_time()));

    auto select = [&](const std::string& formatted, const QString& phrase, const QString& shortp, const QString& prefix = {}) -> QString {
        if (ymd == current_ymd) {
            return phrase;
        }
        return shortp + " " + prefix + QString::fromUtf8(formatted);
    };

    switch(dt) {
    case ActionDueKind::DATETIME:
        return tr("Time") + " " + QString::fromUtf8(std::format("{:%F %R}", zoned));
    case ActionDueKind::DATE:
        return select(format("{:%F}", zoned), tr("Today"), tr("Day"));
    case ActionDueKind::WEEK:
        return select(format("{:%W %Y}", zoned), tr("This week"), tr("Week"), tr("W"));
    case ActionDueKind::MONTH:
        return select(std::format("{:%b %Y}", zoned), tr("This month"), tr("Month"));
    case ActionDueKind::QUARTER: {
        const auto month = static_cast<unsigned>(ymd.month());
        const auto quarter = (month - 1) / 3 + 1;
        return select(std::format("{} {:%Y}", quarter, zoned), tr("This Quarter"), tr("Quarter"), tr("Q"));
        }
    case ActionDueKind::YEAR:
            return select(std::format("{:%Y}", zoned), tr("This year"), tr("Year"));
    case ActionDueKind::UNSET:
        return tr("No due time set");
    }
    return {};
#endif
}

QString ActionsModel::formatDue(const nextapp::pb::Due &due)
{
    auto when = due.hasStart() ? due.start() : 0;
    return formatWhen(when, due.kind());
}

QString ActionsModel::whenListElement(uint64_t when,
                                      nextapp::pb::ActionDueKindGadget::ActionDueKind dt,
                                      nextapp::pb::ActionDueKindGadget::ActionDueKind btn)
{
    using namespace nextapp::pb::ActionDueKindGadget;

    if (when == 0 || dt > btn) {
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
        default:
            ;
        }
    }

    return formatWhen(when, btn);
}



QStringListModel *ActionsModel::getDueSelections(uint64_t when, nextapp::pb::ActionDueKindGadget::ActionDueKind dt)
{
    using namespace nextapp::pb::ActionDueKindGadget;
    auto model = new QStringListModel{};

    QStringList list;
    list << whenListElement(when, dt, ActionDueKind::DATETIME);
    list << whenListElement(when, dt, ActionDueKind::DATE);
    list << whenListElement(when, dt, ActionDueKind::WEEK);
    list << whenListElement(when, dt, ActionDueKind::MONTH);
    list << whenListElement(when, dt, ActionDueKind::QUARTER);
    list << whenListElement(when, dt, ActionDueKind::YEAR);
    list << whenListElement(when, dt, ActionDueKind::UNSET);

    model->setStringList(list);
    return model;
}

#ifndef ANDROID
auto timeZoneOffset(const std::chrono::time_zone *tz, const auto& tp) {

    const auto ts_offset = tz->get_info(tp).offset;
    const auto offset = ts_offset.count();
    return offset;
}
#endif

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
        end = start = when;
        break;
    case pb::ActionDueKindGadget::ActionDueKind::DATE: {
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addDays(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch();
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::WEEK: {
        auto day_in_week = qt_start.date().dayOfWeek();
        assert(day_in_week != 0);
        // Jump back in time to the start of the week
        auto offset = days_offset.at(day_in_week - 1) * -1;
        auto w_start = d_start.addDays(offset);
        start = w_start.toSecsSinceEpoch();
        auto d_end = w_start.addDays(7);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch();
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::MONTH: {
        auto m_start = d_start;
        m_start.setDate(QDate{d_start.date().year(), d_start.date().month(), 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addMonths(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch();
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::QUARTER: {
        auto m_start = d_start;
        auto qmonth = quarters.at(d_start.date().month() - 1);
        m_start.setDate(QDate{d_start.date().year(), qmonth, 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addMonths(3);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch();
    }
    case pb::ActionDueKindGadget::ActionDueKind::YEAR: {
        auto y_start = d_start;
        y_start.setDate(QDate{d_start.date().year(), 1, 1});
        start = d_start.toSecsSinceEpoch();
        auto d_end = d_start.addYears(1);
        d_end.setTime(QTime(0,0));
        end = d_end.toSecsSinceEpoch();
    }
    break;
    case pb::ActionDueKindGadget::ActionDueKind::UNSET:
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
        end = today.addDays(1).startOfDay().toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        break;
    case TOMORROW:
        start = today.addDays(1).startOfDay().toSecsSinceEpoch();
        end = today.addDays(2).startOfDay().toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        break;
    case THIS_WEEKEND: {
        auto day_in_week = today.dayOfWeek();
        auto offset = Qt::DayOfWeek::Saturday - day_in_week;
        auto w_start = today.addDays(offset);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(2).startOfDay().toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        }
        break;
    case NEXT_MONDAY: {
        auto day_in_week = today.dayOfWeek();
        auto offset = (Qt::DayOfWeek::Sunday - day_in_week) + 1;
        auto w_start = today.addDays(offset);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(1).startOfDay().toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::DATE;
        }
        break;
    case THIS_WEEK: {
        auto day_in_week = today.dayOfWeek();
        auto w_start = today.addDays((day_in_week  -1) * -1);
        start = w_start.startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(7).startOfDay().toSecsSinceEpoch();
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
            end = e_date.startOfDay().toSecsSinceEpoch();
        }
        }
        break;
    case NEXT_WEEK: {
        auto day_in_week = today.dayOfWeek();
        auto w_start = today.addDays((day_in_week  -1) * -1);
        start = w_start.addDays(7).startOfDay().toSecsSinceEpoch();
        end = w_start.addDays(14).startOfDay().toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::WEEK;
        }
        break;
    case THIS_MONTH: {
        auto m_start = QDateTime{QDate{today.year(), today.month(), 1}, QTime{0, 0}};
        start = m_start.toSecsSinceEpoch();
        end = m_start.addMonths(1).toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::MONTH;
        }
        break;
    case NEXT_MONTH: {
        auto m_start = QDateTime{QDate{today.year(), today.month(), 1}, QTime{0, 0}};
        start = m_start.addMonths(1).toSecsSinceEpoch();
        end = m_start.addMonths(2).toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::MONTH;
        }
        break;
    case THIS_QUARTER: {
        auto qmonth = quarters.at(today.month() - 1);
        auto m_start = QDateTime{QDate{today.year(), qmonth, 1}, QTime{0, 0}};
        start = m_start.toSecsSinceEpoch();
        end = m_start.addMonths(3).toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::QUARTER;
        }
        break;
    case NEXT_QUARTER: {
        auto qmonth = quarters.at(today.month() - 1);
        auto m_start = QDateTime{QDate{today.year(), qmonth, 1}, QTime{0, 0}};
        start = m_start.addMonths(3).toSecsSinceEpoch();
        end = m_start.addMonths(6).toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::QUARTER;
        }
        break;
    case THIS_YEAR: {
        auto y_start = QDateTime{QDate{today.year(), 1, 1}, QTime{0, 0}};
        start = y_start.toSecsSinceEpoch();
        end = y_start.addYears(1).toSecsSinceEpoch();
        kind = pb::ActionDueKindGadget::ActionDueKind::YEAR;
        }
        break;
    case NEXT_YEAR: {
        auto y_start = QDateTime{QDate{today.year(), 1, 1}, QTime{0, 0}};
        start = y_start.addYears(1).toSecsSinceEpoch();
        end = y_start.addYears(2).toSecsSinceEpoch();
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
    auto row = findCurrentRow(actions_->actions(), actionUuid);
    if (row >= 0) {
        auto& ai= actions_->actions().at(row);
        if (ai.node() == nodeUuid) {
            LOG_DEBUG_N << "Cannot move to the same node";
            return false;
        }

        ServerComm::instance().moveAction(actionUuid, nodeUuid);

        return true;
    }
    return false;
}

int ActionsModel::rowCount(const QModelIndex &parent) const
{
    return actions_->actions().size();
}

QVariant ActionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const auto row = index.row();
    if (row < 0 && row >= actions_->actions().size()) {
        return {};
    }

    const auto& action = actions_->actions().at(row);

    switch(role) {
    case NameRole:
        return action.name();
    case UuidRole:
        return action.id_proto();
    case PriorityRole:
        return static_cast<unsigned>(action.priority());
    case StatusRole:
        return action.status();
    case NodeRole:
        return action.node();
    case CreatedDateRole:
        return QDate{action.createdDate().year(), action.createdDate().month(), action.createdDate().mday()}.toString();
    case DueTypeRole:
        return action.due().kind();
    case DueByTimeRole:
        return static_cast<quint64>(action.due().due());
    case CompletedRole:
        return action.status() == nextapp::pb::ActionStatusGadget::ActionStatus::DONE;
    case CompletedTimeRole:
        if (action.completedTime()) {
            return QDateTime::fromSecsSinceEpoch(action.completedTime());
        }
        return {};
    case SectionRole:
        return action.kind();
    case SectionNameRole: {
        auto name = toName(action.kind());
        return name;
        }
    case DueRole:
        //return action.due(); // Does not work
        return {};
    case FavoriteRole:
        return action.favorite();
    case HasWorkSessionRole:
        return worked_on_.contains(toQuid(action.id_proto()));
    case ListNameRole:
        if (MainTreeModel::instance()->selected() == action.node()) {
            return {};
        }
        return MainTreeModel::instance()->nodeNameFromUuid(action.node(), true);
    case CategoryRole:
        return action.category();
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
        case SectionRole:
            return "Section";
        case SectionNameRole:
            return "Section Name";
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
    roles[SectionRole] = "section";
    roles[SectionNameRole] = "sname";
    roles[DueRole] = "due";
    roles[FavoriteRole] = "favorite";
    roles[HasWorkSessionRole] = "hasWorkSession";
    roles[ListNameRole] = "listName";
    roles[CategoryRole] = "category";
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
    return pagination_.hasMore();
}

void ActionsModel::fetchIf(bool restart)
{
    if (restart) {
        pagination_.reset();
    }

    if (!isVisible() || !ServerComm::instance().connected()) {
        LOG_DEBUG_N << "Not connected or not visible. Skipping fetch.";
        return;
    }

    LOG_DEBUG_N << "Fetching actions. Mode is " << mode_;

    nextapp::pb::GetActionsReq req;
    req.setFlags(flags_);
    nextapp::pb::PageSpan page;
    page.setPageSize(QSettings{}.value("pagination/page_size", 100).toInt());
    page.setOffset(pagination_.nextOffset());
    req.setPage(page);

    switch(mode_) {
    case FetchWhat::FW_TODAY: {
        auto date = QDate::currentDate();
        req.startSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        req.startSpan().setEnd(date.addDays(1).startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_TODAY_AND_OVERDUE: {
        auto date = QDate::currentDate();
        req.startSpan().setStart(3600);
        req.startSpan().setEnd(date.addDays(1).startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_CURRENT_WEEK: {
        auto date = QDate::currentDate();
        auto day_in_week = date.dayOfWeek();
        auto w_start = date.addDays((day_in_week  -1) * -1);
        req.startSpan().setStart(w_start.startOfDay().toSecsSinceEpoch());
        req.startSpan().setEnd(w_start.addDays(7).startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_CURRENT_WEEK_AND_OVERDUE: {
        auto date = QDate::currentDate();
        auto day_in_week = date.dayOfWeek();
        auto w_start = date.addDays((day_in_week  -1) * -1);
        req.startSpan().setStart(1);
        req.startSpan().setEnd(w_start.addDays(7).startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_CURRENT_MONTH: {
        auto date = QDate::currentDate();
        date.setDate(date.year(), date.month(), 1);
        req.startSpan().setStart(date.startOfDay().toSecsSinceEpoch());
        date = date.addMonths(1);
        req.startSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_CURRENT_MONTH_AND_OVERDUE: {
        auto date = QDate::currentDate();
        date.setDate(date.year(), date.month(), 1);
        req.startSpan().setStart(1);
        date = date.addMonths(1);
        req.startSpan().setEnd(date.startOfDay().toSecsSinceEpoch());
    } break;
    case FetchWhat::FW_SELECTED_NODE:
        req.setNodeId(MainTreeModel::instance()->selected());
        break;
    case FW_SELECTED_NODE_AND_CHILDREN:
        req.setNodeIdAndChildren(MainTreeModel::instance()->selected());
        break;
    case FetchWhat::FW_FAVORITES:
        req.setFavorites(true);
        break;
    }

    // TODO: Handle pagination, visibility
    fetch(req);
}

void ActionsModel::selectedChanged()
{
    if (mode_ == FetchWhat::FW_SELECTED_NODE || mode_ == FetchWhat::FW_SELECTED_NODE_AND_CHILDREN) {
        fetchIf();
    }
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
    : valid_{false}, uuid_{actionUuid}
{
    if (QUuid{actionUuid}.isNull()) {
        throw runtime_error{"Invalid uuid for action"};
    }

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedAction,
            this,
            &ActionPrx::receivedAction);

    pb::GetActionReq req;
    req.setUuid(actionUuid);
    ServerComm::instance().getAction(req);
}

ActionPrx::ActionPrx()
    : valid_{true}
{
    action_.setPriority(nextapp::pb::ActionPriorityGadget::PRI_NORMAL);
    action_.setDifficulty(nextapp::pb::ActionDifficultyGadget::NORMAL);
}

void ActionPrx::receivedAction(const nextapp::pb::Status &status)
{
    if (!valid_) {
        if (status.hasAction()) {
            const auto& action = status.action();
            if (action.id_proto() == uuid_) {
                valid_ = true;
                action_ = action;
                emit actionChanged();
                emit validChanged();
                return;
            }
        }

        if (status.error() != pb::ErrorGadget::Error::OK) {
            // TODO: Add uuid to status so we can validate it's the relevant failure
            // TODO: Make sure that the UI handles the failure to get a action
            emit actionChanged();
        }
    }
}
