#include "NextAppCore.h"
#include <QDateTime>
#include <QQmlEngine>

#include "WeeklyWorkReportModel.h"
#include "util.h"

using namespace std;

namespace {
QDate getDefaultDate(time_t when) {
    if (when > 0) {
        return QDateTime::fromSecsSinceEpoch(when).date();
    }
    return QDate::currentDate();
}
} // anon ns

NextAppCore *NextAppCore::instance_;

NextAppCore::NextAppCore() {
    assert(instance_ == nullptr);
    instance_ = this;
}

QDateTime NextAppCore::dateFromWeek(int year, int week)
{
    QDate date(year, 1, 1);
    date = date.addDays(7 * (week - 1));
    while (date.dayOfWeek() != Qt::Monday) {
        date = date.addDays(-1);
    }
    return QDateTime(date, QTime{0,0});
}

int NextAppCore::weekFromDate(const QDateTime &date)
{
    QDate d = date.date();
    int week = d.weekNumber();
    if (d.dayOfWeek() == Qt::Monday) {
        return week;
    }
    return week - 1;
}

WorkModel *NextAppCore::createWorkModel()
{
    auto model = make_unique<WorkModel>();
    QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
    return model.release();
}

WeeklyWorkReportModel *NextAppCore::createWeeklyWorkReportModel()
{
    LOG_DEBUG_N << "Creating WeeklyWorkReportModel";
    auto model = new WeeklyWorkReportModel(instance());
    //QQmlEngine::setObjectOwnership(model.get(), QQmlEngine::JavaScriptOwnership);
    return model;
}

QString NextAppCore::toHourMin(int duration)
{
    return ::toHourMin(duration);
}

time_t NextAppCore::parseDateOrTime(const QString &str, time_t defaultDate)
{
    try {
        return ::parseDateOrTime(str, getDefaultDate(defaultDate));
    } catch (const std::exception &e) {
        LOG_DEBUG << "Could not parse time/date: " << str;
    }
    return -1;
}

QString NextAppCore::toDateAndTime(time_t when, time_t defaultDate)
{
    auto dd = getDefaultDate(defaultDate);
    auto actualTime = QDateTime::fromSecsSinceEpoch(when);

    if (dd == actualTime.date()) {
        return actualTime.time().toString("hh:mm");
    }

    return QDateTime::fromSecsSinceEpoch(when).toString("yyyy-MM-dd hh:mm");
}

time_t NextAppCore::parseHourMin(const QString &str)
{
    try {
        return ::parseDuration(str);
    } catch (const std::exception &e) {
        LOG_DEBUG << "Could not parse hour/min: " << str;
    }

    return -1;
}

QQmlApplicationEngine &NextAppCore::engine()
{
    static QQmlApplicationEngine engine_;
    return engine_;
}

