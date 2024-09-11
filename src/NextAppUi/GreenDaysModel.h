#pragma once

#include <queue>

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>

#include "GreenMonthModel.h"

#include "nextapp.qpb.h"

class GreenDayModel;

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


/*! Business logic to work with days and months from QML.
 *
 *  We cache the relevant days in memory.
 *
 *  Initially, we populate only the day-colors in bulk.
 *  Later we may re-fecth individual days to the cache
 *  with the complete information.
 *
 *  We keep ownership of the GreenDayModel objects we pass to Qml.
 */
class GreenDaysModel : public QObject
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

    GreenDaysModel();

    static GreenDaysModel* instance() noexcept {
        return instance_;
    }

    //using day_table_t = std::unordered_map<uint32_t, >;

    Q_INVOKABLE GreenDayModel *getDay(int year, int month, int day);
    Q_INVOKABLE GreenMonthModel *getMonth(int year, int month);

    std::optional<nextapp::pb::DayColor> getDayColor(const QUuid& uuid) const;

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
    void onOnline();
    void refetchAllMonths();

    uint32_t static getKey(int year, int month) noexcept;
    DayInfo *lookup(int year, int month, int day);
    void toDayInfo(const nextapp::pb::Day& day, GreenDaysModel::DayInfo& di);
    // Returns true if the value was changed
    bool setDayColor(DayInfo &di, const QUuid& color);
    using days_in_month_t = std::array<DayInfo, 31>;
    using months_t = std::map<uint16_t, days_in_month_t>;
    months_t months_;
    nextapp::pb::DayColorDefinitions color_definitions_;
    static GreenDaysModel *instance_;
};





