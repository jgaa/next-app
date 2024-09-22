#pragma once

#include <queue>
#include <set>

#include <QObject>
#include <QMap>
#include <QHash>
#include <QUuid>
#include <QAbstractItemModel>
#include "qcorotask.h"

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

public:
    struct ColorDef {
        QString id;
        QString name;
        QString color;
        int score{};
    };

    enum class State {
        LOCAL,
        SYNCHING,
        SYNCHED,
        LOADING,
        VALID,
        ERROR
    };

    Q_ENUM(State)

    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(State state MEMBER state_ NOTIFY stateChanged)


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

    void Q_INVOKABLE addYear(int year);

    //using day_table_t = std::unordered_map<uint32_t, >;

    // Year as an int, month as 1 - 12, day as 1 - 31
    Q_INVOKABLE GreenDayModel *getDay(int year, int month, int day);

    // Year as an int, month as 1 - 12
    Q_INVOKABLE GreenMonthModel *getMonth(int year, int month);

    std::optional<ColorDef> getDayColor(const QUuid& uuid) const;

    void fetchMonth(int year, int month);
    void fetchDay(int year, int month, int day);
    void fetchColors();

    bool valid() const noexcept {
        return state_ == State::VALID;
    }

    QString getColorUuid(int year, int month, int day);
    QString getColorName(int year, int month, int day);
    bool hasMonth(int year, int month);

    [[nodiscard]] uint getColorIx(const QUuid& uuid) const noexcept;

    const nextapp::pb::DayColorDefinitions getAsDayColorDefinitions() const;

signals:
    void validChanged();
    void updatedMonth(int year, int month);
    void updatedDay(int year, int month, int day);
    void dayColorsChanged(const nextapp::pb::DayColorDefinitions& defs);
    void stateChanged(State state);

public slots:

    // void fetchedColors(const nextapp::pb::DayColorDefinitions& defs);
    // void fetchedMonth(const nextapp::pb::Month& defs);

    // Used to update the state if it is changed
    void onUpdate(const std::shared_ptr<nextapp::pb::Update>& update);

private:
    QCoro::Task<void> onUpdatedDay(nextapp::pb::CompleteDay completeDay);
    QCoro::Task<void> onOnline();
    void refetchAllMonths();
    void setState(State state) noexcept;
    QCoro::Task<void> synchFromServer();
    QCoro::Task<bool> synchColorsFromServer();
    QCoro::Task<bool> synchDaysFromServer();
    QCoro::Task<void> loadFromCache();
    QCoro::Task<bool> loadColorDefsFromCache();
    QCoro::Task<bool> loadDaysFromCache();
    QCoro::Task<bool> storeDay(const nextapp::pb::CompleteDay& day);

    State state_{State::LOCAL};
    // Year as an int, month as 1 - 12
    uint32_t static getKey(int year, int month) noexcept;
    // Year as an int, month as 1 - 12, day as 1 - 31
    DayInfo *lookup(int year, int month, int day);
    void toDayInfo(const nextapp::pb::Day& day, GreenDaysModel::DayInfo& di);
    // Returns true if the value was changed
    bool setDayColor(DayInfo &di, const QUuid& color);
    using days_in_month_t = std::array<DayInfo, 31>;
    using months_t = std::map<uint16_t, days_in_month_t>;
    months_t months_;
    std::set<int> years_to_cache_;
    //nextapp::pb::DayColorDefinitions color_definitions_;
    std::map<QUuid, uint> color_definitions_;
    std::vector<ColorDef> color_data_;
    static GreenDaysModel *instance_;
};






