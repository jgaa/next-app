#include "CalendarDayModel.h"
#include "ServerComm.h"

CalendarDayModel::CalendarDayModel(QDate date, QObject *parent)
    : QObject(parent)
    , date_(date)
{

}

void CalendarDayModel::createTimeBox(QString name, QString category, int start, int end)
{
    nextapp::pb::TimeBlock tb;
    tb.setName(name);
    tb.setCategory(category);

    if (start >= end) {
        qWarning() << "Invalid timebox start/end";
        return;
    }

    nextapp::pb::TimeSpan ts;
    ts.setStart(date_.startOfDay().addSecs(start * 60).toSecsSinceEpoch());
    ts.setEnd(date_.startOfDay().addSecs(end * 60).toSecsSinceEpoch());
    tb.setTimeSpan(ts);

    ServerComm::instance().addTimeBlock(tb);
}

nextapp::pb::CalendarEvent CalendarDayModel::event(int index) const noexcept {
    LOG_TRACE_N << "index: " << index;

    if (index < 0 || index >= events_.size())
        return {};
    return events_[index];
}

int CalendarDayModel::size() const noexcept  {
    if (!valid_) {
        return 0;
    }
    return events_.size();
}

void CalendarDayModel::setValid(bool valid)
{
    if (valid_ == valid) return;
    valid_ = valid;
    emit validChanged();
}
