#include "GreenDaysModel.h"
#include "GreenDayModel.h"
#include "MaterialDesignStyling.h"

#include "ServerComm.h"
#include "logging.h"

using namespace std;
using namespace nextapp;

// GreenDayModel::GreenDayModel(uint day, uint month, uint year, GreenDaysModel *parent)
//     : QObject{parent}, day_{day}, month_{month}, year_{year}
// {
//     assert(parent);
// }

// GreenDaysModel &GreenDayModel::parent()
// {
//     auto *p = QObject::parent();
//     assert(p);
//     return dynamic_cast<GreenDaysModel&>(*p);
// }

GreenDaysModel *GreenDaysModel::instance_;

GreenDaysModel::GreenDaysModel()
{
    instance_ = this;

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedDayColorDefinitions,
            this,
            &GreenDaysModel::fetchedColors);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedMonth,
            this,
            &GreenDaysModel::fetchedMonth);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::onUpdate,
            this,
            &GreenDaysModel::onUpdate);

    connect(std::addressof(ServerComm::instance()), &ServerComm::connectedChanged, this, [this] {
        if (ServerComm::instance().connected()) {
            onOnline();
        }
    });

    if (ServerComm::instance().connected()) {
        onOnline();
    }
}

GreenDayModel *GreenDaysModel::getDay(int year, int month, int day)
{
    assert(day > 0);
    auto rval = make_unique<GreenDayModel>(year, month, day, this);

    assert(day > 0);
    fetchDay(year, month, day);
    return rval.release();
}

GreenMonthModel *GreenDaysModel::getMonth(int year, int month)
{
    const auto key = getKey(year, month);
    if (!hasMonth(year, month)) {
        months_[key] = {};
        fetchMonth(year, month);
    }

    return new GreenMonthModel(year, month, *this);
}

std::optional<pb::DayColor> GreenDaysModel::getDayColor(const QUuid &uuid) const
{
    const auto key = uuid.toString(QUuid::WithoutBraces);
    const auto& range = color_definitions_.dayColors();
    if (auto it = ranges::find_if(range,
            [this, &key](const auto &v) {
                return v.id_proto() == key;
            }); it != range.end()) {
        return *it;
    }
    return {};
}


void GreenDaysModel::fetchMonth(int year, int month)
{
    if (!ServerComm::instance().connected()) {
        return;
    }

    ServerComm::instance().getColorsInMonth(year, month);
}

void GreenDaysModel::fetchDay(int year, int month, int day)
{
    assert(day > 0);
    ServerComm::instance().fetchDay(year, month, day);
}

void GreenDaysModel::fetchColors()
{
    if (ServerComm::instance().connected()) {
        ServerComm::instance().getDayColorDefinitions();
    }
}

QString GreenDaysModel::getColorUuid(int year, int month, int day)
{
    if (auto *di = lookup(year, month, day)) {
        if (di->haveColor()) {
            return color_definitions_.dayColors().at(di->color_ix).id_proto();
        }
    }

    return {};
}

QString GreenDaysModel::getColorName(int year, int month, int day)
{
    if (auto *di = lookup(year, month, day)) {
        if (di->haveColor()) {
            const auto rval = color_definitions_.dayColors().at(di->color_ix).color();
            return rval;
        }
    }

    return "transparent"; //MaterialDesignStyling::instance().primary();
}

bool GreenDaysModel::hasMonth(int year, int month)
{
    PackedMonth key = {};
    key.date.month_ = month;
    key.date.year_ = year;
    if (auto it = months_.find(key.as_number); it != months_.end()) {
        return true;
    }

    return false;
}

void GreenDaysModel::fetchedColors(const nextapp::pb::DayColorDefinitions &defs)
{
    color_definitions_ = defs;
    // TODO: If we actually re-fetch the color defs, we must signal all the existing months that they are invalid
    //months_.clear();
    refetchAllMonths();
    emit validChanged();
    emit dayColorsChanged(color_definitions_);
}

uint GreenDaysModel::getColorIx(const QUuid& uuid) const noexcept {
    const auto key = uuid.toString(QUuid::WithoutBraces);

    const auto& list = color_definitions_.dayColors();
    uint row = 0;
    for(auto& c : list) {
        if (key == c.id_proto()) {
            return row;
        }
        ++row;
    }

    assert(false); // Shound not happen
    return 0;
}


void GreenDaysModel::fetchedMonth(const nextapp::pb::Month &month)
{
    PackedMonth key = {};
    key.date.month_ = month.month();
    key.date.year_ = month.year();

    auto& m = months_[key.as_number];
    assert(m.size() >= month.days().size());

    m.fill({});
    for(const auto& md : month.days()) {
        const auto mday = md.date().mday();
        assert(mday > 0);
        auto& day = m.at(md.date().mday() -1);
        toDayInfo(md, day);
    }
    emit updatedMonth(month.year(), month.month());
}

void GreenDaysModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    auto set = [this](const nextapp::pb::Date& when, const QString& color) {
        PackedMonth key = {};
        key.date.month_ = when.month();
        key.date.year_ = when.year();
        if (auto it = months_.find(key.as_number); it != months_.end()) {
            const auto mday = when.mday() -1;
            if (mday >= it->second.size()) {
                assert(false);
                return;
            }
            auto& md = it->second[mday];
            if (setDayColor(md, QUuid{color})) {
                emit updatedMonth(when.year(), when.month());
                emit updatedDay(when.year(), when.month(), when.mday());
            }
        } else  {
            LOG_TRACE << "GreenDaysModel::onUpdate - could not find month for " << when.year() << '-' << when.mday();
        }
    };

    if (update->hasDay()) {
        set(update->day().day().date(), update->day().day().color());
    }
    if (update->hasDayColor()) {
        set(update->dayColor().date(), update->dayColor().color());
    }
}

void GreenDaysModel::onOnline()
{
    fetchColors();
}

void GreenDaysModel::refetchAllMonths()
{
    for(const auto &[v, _] : months_) {
        PackedMonth key = {};
        key.as_number = v;
        fetchMonth(key.date.year_, key.date.month_);
    }
}

uint32_t GreenDaysModel::getKey(int year, int month) noexcept
{
    PackedMonth key = {};
    key.date.month_ = month;
    key.date.year_ = year;
    return key.as_number;
}

GreenDaysModel::DayInfo *GreenDaysModel::lookup(int year, int month, int day)
{
    assert(day > 0);
    if (auto it = months_.find(getKey(year, month)); it != months_.end()) {
        return &it->second.at(day-1);
    }

    return {};
}

void GreenDaysModel::toDayInfo(const nextapp::pb::Day &day, DayInfo &di)
{
    QUuid color{day.color()};
    setDayColor(di, color);
    di.have_notes = day.hasNotes();
    di.have_report = day.hasReport();
    di.valid = true;
}

bool GreenDaysModel::setDayColor(DayInfo &di, const QUuid &color)
{
    if (color.isNull()) {
        if (di.haveColor()) {
            di.clearColor();
            return true;
        }
    } else {
        auto ix = getColorIx(color);
        if (ix != di.color_ix) {
            di.color_ix = ix;
            return true;
        }
    }
    return false; // Not changed
}
