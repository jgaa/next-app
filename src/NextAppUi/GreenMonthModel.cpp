#include "GreenMonthModel.h"
#include "ServerComm.h"
#include "GreenDaysModel.h"
#include <QUuid>

#include "logging.h"

using namespace std;




GreenMonthModel::GreenMonthModel(unsigned int year, unsigned int month, GreenDaysModel &parent)
    : year_{year}, month_{month}, parent_{parent}
{
    connect(&parent,
            &GreenDaysModel::updatedMonth,
            this,
            &GreenMonthModel::updatedMonth);

    connect(&parent, &GreenDaysModel::validChanged, this, [this]() {
        valid_ = parent_.valid();
        emit colorsChanged();
    });

    valid_ = parent_.valid();
}

QString GreenMonthModel::getColorForDayInMonth(int day)
{
    assert(day >= 1);
    assert(day <= 31);

    return parent_.getColorName(year_, month_, day);
}

QString GreenMonthModel::getUuidForDayInMonth(int day)
{
    return parent_.getColorUuid(year_, month_, day);
}

void GreenMonthModel::updatedMonth(int year, int month)
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
