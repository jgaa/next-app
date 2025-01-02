#include "GreenDaysModel.h"
#include "GreenDayModel.h"
#include "MaterialDesignStyling.h"

#include "ServerComm.h"
#include "NextAppCore.h"
#include "logging.h"
#include "DbStore.h"
#include "util.h"
#include "format_wrapper.h"

using namespace std;
using namespace nextapp;

GreenDaysModel *GreenDaysModel::instance_;

GreenDaysModel::GreenDaysModel()
{
    instance_ = this;
    years_to_cache_.emplace(QDate::currentDate().year());

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
    const auto [_, added] = years_to_cache_.insert(year);
    if (added) {
        loadFromCache();
    }
}

GreenDayModel *GreenDaysModel::getDay(int year, int month, int day)
{
    assert(day > 0);
    assert(day <= 31);
    assert(month >= 1);
    assert(month <= 12);

    auto rval = make_unique<GreenDayModel>(year, month, day, this);
    return rval.release();
}

GreenMonthModel *GreenDaysModel::getMonth(int year, int month)
{
    assert(month >= 1);
    assert(month <= 12);

    const auto key = getKey(year, month);
    if (!hasMonth(year, month)) {
        months_[key] = {};
        addYear(year);
    }

    auto *mm  = new GreenMonthModel(year, month, *this);
    //QQmlEngine::setObjectOwnership(mm, QQmlEngine::CppOwnership);
    return mm;
}

std::optional<GreenDaysModel::ColorDef> GreenDaysModel::getDayColor(const QUuid &uuid) const
{
    const auto key = uuid.toString(QUuid::WithoutBraces);
    if (auto it = color_definitions_.find(uuid); it != color_definitions_.end()) {
        return color_data_.at(it->second);
    }

    return {};
}


void GreenDaysModel::fetchMonth(int year, int month)
{
    // if (!ServerComm::instance().connected()) {
    //     return;
    // }

    // ServerComm::instance().getColorsInMonth(year, month);
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
            return color_data_.at(di->color_ix).id;
        }
    }

    return {};
}

QString GreenDaysModel::getColorName(int year, int month, int day)
{
    if (auto *di = lookup(year, month, day)) {
        if (di->haveColor()) {
            return color_data_.at(di->color_ix).color;
        }
    }

    return "transparent"; //MaterialDesignStyling::instance().primary();
}

bool GreenDaysModel::hasMonth(int year, int month)
{
    assert(month >= 1);
    assert(month <= 12);
    auto key = getKey(year, month);
    if (auto it = months_.find(key); it != months_.end()) {
        return true;
    }

    return false;
}

uint GreenDaysModel::getColorIx(const QUuid& uuid) const noexcept {

    if (auto it = color_definitions_.find(uuid); it != color_definitions_.end()) {
        return it->second;
    }

    return 0;
}

const pb::DayColorDefinitions GreenDaysModel::getAsDayColorDefinitions() const
{
    nextapp::pb::DayColorDefinitions defs;
    nextapp::pb::DayColorRepeated list;

    for(const auto& cd : color_data_) {
        nextapp::pb::DayColor dc;
        dc.setId_proto(cd.id);
        dc.setColor(cd.color);
        dc.setName(cd.name);
        dc.setScore(cd.score);
        list.append(std::move(dc));
    }

    defs.setDayColors(std::move(list));
    return defs;
}

void GreenDaysModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    if (update->hasDay()) {
        onUpdatedDay(update->day());
    }
}


QCoro::Task<void> GreenDaysModel::onUpdatedDay(nextapp::pb::CompleteDay completeDay)
{
    const auto date = completeDay.day().date();

    co_await storeDay(completeDay);
    auto *di = lookup(date.year(), date.month() + 1, date.mday());
    if (di) {
        toDayInfo(completeDay.day(), *di);
        emit updatedMonth(date.year(), date.month() + 1);
        emit updatedDay(date.year(), date.month() + 1, date.mday());
    }
}

QCoro::Task<void> GreenDaysModel::onOnline()
{
    //fetchColors();
    // if (ServerComm::instance().status() == ServerComm::Status::ONLINE) {
    //     co_await synchFromServer();
    // }
    co_return;
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

        if (state_ == State::VALID || old_state == State::VALID) {
            LOG_TRACE_N << "Valid changed to " << (valid() ? "VALID" : "NOT VALID");
            emit validChanged();
        }
    }
}

QCoro::Task<bool>  GreenDaysModel::synchFromServer()
{
    if (state_ == State::SYNCHING) {
        LOG_DEBUG_N << "We are already synching.";
        co_return false;
    }

    LOG_DEBUG_N << "Synching green days from server";

    setState(State::SYNCHING);

    if (co_await synchColorsFromServer() && co_await synchDaysFromServer()) {
            setState(State::SYNCHED);
            co_await loadFromCache();
            co_return true;
    }

    setState(State::ERROR);
    co_return false;
}

QCoro::Task<bool> GreenDaysModel::synchColorsFromServer()
{
    LOG_DEBUG_N << "Synching colors from server";
    nextapp::pb::GetNewReq req;
    auto& db = NextAppCore::instance()->db();

    LOG_DEBUG_N << "Getting last updated";
    const auto last_updated = co_await db.queryOne<qlonglong>("SELECT MAX(updated) FROM day_colors");

    if (last_updated) {
        LOG_DEBUG_N << "Setting last updated " << last_updated.value();
        req.setSince(last_updated.value());
    }

    LOG_DEBUG_N << "Caling getNewDayColorDefinitions";
    auto res = co_await ServerComm::instance().getNewDayColorDefinitions(req);
    if (res.error() == nextapp::pb::ErrorGadget::Error::OK) {
        if (res.hasDayColorDefinitions()) {
            for(const auto cdd : res.dayColorDefinitions().dayColors()) {
                QList<QVariant> params;
                QString sql = R"(INSERT INTO day_colors
                    (id, color, score, name, updated)
                    VALUES (?, ?, ?, ?, ?)
                    ON CONFLICT(id) DO UPDATE SET
                        color=excluded.color,
                        score=excluded.score,
                        name=excluded.name,
                        updated=excluded.updated)";

                const qlonglong updated = cdd.updated();
                const int score = cdd.score();
                params.append(cdd.id_proto());
                params.append(cdd.color());
                params.append(score);
                params.append(cdd.name());
                params.append(updated);
                const auto rval = co_await db.query(sql, &params);
                if (!rval) {
                    LOG_ERROR_N << "Failed to update day color "
                                << cdd.id_proto() << " " << cdd.name()
                                << " err=" << rval.error();
                    co_return false;
                }
            }
        }

        LOG_DEBUG_N << "Return true";
        co_return true;
    }

    LOG_DEBUG_N << "Return false end";
    co_return false;
}

QCoro::Task<bool> GreenDaysModel::synchDaysFromServer()
{
    // Get a stream of updates from servercomm.
    LOG_DEBUG_N << "Synching days from server";
    nextapp::pb::GetNewReq req;
    auto& db = NextAppCore::instance()->db();

    LOG_TRACE_N << "Getting last updated";
    const auto last_updated = co_await db.queryOne<qlonglong>("SELECT MAX(updated) FROM day");

    if (last_updated) {
        LOG_TRACE_N << "Setting last updated " << last_updated.value();
        req.setSince(last_updated.value());
    }

    LOG_TRACE_N << "Calling synchGreenDays";
    auto stream = ServerComm::instance().synchGreenDays(req);

    bool looks_ok = false;
    LOG_DEBUG_N << "Entering message-loop";
    while (auto update = co_await stream->next<nextapp::pb::Status>()) {
        LOG_TRACE_N << "next returned something";
        if (update.has_value()) {
            auto &u = update.value();
            LOG_TRACE_N << "next has value";
            if (u.error() == nextapp::pb::ErrorGadget::Error::OK) {
                LOG_TRACE_N << "Got OK from server";
                if (u.hasDays()) {
                    LOG_TRACE_N << "Got days from server. Count=" << u.days().days().size();
                    for(const auto& item : u.days().days()) {
                        co_await storeDay(item);
                    }
                 }

            } else {
                LOG_DEBUG_N << "Got error from server. err=" << u.error()
                << " msg=" << u.message();
                co_return false;
            }
        } else {
            LOG_TRACE_N << "Stream returned nothing. Done. err="
                        << update.error().err_code();
            co_return false;
        }
    }

    LOG_DEBUG_N << "End of message-loop. Returns true";
    co_return true;
}

QCoro::Task<void> GreenDaysModel::loadFromCache()
{
    if (state_ == State::LOADING) {
        LOG_DEBUG_N << "Already loading";
        co_return;
    }
    setState(State::LOADING);

    if (!co_await loadColorDefsFromCache() || ! co_await loadDaysFromCache()) {
        setState(State::ERROR);
        co_return;
    }

    setState(State::VALID);
}

QCoro::Task<bool> GreenDaysModel::loadColorDefsFromCache()
{
    auto& db = NextAppCore::instance()->db();

    QString query = "SELECT id, score, color, name FROM day_colors";
    enum Cols {
        ID = 0,
        SCORE,
        COLOR,
        NAME
    };

    color_definitions_.clear();
    color_data_.clear();

    const auto rval = co_await db.query(query);
    if (rval) {
        color_data_.reserve(rval.value().size());
        for (const auto& row : rval.value()) {
            color_definitions_[QUuid(row.at(ID).toString())] = color_data_.size();
            color_data_.push_back({row.at(ID).toString(),
                                     row.at(NAME).toString(),
                                     row.at(COLOR).toString(),
                                     row.at(SCORE).toInt()});
        }
        emit dayColorsChanged(getAsDayColorDefinitions());
        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> GreenDaysModel::loadDaysFromCache()
{
    auto& db = NextAppCore::instance()->db();

    string where;
    assert(!years_to_cache_.empty());

    if (years_to_cache_.size() == 1) {
        where = NA_FORMAT(" WHERE strftime('%Y', date) = '{}' ",
                       *years_to_cache_.begin());
    } else {
        where = NA_FORMAT(" WHERE strftime('%Y', date) BETWEEN '{}' AND '{}'",
                       *years_to_cache_.begin(), *years_to_cache_.rbegin());
    }

    QString query = "SELECT date, color, notes, report FROM day"
                    + QString::fromStdString(where);

    enum Cols {
        DATE = 0,
        COLOR,
        NOTES,
        REPORT
    };

    months_.clear();

    const auto rval = co_await db.query(query);
    if (rval) {
        for (const auto& row : rval.value()) {
            const auto date = row.at(DATE).toDate();
            const auto color = row.at(COLOR).toString();
            const auto notes = row.at(NOTES).toString();
            const auto report = row.at(REPORT).toString();

            DayInfo day;
            day.valid = true;
            if (color.isEmpty()) {
                day.clearColor();
            } else {
                day.color_ix = getColorIx(QUuid{color});
            }
            day.have_notes = !notes.isEmpty();
            day.have_report = !report.isEmpty();
            months_[getKey(date.year(), date.month())][date.day() -1] = day;
        }
        co_return true;
    }

    co_return false;
}

QCoro::Task<bool> GreenDaysModel::storeDay(const nextapp::pb::CompleteDay& fullDay)
{
    auto& db = NextAppCore::instance()->db();
    QList<QVariant> params;

    if (fullDay.day().deleted()) {
        // Delete the day
        QString sql = R"(DELETE FROM day WHERE date = ?)";
        params.append(QDate{fullDay.day().date().year(), fullDay.day().date().month() + 1, fullDay.day().date().mday()});
        LOG_TRACE_N << "Deleting day " << fullDay.day().date().year() << '-'
                    << fullDay.day().date().month() << '-'
                    << fullDay.day().date().mday();
        const auto rval = co_await db.query(sql, &params);
        if (!rval) {
            LOG_ERROR_N << "Failed to delete day "
                        << fullDay.day().date().year() << '-'
                        << fullDay.day().date().month() << '-'
                        << fullDay.day().date().mday()
                        << " err=" << rval.error();
        }
        co_return true;
    }

    QString sql = R"(INSERT INTO day
        (date, color, notes, report, updated)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(date) DO UPDATE SET
            color=excluded.color,
            notes=excluded.notes,
            report=excluded.report,
            updated=excluded.updated)";

    const auto& day = fullDay.day();
    const qlonglong updated = day.updated();
    params.append(QDate{day.date().year(), day.date().month() + 1, day.date().mday()});
    params.append(day.color());
    params.append(fullDay.hasNotes() ? fullDay.notes() : QVariant{});
    params.append(fullDay.hasReport() ? fullDay.report(): QVariant{});
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
        co_return false;
    }

    co_return true;
}

uint32_t GreenDaysModel::getKey(int year, int month) noexcept
{
    assert(month >= 1);
    assert (month <= 12);
    PackedMonth key = {};
    key.date.month_ = month -1;
    key.date.year_ = year;
    return key.as_number;
}

GreenDaysModel::DayInfo *GreenDaysModel::lookup(int year, int month, int day)
{
    assert(day >= 1);
    assert(day <= 31);
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
