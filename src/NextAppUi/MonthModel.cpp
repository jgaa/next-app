#include "MonthModel.h"
#include "ServerComm.h"

#include "logging.h"

using namespace std;


MonthModel::MonthModel(unsigned int year, unsigned int month, QObject *parent)
    : QObject{parent}, year_{year}, month_{month}
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::monthColorsChanged,
            [this](unsigned year, unsigned month, const ServerComm::colors_in_months_t& colors) {
                if (year == year_ && month == month_) {
                    LOG_TRACE_N << "Setting day-in-month colors for " << year_ << '-' << month_;
                    setColors(*colors);
                }
            });

    ServerComm::instance().getColorsInMonth(year_, month_);
}

QList<QString> MonthModel::getColors()
{
    return colors_;
}

void MonthModel::setColors(QList<QString> colors)
{
    colors_ = std::move(colors);
    emit colorsChanged();
}
