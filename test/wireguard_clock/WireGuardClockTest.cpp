#include <gtest/gtest.h>
#include <nvs.h>

#include <array>
#include <limits>

#include "WireGuardClock.h"

namespace {
using Timestamp = std::array<uint8_t, 12>;
class WireGuardClockTest : public ::testing::Test {
  void SetUp() override { mocknvs::reset(); }
};

TEST_F(WireGuardClockTest, EncodesBigEndianTai64nBeyondLegacyUptime) {
  WireGuardClock clock;
  Timestamp stamp{};
  ASSERT_TRUE(clock.begin());
  ASSERT_EQ(mocknvs::commits, 1);
  ASSERT_TRUE(clock.writeTimestamp(stamp.data()));
  uint64_t seconds = 0;
  for (int i = 0; i < 8; ++i) seconds = (seconds << 8) | stamp[i];
  EXPECT_EQ(seconds, 0x400000000000000AULL + 1735689600ULL);
  EXPECT_EQ(stamp[8] | stamp[9] | stamp[10] | stamp[11], 0);
}

TEST_F(WireGuardClockTest, IncreasesWithoutClockOrUptimeChanges) {
  WireGuardClock clock;
  Timestamp first{}, second{};
  ASSERT_TRUE(clock.writeTimestamp(first.data()));
  ASSERT_TRUE(clock.writeTimestamp(second.data()));
  EXPECT_LT(first, second);
  EXPECT_EQ(second[10], 3);
  EXPECT_EQ(second[11], 232);  // 1000 nanoseconds
  ASSERT_TRUE(clock.begin());  // Restarting MicroLink does not consume another range.
  EXPECT_EQ(mocknvs::commits, 1);
}

TEST_F(WireGuardClockTest, RebootSkipsUnusedReservedCounters) {
  WireGuardClock firstBoot;
  Timestamp before{}, after{};
  ASSERT_TRUE(firstBoot.writeTimestamp(before.data()));
  WireGuardClock nextBoot;
  ASSERT_TRUE(nextBoot.writeTimestamp(after.data()));
  EXPECT_GT(after, before);
  EXPECT_EQ(mocknvs::commits, 2);
}

TEST_F(WireGuardClockTest, PowerLossBeforeFirstHandshakeStillDoesNotReuseReservation) {
  WireGuardClock firstBoot;
  ASSERT_TRUE(firstBoot.begin());
  const auto reservedEnd = mocknvs::stored;
  WireGuardClock nextBoot;
  ASSERT_TRUE(nextBoot.begin());
  EXPECT_GT(mocknvs::stored, reservedEnd);
}

TEST_F(WireGuardClockTest, RenewsOnlyAfterOneMillionHandshakes) {
  WireGuardClock clock;
  Timestamp before{}, after{};
  for (int i = 0; i < 1000000; ++i) ASSERT_TRUE(clock.writeTimestamp(before.data()));
  EXPECT_EQ(mocknvs::commits, 1);
  ASSERT_TRUE(clock.writeTimestamp(after.data()));
  EXPECT_GT(after, before);
  EXPECT_EQ(mocknvs::commits, 2);
}

TEST_F(WireGuardClockTest, NvsFailuresNeverIssueUnreservedTimestamps) {
  for (int* error : {&mocknvs::openError, &mocknvs::readError, &mocknvs::writeError, &mocknvs::commitError}) {
    mocknvs::reset();
    *error = ESP_FAIL;
    WireGuardClock clock;
    Timestamp stamp;
    stamp.fill(255);
    EXPECT_FALSE(clock.begin());
    EXPECT_FALSE(clock.writeTimestamp(stamp.data()));
    EXPECT_EQ(stamp, Timestamp{});
    *error = ESP_OK;
    EXPECT_TRUE(clock.begin());
    EXPECT_TRUE(clock.writeTimestamp(stamp.data()));
    EXPECT_GT(stamp, Timestamp{});
  }
}

TEST_F(WireGuardClockTest, ExhaustedRangeFailsClosedIfNextReservationCannotCommit) {
  WireGuardClock clock;
  Timestamp stamp{};
  for (int i = 0; i < 1000000; ++i) ASSERT_TRUE(clock.writeTimestamp(stamp.data()));
  const auto before = stamp;
  mocknvs::commitError = ESP_FAIL;
  EXPECT_FALSE(clock.writeTimestamp(stamp.data()));
  EXPECT_EQ(stamp, Timestamp{});
  mocknvs::commitError = ESP_OK;
  ASSERT_TRUE(clock.writeTimestamp(stamp.data()));
  EXPECT_GT(stamp, before);
}

TEST_F(WireGuardClockTest, CorruptCounterCannotWrapBackwards) {
  mocknvs::stored = std::numeric_limits<uint64_t>::max();
  WireGuardClock clock;
  EXPECT_FALSE(clock.begin());
  EXPECT_EQ(mocknvs::commits, 0);
}
}  // namespace
