#pragma once

//#include <queue>

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>
#include "qcorotask.h"

#include "nextapp.qpb.h"

class GreenDaysModel;

/*! Async object representing a day-color.
 *
 *  It may be obtained before it's fetched from the server, in
 *  wich case `valid' will be false until it has received the
 *  it's state.
 */
class GreenDayModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_VALUE_TYPE(GreenDayModel)

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(QString color READ color NOTIFY colorChanged)
    Q_PROPERTY(QString colorUuid READ colorUuid NOTIFY colorUuidChanged)
    Q_PROPERTY(QString colorUuid READ colorUuid WRITE setColorUuid NOTIFY colorUuidChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY notesChanged)
    Q_PROPERTY(QString report READ report WRITE setReport NOTIFY reportChanged)

public:
    GreenDayModel(int year, int month, int day, GreenDaysModel* parent);

    bool valid() const noexcept;

    QString color() const;

    QString colorUuid() const;

    int day() const;

    int month() const;

    int year() const;

    bool haveNotes() const;

    bool haveReport() const;

    QString report() const;

    QString notes() const;

    void setNotes(const QString& value);

    void setReport(const QString& value);

    void setColorUuid(const QString& value);

    // Commit changes
    Q_INVOKABLE void commit();

    // Revert to the saved value
    Q_INVOKABLE void revert();

    GreenDaysModel& parent();

    static std::optional<QString> notes(const nextapp::pb::CompleteDay& day);
    static std::optional<QString> report(const nextapp::pb::CompleteDay& day);

signals:

    void validChanged();
    void colorChanged();
    void colorUuidChanged();
    void notesChanged();
    void reportChanged();

public slots:
    void receivedDay(const nextapp::pb::CompleteDay& day);

    // Used to update the state if it is changed
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

private:
    void updateSelf(const nextapp::pb::CompleteDay& day);
    // Called if we need to fect the day.
    QCoro::Task<void> fetch();

    bool valid_{false};
    nextapp::pb::CompleteDay day_;
    nextapp::pb::CompleteDay old_day_;
    const int year_;
    const int month_;
    const int mday_;
};







