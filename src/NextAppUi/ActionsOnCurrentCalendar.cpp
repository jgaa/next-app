#include "ActionsOnCurrentCalendar.h"

using namespace std;

ActionsOnCurrentCalendar *ActionsOnCurrentCalendar::instance()
{
    static ActionsOnCurrentCalendar instance;
    return &instance;
}

void ActionsOnCurrentCalendar::addAction(const QUuid &action)
{
    // insert action and emit signal if it was added
    if (actions_.insert(action).second) {
        emit actionAdded(action);
    }
}

void ActionsOnCurrentCalendar::removeAction(const QUuid &action)
{
    // remove action and emit signal if it was removed
    if (actions_.erase(action)) {
        emit actionRemoved(action);
    }
}
