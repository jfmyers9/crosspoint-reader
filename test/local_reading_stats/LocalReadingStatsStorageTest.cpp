#include <gtest/gtest.h>

#include "HalClock.h"
#include "HalStorage.h"
#include "LocalReadingStats.h"

StorageStub Storage;
HalClockStub halClock;
int64_t stubMonotonicMs = 0;

// Each test runs in its own process through gtest_discover_tests: service globals
// deliberately have the firmware's boot lifetime.
TEST(LocalReadingStatsStorageTest, OfflineTotalsPersistAcrossBookSwitches) {
  halClock.valid = false;
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 30000;
  LocalReadingStats::checkpoint();
  LocalReadingStats::Summary summary;
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.bookSeconds, 30u);
  EXPECT_FALSE(summary.clockValid);
  EXPECT_EQ(summary.todaySeconds, 0u);
  LocalReadingStats::beginBook("/b.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 40000;
  LocalReadingStats::pause();
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.bookSeconds, 30u);
  EXPECT_TRUE(Storage.exists("/.crosspoint/local-reading-stats/global.bin"));
}
TEST(LocalReadingStatsStorageTest, FailedSwitchNeverAttributesNewBookToOldRecord) {
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 30000;
  Storage.failRename = true;
  LocalReadingStats::beginBook("/b.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 60000;
  LocalReadingStats::checkpoint();
  LocalReadingStats::Summary summary;
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.bookSeconds, 30u);
  EXPECT_FALSE(summary.storageHealthy);
  Storage.failRename = false;
  LocalReadingStats::beginBook("/b.epub");
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.bookSeconds, 0u);
  EXPECT_TRUE(summary.storageHealthy);
}
TEST(LocalReadingStatsStorageTest, CorruptHistoryIsNotOverwritten) {
  const std::string path = "/.crosspoint/local-reading-stats/global.bin";
  Storage.files[path] = {1, 2, 3};
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 60000;
  LocalReadingStats::pause();
  EXPECT_EQ(Storage.files[path], (std::vector<uint8_t>{1, 2, 3}));
}
TEST(LocalReadingStatsStorageTest, BackupRecoversCorruptBookRecord) {
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 30000;
  LocalReadingStats::checkpoint();
  stubMonotonicMs = 60000;
  LocalReadingStats::pause();
  LocalReadingStats::beginBook("/b.epub");
  for (auto& [path, bytes] : Storage.files) {
    if (path.ends_with(".bin") && path.find("global") == std::string::npos) bytes = {1};
  }
  LocalReadingStats::beginBook("/a.epub");
  LocalReadingStats::Summary summary;
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.bookSeconds, 30u);
}
TEST(LocalReadingStatsStorageTest, DashboardPauseAndResumeKeepLearnedPace) {
  LocalReadingStats::beginBook("/pace.epub");
  LocalReadingStats::showPage(0);
  for (int i = 1; i <= 3; ++i) {
    stubMonotonicMs = i * 10000;
    LocalReadingStats::suspend();
    LocalReadingStats::showPage(i * .001f);
  }
  uint32_t before = 0, after = 0;
  ASSERT_TRUE(LocalReadingStats::estimateSeconds(.5f, before));
  LocalReadingStats::pause();
  ASSERT_TRUE(LocalReadingStats::estimateSeconds(.5f, after));
  EXPECT_EQ(before, after);
  stubMonotonicMs += 60000;
  LocalReadingStats::showPage(.003f);
  ASSERT_TRUE(LocalReadingStats::estimateSeconds(.5f, after));
  EXPECT_EQ(before, after);
  LocalReadingStats::breakSequence();
  LocalReadingStats::showPage(.8f);
  ASSERT_TRUE(LocalReadingStats::estimateSeconds(.5f, after));
  EXPECT_EQ(before, after);
  LocalReadingStats::resetPace();
  EXPECT_FALSE(LocalReadingStats::estimateSeconds(.5f, after));
}
TEST(LocalReadingStatsStorageTest, DatedTotalsAndCheckpointWritesAreThrottled) {
  setenv("TZ", "UTC0", 1);
  tzset();
  halClock.epoch = 1800000000;
  LocalReadingStats::beginBook("/dated.epub");
  LocalReadingStats::showPage(0);
  EXPECT_EQ(Storage.writes, 0u);
  stubMonotonicMs = 10000;
  halClock.epoch += 10;
  LocalReadingStats::checkpoint();
  LocalReadingStats::Summary summary;
  LocalReadingStats::getSummary(summary);
  EXPECT_TRUE(summary.clockValid);
  EXPECT_TRUE(summary.storageHealthy);
  EXPECT_EQ(summary.totalSeconds, 10u);
  EXPECT_EQ(summary.todaySeconds, 10u);
  EXPECT_EQ(summary.bookDaily[0].seconds, 10u);
  EXPECT_EQ(Storage.writes, 0u);
  stubMonotonicMs = 30000;
  halClock.epoch += 20;
  LocalReadingStats::checkpoint();
  EXPECT_EQ(Storage.writes, 2u);
  LocalReadingStats::checkpoint();
  LocalReadingStats::getSummary(summary);
  EXPECT_EQ(summary.totalSeconds, 30u);
  EXPECT_EQ(summary.todaySeconds, 30u);
  EXPECT_EQ(summary.daily[0].dateKey, LocalStats::dateKey(halClock.epoch));
  EXPECT_EQ(Storage.writes, 2u);
  LocalReadingStats::pause();
  EXPECT_EQ(Storage.writes, 2u);
}
TEST(LocalReadingStatsStorageTest, FailedSaveMarksSnapshotUnhealthyUntilRetrySucceeds) {
  LocalReadingStats::beginBook("/save.epub");
  LocalReadingStats::showPage(0);
  stubMonotonicMs = 30000;
  Storage.failRename = true;
  LocalReadingStats::checkpoint();
  LocalReadingStats::Summary summary;
  LocalReadingStats::getSummary(summary);
  EXPECT_FALSE(summary.storageHealthy);
  EXPECT_EQ(summary.totalSeconds, 30u);
  Storage.failRename = false;
  LocalReadingStats::pause();
  LocalReadingStats::getSummary(summary);
  EXPECT_TRUE(summary.storageHealthy);
  EXPECT_EQ(summary.totalSeconds, 30u);
}
