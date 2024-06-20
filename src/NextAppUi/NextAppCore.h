#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQmlApplicationEngine>

#include "WorkModel.h"
#include "WeeklyWorkReportModel.h"

class WeeklyWorkReportModel;
class CalendarModel;

class NextAppCore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    //QML_SINGLETON

    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(bool isMobile READ isMobile CONSTANT)
    Q_PROPERTY(bool isMobileSimulation READ isMobileSimulation CONSTANT)

    // True if the app was build with the CMAKE option DEVEL_SETTINGS enabled
    Q_PROPERTY(bool develBuild READ isDevelBuild CONSTANT)
public:
    NextAppCore();

    static QString qtVersion() {
        return QT_VERSION_STR;
    }

    static Q_INVOKABLE QDateTime dateFromWeek(int year, int week);
    static Q_INVOKABLE int weekFromDate(const QDateTime& date);
    static Q_INVOKABLE WorkModel *createWorkModel();
    static Q_INVOKABLE WeeklyWorkReportModel *createWeeklyWorkReportModel();
    static Q_INVOKABLE QString toHourMin(int duration);
    static Q_INVOKABLE CalendarModel *createCalendarModel();

    // returns -1 on error
    static Q_INVOKABLE time_t parseDateOrTime(const QString& str, time_t defaultDate = 0);
    static Q_INVOKABLE QString toDateAndTime(time_t when, time_t defaultDate = 0);
    static Q_INVOKABLE QString toTime(time_t when);

    // returns -1 on error
    static Q_INVOKABLE time_t parseHourMin(const QString& str);

    bool isMobile() const noexcept {
#if defined (__ANDROID__) || defined (USE_ANDROID_UI)
        return true;
#else
        return false;
#endif
    }

    bool isMobileSimulation() const noexcept {
#if defined (USE_ANDROID_UI)
        return true;
#else
        return false;
#endif
}

    static constexpr bool isDevelBuild() {
#if defined(DEVEL_SETTINGS)
        return true;
#else
        return false;
#endif
    }

    void setOnline(bool online) {
        emit onlineChanged(online);
    }

    void modelsAreCreated() {
        emit allBaseModelsCreated();
    }

    static NextAppCore *instance() {
        assert(instance_ != nullptr);
        return instance_;
    }

    static QQmlApplicationEngine& engine();

signals:
    void allBaseModelsCreated();
    void onlineChanged(bool online);

private:
    static NextAppCore *instance_;
};
