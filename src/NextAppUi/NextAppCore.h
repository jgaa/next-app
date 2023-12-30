#pragma once

#include <QObject>
#include <QQmlEngine>

class NextAppCore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
public:
    NextAppCore();

    QString qtVersion() const {
        return QT_VERSION_STR;
    }
};
