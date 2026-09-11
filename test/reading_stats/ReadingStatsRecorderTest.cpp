#include <gtest/gtest.h>

#include <array>

#include "ReadingStatsRecorder.h"

namespace {
struct Capture {
  std::array<ReadingStatsEvent, 16> events{};
  size_t count = 0;
  bool fail = false;
  ReadingStatsEvent attempted{};

  static bool accept(void* context, const ReadingStatsEvent& event) {
    auto& self = *static_cast<Capture*>(context);
    self.attempted = event;
    if (self.fail || self.count == self.events.size()) {
      return false;
    }
    self.events[self.count++] = event;
    return true;
  }
};

class ReadingStatsRecorderTest : public testing::Test {
 protected:
  Capture capture;
  ReadingStatsRecorder recorder{Capture::accept, &capture};
};
}  // namespace

TEST_F(ReadingStatsRecorderTest, PauseExcludesMenuAndResumeStartsAnotherInterval) {
  ASSERT_TRUE(recorder.showPage(3, 100, 1000));
  ASSERT_TRUE(recorder.pause(11000));
  ASSERT_TRUE(recorder.showPage(3, 100, 71000));
  ASSERT_TRUE(recorder.pause(81000));
  ASSERT_EQ(capture.count, 2u);
  EXPECT_EQ(capture.events[0].durationSeconds, 10u);
  EXPECT_EQ(capture.events[1].durationSeconds, 10u);
  EXPECT_EQ(capture.events[1].startMonotonicMs, 71000u);
}

TEST_F(ReadingStatsRecorderTest, RedrawDoesNotResetDwellOrIdleLimit) {
  ASSERT_TRUE(recorder.showPage(3, 100, 0));
  ASSERT_TRUE(recorder.showPage(3, 100, 3000));
  ASSERT_TRUE(recorder.showPage(3, 100, 119000));
  ASSERT_TRUE(recorder.pause(300000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].durationSeconds, 120u);
}

TEST_F(ReadingStatsRecorderTest, SkipsQuickPageTurnsAndInvalidPositions) {
  ASSERT_TRUE(recorder.showPage(1, 100, 0));
  ASSERT_TRUE(recorder.showPage(2, 100, 4000));
  ASSERT_TRUE(recorder.showPage(1, 0, 9000));
  ASSERT_TRUE(recorder.pause(90000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].page, 2u);
  EXPECT_EQ(capture.events[0].durationSeconds, 5u);
}

TEST_F(ReadingStatsRecorderTest, CheckpointsAreDisjointAndShareOneIdleBudget) {
  ASSERT_TRUE(recorder.showPage(1, 100, 1000));
  ASSERT_TRUE(recorder.checkpoint(31500));
  ASSERT_TRUE(recorder.checkpoint(62000));
  ASSERT_TRUE(recorder.checkpoint(200000));
  ASSERT_TRUE(recorder.pause(300000));
  ASSERT_EQ(capture.count, 3u);
  EXPECT_EQ(capture.events[0].durationSeconds, 30u);
  EXPECT_EQ(capture.events[1].startMonotonicMs, 31000u);
  EXPECT_EQ(capture.events[1].durationSeconds, 31u);
  EXPECT_EQ(capture.events[2].durationSeconds, 59u);
}

TEST_F(ReadingStatsRecorderTest, UnknownClockProducesUndatedRecord) {
  ASSERT_TRUE(recorder.showPage(1, 100, 7000));
  ASSERT_TRUE(recorder.pause(17000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].startEpochSeconds, 0);
  EXPECT_EQ(capture.events[0].startMonotonicMs, 7000u);
}

TEST_F(ReadingStatsRecorderTest, SameBootAnchorDatesActiveIntervalRetroactively) {
  ASSERT_TRUE(recorder.showPage(1, 100, 1000));
  recorder.setTimeAnchor(21000, 1700000020);
  ASSERT_TRUE(recorder.pause(31000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].startEpochSeconds, 1700000000);
  EXPECT_EQ(capture.events[0].durationSeconds, 30u);
}

TEST_F(ReadingStatsRecorderTest, ClockCorrectionCannotChangeMeasuredDuration) {
  recorder.setTimeAnchor(0, 1700000000);
  ASSERT_TRUE(recorder.showPage(1, 100, 1000));
  recorder.setTimeAnchor(6000, 1800000000);
  ASSERT_TRUE(recorder.showPage(2, 100, 11000));
  ASSERT_TRUE(recorder.pause(21000));
  ASSERT_EQ(capture.count, 2u);
  EXPECT_EQ(capture.events[0].startEpochSeconds, 1700000001);
  EXPECT_EQ(capture.events[0].durationSeconds, 10u);
  EXPECT_EQ(capture.events[1].startEpochSeconds, 1800000005);
  EXPECT_EQ(capture.events[1].durationSeconds, 10u);
}

TEST_F(ReadingStatsRecorderTest, FailedWriteRetainsImmutableRecordForRetry) {
  ASSERT_TRUE(recorder.showPage(1, 100, 1000));
  capture.fail = true;
  EXPECT_FALSE(recorder.pause(11000));
  recorder.setTimeAnchor(21000, 1700000020);
  EXPECT_FALSE(recorder.checkpoint(21000));
  EXPECT_EQ(capture.attempted.startEpochSeconds, 0);
  EXPECT_EQ(capture.attempted.durationSeconds, 10u);
  capture.fail = false;
  ASSERT_TRUE(recorder.checkpoint(31000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].durationSeconds, 10u);
  EXPECT_EQ(capture.events[0].startEpochSeconds, 0);
}

TEST_F(ReadingStatsRecorderTest, PauseDuringFailedCheckpointRetainsTailWithoutMenuTime) {
  ASSERT_TRUE(recorder.showPage(1, 100, 0));
  capture.fail = true;
  EXPECT_FALSE(recorder.checkpoint(10000));
  EXPECT_FALSE(recorder.pause(20000));
  capture.fail = false;
  ASSERT_TRUE(recorder.checkpoint(90000));
  ASSERT_EQ(capture.count, 2u);
  EXPECT_EQ(capture.events[0].durationSeconds, 10u);
  EXPECT_EQ(capture.events[1].durationSeconds, 10u);
  EXPECT_EQ(capture.events[1].startMonotonicMs, 10000u);
}

TEST_F(ReadingStatsRecorderTest, ZeroProgressIsValid) {
  ASSERT_TRUE(recorder.showPage(0, 10000, 0));
  ASSERT_TRUE(recorder.pause(10000));
  ASSERT_EQ(capture.count, 1u);
  EXPECT_EQ(capture.events[0].page, 0u);
}

TEST_F(ReadingStatsRecorderTest, RenderingSuspensionsExcludeTimeAndPreserveIdleDeadline) {
  ASSERT_TRUE(recorder.showPage(1, 100, 0));
  ASSERT_TRUE(recorder.suspend(60000));
  ASSERT_TRUE(recorder.showPage(1, 100, 70000));
  ASSERT_TRUE(recorder.suspend(130000));
  ASSERT_TRUE(recorder.showPage(1, 100, 140000));
  ASSERT_TRUE(recorder.pause(200000));
  ASSERT_EQ(capture.count, 2u);
  EXPECT_EQ(capture.events[0].durationSeconds, 60u);
  EXPECT_EQ(capture.events[1].durationSeconds, 50u);
}

TEST_F(ReadingStatsRecorderTest, MenuReturnRedrawStartsFreshIdleDeadlineOnSamePage) {
  ASSERT_TRUE(recorder.showPage(1, 100, 0));
  ASSERT_TRUE(recorder.pause(120000));
  ASSERT_TRUE(recorder.suspend(300000));
  ASSERT_TRUE(recorder.showPage(1, 100, 301000));
  ASSERT_TRUE(recorder.pause(361000));
  ASSERT_EQ(capture.count, 2u);
  EXPECT_EQ(capture.events[0].durationSeconds, 120u);
  EXPECT_EQ(capture.events[1].durationSeconds, 60u);
  EXPECT_EQ(capture.events[1].startMonotonicMs, 301000u);
}
