#include "ActionSuggestionsModel.h"

#include <algorithm>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include "ActionInfoCache.h"
#include "ActionsModel.h"
#include "DbStore.h"
#include "NextAppCore.h"
#include "logging.h"

ActionSuggestionsModel::ActionSuggestionsModel(QObject* parent)
    : ActionSuggestionsModel(*NextAppCore::instance(), parent) {}

ActionSuggestionsModel::ActionSuggestionsModel(RuntimeServices& runtime, QObject* parent)
    : QObject(parent), runtime_(runtime) {}

std::optional<double> ActionSuggestionsModel::rank(const nextapp::pb::Action& action,
                                                   int state, int minutes, int scope, qint64 now)
{
    using namespace nextapp::pb;
    if (state < 0 || state > 4 || minutes <= 0 || scope < 0 || scope > 2
        || action.status() != ActionStatusGadget::ActionStatus::ACTIVE) {
        return {};
    }
    const auto& due = action.due();
    const bool unset = !due.hasDue() || !due.due()
        || due.kind() == ActionDueKindGadget::ActionDueKind::UNSET;
    if ((scope == 0 && unset) || (scope == 1 && !unset)
        || (due.hasStart() && due.start() > now)) {
        return {};
    }
    // Exhausted: at most easy; normal: normal; exceptional: inspired.
    const int capacity = state + 1;
    const int difficulty = static_cast<int>(action.difficulty());
    const auto estimate = action.timeEstimate();
    if (difficulty > capacity || difficulty < 0 || estimate > static_cast<quint32>(minutes)) {
        return {};
    }
    // Priority and deadline dominate. Prefer work that makes use of the current
    // capacity; unknown estimates remain eligible, with a small uncertainty penalty.
    return 0.85 * ActionInfoCache::getScore(action)
        + 0.10 * (1.0 - double(capacity - difficulty) / 5.0)
        + (estimate > 0 ? 0.05 * double(estimate) / minutes : 0.0);
}

int ActionSuggestionsModel::timeBoxMinutes(int estimate, int available,
                                           const nextapp::pb::UserGlobalSettings& settings)
{
    // QtProtobuf signed integer getters may return TransparentWrapper rather
    // than int. Convert before conditionals and template argument deduction.
    const int configuredMinimum = static_cast<int>(settings.suggestionTimeBoxMinMinutes());
    const int configuredMaximum = static_cast<int>(settings.suggestionTimeBoxMaxMinutes());
    const int minimum = std::clamp(configuredMinimum > 0 ? configuredMinimum : 30, 1, 1440);
    const int maximum = std::clamp(configuredMaximum > 0 ? configuredMaximum : 240, minimum, 1440);
    return std::clamp(estimate > 0 ? estimate : available, minimum, maximum);
}

void ActionSuggestionsModel::suggest(int state, int minutes, const QStringList& categories,
                                     int scope, bool restricted, const QStringList& ids,
                                     int listScope, const QString& node)
{
    if (busy_) {
        return;
    }
    suggestions_.clear();
    error_.clear();
    if (state < 0 || state > 4 || minutes < 1 || minutes > 1440 || scope < 0 || scope > 2
        || listScope < 0 || listScope > 2) {
        error_ = tr("Choose a valid state and available time (1–1440 minutes).");
        emit suggestionsChanged();
        return;
    }
    busy_ = true;
    emit suggestionsChanged();
    emit busyChanged();
    load(state, minutes, categories, scope, restricted, ids, node.isEmpty() ? 0 : listScope, node);
}

QCoro::Task<void> ActionSuggestionsModel::load(int state, int minutes, QStringList categories,
                                             int scope, bool restricted, QStringList ids,
                                             int listScope, QString node)
{
    LOG_DEBUG_N << "Finding suggestions: state=" << state << " minutes=" << minutes
                << " scope=" << scope << " selectionOnly=" << restricted << " listScope=" << listScope;
    // Read all eligible local rows in one query. This is independent of action-list
    // pagination, and doesn't fetch descriptions or contact an external service.
    QPointer<ActionSuggestionsModel> guard(this);
    QString sql = "SELECT id, name, node, category, priority, dyn_importance, dyn_urgency, "
        "due_kind, start_time, due_by_time, time_estimate, difficulty, time_spent "
        "FROM action WHERE status=0";
    QList<QVariant> params;
    if (listScope == 1) {
        sql += " AND node=?";
        params.append(node);
    } else if (listScope == 2) {
        // UNION also prevents a malformed parent cycle from recursing forever.
        sql = "WITH RECURSIVE selected_lists(uuid) AS ("
              "SELECT uuid FROM node WHERE uuid=? UNION "
              "SELECT n.uuid FROM node n JOIN selected_lists s ON n.parent=s.uuid) "
            + sql + " AND node IN (SELECT uuid FROM selected_lists)";
        params.append(node);
    }
    const auto result = co_await runtime_.db().query(sql, params);
    if (!guard) {
        co_return;
    }
    if (!result) {
        LOG_WARN_N << "Failed to load suggestion candidates";
        error_ = tr("Could not load actions. Please try again.");
    } else {
        const QSet<QString> selected(ids.begin(), ids.end());
        const auto now = QDateTime::currentSecsSinceEpoch();
        for (const auto& row : result->rows) {
            const auto uuid = row[0].toString();
            if ((restricted && !selected.contains(uuid))
                || (!categories.isEmpty() && !categories.contains(row[3].toString()))) {
                continue;
            }
            nextapp::pb::Action action;
            action.setId_proto(uuid);
            nextapp::pb::Priority priority;
            if (!row[5].isNull() && !row[6].isNull()) {
                nextapp::pb::UrgencyImportance ui;
                ui.setImportance(row[5].toDouble());
                ui.setUrgency(row[6].toDouble());
                priority.setUrgencyImportance(ui);
            } else {
                priority.setPriority(static_cast<nextapp::pb::ActionPriorityGadget::ActionPriority>(
                    row[4].isNull() ? 4 : row[4].toInt()));
            }
            action.setDynamicPriority(priority);
            nextapp::pb::Due due;
            due.setKind(static_cast<nextapp::pb::ActionDueKindGadget::ActionDueKind>(row[7].toInt()));
            if (!row[8].isNull()) due.setStart(row[8].toDateTime().toSecsSinceEpoch());
            if (!row[9].isNull()) due.setDue(row[9].toDateTime().toSecsSinceEpoch());
            action.setDue(due);
            action.setTimeEstimate(row[10].toUInt());
            action.setTimeSpent(row[12].toInt());
            action.setDifficulty(static_cast<nextapp::pb::ActionDifficultyGadget::ActionDifficulty>(
                row[11].isNull() ? 2 : row[11].toInt()));
            if (const auto score = rank(action, state, minutes, scope, now)) {
                suggestions_.append(QVariantMap{
                    {"uuid", uuid}, {"name", row[1].toString()}, {"node", row[2].toString()},
                    {"category", row[3].toString()},
                    {"estimate", row[10].toInt()}, {"difficulty", static_cast<int>(action.difficulty())},
                    {"due", ActionsModel::formatDue(due)}, {"score", *score},
                    {"statusColor", ActionsModel::getStatusColor(action)},
                    {"scoreColor", ActionInfoCache::getScoreColor(ActionInfoCache::getScore(action)).name()},
                    {"duration", timeBoxMinutes(action.timeEstimate(), minutes,
                                                runtime_.serverComm().globalSettings())}});
            }
        }
        std::sort(suggestions_.begin(), suggestions_.end(), [](const QVariant& lhs, const QVariant& rhs) {
            const auto a = lhs.toMap(), b = rhs.toMap();
            if (a["score"].toDouble() != b["score"].toDouble()) {
                return a["score"].toDouble() > b["score"].toDouble();
            }
            const int byName = QString::compare(a["name"].toString(), b["name"].toString(), Qt::CaseInsensitive);
            return byName ? byName < 0 : a["uuid"].toString() < b["uuid"].toString();
        });
        // Keep the highest-ranked candidates; never limit the database query
        // before filtering and sorting, which could omit better matches.
        const int configuredLimit = static_cast<int>(runtime_.serverComm().globalSettings().suggestionLimit());
        const int limit = std::clamp(configuredLimit == 0 ? 20 : configuredLimit, 5, 100);
        const auto matchingCount = suggestions_.size();
        if (matchingCount > limit) {
            suggestions_.erase(suggestions_.begin() + limit, suggestions_.end());
        }
        LOG_DEBUG_N << "Found " << matchingCount << " matching actions; showing "
                    << suggestions_.size() << " suggestions (limit=" << limit << ")";
    }
    busy_ = false;
    emit busyChanged();
    emit suggestionsChanged();
}

bool ActionSuggestionsModel::createTimeBox(const QString& uuid, qint64 start, int minutes)
{
    const auto found = std::find_if(suggestions_.begin(), suggestions_.end(), [&uuid](const QVariant& row) {
        return row.toMap()["uuid"].toString() == uuid;
    });
    if (found == suggestions_.end() || start <= 0 || minutes < 1 || minutes > 1440) {
        LOG_WARN_N << "Invalid suggestion time-box request";
        return false;
    }
    const auto row = found->toMap();
    nextapp::pb::TimeBlock tb;
    tb.setName(row["name"].toString());
    tb.setCategory(row["category"].toString());
    nextapp::pb::StringList actions;
    actions.setList({uuid});
    tb.setActions(std::move(actions));
    nextapp::pb::TimeSpan span;
    span.setStart(start);
    span.setEnd(start + minutes * 60);
    tb.setTimeSpan(span);
    LOG_DEBUG_N << "Creating suggestion time box: start=" << start << " minutes=" << minutes;
    runtime_.serverComm().addTimeBlock(tb);
    return true;
}
