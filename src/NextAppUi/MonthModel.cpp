#include "MonthModel.h"
#include "ServerComm.h"
#include <QUuid>

#include "logging.h"

using namespace std;


MonthModel::MonthModel(unsigned int year, unsigned int month, QObject *parent)
    : QObject{parent}, year_{year}, month_{month}
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::monthColorsChanged,
            [this](unsigned year, unsigned month, const ServerComm::colors_in_months_t& colors) {
                if (year == year_ && month == month_) {
                    LOG_TRACE_N << "Setting day-in-month colors for " << year_ << '-' << (month_ + 1);

                    // Somehow we need to toggle the valid variable for the UI to update...
                    valid_ = false;
                    emit colorsChanged();

                    setColors(*colors);
                }
            });

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::dayColorChanged,
            [this](unsigned year, unsigned month, unsigned mday, QUuid color) {
                if (year == year_ && month == month_) {
                    LOG_TRACE_N << "Setting day-in-month color for one day: "
                                << year_ << '-' << (month_ + 1) << '-' << mday;

                    // Somehow we need to toggle the valid variable for the UI to update...
                    valid_ = false;
                    emit colorsChanged();

                    uuids_.replace(mday - 1, color);
                    valid_ = true;
                    emit colorsChanged();
                }
            });


    ServerComm::instance().getColorsInMonth(year_, month_);
}

void MonthModel::setColors(QList<QUuid> uuids)
{
    uuids_ = std::move(uuids);
    valid_ = true;
    emit colorsChanged();
}

QString MonthModel::getColorForDayInMonth(int day)
{
    assert(day >= 1);
    assert(day <= 31);

    if (const auto& uuid = uuids_.at(day -1); !uuid.isNull()) {
        return ServerComm::instance().toDayColorName(uuid);
    }

    return "white";
}

QString MonthModel::getUuidForDayInMonth(int day)
{
    if (const auto& uuid = uuids_.at(day -1); !uuid.isNull()) {
        return uuid.toString(QUuid::WithoutBraces);
    }

    return {};
}
