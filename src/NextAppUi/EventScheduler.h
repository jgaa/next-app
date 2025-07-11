#pragma once

#include <QObject>
#include <QDateTime>

class EventScheduler : public QObject
{
    Q_OBJECT
public:
    EventScheduler();

    virtual void setNextEvent(QDateTime nextEventTime);


};
