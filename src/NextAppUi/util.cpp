#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QLocale>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMetaProperty>

#include <regex>

#include "util.h"
#include "logging.h"
#include "ServerComm.h"


using namespace std;

QString toValidQuid(const QString &str) {
    QUuid uuid{str};
    if (uuid.isNull()) {
        return {};
    }
    return uuid.toString(QUuid::WithoutBraces);
}


QUuid toQuid(const QString &str)
{
    QUuid uuid{str};
    if (uuid.isNull()) {
        throw std::invalid_argument{"Invalid UUID"};
    }
    return uuid;
}

QString toHourMin(const int duration, bool showEmpty)
{
    if (duration == 0) {
        return showEmpty ?  "00:00" : "";
    }

    auto minutes = duration / 60;
    auto hours = minutes / 60;
    minutes -= (hours * 60);

    QString val;
    return val.asprintf("%02d:%02d", hours, minutes);
}

int parseDuration(const QString &value)
{
    int minutes = {}, hours = {};
    bool have_seen_column = false;

    for(const auto& ch : value) {
        if (ch >= '0' && ch <= '9') {
            minutes *= 10;
            minutes += (ch.toLatin1() - '0');
        } else if (ch == ':') {
            if (have_seen_column) {
                throw std::runtime_error("Invalid input");
            }
            hours = minutes * 60 * 60;
            minutes = 0;
        } else {
            throw std::runtime_error("Invalid input");
        }
    }

    return (minutes * 60) + hours;
}

time_t parseDateOrTime(const QString& str, const QDate& defaultDate)
{
    if (str.isEmpty()) {
        return 0;
    }

    regex pattern(R"((\d{4}-\d{2}-\d{2} )?(\d{2}:\d{2}))");

    std::smatch match;
    const auto nstr = str.trimmed().toStdString();
    if (std::regex_match(nstr, match, pattern)) {
        if (match[1].matched) {
            // The input is an ANSI date + time
            auto when = QDateTime::fromString(str, "yyyy-MM-dd hh:mm").toSecsSinceEpoch();
            return when;
        } else {
            // The input is just a time
            auto time = static_cast<time_t>(parseDuration(str));
            auto timedate = QDateTime{defaultDate, QTime::fromMSecsSinceStartOfDay(time * 1000)};
            auto when = timedate.toSecsSinceEpoch();
            return when;
        }
    } else {
        // The input does not match either format
        LOG_WARN << "Could not parse time/date: " << str;
    }

    throw std::runtime_error{"Invalid date/time format"};
}

QDate getFirstDayOfWeek(const QDate &when)
{
    const auto gs = ServerComm::instance().getGlobalSettings();
    int dayOfWeek = when.dayOfWeek();

    int firstDayOfWeek = gs.firstDayOfWeekIsMonday() ? 1 : 7;

    int daysToFirstDayOfWeek = firstDayOfWeek - dayOfWeek;
    if (daysToFirstDayOfWeek > 0) {
        daysToFirstDayOfWeek -= 7;
    }

    auto startOfWeek = when.addDays(daysToFirstDayOfWeek);
    return startOfWeek;
}

bool isToday(time_t when)
{
    auto today = QDateTime::currentDateTime().date();
    auto date = QDate::currentDate();
    return date == today;
}

bool isYesterday(time_t when)
{
    auto yesterday = QDate::currentDate().addDays(-1);
    auto date = QDateTime::fromSecsSinceEpoch(when).date();
    return date == yesterday;
}

bool isCurrentWeek(time_t when)
{
    auto start_of_week = getFirstDayOfWeek();
    auto next_week = start_of_week.addDays(7);
    auto date = QDateTime::fromSecsSinceEpoch(when).date();
    return date >= start_of_week && date < next_week;
}

bool isLastWeek(time_t when)
{
    auto start_of_week = getFirstDayOfWeek();
    auto date = QDateTime::fromSecsSinceEpoch(when).date();
    return date >= start_of_week.addDays(-7) && date < start_of_week;
}

bool isCurrentMonth(time_t when)
{
    auto date = QDateTime::fromSecsSinceEpoch(when).date();
    auto month = date.month();
    auto year = date.year();
    auto today = QDate::currentDate();
    return month == today.month() && year == today.year();
}

bool isLastMonth(time_t when)
{
    auto date = QDateTime::fromSecsSinceEpoch(when).date();
    auto month = date.month();
    auto year = date.year();
    auto today = QDate::currentDate();
    return month == today.addMonths(-1).month() && year == today.addMonths(-1).year();
}

QString toJson(const QObject &o)
{
    QJsonObject obj;
    const QMetaObject* meta = o.metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        auto prop = meta->property(i);
        obj[prop.name()] = prop.read(&o).toString();
    }
    return QString(QJsonDocument(obj).toJson());
}

QDate toQDate(const nextapp::pb::Date &date)
{
    return QDate{date.year(), date.month() +1, date.mday()};
}

nextapp::pb::Date toDate(const QDate &date)
{
    nextapp::pb::Date d;
    d.setYear(date.year());
    d.setMonth(date.month() -1);
    d.setMday(date.day());
    return d;
}
