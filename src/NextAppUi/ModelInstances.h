#pragma once

#include <QQmlEngine>


#include "NotificationsModel.h"
#include "ActionStatsModel.h"

/* Simple singleton to give QML access to shared models */

class ModelInstances : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    ModelInstances();

    Q_INVOKABLE NotificationsModel *getNotificationsModel() noexcept;
    Q_INVOKABLE ActionStatsModelPtr *getActionStatsModel(QString actionId) noexcept;
};
