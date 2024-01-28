#include "MonthModel.h"
#include "ServerComm.h"
#include "DaysModel.h"
#include <QUuid>

#include "logging.h"

using namespace std;




MonthModel::MonthModel(unsigned int year, unsigned int month, DaysModel &parent)
    : year_{year}, month_{month}, parent_{parent}
{
    connect(&parent,
            &DaysModel::updatedMonth,
            this,
            &MonthModel::updatedMonth);
}

QString MonthModel::getColorForDayInMonth(int day)
{
    assert(day >= 1);
    assert(day <= 31);

    return parent_.getColorName(year_, month_, day);
}

QString MonthModel::getUuidForDayInMonth(int day)
{
    return parent_.getColorUuid(year_, month_, day);
}

void MonthModel::updatedMonth(int year, int month)
{
    if (year == year_ && month_ == month) {
        const auto was_valid = valid_;
        valid_ = parent_.hasMonth(year_, month_);

        if (was_valid && valid_) {
            // Force the UI for the month to update
            valid_ = false;
            emit colorsChanged();
            valid_ = true;
        }
        emit colorsChanged();
    }
}
