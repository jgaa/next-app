#include "DaysModel.h"
#include "DayModel.h"
#include "MaterialDesignStyling.h"

#include "ServerComm.h"
#include "logging.h"

using namespace std;
using namespace nextapp;

// DayModel::DayModel(uint day, uint month, uint year, DaysModel *parent)
//     : QObject{parent}, day_{day}, month_{month}, year_{year}
// {
//     assert(parent);
// }

// DaysModel &DayModel::parent()
// {
//     auto *p = QObject::parent();
//     assert(p);
//     return dynamic_cast<DaysModel&>(*p);
// }

DaysModel *DaysModel::instance_;

DaysModel::DaysModel()
{
    instance_ = this;
}

DayModel *DaysModel::getDay(int year, int month, int day)
{
    assert(day > 0);
    auto rval = make_unique<DayModel>(year, month, day, this);

    assert(day > 0);
    fetchDay(year, month, day);
    return rval.release();
}

MonthModel *DaysModel::getMonth(int year, int month)
{
    const auto key = getKey(year, month);
    if (!hasMonth(year, month)) {

        months_[key] = {};
        fetchMonth(year, month);
    }

    return new MonthModel(year, month, *this);
}

std::optional<pb::DayColor> DaysModel::getDayColor(const QUuid &uuid) const
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

void DaysModel::start()
{
    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedDayColorDefinitions,
            this,
            &DaysModel::fetchedColors);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::receivedMonth,
            this,
            &DaysModel::fetchedMonth);

    connect(std::addressof(ServerComm::instance()),
            &ServerComm::onUpdate,
            this,
            &DaysModel::onUpdate);

    started_ = true;
    fetchColors();
}

void DaysModel::fetchMonth(int year, int month)
{
    if (!valid()) {
        actions_queue_.emplace([this, year, month] {
            fetchMonth(year, month);
        });
        return;
    }

    ServerComm::instance().getColorsInMonth(year, month);
}

void DaysModel::fetchDay(int year, int month, int day)
{
    assert(day > 0);
    ServerComm::instance().fetchDay(year, month, day);
}

void DaysModel::fetchColors()
{
    if (!started_) {
        actions_queue_.emplace([this] {
            fetchColors();
        });
        return;
    }

    ServerComm::instance().getDayColorDefinitions();
}

QString DaysModel::getColorUuid(int year, int month, int day)
{
    if (auto *di = lookup(year, month, day)) {
        if (di->haveColor()) {
            return color_definitions_.dayColors().at(di->color_ix).id_proto();
        }
    }

    return {};
}

QString DaysModel::getColorName(int year, int month, int day)
{
    if (auto *di = lookup(year, month, day)) {
        if (di->haveColor()) {
            const auto rval = color_definitions_.dayColors().at(di->color_ix).color();
            return rval;
        }
    }

    return "transparent"; //MaterialDesignStyling::instance().primary();
}

bool DaysModel::hasMonth(int year, int month)
{
    PackedMonth key = {};
    key.date.month_ = month;
    key.date.year_ = year;
    if (auto it = months_.find(key.as_number); it != months_.end()) {
        return true;
    }

    return false;
}

void DaysModel::fetchedColors(const nextapp::pb::DayColorDefinitions &defs)
{
    color_definitions_ = defs;
    // TODO: If we actually re-fetch the color defs, we must signal all the existing months that they are invalid
    months_.clear();
    for(; !actions_queue_.empty(); actions_queue_.pop()) {
        actions_queue_.front()();
    }
    emit validChanged();
    emit dayColorsChanged(color_definitions_);
}

uint DaysModel::getColorIx(const QUuid& uuid) const noexcept {
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


void DaysModel::fetchedMonth(const nextapp::pb::Month &month)
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

void DaysModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
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
            LOG_TRACE << "DaysModel::onUpdate - could not find month for " << when.year() << '-' << when.mday();
        }
    };

    if (update->hasDay()) {
        set(update->day().day().date(), update->day().day().color());
    }
    if (update->hasDayColor()) {
        set(update->dayColor().date(), update->dayColor().color());
    }
}

uint32_t DaysModel::getKey(int year, int month) noexcept
{
    PackedMonth key = {};
    key.date.month_ = month;
    key.date.year_ = year;
    return key.as_number;
}

DaysModel::DayInfo *DaysModel::lookup(int year, int month, int day)
{
    assert(day > 0);
    if (auto it = months_.find(getKey(year, month)); it != months_.end()) {
        return &it->second.at(day-1);
    }

    return {};
}

void DaysModel::toDayInfo(const nextapp::pb::Day &day, DayInfo &di)
{
    QUuid color{day.color()};
    setDayColor(di, color);
    di.have_notes = day.hasNotes();
    di.have_report = day.hasReport();
    di.valid = true;
}

bool DaysModel::setDayColor(DayInfo &di, const QUuid &color)
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
