#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>

#include "LocalReadingStatsModel.h"
#include "ReadingStatsRecorder.h"

namespace {
struct Session {
  LocalStats::Totals totals;
  LocalStats::Pace pace;
  static bool accept(void* context, const ReadingStatsEvent& event) {
    auto& self = *static_cast<Session*>(context);
    LocalStats::add(self.totals, event.startEpochSeconds, event.durationSeconds);
    self.pace.credit(event.durationSeconds);
    return true;
  }
  ReadingStatsRecorder recorder{accept, this};
  void show(uint32_t progress, uint64_t time) {
    recorder.showPage(progress, 10000, time);
    pace.show(progress);
  }
};
time_t civil(int year, int month, int day, int hour, int minute, int second) {
  tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = second;
  local.tm_isdst = -1;
  return mktime(&local);
}
class LocalReadingStatsTest : public testing::Test {
 protected:
  void SetUp() override {
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
  }
};
}  // namespace
TEST_F(LocalReadingStatsTest, PaceRequiresThreeTransitionsAndThirtySeconds) {
  Session s;
  uint32_t seconds = 0;
  s.show(0, 0);
  s.show(10, 10000);
  s.show(20, 20000);
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
  s.show(30, 30000);
  ASSERT_TRUE(s.pace.estimate(.5f, seconds));
  EXPECT_EQ(seconds, 5000u);
}
TEST_F(LocalReadingStatsTest, DuplicateRepaintAndCheckpointDoNotAddTransitionsOrDoubleCount) {
  Session s;
  uint32_t seconds;
  s.show(0, 0);
  s.recorder.checkpoint(30000);
  s.show(0, 40000);
  s.recorder.checkpoint(60000);
  EXPECT_EQ(s.totals.seconds, 60u);
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
  s.show(10, 70000);
  s.show(20, 80000);
  s.show(30, 90000);
  EXPECT_EQ(s.totals.seconds, 90u);
  ASSERT_TRUE(s.pace.estimate(.5f, seconds));
  EXPECT_EQ(seconds, 15000u);
}
TEST_F(LocalReadingStatsTest, RapidBackwardAndLargeJumpClearConfidence) {
  Session s;
  uint32_t seconds;
  s.show(0, 0);
  s.show(10, 10000);
  s.show(20, 20000);
  s.show(30, 30000);
  ASSERT_TRUE(s.pace.estimate(.5f, seconds));
  s.show(40, 31000);
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
  EXPECT_EQ(s.totals.seconds, 30u);
  s.show(50, 41000);
  s.show(60, 51000);
  s.show(70, 61000);
  ASSERT_TRUE(s.pace.estimate(.5f, seconds));
  s.show(60, 71000);
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
  s.show(1000, 81000);
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
  s.pace.reset();
  EXPECT_FALSE(s.pace.estimate(.5f, seconds));
}
TEST_F(LocalReadingStatsTest, IdleAndUnknownClockDoNotFabricateDays) {
  Session s;
  s.show(0, 0);
  s.recorder.checkpoint(300000);
  s.recorder.checkpoint(400000);
  EXPECT_EQ(s.totals.seconds, 120u);
  for (const auto& day : s.totals.daily) EXPECT_EQ(day.dateKey, 0);
}
TEST_F(LocalReadingStatsTest, MidnightSplitsIntoConfiguredLocalDays) {
  LocalStats::Totals totals;
  LocalStats::add(totals, civil(2026, 9, 19, 23, 59, 50), 30);
  LocalStats::DayTotal days[30];
  LocalStats::recentDays(totals, civil(2026, 9, 20, 0, 0, 20), days);
  EXPECT_EQ(days[0].dateKey, 20260920);
  EXPECT_EQ(days[0].seconds, 20u);
  EXPECT_EQ(days[1].dateKey, 20260919);
  EXPECT_EQ(days[1].seconds, 10u);
}
TEST_F(LocalReadingStatsTest, DstTransitionsAndShortDaysKeepCorrectCivilDate) {
  LocalStats::Totals totals;
  LocalStats::DayTotal days[30];
  LocalStats::add(totals, civil(2026, 3, 8, 1, 59, 50), 30);
  LocalStats::recentDays(totals, civil(2026, 3, 9, 0, 0, 0), days);
  EXPECT_EQ(days[1].dateKey, 20260308);
  EXPECT_EQ(days[1].seconds, 30u);
  EXPECT_EQ(days[2].dateKey, 20260307);
  LocalStats::add(totals, civil(2026, 11, 1, 0, 59, 50), 30);
  LocalStats::recentDays(totals, civil(2026, 11, 2, 0, 0, 0), days);
  EXPECT_EQ(days[1].dateKey, 20261101);
  EXPECT_EQ(days[1].seconds, 30u);
}
TEST_F(LocalReadingStatsTest, BoundedHistoryRetainsLifetimeTotalAndRejectsInvalidEstimate) {
  LocalStats::Totals totals;
  LocalStats::DayTotal days[30];
  for (int day = 1; day <= 31; ++day) LocalStats::add(totals, civil(2026, 8, day, 12, 0, 0), 10);
  EXPECT_EQ(totals.seconds, 310u);
  LocalStats::recentDays(totals, civil(2026, 10, 1, 0, 0, 0), days);
  for (const auto& day : days) EXPECT_EQ(day.seconds, 0u);
  LocalStats::Pace pace;
  uint32_t seconds;
  pace.show(0);
  for (unsigned i = 1; i <= 3; ++i) {
    pace.credit(10);
    pace.show(i * 10);
  }
  EXPECT_FALSE(pace.estimate(std::numeric_limits<float>::quiet_NaN(), seconds));
  EXPECT_FALSE(pace.estimate(-1, seconds));
  EXPECT_FALSE(pace.estimate(2, seconds));
}
