#include <HalClock.h>
#include <gtest/gtest.h>

#include <ctime>

class HalClockUtcTest : public testing::Test {
 protected:
  HalClock clock;

  void SetUp() override {
    Rtc::present = true;
    Rtc::valid = true;
    Rtc::value = {2024, 1, 1, 0, 0, 0, 1};
    clock.begin();
    clock.setTimezone("UTC0");
  }

  void TearDown() override { clock.setTimezone("UTC0"); }
};

TEST_F(HalClockUtcTest, ConvertsCalendarBoundariesAndPost2038Time) {
  struct Example {
    Rtc::DateTime date;
    int64_t epoch;
  };
  const Example examples[] = {
      {{2024, 1, 1, 0, 0, 0, 1}, 1704067200LL},   {{2024, 2, 29, 0, 0, 0, 4}, 1709164800LL},
      {{2024, 3, 1, 0, 0, 0, 5}, 1709251200LL},   {{2025, 1, 1, 0, 0, 0, 3}, 1735689600LL},
      {{2038, 1, 19, 3, 14, 8, 2}, 2147483648LL}, {{2099, 12, 31, 23, 59, 59, 4}, 4102444799LL},
  };
  for (const auto& example : examples) {
    Rtc::value = example.date;
    int64_t epoch = -1;
    ASSERT_TRUE(clock.getUtcTime(epoch));
    EXPECT_EQ(epoch, example.epoch);
  }
}

TEST_F(HalClockUtcTest, RejectsInvalidCalendarAndUnsupportedYearsWithoutChangingOutput) {
  const Rtc::DateTime invalid[] = {
      {2023, 12, 31, 0, 0, 0, 0}, {2100, 1, 1, 0, 0, 0, 0},  {2100, 2, 29, 0, 0, 0, 0}, {2024, 0, 1, 0, 0, 0, 0},
      {2024, 13, 1, 0, 0, 0, 0},  {2024, 1, 0, 0, 0, 0, 0},  {2024, 4, 31, 0, 0, 0, 0}, {2025, 2, 29, 0, 0, 0, 0},
      {2024, 2, 30, 0, 0, 0, 0},  {2024, 1, 1, 24, 0, 0, 0}, {2024, 1, 1, 0, 60, 0, 0}, {2024, 1, 1, 0, 0, 60, 0},
  };
  for (const auto& date : invalid) {
    Rtc::value = date;
    int64_t epoch = 123;
    EXPECT_FALSE(clock.getUtcTime(epoch));
    EXPECT_EQ(epoch, 123);
  }
}

TEST_F(HalClockUtcTest, RejectsMissingOrUninitializedRtc) {
  HalClock uninitialized;
  int64_t epoch = 123;
  EXPECT_FALSE(uninitialized.getUtcTime(epoch));
  EXPECT_EQ(epoch, 123);
  Rtc::present = false;
  clock.begin();
  EXPECT_FALSE(clock.getUtcTime(epoch));
  EXPECT_EQ(epoch, 123);
}

TEST_F(HalClockUtcTest, RejectsUnreliableRtcDespiteCachedDisplayTime) {
  uint8_t hour;
  uint8_t minute;
  ASSERT_TRUE(clock.getTime(hour, minute));
  Rtc::valid = false;
  int64_t epoch = 123;
  EXPECT_FALSE(clock.getUtcTime(epoch));
  EXPECT_EQ(epoch, 123);
  EXPECT_TRUE(clock.getTime(hour, minute));
}

TEST_F(HalClockUtcTest, ReadsFreshSecondsWithoutDisplayCacheDelay) {
  uint8_t hour;
  uint8_t minute;
  ASSERT_TRUE(clock.getTime(hour, minute));
  int64_t before;
  ASSERT_TRUE(clock.getUtcTime(before));
  Rtc::value.second = 1;
  int64_t after;
  ASSERT_TRUE(clock.getUtcTime(after));
  EXPECT_EQ(after, before + 1);
}

TEST_F(HalClockUtcTest, MatchesStandardUtcCalendarThroughoutSupportedRange) {
  // Daily boundaries exercise all month lengths and every supported leap year.
  for (int64_t epoch = 1704067200LL; epoch < 4102444800LL; epoch += 86400) {
    for (int seconds : {0, 86399}) {
      const time_t expected = static_cast<time_t>(epoch + seconds);
      const std::tm* date = std::gmtime(&expected);
      ASSERT_NE(date, nullptr);
      Rtc::value = {static_cast<uint16_t>(date->tm_year + 1900),
                    static_cast<uint8_t>(date->tm_mon + 1),
                    static_cast<uint8_t>(date->tm_mday),
                    static_cast<uint8_t>(date->tm_hour),
                    static_cast<uint8_t>(date->tm_min),
                    static_cast<uint8_t>(date->tm_sec),
                    0};
      int64_t actual = -1;
      ASSERT_TRUE(clock.getUtcTime(actual));
      ASSERT_EQ(actual, expected);
    }
  }
}

TEST_F(HalClockUtcTest, UtcReadingIgnoresDisplayTimezoneAndDaylightSaving) {
  clock.setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  Rtc::value = {2024, 1, 1, 0, 0, 0, 1};
  int64_t epoch = -1;
  ASSERT_TRUE(clock.getUtcTime(epoch));
  EXPECT_EQ(epoch, 1704067200LL);
  uint8_t hour;
  uint8_t minute;
  ASSERT_TRUE(clock.getTime(hour, minute));
  EXPECT_EQ(hour, 1);
  EXPECT_EQ(minute, 0);

  Rtc::value = {2024, 7, 1, 0, 0, 0, 1};
  clock.setTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  ASSERT_TRUE(clock.getUtcTime(epoch));
  EXPECT_EQ(epoch, 1719792000LL);
  ASSERT_TRUE(clock.getTime(hour, minute));
  EXPECT_EQ(hour, 2);
  EXPECT_EQ(minute, 0);
}
