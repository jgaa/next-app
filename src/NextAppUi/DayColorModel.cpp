#include <algorithm>
#include <ranges>

#include "DayColorModel.h"
#include "GreenDaysModel.h"

#include "logging.h"

using namespace std;

DayColorModel::DayColorModel(QObject *parent)
    : QObject{parent}
{
    connect(GreenDaysModel::instance(),
            &GreenDaysModel::dayColorsChanged,
            this,
            &DayColorModel::dayColorsChanged);
}

QStringList DayColorModel::getNames() const
{
    QStringList list = {tr("--- Unset ---")};
    list.reserve(daycolors_.size());

    ranges::copy(views::transform(daycolors_, [](const nextapp::pb::DayColor& v) {
        return v.name();
    }), back_inserter(list));

    return list;
}

QUuid DayColorModel::getUuid(int index)
{
    if (index == 0) {
        return {};
    }

    if (index < 1 || index > daycolors_.size()) {
        LOG_WARN_N << "Index out of bound: " << index;
        return {};
    }

    return QUuid{daycolors_.at(index -1).id_proto()};
}

int DayColorModel::getIndexForColorUuid(const QString &uuid)
{
    if (!uuid.isEmpty()) {
        unsigned ix = 1; // First visible item is the "--- select ---" text
        for(const auto& dc : daycolors_) {
            if (uuid == dc.id_proto()) {
                return ix;
            }
            ++ix;
        }
    }

    return -1;
}

// void DayColorModel::setDayColor(int year, int month, int day, int ix,
//                                 const QString& notes, const QString& report)
// {
//     const auto uuid = getUuid(ix);
//     ServerComm::instance().setDayColor(year, month, day, uuid, notes, report);
// }

void DayColorModel::dayColorsChanged(const nextapp::pb::DayColorDefinitions& defs)
{
    daycolors_ = defs.dayColors();
    LOG_DEBUG << "DayColorModel -  Colors changed.";
    emit colorsChanged();
}
