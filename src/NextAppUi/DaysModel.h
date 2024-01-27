#pragma once

#include <queue>

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>

#include "MonthModel.h"

#include "nextapp.qpb.h"

class DayModel;

union PackedDate {
    uint32_t as_number;
    struct date {
         uint32_t year_        : 16 = 0;
         uint32_t month_       :  8 = 0;
         uint32_t day_         :  8 = 0;
    };
};

union PackedMonth {
    uint16_t as_number;
    struct {
        uint32_t year_        : 12 = 0;
        uint32_t month_       :  4 = 0;
    } date;
};


// /*! Async object representing a day-color.
//  *
//  *  It may be obtained before it's fetched from the server, in
//  *  wich case `valid' will be false until it has received the
//  *  it's state.
//  */
// class DayModel : public QObject {
//     Q_OBJECT
//     QML_ELEMENT

//     Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
//     Q_PROPERTY(QString color READ color NOTIFY colorChanged)
//     Q_PROPERTY(QString colorUuid READ colorUuid NOTIFY colorUuidChanged)
//     Q_PROPERTY(QString colorUuid READ colorUuid WRITE setColorUuid NOTIFY colorUuidChanged)
//     Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY notesChanged)
//     Q_PROPERTY(QString report READ report WRITE setReport NOTIFY reportChanged)

// public:
//     DayModel(uint day, uint month, uint year, DaysModel* parent);

//     bool valid() const noexcept {
//         return valid_;
//     }

//     QString color() const noexcept {
//         return color_;
//     }

//     QString colorUuid() const noexcept {
//         return color_uuid_.toString(QUuid::WithoutBraces);
//     }

//     auto day() const noexcept {
//         return day_;
//     }

//     auto month() const noexcept {
//         return month_;
//     }

//     auto year() const noexcept {
//         return year_;
//     }

//     bool haveNotes() const noexcept {
//         return have_report_;
//     }

//     bool haveReport() const noexcept {
//         return have_report_;
//     }

//     QString report() const {
//         return report_;
//     }

//     QString notes() const {
//         return notes_;
//     }

//     void setNotes(const QString& value) {
//         if (value != notes_) {
//             notes_ = value;
//             emit notesChanged();
//         }
//     }

//     void setReport(const QString& value) {
//         if (value != report_) {
//             report_ = value;
//             emit notesChanged();
//         }
//     }

//     void setColorUuid(const QString& value) {
//         const QUuid uuid{value};
//         if (uuid != color_uuid_) {
//             color_uuid_ = uuid;
//             emit colorUuidChanged();
//         }
//     }

//     // Commit changes
//     Q_INVOKABLE void commit() {}

//     // Revert to the saved value
//     Q_INVOKABLE void revert() {}

//     // Called if we need to fect the day.
//     Q_INVOKABLE void fetch();

//     DaysModel& parent();

// signals:

//     void validChanged();
//     void colorChanged();
//     void colorUuidChanged();
//     void notesChanged();
//     void reportChanged();

// public slots:
//     void fetched(nextapp::pb::Day& day);

//     // Used to update the state if it is changed
//     void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

// private:

//     bool valid_{false};
//     QUuid color_uuid_;
//     QString color_;
//     QString notes_;
//     QString report_;

//     uint32_t day_         :  5 = 0;
//     uint32_t month_       :  4 = 0;
//     uint32_t have_notes_  :  1 = 0;
//     uint32_t have_report_ :  1 = 0;
//     uint32_t color_only_  :  1 = 0; // Have fetched/is fetching only the color
//     uint32_t padding_     :  5 = 0;
//     uint32_t year_        : 16 = 0;
// };


/*! Business logic to work with days and months from QML.
 *
 *  We cache the relevant days in memory.
 *
 *  Initially, we populate only the day-colors in bulk.
 *  Later we may re-fecth individual days to the cache
 *  with the complete information.
 *
 *  We keep ownership of the DayModel objects we pass to Qml.
 */
class DaysModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
public:
    struct DayInfo {
        DayInfo() = default;
        DayInfo(const DayInfo&) = default;
        DayInfo& operator = (const DayInfo&) = default;

        uint32_t color_ix     : 8 = noclor_tag; // Index into the global color table.
        uint32_t have_notes   : 1 = 0;
        uint32_t have_report  : 1 = 0;
        uint32_t valid        : 1 = 0;

        bool haveColor() const noexcept {
            return color_ix != noclor_tag;
        }

        void clearColor() {
            color_ix = noclor_tag;
        }

        static constexpr uint8_t noclor_tag = 0xff;
    };

    DaysModel();

    static DaysModel* instance() noexcept {
        return instance_;
    }

    //using day_table_t = std::unordered_map<uint32_t, >;

    Q_INVOKABLE DayModel *getDay(int year, int month, int day);
    Q_INVOKABLE MonthModel *getMonth(int year, int month);

    std::optional<nextapp::pb::DayColor> getDayColor(const QUuid& uuid) const;

    void start();

    void fetchMonth(int year, int month);
    void fetchDay(int year, int month, int day);
    void fetchColors();

    bool valid() const noexcept {
        return !color_definitions_.dayColors().empty();
    }

    QString getColorUuid(int year, int month, int day);
    QString getColorName(int year, int month, int day);
    bool hasMonth(int year, int month);

    [[nodiscard]] uint getColorIx(const QUuid& uuid) const noexcept;

signals:
    void validChanged();
    void updatedMonth(int year, int month);
    void updatedDay(int year, int month, int day);
    void dayColorsChanged(const nextapp::pb::DayColorDefinitions& defs);

public slots:

    void fetchedColors(const nextapp::pb::DayColorDefinitions& defs);
    void fetchedMonth(const nextapp::pb::Month& defs);

    // Used to update the state if it is changed
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

private:
    uint32_t static getKey(int year, int month) noexcept;
    DayInfo *lookup(int year, int month, int day);
    void toDayInfo(const nextapp::pb::Day& day, DaysModel::DayInfo& di);
    // Returns true if the value was changed
    bool setDayColor(DayInfo &di, const QUuid& color);
    using days_in_month_t = std::array<DayInfo, 31>;
    using months_t = std::map<uint16_t, days_in_month_t>;
    std::queue<std::function<void()>> actions_queue_;
    months_t months_;
    nextapp::pb::DayColorDefinitions color_definitions_;
    bool started_ = false;
    static DaysModel *instance_;
};





