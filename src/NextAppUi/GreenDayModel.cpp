#include "GreenDayModel.h"
#include "GreenDaysModel.h"
#include "NextAppCore.h"
#include "ServerComm.h"
#include "util.h"

GreenDayModel::GreenDayModel(int year, int month, int day, GreenDaysModel *parent)
    : QObject{parent}, year_{year}, month_{month}, mday_{day}
{
    assert(year_);
    connect(&ServerComm::instance(),
            &ServerComm::receivedDay,
            this,
            &GreenDayModel::receivedDay);

    connect(&ServerComm::instance(),
            &ServerComm::onUpdate,
            this,
            &GreenDayModel::onUpdate);

    fetch();
}

bool GreenDayModel::valid() const noexcept {
    return valid_;
}

QString GreenDayModel::color() const {
    if (auto dc = GreenDaysModel::instance()->getDayColor(QUuid{day_.day().color()})) {
        return dc->color;
    }
    return {};
}

QString GreenDayModel::colorUuid() const {
    return day_.day().color();
}

int GreenDayModel::day() const {
    return day_.day().date().mday();
}

int GreenDayModel::month() const {
    return day_.day().date().month() + 1;
}

int GreenDayModel::year() const {
    return day_.day().date().year();
}

bool GreenDayModel::haveNotes() const {
    return day_.day().hasNotes();
}

bool GreenDayModel::haveReport() const {
    return day_.day().hasReport();
}

QString GreenDayModel::report() const {
    if (auto val = report(day_)) {
        return *val;
    }
    return {};
}

QString GreenDayModel::notes() const {
    if (auto val = notes(day_)) {
        return *val;
    }
    return {};
}

void GreenDayModel::setNotes(const QString &value) {
    if (value != notes()) {
        day_.setNotes(value);
        emit notesChanged();
    }
}

void GreenDayModel::setReport(const QString &value) {
    if (value != report()) {
        day_.setReport(value);
        emit notesChanged();
    }
}

void GreenDayModel::setColorUuid(const QString &value) {
    const QUuid uuid{value};
    if (uuid != QUuid{colorUuid()}) {
        day_.day().setColor(uuid.toString(QUuid::WithoutBraces));
        emit colorUuidChanged();
    }
}

void GreenDayModel::commit() {

    // Avoid sending `00000000-0000-0000-0000-000000000000` which will not work on the server
    day_.day().setColor(toValidQuid(day_.day().color()));

    assert(day_.day().date().year() == year_);
    assert(day_.day().date().month() + 1 == month_);
    assert(day_.day().date().mday() == mday_);

    ServerComm::instance().setDay(day_);
}

void GreenDayModel::revert() {
    fetch();
}

GreenDaysModel &GreenDayModel::parent()
{
    return dynamic_cast<GreenDaysModel&>(*QObject::parent());
}

std::optional<QString> GreenDayModel::notes(const nextapp::pb::CompleteDay &day)
{
    if (day.hasNotes()) {
        return day.notes();
    }
    return {};
}

std::optional<QString> GreenDayModel::report(const nextapp::pb::CompleteDay &day)
{
    if (day.hasReport()) {
        return day.report();
    }
    return {};
}

void GreenDayModel::receivedDay(const nextapp::pb::CompleteDay& day) {
    updateSelf(day);
}

void GreenDayModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    assert(update);
    if (update->hasDay()) {
        const auto& date = update->day().day().date();
        if (date == day_.day().date()) {
            updateSelf(update->day());
        }
    }
}

void GreenDayModel::updateSelf(const nextapp::pb::CompleteDay &day) {
    const auto old = day_;
    day_ = day;

    const auto& date = day_.day().date();

    if (!valid_) {
        valid_ = true;
        emit validChanged();
    }

    if (old.day().color() != day_.day().color()) {
        emit colorChanged();
        emit colorUuidChanged();
    }

    if (notes(old) != notes(day)) {
        emit notesChanged();
    }

    if (report(old) != report(day_)) {
        emit reportChanged();
    }
}

QCoro::Task<void> GreenDayModel::fetch()
{
    // TODO: Handle offline/not synched states
    auto& db = NextAppCore::instance()->db();

    QList<QVariant> params;
    params.append(QDate{year_, month_, mday_});
    auto res = co_await db.query("SELECT date, color, notes, report, updated FROM day WHERE date = ?", &params);
    enum Cols { DATE, COLOR, NOTES, REPORT, UPDATED };
    if (res) {
        if (!res.value().empty()) {
            const auto& row = res.value().front();
            nextapp::pb::CompleteDay cd;
            nextapp::pb::Day d;
            d.setDate(toDate((row[DATE].toDate())));
            d.setColor(row[COLOR].toString());
            d.setUpdated(row[UPDATED].toLongLong());
            cd.setDay(std::move(d));

            if (!row[NOTES].isNull()) {
                cd.setNotes(row[NOTES].toString());
            }

            if (!row[REPORT].isNull()) {
                cd.setReport(row[REPORT].toString());
            }

            day_ = cd;
            assert(day_.day().date().year() == year_);
            assert(day_.day().date().month() + 1 == month_);
            assert(day_.day().date().mday() == mday_);
        } else {
            QDate date{year_, month_, mday_};
            day_.day().setDate(toDate(date));
        }
        valid_ = true; // Non-existent in the db is also a valid state.
    } else {
        valid_ = false;
    }

    emit validChanged();
}
