#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QLocale>

#include <regex>

#include "util.h"
#include "logging.h"


using namespace std;

QString toValidQuid(const QString &str) {
    QUuid uuid{str};
    if (uuid.isNull()) {
        return {};
    }
    auto rval = uuid.toString(QUuid::WithoutBraces);
    return rval;
}


QUuid toQuid(const QString &str)
{
    QUuid uuid{str};
    if (uuid.isNull()) {
        throw std::invalid_argument{"Invalid UUID"};
    }
    return uuid;
}

QString toHourMin(const int duration)
{
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

time_t parseDateOrTime(const QString& str)
{
    if (str.isEmpty()) {
        return 0;
    }

    regex pattern(R"((\d{4}-\d{2}-\d{2} )?(\d{2}:\d{2}))");

    std::smatch match;
    const auto nstr = str.toStdString();
    if (std::regex_match(nstr, match, pattern)) {
        if (match[1].matched) {
            // The input is an ANSI date + time
            auto when = QDateTime::fromString(str, "yyyy-MM-dd hh:mm").toSecsSinceEpoch();
            return when;
        } else {
            // The input is just a time
            auto time = static_cast<time_t>(parseDuration(str));
            auto timedate = QDateTime{QDate::currentDate(), QTime::fromMSecsSinceStartOfDay(time * 1000)};
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

    int dayOfWeek = when.dayOfWeek();

    QLocale locale;
    int firstDayOfWeek = locale.firstDayOfWeek();

    int daysToFirstDayOfWeek = firstDayOfWeek - dayOfWeek;
    if (daysToFirstDayOfWeek > 0) {
        daysToFirstDayOfWeek -= 7;
    }

    auto startOfWeek = when.addDays(daysToFirstDayOfWeek);
    return startOfWeek;
}
