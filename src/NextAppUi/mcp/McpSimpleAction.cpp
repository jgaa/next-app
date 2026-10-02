#include "McpSimpleAction.h"
#include "McpHelp.h"
#include "ActionsModel.h"
#include "util.h"

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <limits>

namespace nextapp::mcp {
namespace {
QString token(QString value) {
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("[\\s-]+")), QStringLiteral("_"));
    return value;
}
template<typename Enum>
std::optional<Enum> enumValue(const QJsonValue& input, QString prefix = {}) {
    const auto meta = QMetaEnum::fromType<Enum>();
    if (input.isDouble()) {
        const auto number = input.toDouble();
        if (std::isfinite(number) && std::floor(number) == number
            && number >= 0 && number <= std::numeric_limits<int>::max()
            && meta.valueToKey(int(number))) return static_cast<Enum>(int(number));
        return {};
    }
    if (!input.isString()) return {};
    auto name = token(input.toString());
    if (name == QStringLiteral("very_important")) name = QStringLiteral("very_impornant");
    if (name == QStringLiteral("very_hard")) name = QStringLiteral("veryhard");
    for (int i = 0; i < meta.keyCount(); ++i) {
        auto key = QString::fromLatin1(meta.key(i)).toLower();
        if (name == key || (!prefix.isEmpty() && (key.startsWith(prefix) && name == key.sliced(prefix.size()))))
            return static_cast<Enum>(meta.value(i));
    }
    return {};
}
int monthNumber(const QJsonValue& value) {
    if (value.isDouble() && value.toDouble() == value.toInt()) return value.toInt();
    const auto name = value.toString().trimmed();
    bool ok = false;
    const auto number = name.toInt(&ok);
    if (ok) return number;
    for (int i = 1; i <= 12; ++i) {
        for (const auto locale : {QLocale{QLocale::English}, QLocale::system()}) {
            if (name.compare(locale.monthName(i, QLocale::LongFormat), Qt::CaseInsensitive) == 0
                || name.compare(locale.monthName(i, QLocale::ShortFormat), Qt::CaseInsensitive) == 0)
                return i;
        }
    }
    return 0;
}
using DueResult = std::variant<nextapp::pb::Due, QJsonObject>;
DueResult schedule(const QJsonValue& input, const nextapp::pb::UserGlobalSettings& settings,
                   const QDate& today) {
    using Kind = nextapp::pb::ActionDueKindGadget::ActionDueKind;
    const auto fail = [&](const QString& message) -> DueResult {
        return simpleActionError(QStringLiteral("schedule"), input, message);
    };
    auto object = input.toObject();
    if (input.isString()) {
        auto name = token(input.toString());
        if (name == QStringLiteral("next_quater")) name = QStringLiteral("next_quarter");
        const QList<QPair<QString, ActionsModel::Shortcuts>> shortcuts{
            {"today", ActionsModel::TODAY}, {"tomorrow", ActionsModel::TOMORROW},
            {"this_weekend", ActionsModel::THIS_WEEKEND}, {"next_monday", ActionsModel::NEXT_MONDAY},
            {"this_week", ActionsModel::THIS_WEEK}, {"after_one_week", ActionsModel::AFTER_ONE_WEEK},
            {"next_week", ActionsModel::NEXT_WEEK}, {"this_month", ActionsModel::THIS_MONTH},
            {"next_month", ActionsModel::NEXT_MONTH}, {"this_quarter", ActionsModel::THIS_QUARTER},
            {"next_quarter", ActionsModel::NEXT_QUARTER}, {"this_year", ActionsModel::THIS_YEAR},
            {"next_year", ActionsModel::NEXT_YEAR}};
        for (const auto& [key, shortcut] : shortcuts) {
            if (name == key) {
                nextapp::pb::Due empty; empty.setKind(Kind::UNSET);
                return ActionsModel::resolveDueShortcut(shortcut, empty, settings, today);
            }
        }
        if (name == QStringLiteral("unset") || name == QStringLiteral("unscheduled")) {
            nextapp::pb::Due due; due.setKind(Kind::UNSET); return due;
        }
        const auto text = input.toString().trimmed();
        const auto period = QRegularExpression(QStringLiteral(
            "^(week|month|quarter|year)\\s*#?\\s*([^\\s]+)(?:\\s+(\\d{4}))?$"),
            QRegularExpression::CaseInsensitiveOption).match(text);
        if (period.hasMatch()) {
            object = {{"kind", period.captured(1).toLower()}, {"value", period.captured(2)}};
            if (!period.captured(3).isEmpty()) object.insert("year", period.captured(3).toInt());
        } else {
            object = {{"kind", text.size() == 10 ? "date" : "datetime"}, {"value", text}};
        }
    } else if (!input.isObject()) return fail(QStringLiteral("Use a shortcut, ISO date/time, or {kind, value, year?}."));
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.key() != "kind" && it.key() != "value" && it.key() != "year")
            return fail(QStringLiteral("Unknown schedule field: ") + it.key());
    if (!object.value("kind").isString() || !object.contains("value"))
        return fail(QStringLiteral("A schedule object requires kind and value."));
    const auto kind = enumValue<Kind>(object.value("kind"));
    if (!kind || *kind == Kind::UNSET || *kind == Kind::SPAN_DAYS || *kind == Kind::SPAN_HOURS)
        return fail(QStringLiteral("Supported kinds: datetime, date, week, month, quarter, year."));
    auto zone = QTimeZone{settings.timeZone().toUtf8()};
    if (!zone.isValid()) zone = QTimeZone::systemTimeZone();
    auto year = today.year();
    if (object.contains("year")) {
        const auto value = object.value("year");
        if (!value.isDouble() || value.toDouble() != value.toInt() || value.toInt() < 1970 || value.toInt() > 9999)
            return fail(QStringLiteral("year must be an integer from 1970 through 9999."));
        year = value.toInt();
    }
    const auto value = object.value("value");
    QDateTime when;
    if (*kind == Kind::DATETIME || *kind == Kind::DATE) {
        if (!value.isString() || object.contains("year"))
            return fail(QStringLiteral("Date/time values must be ISO strings; omit year."));
        if (*kind == Kind::DATE) {
            const auto date = QDate::fromString(value.toString(), Qt::ISODate);
            when = date.startOfDay(zone);
        } else {
            const auto text = value.toString().trimmed();
            when = QDateTime::fromString(text, Qt::ISODate);
            if (!when.isValid()) when = QDateTime::fromString(text, "yyyy-MM-dd HH:mm");
            if (when.isValid() && when.timeSpec() == Qt::LocalTime) when.setTimeZone(zone);
        }
    } else {
        bool ok = false;
        const auto number = value.isDouble() && value.toDouble() == value.toInt()
            ? (ok = true, value.toInt()) : value.toString().toInt(&ok);
        QDate date;
        if (*kind == Kind::WEEK && ok && number >= 1 && number <= 53) {
            if (!object.contains("year")) today.weekNumber(&year);
            const QDate jan4{year, 1, 4};
            date = jan4.addDays(1 - jan4.dayOfWeek() + (number - 1) * 7);
            int weekYear = 0;
            if (date.weekNumber(&weekYear) != number || weekYear != year) date = {};
        } else if (*kind == Kind::MONTH) {
            date = QDate{year, monthNumber(value), 1};
        } else if (*kind == Kind::QUARTER && ok && number >= 1 && number <= 4) {
            date = QDate{year, (number - 1) * 3 + 1, 1};
        } else if (*kind == Kind::YEAR && ok && !object.contains("year")) {
            date = QDate{number, 1, 1};
        }
        when = date.startOfDay(zone);
    }
    if (!when.isValid() || when.toSecsSinceEpoch() <= 0 || when.date().year() > 9999)
        return fail(QStringLiteral("Invalid date or period. Use an ISO date/time, ISO week 1–53, month name/1–12, quarter 1–4, or year."));
    auto due = ActionsModel::adjustDue(when.toSecsSinceEpoch(), *kind, settings);
    if (!due.hasStart() || !due.hasDue() || due.start() > due.due())
        return fail(QStringLiteral("The schedule cannot be represented as a valid UI date range."));
    return due;
}
} // namespace

QJsonObject simpleActionError(const QString& field, const QJsonValue& value, const QString& message) {
    return {{"error", "invalid_field_value"}, {"field", field}, {"value", value}, {"message", message},
        {"help", QJsonObject{{"subject", "tool"}, {"name", "nextapp_add_action_simple"}}}};
}

std::variant<nextapp::pb::Action, QJsonObject> normalizeSimpleAction(
    const QJsonObject& arguments, const nextapp::pb::UserGlobalSettings& settings, const QDate& today) {
    using namespace nextapp::pb;
    if (const auto error = validateMutationArguments(QStringLiteral("nextapp_add_action_simple"), arguments)) return *error;
    if (arguments.contains("category") && arguments.contains("categoryId"))
        return simpleActionError("category", arguments.value("category"), "Supply category or categoryId, not both.");
    Action action;
    const auto text = arguments.value("text").toString().trimmed();
    if (text.isEmpty()) return simpleActionError("text", arguments.value("text"), "Action text must contain non-whitespace characters.");
    const auto topic = arguments.value("topic").toString().simplified();
    if (arguments.contains("topic") && (topic.isEmpty() || topic.size() > 255))
        return simpleActionError("topic", arguments.value("topic"), "Topic must contain 1–255 characters.");
    const auto words = settings.pasteActionTitleWordCount();
    action.setName(topic.isEmpty() ? pasteActionTitle(text, words > 0 ? int(words) : 9).left(255) : topic);
    action.setDescr(boundedUtf8(text, 65535));
    action.setNode(arguments.value("nodeId").toString());
    Priority priority;
    const auto pri = enumValue<ActionPriorityGadget::ActionPriority>(arguments.value("priority").isUndefined()
        ? QJsonValue{"normal"} : arguments.value("priority"), QStringLiteral("pri_"));
    if (!pri) return simpleActionError("priority", arguments.value("priority"), "Use a priority name from concept/action_priority, or its numeric value. Default: normal.");
    priority.setPriority(*pri); action.setDynamicPriority(priority);
    if (arguments.contains("difficulty")) {
        const auto difficulty = enumValue<ActionDifficultyGadget::ActionDifficulty>(arguments.value("difficulty"));
        if (!difficulty) return simpleActionError("difficulty", arguments.value("difficulty"), "Use trivial, easy, normal, hard, very_hard, inspired, or its numeric value.");
        action.setDifficulty(*difficulty);
    }
    action.setFavorite(arguments.value("favorite").toBool());
    if (arguments.contains("timeEstimate")) {
        const auto value = arguments.value("timeEstimate");
        double minutes = value.toDouble(-1);
        if (value.isString()) {
            const auto parts = value.toString().trimmed().split(u':');
            minutes = 0;
            if (parts.size() < 1 || parts.size() > 3) minutes = -1;
            else for (int i = 0; i < parts.size(); ++i) {
                bool ok = false; const auto n = parts.at(i).toULongLong(&ok);
                if (!ok) { minutes = -1; break; }
                const auto remaining = parts.size() - i - 1;
                minutes += double(n) * (remaining == 2 ? 480 : remaining == 1 ? 60 : 1);
            }
        }
        if (!std::isfinite(minutes) || minutes < 0 || std::floor(minutes) != minutes || minutes > std::numeric_limits<int>::max())
            return simpleActionError("timeEstimate", value, "Use nonnegative minutes, H:MM, or D:H:MM (UI workday = 8 hours), up to INT_MAX minutes.");
        action.setTimeEstimate(quint32(minutes));
    }
    if (arguments.contains("tags")) {
        QStringList tags;
        const auto value = arguments.value("tags");
        if (value.isString()) tags = value.toString().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
        else for (const auto& tag : value.toArray()) {
            if (!tag.isString()) return simpleActionError("tags", value, "Each tag must be a string.");
            tags.append(tag.toString().trimmed());
        }
        QStringList normalized;
        for (const auto& tag : tags) {
            if (tag.isEmpty() || tag.contains(QRegularExpression("\\s")) || tag.contains(u',') || tag.contains(u';'))
                return simpleActionError("tags", value, "Tags must be nonempty individual words without whitespace or commas.");
            if (!normalized.contains(tag)) normalized.append(tag);
        }
        action.setTags(ActionsModel::tagsToList(normalized.join(u' ')));
    }
    Due due; due.setKind(ActionDueKindGadget::ActionDueKind::UNSET);
    if (arguments.contains("schedule")) {
        const auto resolved = schedule(arguments.value("schedule"), settings, today);
        if (const auto* error = std::get_if<QJsonObject>(&resolved)) return *error;
        due = std::get<Due>(resolved);
    }
    if (arguments.contains("repeat")) {
        const auto value = arguments.value("repeat");
        if (value.isString()) {
            if (token(value.toString()) != "never") return simpleActionError("repeat", value, "Use never, or a structured repeat object.");
        } else {
            const auto repeat = value.toObject();
            const auto from = enumValue<Action::RepeatKind>(repeat.value("from").isUndefined() ? QJsonValue{"completed"} : repeat.value("from"));
            if (!from || *from == Action::RepeatKind::NEVER)
                return simpleActionError("repeat.from", repeat.value("from"), "Use completed, start_time, or due_time. Use repeat=never to disable.");
            action.setRepeatKind(*from);
            if (repeat.contains("on")) {
                if (repeat.contains("every") || repeat.contains("unit"))
                    return simpleActionError("repeat", value, "Choose on, or every/unit; these forms cannot be combined.");
                int bits = 0;
                for (const auto& day : repeat.value("on").toArray()) {
                    const auto spec = enumValue<Action::RepeatSpecs>(day);
                    if (!spec) return simpleActionError("repeat.on", day, "Use a weekday or UI repeat-day specification from the tool help.");
                    bits |= 1 << int(*spec);
                }
                if (!bits) return simpleActionError("repeat.on", repeat.value("on"), "Select at least one repeat day.");
                action.setRepeatWhen(Action::RepeatWhen::AT_DAYSPEC); action.setRepeatAfter(bits);
            } else {
                const auto every = repeat.value("every").toInt(1);
                const auto unit = enumValue<Action::RepeatUnit>(repeat.value("unit").isUndefined() ? QJsonValue{"days"} : repeat.value("unit"));
                if (!unit || every < 1 || every > 99)
                    return simpleActionError("repeat", value, "Use every=1–99 and unit=days, weeks, months, quarters, or years.");
                action.setRepeatUnits(*unit); action.setRepeatAfter(every);
            }
            if (due.kind() == ActionDueKindGadget::ActionDueKind::UNSET) {
                if (arguments.contains("schedule"))
                    return simpleActionError("repeat", value, "Repeating actions require a schedule; omit schedule to default to today.");
                due = ActionsModel::resolveDueShortcut(ActionsModel::TODAY, {}, settings, today);
            }
        }
    }
    action.setDue(due);
    return action;
}
} // namespace nextapp::mcp
