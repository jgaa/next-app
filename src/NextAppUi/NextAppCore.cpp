#include "NextAppCore.h"
#include <QDateTime>
#include <QQmlEngine>

#include "WeeklyWorkReportModel.h"

using namespace std;

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
