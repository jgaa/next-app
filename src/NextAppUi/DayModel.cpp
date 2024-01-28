#include "DayModel.h"
#include "DaysModel.h"
#include "ServerComm.h"
#include "util.h"

DayModel::DayModel(int year, int month, int day, DaysModel *parent)
    : QObject{parent}, year_{year}, month_{month}, mday_{day}
{
    assert(year_);
    connect(&ServerComm::instance(),
            &ServerComm::receivedDay,
            this,
            &DayModel::receivedDay);

    connect(&ServerComm::instance(),
            &ServerComm::onUpdate,
            this,
            &DayModel::onUpdate);

    fetch();
}

bool DayModel::valid() const noexcept {
    return valid_;
}

QString DayModel::color() const {
    if (auto dc = DaysModel::instance()->getDayColor(QUuid{day_.day().color()})) {
        return dc->color();
    }
    return {};
}

QString DayModel::colorUuid() const {
    return day_.day().color();
}

int DayModel::day() const {
    return day_.day().date().mday();
}

int DayModel::month() const {
    return day_.day().date().month();
}

int DayModel::year() const {
    return day_.day().date().year();
}

bool DayModel::haveNotes() const {
    return day_.day().hasNotes();
}

bool DayModel::haveReport() const {
    return day_.day().hasReport();
}

QString DayModel::report() const {
    return day_.report();
}

QString DayModel::notes() const {
    return day_.notes();
}

void DayModel::setNotes(const QString &value) {
    if (value != notes()) {
        day_.setNotes(value);
        emit notesChanged();
    }
}

void DayModel::setReport(const QString &value) {
    if (value != report()) {
        day_.setReport(value);
        emit notesChanged();
    }
}

void DayModel::setColorUuid(const QString &value) {
    const QUuid uuid{value};
    if (uuid != QUuid{colorUuid()}) {
        day_.day().setColor(uuid.toString(QUuid::WithoutBraces));
        emit colorUuidChanged();
    }
}

void DayModel::commit() {

    // Avoid sending `00000000-0000-0000-0000-000000000000` which will not work on the server
    day_.day().setColor(toValidQuid(day_.day().color()));

    ServerComm::instance().setDay(day_);
}

void DayModel::revert() {
    fetch();
}

void DayModel::fetch()
{
    // assert(mday_ > 0);
    // assert(mday_ <= 31);
    ServerComm::instance().fetchDay(year_, month_, mday_);
}

DaysModel &DayModel::parent()
{
    return dynamic_cast<DaysModel&>(*QObject::parent());
}

void DayModel::receivedDay(const nextapp::pb::CompleteDay& day) {
    updateSelf(day);
}

void DayModel::onUpdate(const std::shared_ptr<nextapp::pb::Update> &update)
{
    assert(update);
    if (update->hasDayColor()) {
        const auto& date = update->dayColor().date();
        if (date == day_.day().date()) {
            auto day = day_;
            day.day().setColor(update->dayColor().color());
            updateSelf(day);
        }
    } else if (update->hasDay()) {
        const auto& date = update->day().day().date();
        if (date == day_.day().date()) {
            updateSelf(update->day());
        }
    }
}

void DayModel::updateSelf(const nextapp::pb::CompleteDay &day) {
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

    if (old.notes() != day_.notes()) {
        emit colorChanged();
    }

    if (old.report() != day_.report()) {
        emit reportChanged();
    }
}
