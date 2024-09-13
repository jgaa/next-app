#include "GreenDaysModel.h"
#include "GreenDayModel.h"
#include "MaterialDesignStyling.h"

#include "ServerComm.h"
#include "NextAppCore.h"
#include "logging.h"
#include "DbStore.h"
#include "util.h"

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
    years_to_cache_.emplace(QDate::currentDate().year());

    // connect(std::addressof(ServerComm::instance()),
    //         &ServerComm::receivedDayColorDefinitions,
    //         this,
    //         &GreenDaysModel::fetchedColors);

    // connect(std::addressof(ServerComm::instance()),
    //         &ServerComm::receivedMonth,
    //         this,
    //         &GreenDaysModel::fetchedMonth);

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

void GreenDaysModel::addYear(int year)
{
    years_to_cache_.insert(year);
    fetchFromCache();
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

QCoro::Task<void> GreenDaysModel::onOnline()
{
    //fetchColors();
    if (ServerComm::instance().status() == ServerComm::Status::ONLINE) {
        co_await synchFromServer();
    }
}

void GreenDaysModel::refetchAllMonths()
{
    for(const auto &[v, _] : months_) {
        PackedMonth key = {};
        key.as_number = v;
        fetchMonth(key.date.year_, key.date.month_);
    }
}

void GreenDaysModel::setState(State state) noexcept
{
    const auto old_state = state_;
    if (state_ != state) {
        state_ = state;
        emit stateChanged(state);

        if (state_ == State::SYNCHED || old_state == State::SYNCHED) {
            emit validChanged();
        }
    }
}

QCoro::Task<void>  GreenDaysModel::synchFromServer()
{
    if (state_ == State::SYNCHING) {
        LOG_DEBUG_N << "We are already synching.";
        co_return;
    }

    LOG_DEBUG_N << "Synching green days from server";

    setState(State::SYNCHING);

    // Get a stream of updates from servercomm.
    nextapp::pb::GetNewReq req;
    //TODO: Get the latest timestamp we have from the database

    auto& db = NextAppCore::instance()->db();
    const auto last_updated = co_await db.queryOne<qlonglong>("SELECT MAX(updated) FROM day");

    if (last_updated) {
        req.setSince(last_updated.value());
    }
    auto stream = ServerComm::instance().synchGreenDays(req);

    bool looks_ok = false;
    LOG_TRACE_N << "Entering message-loop";
    while (auto update = co_await stream->next<nextapp::pb::Status>()) {
        LOG_TRACE_N << "next returned something";
        if (update.has_value()) {
            auto &u = update.value();
            LOG_TRACE_N << "next has value";
            if (u.error() == nextapp::pb::ErrorGadget::OK) {
                LOG_TRACE_N << "Got OK from server";
                if (u.hasDays()) {
                    LOG_TRACE_N << "Got days from server. Count=" << u.days().days().size();
                    for(const auto& item : u.days().days()) {
                        QList<QVariant> params;

                        if (item.day().deleted()) {
                            // Delete the day
                            QString sql = R"(DELETE FROM day WHERE date = ?)";
                            params.append(QDate{item.day().date().year(), item.day().date().month() + 1, item.day().date().mday()});
                            LOG_TRACE_N << "Deleting day " << item.day().date().year() << '-'
                                        << item.day().date().month() << '-'
                                        << item.day().date().mday();
                            const auto rval = co_await db.query(sql, &params);
                            if (!rval) {
                                LOG_ERROR_N << "Failed to delete day "
                                            << item.day().date().year() << '-'
                                            << item.day().date().month() << '-'
                                            << item.day().date().mday()
                                            << " err=" << rval.error();
                            }
                            continue;
                        }

                        QString sql = R"(INSERT INTO day
        (date, color, notes, report, updated)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(date) DO UPDATE SET
            color=excluded.color,
            notes=excluded.notes,
            report=excluded.report,
            updated=excluded.updated)";

                        const auto& day = item.day();
                        const qlonglong updated = day.updated();
                        params.append(QDate{day.date().year(), day.date().month() + 1, day.date().mday()});
                        params.append(day.color());
                        params.append(item.hasNotes() ? item.notes() : QVariant{});
                        params.append(item.hasReport() ? item.report(): QVariant{});
                        params.append(updated);
                        LOG_TRACE_N << "Updating day " << day.date().year() << '-'
                                    << day.date().month() << '-'
                                    << day.date().mday();
                        const auto rval = co_await db.query(sql, &params);
                        if (!rval) {
                            LOG_ERROR_N << "Failed to update day "
                                        << day.date().year() << '-'
                                        << day.date().month() << '-'
                                        << day.date().mday()
                                        << " err=" << rval.error();
                        }
                    }
                }

            } else {
                LOG_DEBUG_N << "Got error from server. err=" << u.error()
                            << " msg=" << u.message();
                setState(State::ERROR);
                // Todo: Schedule a re-try
                co_return;
            }
        } else {
            LOG_TRACE_N << "Stream returned nothing. Done. err="
                        << update.error().err_code();
            co_return;
        }
    }

    setState(State::SYNCHED);
    co_await fetchFromCache();
}

QCoro::Task<void> GreenDaysModel::fetchFromCache()
{
    if (state_ == State::LOADING) {
        LOG_DEBUG_N << "Already loading";
        co_return;
    }
    setState(State::LOADING);

    auto& db = NextAppCore::instance()->db();

    string where;
    assert(!years_to_cache_.empty());

    if (years_to_cache_.size() == 1) {
        where = format(" WHERE strftime('%Y', updated) = '{}' ",
                       *years_to_cache_.begin());
    } else {
        where = format(" WHERE strftime('%Y', updated) BETWEEN '{}' AND '{}'",
                       *years_to_cache_.begin(), *years_to_cache_.rbegin());
    }

    QString query = "SELECT date, color, notes, report FROM day"
                    + QString::fromStdString(where);

    months_.clear();

    const auto rval = co_await db.query(query);
    if (rval) {
        for (const auto& row : rval.value()) {
            const auto date = row.at(0).toDate();
            const auto color = row.at(1).toString();
            const auto notes = row.at(2).toString();
            const auto report = row.at(3).toString();

            DayInfo day;
            day.valid = true;
            day.color_ix = getColorIx(QUuid{color});
            day.have_notes = !notes.isEmpty();
            day.have_report = !report.isEmpty();
            months_[getKey(date.year(), date.month())][date.day() -1] = day;
        }
    } else {
        setState(State::ERROR);
    }

    setState(State::VALID);
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
