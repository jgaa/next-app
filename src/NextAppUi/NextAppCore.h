#pragma once

#include <QObject>
#include <QQmlEngine>

class NextAppCore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)

    // True if the app was build with the CMAKE option DEVEL_SETTINGS enabled
    Q_PROPERTY(bool develBuild READ isDevelBuild CONSTANT)
public:
    NextAppCore();

    static QString qtVersion() {
        return QT_VERSION_STR;
    }

    static Q_INVOKABLE QDateTime dateFromWeek(int year, int week);
    static Q_INVOKABLE int weekFromDate(const QDateTime& date);

    static constexpr bool isDevelBuild() {
#if defined(DEVEL_SETTINGS) && DEVEL_SETTINGS
        return true;
#else
        return false;
#endif
    }
};
