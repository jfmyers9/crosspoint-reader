#include <BookOrbitStats.h>
#include <BookOrbitStatsClient.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <ReadingStatsOutbox.h>
#include <gtest/gtest.h>

StorageStub Storage;
HalClockStub halClock;
CredentialStub credentialStub;
int64_t stubMonotonicMs = 0;

namespace {
struct Upload {
  std::string hash;
  std::vector<ReadingStatsEvent> events;
};
std::vector<Upload> uploads;
std::vector<std::pair<uint32_t, uint32_t>> sweeps;
bool acceptUpload = true;
bool acceptSweep = true;
bool lastUnmatched = false;
std::string unmatchedHash;
int64_t uploadElapsedMs = 0;
std::string device = "reader-1";

size_t countFiles(const char* suffix) {
  size_t count = 0;
  for (const auto& [name, bytes] : Storage.files) {
    (void)bytes;
    if (name.ends_with(suffix)) ++count;
  }
  return count;
}

class BookOrbitStatsTest : public testing::Test {
 protected:
  void SetUp() override {
    BookOrbitStats::pause();
    Storage.files.clear();
    Storage.directories.clear();
    Storage.failRename = false;
    uploads.clear();
    sweeps.clear();
    halClock = {};
    acceptUpload = acceptSweep = true;
    lastUnmatched = false;
    unmatchedHash.clear();
    uploadElapsedMs = 0;
    device = "reader-1";
    static unsigned sequence = 0;
    credentialStub = {};
    credentialStub.user = "reader-" + std::to_string(++sequence);
    stubMonotonicMs += 1000000;
  }

  void readBook(const std::string& book = "book-a", uint32_t seconds = 10) {
    BookOrbitStats::beginBook(book);
    BookOrbitStats::showPage(0.25f);
    stubMonotonicMs += seconds * 1000;
    BookOrbitStats::pause();
  }
};
}  // namespace

const char* BookOrbitStatsClient::deviceId() { return device.c_str(); }
bool BookOrbitStatsClient::upload(const char* hash, const ReadingStatsEvent* events, size_t count) {
  uploads.push_back({hash, {events, events + count}});
  stubMonotonicMs += uploadElapsedMs;
  lastUnmatched = hash == unmatchedHash;
  return acceptUpload && !lastUnmatched;
}
bool BookOrbitStatsClient::lastUploadWasUnmatched() { return lastUnmatched; }
bool BookOrbitStatsClient::completeSweep(uint32_t uploaded, uint32_t booksMatched) {
  sweeps.emplace_back(uploaded, booksMatched);
  return acceptSweep;
}

TEST_F(BookOrbitStatsTest, PausePersistsAndNextBookGetsIndependentIdentityAndDuration) {
  readBook();
  ASSERT_EQ(countFiles(".bin"), 1u);
  stubMonotonicMs += 60000;
  readBook("book-b", 20);
  ASSERT_EQ(countFiles(".bin"), 2u);
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 2u);
  EXPECT_EQ(uploads[0].events[0].durationSeconds + uploads[1].events[0].durationSeconds, 30u);
  EXPECT_NE(uploads[0].hash, uploads[1].hash);
  EXPECT_EQ(uploads[0].events[0].totalPages, 10000u);
  EXPECT_EQ(uploads[0].events[0].page, 2500u);
  EXPECT_EQ(countFiles(".bin"), 0u);
  ASSERT_EQ(sweeps.size(), 1u);
  EXPECT_EQ(sweeps[0], std::make_pair(2u, 2u));
}

TEST_F(BookOrbitStatsTest, SwitchingAccountCannotReplayAnotherAccountsQueue) {
  readBook();
  const auto original = credentialStub.user;
  credentialStub.user += "-other";
  BookOrbitStats::sync();
  EXPECT_TRUE(uploads.empty());
  EXPECT_EQ(countFiles(".bin"), 1u);
  credentialStub.user = original;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(countFiles(".bin"), 0u);
}

TEST_F(BookOrbitStatsTest, MovingCardToAnotherDeviceCannotReplayQueue) {
  readBook();
  device = "reader-2";
  BookOrbitStats::sync();
  EXPECT_TRUE(uploads.empty());
  EXPECT_EQ(countFiles(".bin"), 1u);
  device = "reader-1";
  BookOrbitStats::sync();
  EXPECT_EQ(uploads.size(), 1u);
}

TEST_F(BookOrbitStatsTest, OfflineUndatedEventsResolveUsingLaterSameBootAnchor) {
  halClock.valid = false;
  const int64_t start = stubMonotonicMs;
  readBook();
  ASSERT_EQ(countFiles(".bin"), 1u);
  for (const auto& [path, bytes] : Storage.files) {
    (void)bytes;
    if (!path.ends_with(".bin")) continue;
    ReadingStatsOutbox::Batch batch;
    ASSERT_TRUE(ReadingStatsOutbox::load(path.c_str(), batch));
    EXPECT_EQ(batch.events[0].startEpochSeconds, 0);
  }
  BookOrbitStats::sync();
  EXPECT_TRUE(uploads.empty());
  EXPECT_TRUE(sweeps.empty());
  stubMonotonicMs = start + 60000;
  halClock.valid = true;
  halClock.epoch = 1800000060;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(uploads[0].events[0].startEpochSeconds, 1800000000);
  EXPECT_EQ(uploads[0].events[0].durationSeconds, 10u);
}

TEST_F(BookOrbitStatsTest, FailedPostRetainsIdenticalBatchUntilAcknowledged) {
  readBook();
  acceptUpload = false;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(countFiles(".bin"), 1u);
  EXPECT_TRUE(sweeps.empty());
  const auto first = uploads[0].events[0];
  halClock.epoch += 3600;
  stubMonotonicMs += 3600000;
  acceptUpload = true;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 2u);
  EXPECT_EQ(uploads[1].events[0].startEpochSeconds, first.startEpochSeconds);
  EXPECT_EQ(uploads[1].events[0].durationSeconds, first.durationSeconds);
  EXPECT_EQ(countFiles(".bin"), 0u);
  EXPECT_EQ(sweeps.size(), 1u);
}

TEST_F(BookOrbitStatsTest, FailedSweepRetriesWithAnEmptyQueue) {
  readBook();
  acceptSweep = false;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  ASSERT_EQ(sweeps.size(), 1u);
  EXPECT_EQ(countFiles(".bin"), 0u);
  EXPECT_EQ(countFiles("sweep.pending"), 1u);
  acceptSweep = true;
  BookOrbitStats::sync();
  EXPECT_EQ(uploads.size(), 1u);
  EXPECT_EQ(sweeps.size(), 2u);
  EXPECT_EQ(countFiles("sweep.pending"), 0u);
}

TEST_F(BookOrbitStatsTest, UnacknowledgedPartialBatchNeverCompletesSweep) {
  readBook();
  acceptUpload = false;  // Client rejects partial acknowledgments.
  BookOrbitStats::sync();
  BookOrbitStats::sync();
  EXPECT_EQ(uploads.size(), 2u);
  EXPECT_TRUE(sweeps.empty());
  EXPECT_EQ(countFiles(".bin"), 1u);
}

TEST_F(BookOrbitStatsTest, UnmatchedBookRetainsItsFilesWithoutStarvingMatchedBook) {
  readBook("book-a");
  readBook("book-b");
  unmatchedHash = std::string(32, 'a');
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 2u);
  EXPECT_EQ(countFiles(".bin"), 1u);
  for (const auto& [path, bytes] : Storage.files) {
    (void)bytes;
    if (!path.ends_with(".bin")) continue;
    ReadingStatsOutbox::Batch remaining;
    ASSERT_TRUE(ReadingStatsOutbox::load(path.c_str(), remaining));
    EXPECT_EQ(remaining.bookHash, unmatchedHash);
  }
  ASSERT_EQ(sweeps.size(), 1u);
  EXPECT_EQ(sweeps[0], std::make_pair(1u, 1u));
  unmatchedHash.clear();
  BookOrbitStats::sync();
  EXPECT_EQ(countFiles(".bin"), 0u);
  ASSERT_EQ(uploads.size(), 3u);
  EXPECT_EQ(uploads.back().hash, std::string(32, 'a'));
}

TEST_F(BookOrbitStatsTest, UnmatchedOnlyPassRetainsHistoryWithoutCreatingSweep) {
  readBook();
  unmatchedHash = std::string(32, 'a');
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(countFiles(".bin"), 1u);
  EXPECT_EQ(countFiles("sweep.pending"), 0u);
  EXPECT_TRUE(sweeps.empty());
}

TEST_F(BookOrbitStatsTest, GenericKoreaderEndpointDoesNotRecordOrUpload) {
  credentialStub.url = "http://sync.test";
  readBook();
  BookOrbitStats::sync();
  EXPECT_EQ(countFiles(".bin"), 0u);
  EXPECT_TRUE(uploads.empty());
  EXPECT_TRUE(sweeps.empty());
}

TEST_F(BookOrbitStatsTest, FailedFullBufferRetainsOriginalBookAcrossNextBookOpen) {
  BookOrbitStats::beginBook("book-a");
  Storage.failRename = true;
  BookOrbitStats::showPage(0.0f);
  for (unsigned page = 1; page <= 9; ++page) {
    stubMonotonicMs += 10000;
    BookOrbitStats::showPage(static_cast<float>(page) / 100);
  }
  BookOrbitStats::pause();
  EXPECT_EQ(countFiles(".bin"), 0u);
  Storage.failRename = false;
  readBook("book-b", 20);
  BookOrbitStats::sync();
  uint32_t firstDuration = 0;
  uint32_t secondDuration = 0;
  for (const auto& upload : uploads) {
    for (const auto& event : upload.events) {
      if (upload.hash == std::string(32, 'a')) firstDuration += event.durationSeconds;
      if (upload.hash == std::string(32, 'b')) secondDuration += event.durationSeconds;
    }
  }
  EXPECT_EQ(firstDuration, 90u);
  EXPECT_EQ(secondDuration, 20u);
  EXPECT_EQ(countFiles(".bin"), 0u);
}

TEST_F(BookOrbitStatsTest, FailedAnchorPublishCanRecoverAndDateOfflineEvents) {
  halClock.valid = false;
  readBook();
  halClock.valid = true;
  Storage.failRename = true;
  BookOrbitStats::sync();
  EXPECT_TRUE(uploads.empty());
  EXPECT_EQ(countFiles(".bin"), 1u);
  Storage.failRename = false;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_NE(uploads[0].events[0].startEpochSeconds, 0);
}

TEST_F(BookOrbitStatsTest, SameBookBatchesCoalesceAndRetryWithoutLosingOriginalFiles) {
  for (unsigned i = 0; i < 3; ++i) readBook();
  ASSERT_EQ(countFiles(".bin"), 3u);
  acceptUpload = false;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  ASSERT_EQ(uploads[0].events.size(), 3u);
  EXPECT_EQ(countFiles(".bin"), 3u);
  acceptUpload = true;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 2u);
  ASSERT_EQ(uploads[1].events.size(), 3u);
  for (unsigned i = 0; i < 3; ++i) {
    EXPECT_EQ(uploads[0].events[i].startEpochSeconds, uploads[1].events[i].startEpochSeconds);
    EXPECT_EQ(uploads[0].events[i].durationSeconds, uploads[1].events[i].durationSeconds);
  }
  EXPECT_EQ(countFiles(".bin"), 0u);
  ASSERT_EQ(sweeps.size(), 1u);
  EXPECT_EQ(sweeps[0], std::make_pair(3u, 1u));
}

TEST_F(BookOrbitStatsTest, RequestBudgetRetainsBacklogAndDefersSweep) {
  for (unsigned i = 0; i < 257; ++i) readBook();
  ASSERT_EQ(countFiles(".bin"), 257u);
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 32u);
  EXPECT_EQ(countFiles(".bin"), 1u);
  EXPECT_TRUE(sweeps.empty());
  BookOrbitStats::sync();
  EXPECT_EQ(uploads.size(), 33u);
  EXPECT_EQ(countFiles(".bin"), 0u);
  EXPECT_EQ(sweeps.size(), 1u);
}

TEST_F(BookOrbitStatsTest, SlowRequestStopsBeforeStartingAnotherUpload) {
  for (unsigned i = 0; i < 9; ++i) readBook();
  uploadElapsedMs = 50000;
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(countFiles(".bin"), 1u);
  EXPECT_TRUE(sweeps.empty());
  uploadElapsedMs = 0;
  BookOrbitStats::sync();
  EXPECT_EQ(countFiles(".bin"), 0u);
}

TEST_F(BookOrbitStatsTest, UndatedPreviousBootIsRetainedWithoutBlockingCurrentDatedHistory) {
  halClock.valid = false;
  readBook();
  std::string original;
  for (const auto& [path, bytes] : Storage.files) {
    (void)bytes;
    if (path.ends_with(".bin")) original = path;
  }
  ASSERT_FALSE(original.empty());
  ReadingStatsOutbox::Batch oldBoot;
  ASSERT_TRUE(ReadingStatsOutbox::load(original.c_str(), oldBoot));
  std::strcpy(oldBoot.bootId, "abcdefabcdefabcd");
  const auto scope = original.substr(0, original.find_last_of('/'));
  ASSERT_TRUE(Storage.remove(original.c_str()));
  ASSERT_TRUE(ReadingStatsOutbox::save(scope.c_str(), oldBoot));
  halClock.valid = true;
  readBook("book-b");
  BookOrbitStats::sync();
  ASSERT_EQ(uploads.size(), 1u);
  EXPECT_EQ(uploads[0].hash, std::string(32, 'b'));
  EXPECT_EQ(countFiles(".bin"), 1u);
  EXPECT_EQ(sweeps.size(), 1u);
}
