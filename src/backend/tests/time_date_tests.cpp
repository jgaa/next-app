
#include <format>

#include "date/date.h"
#include "date/tz.h"

#include "gtest/gtest.h"

#include "nextapp/config.h"
#include "nextapp/logging.h"
#include "nextapp/Server.h"
#include "nextapp/errors.h"
#include "nextapp/GrpcServer.h"

using namespace std;
using namespace nextapp;
using namespace date;


auto toTimet(const auto& when) {
    const auto zoned = make_zoned(current_zone(), local_days(when));
    return chrono::system_clock::to_time_t(zoned.get_sys_time());
}

TEST(processDueAtDate, setDueDate1day) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/1));
}

TEST(processDueAtDate, setDueDate2days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           2,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/2));
}

TEST(processDueAtDate, setDueDate3days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           3,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/3));
}

TEST(processDueAtDate, setDueDate4days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           4,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/4));
}

TEST(processDueAtDate, setDueDate5days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           5,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/5));
}

TEST(processDueAtDate, setDueDate6days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           6,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/6));
}

TEST(processDueAtDate, setDueDate7days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           7,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/7));
}

TEST(processDueAtDate, setDueDate8days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           8,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/8));
}

TEST(processDueAtDate, setDueDate9days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           9,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/9));
}

TEST(processDueAtDate, setDueDate10days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           10,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/10));
}

TEST(processDueAtDate, setDueDate100days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           100,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/June/8));
}

TEST(processDueAtDate, setDueDate1000days) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_DAYS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1000,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2026_y/November/25));
}

TEST(processDueAtDate, setDueDate1Week) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/7));
}

TEST(processDueAtDate, setDueDate2Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           2,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/14));
}

TEST(processDueAtDate, setDueDate3Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           3,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/21));
}

TEST(processDueAtDate, setDueDate4Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           4,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/28));
}

TEST(processDueAtDate, setDueDate5Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           5,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/4));
}

TEST(processDueAtDate, setDueDate6Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           6,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/11));
}

TEST(processDueAtDate, setDueDate7Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           7,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/18));
}

TEST(processDueAtDate, setDueDate100Weeks) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_WEEKS,
                                                           pb::ActionDueKind::DATETIME,
                                                           100,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2026_y/January/29));
}

TEST(processDueAtDate, setDueDate1Month) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/January/31));
}

TEST(processDueAtDate, setDueDate2Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           2,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/February/29));
}

TEST(processDueAtDate, setDueDate3Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           3,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/March/31));
}

TEST(processDueAtDate, setDueDate4Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           4,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/30));
}

TEST(processDueAtDate, setDueDate5Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           5,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/May/31));
}

TEST(processDueAtDate, setDueDate6Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           6,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/June/30));
}

TEST(processDueAtDate, setDueDate7Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           7,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/July/31));
}

TEST(processDueAtDate, setDueDate8Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           8,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/August/31));
}

TEST(processDueAtDate, setDueDate9Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           9,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/September/30));
}

TEST(processDueAtDate, setDueDate10Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           10,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/October/31));
}

TEST(processDueAtDate, setDueDate11Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           11,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/November/30));
}

TEST(processDueAtDate, setDueDate12Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           12,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/December/31));
}

TEST(processDueAtDate, setDueDate100Months) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_MONTHS,
                                                           pb::ActionDueKind::DATETIME,
                                                           100,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2032_y/April/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromJanuary) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/January/13),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/13));
}

TEST(processDueAtDate, setDueDate1QuarterFromFebruay) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/29));
}

TEST(processDueAtDate, setDueDate1QuarterFromMarch) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/March/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/April/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromApril) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/April/30),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/July/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromMay) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/May/11),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/July/11));
}

TEST(processDueAtDate, setDueDate1QuarterFromJune) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/June/30),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/July/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromJuly) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/July/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/October/31));
}

TEST(processDueAtDate, setDueDate1QuarterFromAugust) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/August/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/October/31));
}

TEST(processDueAtDate, setDueDate1QuarterFromSeptember) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/September/30),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/October/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromOctober) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/October/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2025_y/January/31));
}

TEST(processDueAtDate, setDueDate1QuarterFromNovember) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/November/30),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2025_y/January/30));
}

TEST(processDueAtDate, setDueDate1QuarterFromDecember) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_QUARTERS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2025_y/January/31));
}

TEST(processDueAtDate, setDueDate1Year) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2023_y/December/31),
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                           pb::ActionDueKind::DATETIME,
                                                           1,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2024_y/December/31));
}

TEST(processDueAtDate, setDueDate2Years) {

    auto due = nextapp::grpc::GrpcServer::processDueAtDate(toTimet(2024_y/February/29), // leap year
                                                           pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                           pb::ActionDueKind::DATETIME,
                                                           2,
                                                           *chrono::current_zone());

    EXPECT_EQ(due.start(), toTimet(2026_y/February/28));
}



TEST(processDueAtDayspec, setMondayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_MONDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Monday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}


TEST(processDueAtDayspec, setTuesdayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_TUESDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Tuesday[1]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setWednesdayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_WEDNESDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Wednesday[1]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setThursdayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_THURSDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Thursday[1]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setFridayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FRIDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Friday[2]); // Roll over to next week

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setSaturdayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_SATURDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Saturday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setSundayAtMonday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_SUNDAY;
    const auto from = toTimet(2024_y/March/Monday[1]);
    const auto valid = toTimet(2024_y/March/Sunday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setMondayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_MONDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Monday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setTuesdayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_TUESDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Tuesday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setWednesdayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_WEDNESDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Wednesday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setThursdayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_THURSDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Wednesday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setFridayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_FRIDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Friday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setSaturdayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_SATURDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Saturday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

TEST(processDueAtDayspec, setSundayAtTuesday) {

    const int val = 1 << pb::Action_RepeatSpecs::Action_RepeatSpecs_SUNDAY;
    const auto from = toTimet(2024_y/March/Tuesday[1]);
    const auto valid = toTimet(2024_y/March/Sunday[2]);

    auto due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                              pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                              pb::ActionDueKind::DATETIME,
                                                              val, *chrono::current_zone(), true); // Week start at sunday
    EXPECT_EQ(due.start(), valid);

    due = nextapp::grpc::GrpcServer::processDueAtDayspec(from,
                                                         pb::Action_RepeatUnit::Action_RepeatUnit_YEARS,
                                                         pb::ActionDueKind::DATETIME,
                                                         val, *chrono::current_zone(), false); // Week start at monday
    EXPECT_EQ(due.start(), valid);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    logfault::LogManager::Instance().AddHandler(
        make_unique<logfault::StreamHandler>(clog, logfault::LogLevel::TRACE));
    return RUN_ALL_TESTS();
}
